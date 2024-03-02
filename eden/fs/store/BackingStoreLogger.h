/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#pragma once

#include <folly/Executor.h>
#include <string>

#include "eden/common/utils/PathFuncs.h"
#include "eden/fs/store/ObjectFetchContext.h"

namespace facebook::eden {

class UnboundedQueueExecutor;
class StructuredLogger;
class ProcessInfoCache;

class BackingStoreLogger {
 public:
  BackingStoreLogger(
      std::shared_ptr<StructuredLogger> logger,
      std::shared_ptr<ProcessInfoCache> processInfoCache);

  // for unit tests so that a no-op logger can be passed into the backing store
  BackingStoreLogger() = default;

  void logImport(
      const ObjectFetchContext& context,
      RelativePathPiece importPath,
      ObjectFetchContext::ObjectType fetchedType);

 private:
  std::shared_ptr<StructuredLogger> logger_;
  std::shared_ptr<ProcessInfoCache> processInfoCache_;

  // for unit tests so that a no-op logger can be passed into the backing store
  bool loggingAvailable_ = false;
};

} // namespace facebook::eden
