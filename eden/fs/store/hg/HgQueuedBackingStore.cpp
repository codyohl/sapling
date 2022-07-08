/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#include "eden/fs/store/hg/HgQueuedBackingStore.h"

#include <chrono>
#include <thread>
#include <utility>
#include <variant>

#include <re2/re2.h>

#include <folly/Range.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>
#include <folly/portability/GFlags.h>
#include <folly/system/ThreadName.h>

#include "eden/fs/config/ReloadableConfig.h"
#include "eden/fs/model/Blob.h"
#include "eden/fs/service/ThriftUtil.h"
#include "eden/fs/store/BackingStoreLogger.h"
#include "eden/fs/store/LocalStore.h"
#include "eden/fs/store/ObjectFetchContext.h"
#include "eden/fs/store/hg/HgBackingStore.h"
#include "eden/fs/store/hg/HgImportRequest.h"
#include "eden/fs/store/hg/HgProxyHash.h"
#include "eden/fs/telemetry/EdenStats.h"
#include "eden/fs/telemetry/RequestMetricsScope.h"
#include "eden/fs/telemetry/StructuredLogger.h"
#include "eden/fs/utils/Bug.h"
#include "eden/fs/utils/EnumValue.h"
#include "eden/fs/utils/IDGen.h"
#include "eden/fs/utils/PathFuncs.h"
#include "eden/fs/utils/StaticAssert.h"
#include "eden/fs/utils/Throw.h"
#include "folly/ScopeGuard.h"
#include "folly/String.h"

namespace facebook::eden {

namespace {
// 100,000 hg object fetches in a short term is plausible.
constexpr size_t kTraceBusCapacity = 100000;
static_assert(CheckSize<HgImportTraceEvent, 56>());
// TraceBus is double-buffered, so the following capacity should be doubled.
// 10 MB overhead per backing repo is tolerable.
static_assert(
    CheckEqual<5600000, kTraceBusCapacity * sizeof(HgImportTraceEvent)>());
} // namespace

HgImportTraceEvent::HgImportTraceEvent(
    uint64_t unique,
    EventType eventType,
    ResourceType resourceType,
    const HgProxyHash& proxyHash,
    ImportPriorityKind priority,
    ObjectFetchContext::Cause cause)
    : unique{unique},
      manifestNodeId{proxyHash.revHash()},
      eventType{eventType},
      resourceType{resourceType},
      importPriority{priority},
      importCause{cause} {
  auto hgPath = proxyHash.path().stringPiece();
  path.reset(new char[hgPath.size() + 1]);
  memcpy(path.get(), hgPath.data(), hgPath.size());
  path[hgPath.size()] = 0;
}

HgQueuedBackingStore::HgQueuedBackingStore(
    std::shared_ptr<LocalStore> localStore,
    std::shared_ptr<EdenStats> stats,
    std::unique_ptr<HgBackingStore> backingStore,
    std::shared_ptr<ReloadableConfig> config,
    std::shared_ptr<StructuredLogger> structuredLogger,
    std::unique_ptr<BackingStoreLogger> logger)
    : localStore_(std::move(localStore)),
      stats_(std::move(stats)),
      config_(config),
      backingStore_(std::move(backingStore)),
      queue_(std::move(config)),
      structuredLogger_{std::move(structuredLogger)},
      logger_(std::move(logger)),
      traceBus_{TraceBus<HgImportTraceEvent>::create("hg", kTraceBusCapacity)} {
  uint8_t numberThreads =
      config_->getEdenConfig()->numBackingstoreThreads.getValue();
  if (!numberThreads) {
    XLOG(WARN)
        << "HgQueuedBackingStore configured to use 0 threads. Invalid, using one thread instead";
    numberThreads = 1;
  }
  threads_.reserve(numberThreads);
  for (uint16_t i = 0; i < numberThreads; i++) {
    threads_.emplace_back(&HgQueuedBackingStore::processRequest, this);
  }
}

HgQueuedBackingStore::~HgQueuedBackingStore() {
  queue_.stop();
  for (auto& thread : threads_) {
    thread.join();
  }
}

void HgQueuedBackingStore::processBlobImportRequests(
    std::vector<std::shared_ptr<HgImportRequest>>&& requests) {
  folly::stop_watch<std::chrono::milliseconds> watch;

  XLOG(DBG4) << "Processing blob import batch size=" << requests.size();

  for (auto& request : requests) {
    auto* blobImport = request->getRequest<HgImportRequest::BlobImport>();

    traceBus_->publish(HgImportTraceEvent::start(
        request->getUnique(),
        HgImportTraceEvent::BLOB,
        blobImport->proxyHash,
        request->getPriority().kind,
        request->getCause()));

    XLOGF(DBG4, "Processing blob request for {}", blobImport->hash);
  }

  backingStore_->getDatapackStore().getBlobBatch(requests);

  {
    std::vector<folly::SemiFuture<folly::Unit>> futures;
    futures.reserve(requests.size());

    for (auto& request : requests) {
      auto* promise = request->getPromise<std::unique_ptr<Blob>>();
      if (promise->isFulfilled()) {
        stats_->getHgBackingStoreStatsForCurrentThread()
            .hgBackingStoreGetBlob.addValue(watch.elapsed().count());
        continue;
      }

      // The blobs were either not found locally, or, when EdenAPI is enabled,
      // not found on the server. Let's import the blob through the hg importer.
      // TODO(xavierd): remove when EdenAPI has been rolled out everywhere.
      auto fetchSemiFuture = backingStore_->fetchBlobFromHgImporter(
          request->getRequest<HgImportRequest::BlobImport>()->proxyHash);
      futures.emplace_back(
          std::move(fetchSemiFuture)
              .defer([request = std::move(request), watch, stats = stats_](
                         auto&& result) mutable {
                XLOG(DBG4)
                    << "Imported blob from HgImporter for "
                    << request->getRequest<HgImportRequest::BlobImport>()->hash;
                stats->getHgBackingStoreStatsForCurrentThread()
                    .hgBackingStoreGetBlob.addValue(watch.elapsed().count());
                request->getPromise<HgImportRequest::BlobImport::Response>()
                    ->setTry(std::forward<decltype(result)>(result));
              }));
    }

    folly::collectAll(futures).wait();
  }
}

void HgQueuedBackingStore::processTreeImportRequests(
    std::vector<std::shared_ptr<HgImportRequest>>&& requests) {
  folly::stop_watch<std::chrono::milliseconds> watch;

  for (auto& request : requests) {
    auto* treeImport = request->getRequest<HgImportRequest::TreeImport>();

    traceBus_->publish(HgImportTraceEvent::start(
        request->getUnique(),
        HgImportTraceEvent::TREE,
        treeImport->proxyHash,
        request->getPriority().kind,
        request->getCause()));

    XLOGF(DBG4, "Processing tree request for {}", treeImport->hash);
  }

  {
    auto writeBatch = localStore_->beginWrite();
    backingStore_->getDatapackStore().getTreeBatch(requests, writeBatch.get());
  }

  {
    std::vector<folly::SemiFuture<folly::Unit>> futures;
    futures.reserve(requests.size());

    for (auto& request : requests) {
      auto* promise = request->getPromise<std::unique_ptr<Tree>>();
      if (promise->isFulfilled()) {
        stats_->getHgBackingStoreStatsForCurrentThread()
            .hgBackingStoreGetTree.addValue(watch.elapsed().count());
        continue;
      }

      // The trees were either not found locally, or, when EdenAPI is enabled,
      // not found on the server. Let's import the trees through the hg
      // importer.
      // TODO(xavierd): remove when EdenAPI has been rolled out everywhere.
      auto treeSemiFuture = backingStore_->getTree(request);
      futures.emplace_back(
          std::move(treeSemiFuture)
              .defer([request = std::move(request), watch, stats = stats_](
                         auto&& result) mutable {
                XLOG(DBG4)
                    << "Imported tree from HgImporter for "
                    << request->getRequest<HgImportRequest::TreeImport>()->hash;
                stats->getHgBackingStoreStatsForCurrentThread()
                    .hgBackingStoreGetTree.addValue(watch.elapsed().count());
                request->getPromise<HgImportRequest::TreeImport::Response>()
                    ->setTry(std::forward<decltype(result)>(result));
              }));
    }

    folly::collectAll(futures).wait();
  }
}

void HgQueuedBackingStore::processRequest() {
  folly::setThreadName("hgqueue");
  for (;;) {
    auto requests = queue_.dequeue();

    if (requests.empty()) {
      break;
    }

    const auto& first = requests.at(0);

    if (first->isType<HgImportRequest::BlobImport>()) {
      processBlobImportRequests(std::move(requests));
    } else if (first->isType<HgImportRequest::TreeImport>()) {
      processTreeImportRequests(std::move(requests));
    }
  }
}

bool HgQueuedBackingStore::hasBijectiveBlobIds() {
  return config_->getEdenConfig()->hgBijectiveBlobIds.getValue();
}

RootId HgQueuedBackingStore::parseRootId(folly::StringPiece rootId) {
  // rootId can be 20-byte binary or 40-byte hex. Canonicalize, unconditionally
  // returning 40-byte hex.
  return RootId{hash20FromThrift(rootId).toString()};
}

std::string HgQueuedBackingStore::renderRootId(const RootId& rootId) {
  // In memory, root IDs are stored as 40-byte hex. Thrift clients generally
  // expect 20-byte binary for Mercurial commit hashes, so re-encode that way.
  auto& value = rootId.value();
  if (value.size() == 40) {
    return folly::unhexlify(value);
  } else {
    XCHECK_EQ(0u, value.size());
    // Default-constructed RootId is the Mercurial null hash.
    return folly::unhexlify(kZeroHash.toString());
  }
}

ObjectId HgQueuedBackingStore::staticParseObjectId(
    folly::StringPiece objectId) {
  if (objectId.startsWith("proxy-")) {
    if (objectId.size() != 46) {
      throwf<std::invalid_argument>(
          "invalid proxy hash length: {}", objectId.size());
    }

    return ObjectId{folly::unhexlify<folly::fbstring>(objectId.subpiece(6))};
  }

  if (objectId.size() == 40) {
    return HgProxyHash::makeEmbeddedProxyHash2(Hash20{objectId});
  }

  if (objectId.size() < 41) {
    throwf<std::invalid_argument>("hg object ID too short: {}", objectId);
  }

  if (objectId[40] != ':') {
    throwf<std::invalid_argument>(
        "missing separator colon in hg object ID: {}", objectId);
  }

  Hash20 hgRevHash{objectId.subpiece(0, 40)};
  RelativePathPiece path{objectId.subpiece(41)};
  return HgProxyHash::makeEmbeddedProxyHash1(hgRevHash, path);
}

std::string HgQueuedBackingStore::staticRenderObjectId(
    const ObjectId& objectId) {
  if (auto proxyHash = HgProxyHash::tryParseEmbeddedProxyHash(objectId)) {
    if (proxyHash->path().empty()) {
      return folly::hexlify(proxyHash->byteHash());
    }
    return fmt::format(
        "{}:{}", folly::hexlify(proxyHash->byteHash()), proxyHash->path());
  }
  return fmt::format("proxy-{}", folly::hexlify(objectId.getBytes()));
}

folly::SemiFuture<BackingStore::GetTreeRes> HgQueuedBackingStore::getTree(
    const ObjectId& id,
    ObjectFetchContext& context) {
  HgProxyHash proxyHash;
  try {
    proxyHash = HgProxyHash::load(localStore_.get(), id, "getTree");
  } catch (const std::exception&) {
    logMissingProxyHash();
    throw;
  }

  logBackingStoreFetch(
      context,
      folly::Range{&proxyHash, 1},
      ObjectFetchContext::ObjectType::Tree);

  if (auto tree = backingStore_->getDatapackStore().getTreeLocal(
          id, proxyHash, *localStore_)) {
    XLOG(DBG5) << "imported tree of '" << proxyHash.path() << "', "
               << proxyHash.revHash().toString() << " from hgcache";
    return folly::makeSemiFuture(BackingStore::GetTreeRes{
        std::move(tree), ObjectFetchContext::Origin::FromDiskCache});
  }

  return getTreeImpl(id, proxyHash, context);
}

std::unique_ptr<BlobMetadata> HgQueuedBackingStore::getLocalBlobMetadata(
    const ObjectId& id,
    ObjectFetchContext& /*context*/) {
  HgProxyHash proxyHash;
  try {
    proxyHash =
        HgProxyHash::load(localStore_.get(), id, "getLocalBlobMetadata");
  } catch (const std::exception&) {
    logMissingProxyHash();
    throw;
  }
  return backingStore_->getDatapackStore().getLocalBlobMetadata(
      proxyHash.revHash());
}

folly::SemiFuture<BackingStore::GetTreeRes> HgQueuedBackingStore::getTreeImpl(
    const ObjectId& id,
    const HgProxyHash& proxyHash,
    ObjectFetchContext& context) {
  auto getTreeFuture = folly::makeFutureWith([&] {
    auto request = HgImportRequest::makeTreeImportRequest(
        id, proxyHash, context.getPriority(), context.getCause());
    uint64_t unique = request->getUnique();

    auto importTracker =
        std::make_unique<RequestMetricsScope>(&pendingImportTreeWatches_);
    traceBus_->publish(HgImportTraceEvent::queue(
        unique,
        HgImportTraceEvent::TREE,
        proxyHash,
        context.getPriority().kind,
        context.getCause()));

    return queue_.enqueueTree(std::move(request))
        .ensure([this,
                 unique,
                 proxyHash,
                 &context,
                 importTracker = std::move(importTracker)]() {
          traceBus_->publish(HgImportTraceEvent::finish(
              unique,
              HgImportTraceEvent::TREE,
              proxyHash,
              context.getPriority().kind,
              context.getCause()));
        });
  });

  return std::move(getTreeFuture)
      .thenTry([this, id](folly::Try<std::unique_ptr<Tree>>&& result) {
        this->queue_.markImportAsFinished<Tree>(id, result);
        auto tree = std::move(result).value();
        return BackingStore::GetTreeRes{
            std::move(tree), ObjectFetchContext::Origin::FromNetworkFetch};
      });
}

folly::SemiFuture<BackingStore::GetBlobRes> HgQueuedBackingStore::getBlob(
    const ObjectId& id,
    ObjectFetchContext& context) {
  HgProxyHash proxyHash;
  try {
    proxyHash = HgProxyHash::load(localStore_.get(), id, "getBlob");
  } catch (const std::exception&) {
    logMissingProxyHash();
    throw;
  }

  logBackingStoreFetch(
      context,
      folly::Range{&proxyHash, 1},
      ObjectFetchContext::ObjectType::Blob);

  if (auto blob =
          backingStore_->getDatapackStore().getBlobLocal(id, proxyHash)) {
    return folly::makeSemiFuture(BackingStore::GetBlobRes{
        std::move(blob), ObjectFetchContext::Origin::FromDiskCache});
  }

  return getBlobImpl(id, proxyHash, context);
}

folly::SemiFuture<BackingStore::GetBlobRes> HgQueuedBackingStore::getBlobImpl(
    const ObjectId& id,
    const HgProxyHash& proxyHash,
    ObjectFetchContext& context) {
  auto getBlobFuture = folly::makeFutureWith([&] {
    XLOG(DBG4) << "make blob import request for " << proxyHash.path()
               << ", hash is:" << id;

    auto request = HgImportRequest::makeBlobImportRequest(
        id, proxyHash, context.getPriority(), context.getCause());
    auto unique = request->getUnique();

    auto importTracker =
        std::make_unique<RequestMetricsScope>(&pendingImportBlobWatches_);
    traceBus_->publish(HgImportTraceEvent::queue(
        unique,
        HgImportTraceEvent::BLOB,
        proxyHash,
        context.getPriority().kind,
        context.getCause()));

    return queue_.enqueueBlob(std::move(request))
        .ensure([this,
                 unique,
                 proxyHash,
                 &context,
                 importTracker = std::move(importTracker)]() {
          traceBus_->publish(HgImportTraceEvent::finish(
              unique,
              HgImportTraceEvent::BLOB,
              proxyHash,
              context.getPriority().kind,
              context.getCause()));
        });
  });

  return std::move(getBlobFuture)
      .thenTry([this, id](folly::Try<std::unique_ptr<Blob>>&& result) {
        this->queue_.markImportAsFinished<Blob>(id, result);
        auto blob = std::move(result).value();
        return BackingStore::GetBlobRes{
            std::move(blob), ObjectFetchContext::Origin::FromNetworkFetch};
      });
}

folly::SemiFuture<std::unique_ptr<Tree>> HgQueuedBackingStore::getRootTree(
    const RootId& rootId,
    ObjectFetchContext& /*context*/) {
  return backingStore_->getRootTree(rootId);
}

folly::SemiFuture<folly::Unit> HgQueuedBackingStore::prefetchBlobs(
    ObjectIdRange ids,
    ObjectFetchContext& context) {
  return HgProxyHash::getBatch(localStore_.get(), ids)
      // The caller guarantees that ids will live at least longer than this
      // future, thus we don't need to deep-copy it.
      .thenTry([&context, this, ids](
                   folly::Try<std::vector<HgProxyHash>> tryHashes) {
        if (tryHashes.hasException()) {
          logMissingProxyHash();
        }
        auto& proxyHashes = tryHashes.value();

        logBackingStoreFetch(
            context,
            folly::Range{proxyHashes.data(), proxyHashes.size()},
            ObjectFetchContext::ObjectType::Blob);

        // Do not check for whether blobs are already present locally, this
        // check is useful for latency oriented workflows, not for throughput
        // oriented ones. Mercurial will anyway not re-fetch a blob that is
        // already present locally, so the check for local blob is pure overhead
        // when prefetching.
        std::vector<folly::SemiFuture<BackingStore::GetBlobRes>> futures;
        futures.reserve(ids.size());

        for (size_t i = 0; i < ids.size(); i++) {
          const auto& id = ids[i];
          const auto& proxyHash = proxyHashes[i];

          futures.emplace_back(getBlobImpl(id, proxyHash, context));
        }

        return folly::collectAll(futures).deferValue([](const auto& tries) {
          for (const auto& t : tries) {
            t.throwUnlessValue();
          }
        });
      });
}

void HgQueuedBackingStore::logMissingProxyHash() {
  auto now = std::chrono::steady_clock::now();

  bool shouldLog = false;
  {
    auto last = lastMissingProxyHashLog_.wlock();
    if (now >= *last +
            config_->getEdenConfig()
                ->missingHgProxyHashLogInterval.getValue()) {
      shouldLog = true;
      *last = now;
    }
  }

  if (shouldLog) {
    structuredLogger_->logEvent(MissingProxyHash{});
  }
}

void HgQueuedBackingStore::logBackingStoreFetch(
    ObjectFetchContext& context,
    folly::Range<HgProxyHash*> hashes,
    ObjectFetchContext::ObjectType type) {
  const auto& logFetchPathRegex =
      config_->getEdenConfig()->logObjectFetchPathRegex.getValue();

  if (logFetchPathRegex) {
    for (const auto& hash : hashes) {
      auto path = hash.path();
      auto pathPiece = path.stringPiece();

      if (RE2::PartialMatch(
              re2::StringPiece{pathPiece.data(), pathPiece.size()},
              **logFetchPathRegex)) {
        logger_->logImport(context, path, type);
      }
    }
  }

  if (type != ObjectFetchContext::ObjectType::Tree &&
      isRecordingFetch_.load(std::memory_order_relaxed) &&
      context.getCause() != ObjectFetchContext::Cause::Prefetch) {
    auto guard = fetchedFilePaths_.wlock();
    for (const auto& hash : hashes) {
      guard->emplace(hash.path().stringPiece().str());
    }
  }
}

size_t HgQueuedBackingStore::getImportMetric(
    RequestMetricsScope::RequestStage stage,
    HgBackingStore::HgImportObject object,
    RequestMetricsScope::RequestMetric metric) const {
  return RequestMetricsScope::getMetricFromWatches(
      metric, getImportWatches(stage, object));
}

RequestMetricsScope::LockedRequestWatchList&
HgQueuedBackingStore::getImportWatches(
    RequestMetricsScope::RequestStage stage,
    HgBackingStore::HgImportObject object) const {
  switch (stage) {
    case RequestMetricsScope::RequestStage::PENDING:
      return getPendingImportWatches(object);
    case RequestMetricsScope::RequestStage::LIVE:
      return backingStore_->getLiveImportWatches(object);
  }
  EDEN_BUG() << "unknown hg import stage " << enumValue(stage);
}

RequestMetricsScope::LockedRequestWatchList&
HgQueuedBackingStore::getPendingImportWatches(
    HgBackingStore::HgImportObject object) const {
  switch (object) {
    case HgBackingStore::HgImportObject::BLOB:
    case HgBackingStore::HgImportObject::BATCHED_BLOB:
      return pendingImportBlobWatches_;
    case HgBackingStore::HgImportObject::TREE:
    case HgBackingStore::HgImportObject::BATCHED_TREE:
      return pendingImportTreeWatches_;
    case HgBackingStore::HgImportObject::PREFETCH:
      return pendingImportPrefetchWatches_;
  }
  EDEN_BUG() << "unknown hg import object type " << static_cast<int>(object);
}

void HgQueuedBackingStore::startRecordingFetch() {
  isRecordingFetch_.store(true, std::memory_order_relaxed);
}

std::unordered_set<std::string> HgQueuedBackingStore::stopRecordingFetch() {
  isRecordingFetch_.store(false, std::memory_order_relaxed);
  std::unordered_set<std::string> paths;
  std::swap(paths, *fetchedFilePaths_.wlock());
  return paths;
}

folly::SemiFuture<folly::Unit> HgQueuedBackingStore::importManifestForRoot(
    const RootId& root,
    const Hash20& manifest) {
  // This method is used when the client informs us about a target manifest
  // that it is about to update to, for the scenario when a manifest has
  // just been created.  Since the manifest has just been created locally, and
  // metadata is only available remotely, there will be no metadata available
  // to prefetch.
  //
  // When the local store is populated with metadata for newly-created
  // manifests then we can update this so that is true when appropriate.
  return backingStore_->importTreeManifestForRoot(root, manifest);
}

void HgQueuedBackingStore::periodicManagementTask() {
  backingStore_->periodicManagementTask();
}

namespace {
void dropBlobImportRequest(std::shared_ptr<HgImportRequest>& request) {
  auto* promise = request->getPromise<std::unique_ptr<Blob>>();
  if (promise != nullptr) {
    if (!promise->isFulfilled()) {
      promise->setException(std::runtime_error("Request forcibly dropped"));
    }
  }
}

void dropTreeImportRequest(std::shared_ptr<HgImportRequest>& request) {
  auto* promise = request->getPromise<std::unique_ptr<Tree>>();
  if (promise != nullptr) {
    if (!promise->isFulfilled()) {
      promise->setException(std::runtime_error("Request forcibly dropped"));
    }
  }
}
} // namespace

int64_t HgQueuedBackingStore::dropAllPendingRequestsFromQueue() {
  auto requestVec = queue_.combineAndClearRequestQueues();
  for (auto& request : requestVec) {
    if (request->isType<HgImportRequest::BlobImport>()) {
      XLOG(DBG7, "Dropping blob request");
      dropBlobImportRequest(request);
    } else if (request->isType<HgImportRequest::TreeImport>()) {
      XLOG(DBG7, "Dropping tree request");
      dropTreeImportRequest(request);
    }
  }
  return requestVec.size();
}

} // namespace facebook::eden
