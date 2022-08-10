/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#ifndef _WIN32

#include "eden/fs/inodes/Overlay.h"
#include "eden/fs/inodes/fsoverlay/FsOverlay.h"
#include "eden/fs/inodes/test/OverlayTestUtil.h"

#include <folly/Exception.h>
#include <folly/Expected.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/experimental/TestUtil.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>
#include <folly/test/TestUtils.h>
#include <algorithm>

#include "eden/fs/inodes/EdenMount.h"
#include "eden/fs/inodes/FileInode.h"
#include "eden/fs/inodes/OverlayFile.h"
#include "eden/fs/inodes/TreeInode.h"
#include "eden/fs/model/TestOps.h"
#include "eden/fs/service/PrettyPrinters.h"
#include "eden/fs/telemetry/NullStructuredLogger.h"
#include "eden/fs/testharness/FakeBackingStore.h"
#include "eden/fs/testharness/FakeTreeBuilder.h"
#include "eden/fs/testharness/TempFile.h"
#include "eden/fs/testharness/TestChecks.h"
#include "eden/fs/testharness/TestMount.h"
#include "eden/fs/testharness/TestUtil.h"
#include "eden/fs/utils/SpawnedProcess.h"

using namespace folly::string_piece_literals;

namespace facebook::eden {

constexpr Overlay::OverlayType kOverlayType = Overlay::OverlayType::Legacy;

TEST(OverlayGoldMasterTest, can_load_overlay_v2) {
  // eden/test-data/overlay-v2.tgz contains a saved copy of an overlay
  // directory generated by edenfs.  Unpack it into a temporary directory,
  // then try loading it.
  //
  // This test helps ensure that new edenfs versions can still successfully load
  // this overlay format even if we change how the overlay is saved in the
  // future.
  std::string overlayPath("eden/test-data/overlay-v2.tgz");

  // Support receiving the resource from Buck.
  if (auto overlayPathFromEnv = getenv("RESOURCE_OVERLAY_V2")) {
    overlayPath = overlayPathFromEnv;
  }

  auto tmpdir = makeTempDir("eden_test");
  SpawnedProcess tarProcess(
      {"/usr/bin/tar", "-xzf", overlayPath, "-C", tmpdir.path().string()});
  EXPECT_EQ(tarProcess.wait().str(), "exited with status 0");

  auto overlay = Overlay::create(
      realpath(tmpdir.path().string()) + "overlay-v2"_pc,
      kPathMapDefaultCaseSensitive,
      kOverlayType,
      std::make_shared<NullStructuredLogger>(),
      *EdenConfig::createTestEdenConfig());
  overlay->initialize(EdenConfig::createTestEdenConfig()).get();

  ObjectId hash1{folly::ByteRange{"abcdabcdabcdabcdabcd"_sp}};
  ObjectId hash2{folly::ByteRange{"01234012340123401234"_sp}};
  ObjectId hash3{folly::ByteRange{"e0e0e0e0e0e0e0e0e0e0"_sp}};
  ObjectId hash4{folly::ByteRange{"44444444444444444444"_sp}};

  auto rootTree = overlay->loadOverlayDir(kRootNodeId);
  auto file = overlay->openFile(2_ino, FsOverlay::kHeaderIdentifierFile);
  auto subdir = overlay->loadOverlayDir(3_ino);
  auto emptyDir = overlay->loadOverlayDir(4_ino);
  auto hello = overlay->openFile(5_ino, FsOverlay::kHeaderIdentifierFile);

  ASSERT_TRUE(!rootTree.empty());
  EXPECT_EQ(2, rootTree.size());
  const auto& fileEntry = rootTree.at("file"_pc);
  EXPECT_EQ(2_ino, fileEntry.getInodeNumber());
  EXPECT_EQ(hash1, fileEntry.getHash());
  EXPECT_EQ(S_IFREG | 0644, fileEntry.getInitialMode());
  const auto& subdirEntry = rootTree.at("subdir"_pc);
  EXPECT_EQ(3_ino, subdirEntry.getInodeNumber());
  EXPECT_EQ(hash2, subdirEntry.getHash());
  EXPECT_EQ(S_IFDIR | 0755, subdirEntry.getInitialMode());

  EXPECT_TRUE(file.lseek(FsOverlay::kHeaderLength, SEEK_SET).hasValue());
  auto result = file.readFile();
  EXPECT_FALSE(result.hasError());
  EXPECT_EQ("contents", result.value());

  ASSERT_TRUE(!subdir.empty());
  EXPECT_EQ(2, subdir.size());
  const auto& emptyEntry = subdir.at("empty"_pc);
  EXPECT_EQ(4_ino, emptyEntry.getInodeNumber());
  EXPECT_EQ(hash3, emptyEntry.getHash());
  EXPECT_EQ(S_IFDIR | 0755, emptyEntry.getInitialMode());
  const auto& helloEntry = subdir.at("hello"_pc);
  EXPECT_EQ(5_ino, helloEntry.getInodeNumber());
  EXPECT_EQ(hash4, helloEntry.getHash());
  EXPECT_EQ(S_IFREG | 0644, helloEntry.getInitialMode());

  ASSERT_TRUE(emptyDir.empty());

  EXPECT_TRUE(hello.lseek(FsOverlay::kHeaderLength, SEEK_SET).hasValue());
  result = file.readFile();
  EXPECT_FALSE(result.hasError());
  EXPECT_EQ("", result.value());
}

class OverlayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up a directory structure that we will use for most
    // of the tests below
    FakeTreeBuilder builder;
    builder.setFiles({
        {"dir/a.txt", "This is a.txt.\n"},
    });
    mount_.initialize(builder);
  }

  TestMount mount_;
};

TEST_F(OverlayTest, testRemount) {
  mount_.addFile("dir/new.txt", "test\n");
  mount_.remount();
  // Confirm that the tree has been updated correctly.
  auto newInode = mount_.getFileInode("dir/new.txt");
  EXPECT_FILE_INODE(newInode, "test\n", 0644);
}

TEST_F(OverlayTest, testModifyRemount) {
  // inode object has to be destroyed
  // before remount is called to release the reference
  {
    auto inode = mount_.getFileInode("dir/a.txt");
    EXPECT_FILE_INODE(inode, "This is a.txt.\n", 0644);
  }

  // materialize a directory
  mount_.overwriteFile("dir/a.txt", "contents changed\n");
  mount_.remount();

  auto newInode = mount_.getFileInode("dir/a.txt");
  EXPECT_FILE_INODE(newInode, "contents changed\n", 0644);
}

// In memory timestamps should be same before and after a remount.
// (inmemory timestamps should be written to overlay on
// on unmount and should be read back from the overlay on remount)
TEST_F(OverlayTest, testTimeStampsInOverlayOnMountAndUnmount) {
  // Materialize file and directory
  // test timestamp behavior in overlay on remount.
  InodeTimestamps beforeRemountFile;
  InodeTimestamps beforeRemountDir;
  mount_.overwriteFile("dir/a.txt", "contents changed\n");

  {
    // We do not want to keep references to inode in order to remount.
    auto inodeFile = mount_.getFileInode("dir/a.txt");
    EXPECT_FILE_INODE(inodeFile, "contents changed\n", 0644);
    beforeRemountFile = inodeFile->getMetadata().timestamps;
  }

  {
    // Check for materialized files.
    mount_.remount();
    auto inodeRemount = mount_.getFileInode("dir/a.txt");
    auto afterRemount = inodeRemount->getMetadata().timestamps;
    EXPECT_EQ(beforeRemountFile, afterRemount);
  }

  {
    auto inodeDir = mount_.getTreeInode("dir");
    beforeRemountDir = inodeDir->getMetadata().timestamps;
  }

  {
    // Check for materialized directory
    mount_.remount();
    auto inodeRemount = mount_.getTreeInode("dir");
    auto afterRemount = inodeRemount->getMetadata().timestamps;
    EXPECT_EQ(beforeRemountDir, afterRemount);
  }
}

TEST_F(OverlayTest, roundTripThroughSaveAndLoad) {
  auto hash = ObjectId::fromHex("0123456789012345678901234567890123456789");

  auto overlay = mount_.getEdenMount()->getOverlay();

  auto ino1 = overlay->allocateInodeNumber();
  auto ino2 = overlay->allocateInodeNumber();
  auto ino3 = overlay->allocateInodeNumber();

  DirContents dir(kPathMapDefaultCaseSensitive);
  dir.emplace("one"_pc, S_IFREG | 0644, ino2, hash);
  dir.emplace("two"_pc, S_IFDIR | 0755, ino3);

  overlay->saveOverlayDir(ino1, dir);

  auto result = overlay->loadOverlayDir(ino1);
  ASSERT_TRUE(!result.empty());

  EXPECT_EQ(2, result.size());
  const auto& one = result.find("one"_pc)->second;
  const auto& two = result.find("two"_pc)->second;
  EXPECT_EQ(ino2, one.getInodeNumber());
  EXPECT_FALSE(one.isMaterialized());
  EXPECT_EQ(ino3, two.getInodeNumber());
  EXPECT_TRUE(two.isMaterialized());
}

TEST_F(OverlayTest, getFilePath) {
  InodePath path;

  path = FsOverlay::getFilePath(1_ino);
  EXPECT_EQ("01/1"_relpath, path);
  path = FsOverlay::getFilePath(1234_ino);
  EXPECT_EQ("d2/1234"_relpath, path);

  // It's slightly unfortunate that we use hexadecimal for the subdirectory
  // name and decimal for the final inode path.  That doesn't seem worth fixing
  // for now.
  path = FsOverlay::getFilePath(15_ino);
  EXPECT_EQ("0f/15"_relpath, path);
  path = FsOverlay::getFilePath(16_ino);
  EXPECT_EQ("10/16"_relpath, path);
}

TEST(PlainOverlayTest, new_overlay_is_clean) {
  folly::test::TemporaryDirectory testDir;
  auto overlay = Overlay::create(
      AbsolutePath{testDir.path().string()},
      kPathMapDefaultCaseSensitive,
      kOverlayType,
      std::make_shared<NullStructuredLogger>(),
      *EdenConfig::createTestEdenConfig());
  overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  EXPECT_TRUE(overlay->hadCleanStartup());
}

TEST(PlainOverlayTest, reopened_overlay_is_clean) {
  folly::test::TemporaryDirectory testDir;
  {
    auto overlay = Overlay::create(
        AbsolutePath{testDir.path().string()},
        kPathMapDefaultCaseSensitive,
        kOverlayType,
        std::make_shared<NullStructuredLogger>(),
        *EdenConfig::createTestEdenConfig());
    overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  }

  auto overlay = Overlay::create(
      AbsolutePath{testDir.path().string()},
      kPathMapDefaultCaseSensitive,
      kOverlayType,
      std::make_shared<NullStructuredLogger>(),
      *EdenConfig::createTestEdenConfig());
  overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  EXPECT_TRUE(overlay->hadCleanStartup());
}

TEST(PlainOverlayTest, unclean_overlay_is_dirty) {
  folly::test::TemporaryDirectory testDir;
  auto localDir = AbsolutePath{testDir.path().string()};

  {
    auto overlay = Overlay::create(
        AbsolutePath{testDir.path().string()},
        kPathMapDefaultCaseSensitive,
        kOverlayType,
        std::make_shared<NullStructuredLogger>(),
        *EdenConfig::createTestEdenConfig());
    overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  }

  if (unlink((localDir + "next-inode-number"_pc).c_str())) {
    folly::throwSystemError("removing saved inode number");
  }

  auto overlay = Overlay::create(
      AbsolutePath{testDir.path().string()},
      kPathMapDefaultCaseSensitive,
      kOverlayType,
      std::make_shared<NullStructuredLogger>(),
      *EdenConfig::createTestEdenConfig());
  overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  EXPECT_FALSE(overlay->hadCleanStartup());
}

enum class OverlayRestartMode {
  CLEAN,
  UNCLEAN,
};

class RawOverlayTest : public ::testing::TestWithParam<OverlayRestartMode> {
 public:
  RawOverlayTest() : testDir_{makeTempDir("eden_raw_overlay_test_")} {
    loadOverlay();
  }

  void recreate(std::optional<OverlayRestartMode> restartMode = std::nullopt) {
    unloadOverlay(restartMode);
    loadOverlay();
  }

  void unloadOverlay(
      std::optional<OverlayRestartMode> restartMode = std::nullopt) {
    overlay->close();
    overlay = nullptr;
    switch (restartMode.value_or(GetParam())) {
      case OverlayRestartMode::CLEAN:
        break;
      case OverlayRestartMode::UNCLEAN:
        if (unlink((getLocalDir() + "next-inode-number"_pc).c_str())) {
          folly::throwSystemError("removing saved inode number");
        }
        break;
    }
  }

  void loadOverlay() {
    overlay = Overlay::create(
        getLocalDir(),
        kPathMapDefaultCaseSensitive,
        kOverlayType,
        std::make_shared<NullStructuredLogger>(),
        *EdenConfig::createTestEdenConfig());
    overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  }

  void corruptOverlayFile(InodeNumber inodeNumber) {
    corruptOverlayFileByTruncating(inodeNumber);
  }

  void corruptOverlayFileByTruncating(InodeNumber inodeNumber) {
    EXPECT_FALSE(overlay) << "Overlay should not be open when corrupting";
    folly::checkUnixError(
        folly::truncateNoInt(getOverlayFilePath(inodeNumber).c_str(), 0));
  }

  void corruptOverlayFileByDeleting(InodeNumber inodeNumber) {
    EXPECT_FALSE(overlay) << "Overlay should not be open when corrupting";
    folly::checkUnixError(unlink(getOverlayFilePath(inodeNumber).c_str()));
  }

  AbsolutePath getOverlayFilePath(InodeNumber inodeNumber) {
    return getLocalDir() +
        RelativePathPiece{FsOverlay::getFilePath(inodeNumber)};
  }

  AbsolutePath getLocalDir() {
    return AbsolutePath{testDir_.path().string()};
  }

  folly::test::TemporaryDirectory testDir_;
  std::shared_ptr<Overlay> overlay;
};

TEST_P(RawOverlayTest, closed_overlay_stress_test) {
  constexpr unsigned kThreadCount = 10;
  auto executor = folly::CPUThreadPoolExecutor(kThreadCount + 1);

  std::vector<folly::Future<folly::Unit>> futures;
  futures.reserve(kThreadCount);
  folly::test::Barrier gate{kThreadCount + 1};

  for (unsigned i = 0; i < kThreadCount; ++i) {
    futures.emplace_back(folly::via(&executor, [&] {
      auto ino = overlay->allocateInodeNumber();
      OverlayFile result;
      try {
        result =
            overlay->createOverlayFile(ino, folly::ByteRange{"contents"_sp});
      } catch (std::system_error& e) {
        if ("cannot access overlay after it is closed: Input/output error"_sp !=
            e.what()) {
          printf("createOverlayFile failed: %s\n", e.what());
          throw e;
        }
        // The Overlay is already closed, so just return successfully.
        gate.wait();
        return;
      }

      // Block until after overlay has closed
      gate.wait();

      ASSERT_TRUE(overlay->isClosed());

      try {
        char data[] = "new contents";
        struct iovec iov;
        iov.iov_base = data;
        iov.iov_len = sizeof(data);
        result.pwritev(&iov, 1, FsOverlay::kHeaderLength).value();
        throw std::system_error(
            EIO,
            std::generic_category(),
            "should not be able to successfully write to overlay");
      } catch (std::system_error& e) {
        if (strcmp(
                e.what(),
                "cannot access overlay after it is closed: Input/output error")) {
          printf("pwritev failed: %s\n", e.what());
          throw e;
        }
      }
    }));
  }

  overlay->close();

  // Wake the waiting threads
  gate.wait();

  auto finished = folly::collectAllUnsafe(futures).get();
  for (auto& f : finished) {
    EXPECT_FALSE(f.hasException()) << f.exception().what();
  }
}

TEST_P(RawOverlayTest, cannot_create_overlay_file_in_corrupt_overlay) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  // Remove the overlay directory in order to make file creation fail.
  auto path = testDir_.path();
  boost::filesystem::remove_all(path);

  EXPECT_THROW(
      overlay->createOverlayFile(ino2, folly::ByteRange{"contents"_sp}),
      std::system_error);

  // Restore the overlay directory and make sure we can create an overlay file
  // and close the overlay.
  boost::filesystem::create_directory(path);
  loadOverlay();

  ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  EXPECT_NO_THROW(
      overlay->createOverlayFile(ino2, folly::ByteRange{"contents"_sp}));
  overlay->close();
}

TEST_P(RawOverlayTest, cannot_save_overlay_dir_when_closed) {
  overlay->close();
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  DirContents dir(kPathMapDefaultCaseSensitive);
  EXPECT_THROW_RE(
      overlay->saveOverlayDir(ino2, dir),
      std::system_error,
      "cannot access overlay after it is closed");
}

TEST_P(RawOverlayTest, cannot_create_overlay_file_when_closed) {
  overlay->close();
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  EXPECT_THROW_RE(
      overlay->createOverlayFile(ino2, folly::ByteRange{"contents"_sp}),
      std::system_error,
      "cannot access overlay after it is closed");
}

TEST_P(RawOverlayTest, cannot_remove_overlay_file_when_closed) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  EXPECT_NO_THROW(
      overlay->createOverlayFile(ino2, folly::ByteRange{"contents"_sp}));

  overlay->close();

  EXPECT_THROW_RE(
      overlay->removeOverlayData(ino2),
      std::system_error,
      "cannot access overlay after it is closed");
}

TEST_P(RawOverlayTest, max_inode_number_is_1_if_overlay_is_empty) {
  EXPECT_EQ(kRootNodeId, overlay->getMaxInodeNumber());
  EXPECT_EQ(2_ino, overlay->allocateInodeNumber());

  recreate(OverlayRestartMode::CLEAN);

  EXPECT_EQ(2_ino, overlay->getMaxInodeNumber());
  EXPECT_EQ(3_ino, overlay->allocateInodeNumber());

  recreate(OverlayRestartMode::UNCLEAN);

  EXPECT_EQ(kRootNodeId, overlay->getMaxInodeNumber());
  EXPECT_EQ(2_ino, overlay->allocateInodeNumber());
}

TEST_P(RawOverlayTest, remembers_max_inode_number_of_tree_inodes) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);

  DirContents dir(kPathMapDefaultCaseSensitive);
  overlay->saveOverlayDir(ino2, dir);

  recreate();

  EXPECT_EQ(2_ino, overlay->getMaxInodeNumber());
}

TEST_P(RawOverlayTest, remembers_max_inode_number_of_tree_entries) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);
  auto ino3 = overlay->allocateInodeNumber();
  auto ino4 = overlay->allocateInodeNumber();

  DirContents dir(kPathMapDefaultCaseSensitive);
  dir.emplace(PathComponentPiece{"f"}, S_IFREG | 0644, ino3);
  dir.emplace(PathComponentPiece{"d"}, S_IFDIR | 0755, ino4);
  overlay->saveOverlayDir(kRootNodeId, dir);

  recreate();

  SCOPED_TRACE("Inodes:\n" + debugDumpOverlayInodes(*overlay, kRootNodeId));
  EXPECT_EQ(4_ino, overlay->getMaxInodeNumber());
}

TEST_P(RawOverlayTest, remembers_max_inode_number_of_file) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);
  auto ino3 = overlay->allocateInodeNumber();

  // When materializing, overlay data is written leaf-to-root.

  // The File is written first.
  overlay->createOverlayFile(ino3, folly::ByteRange{"contents"_sp});

  recreate();

  EXPECT_EQ(3_ino, overlay->getMaxInodeNumber());
}

TEST_P(
    RawOverlayTest,
    inode_number_scan_includes_linked_directory_despite_its_corruption) {
  auto subdirectoryIno = overlay->allocateInodeNumber();
  auto rootIno = kRootNodeId;
  ASSERT_GT(subdirectoryIno, rootIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace("subdirectory"_pc, S_IFDIR | 0755, subdirectoryIno);
  overlay->saveOverlayDir(rootIno, root);

  overlay->saveOverlayDir(
      subdirectoryIno, DirContents(kPathMapDefaultCaseSensitive));

  unloadOverlay();
  corruptOverlayFile(subdirectoryIno);
  loadOverlay();

  EXPECT_EQ(subdirectoryIno, overlay->getMaxInodeNumber());
}

TEST_P(
    RawOverlayTest,
    inode_number_scan_continues_scanning_despite_corrupted_directory) {
  // Check that the next inode number is recomputed correctly even in the
  // presence of corrupted directory data in the overlay.
  //
  // The old scan algorithm we used to used would traverse down the directory
  // tree, so we needed to ensure that it still found orphan parts of the tree.
  // The newer OverlayChecker code uses a completely different algorithm which
  // isn't susceptible to this same problem, but it still seems worth testing
  // this behavior.
  //
  // We test with the following overlay structure:
  //
  //   /                               (rootIno)
  //     corrupted_by_truncation/      (corruptedByTruncationIno)
  //     temp/                         (tempDirIno)
  //       temp/corrupted_by_deletion  (corruptedByDeletionIno)
  //

  struct PathNames {
    PathComponentPiece corruptedByTruncationName;
    PathComponentPiece tempName;
  };

  auto rootIno = kRootNodeId;
  auto corruptedByTruncationIno = InodeNumber{};
  auto tempDirIno = InodeNumber{};
  auto corruptedByDeletionIno = InodeNumber{};

  auto setUpOverlay = [&](const PathNames& pathNames) {
    DirContents root(kPathMapDefaultCaseSensitive);
    root.emplace(
        pathNames.corruptedByTruncationName,
        S_IFDIR | 0755,
        corruptedByTruncationIno);
    root.emplace(pathNames.tempName, S_IFDIR | 0755, tempDirIno);
    overlay->saveOverlayDir(rootIno, root);

    overlay->saveOverlayDir(
        corruptedByTruncationIno, DirContents(kPathMapDefaultCaseSensitive));

    DirContents tempDir(kPathMapDefaultCaseSensitive);
    tempDir.emplace(
        "corrupted_by_deletion"_pc, S_IFDIR | 0755, corruptedByDeletionIno);
    overlay->saveOverlayDir(tempDirIno, tempDir);

    overlay->saveOverlayDir(
        corruptedByDeletionIno, DirContents(kPathMapDefaultCaseSensitive));
  };

  const PathNames pathNamesToTest[] = {
      // Test a few different path name variations, to ensure traversal order
      // doesn't matter.
      PathNames{
          .corruptedByTruncationName = "A_corrupted_by_truncation"_pc,
          .tempName = "B_temp"_pc},
      PathNames{
          .corruptedByTruncationName = "B_corrupted_by_truncation"_pc,
          .tempName = "A_temp"_pc},
  };

  for (auto pathNames : pathNamesToTest) {
    corruptedByTruncationIno = overlay->allocateInodeNumber();
    tempDirIno = overlay->allocateInodeNumber();
    corruptedByDeletionIno = overlay->allocateInodeNumber();
    auto maxIno = std::max(
        {tempDirIno, corruptedByTruncationIno, corruptedByDeletionIno});
    ASSERT_EQ(corruptedByDeletionIno, maxIno);

    setUpOverlay(pathNames);

    SCOPED_TRACE(
        "Inodes before corruption:\n" +
        debugDumpOverlayInodes(*overlay, rootIno));

    unloadOverlay();
    corruptOverlayFileByTruncating(corruptedByTruncationIno);
    corruptOverlayFileByDeleting(corruptedByDeletionIno);
    loadOverlay();

    EXPECT_EQ(maxIno, overlay->getMaxInodeNumber());
  }
}

TEST_P(RawOverlayTest, inode_numbers_not_reused_after_unclean_shutdown) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);
  overlay->allocateInodeNumber();
  auto ino4 = overlay->allocateInodeNumber();
  auto ino5 = overlay->allocateInodeNumber();

  // When materializing, overlay data is written leaf-to-root.

  // The File is written first.
  overlay->createOverlayFile(ino5, folly::ByteRange{"contents"_sp});

  // The subdir is written next.
  DirContents subdir(kPathMapDefaultCaseSensitive);
  subdir.emplace(PathComponentPiece{"f"}, S_IFREG | 0644, ino5);
  overlay->saveOverlayDir(ino4, subdir);

  // Crashed before root was written.

  recreate();

  SCOPED_TRACE(
      "Inodes from subdir:\n" + debugDumpOverlayInodes(*overlay, ino4));
  EXPECT_EQ(5_ino, overlay->getMaxInodeNumber());
}

TEST_P(RawOverlayTest, inode_numbers_after_takeover) {
  auto ino2 = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, ino2);
  auto ino3 = overlay->allocateInodeNumber();
  auto ino4 = overlay->allocateInodeNumber();
  auto ino5 = overlay->allocateInodeNumber();

  // Write a subdir.
  DirContents subdir(kPathMapDefaultCaseSensitive);
  subdir.emplace(PathComponentPiece{"f"}, S_IFREG | 0644, ino5);
  overlay->saveOverlayDir(ino4, subdir);

  // Write the root.
  DirContents dir(kPathMapDefaultCaseSensitive);
  dir.emplace(PathComponentPiece{"f"}, S_IFREG | 0644, ino3);
  dir.emplace(PathComponentPiece{"d"}, S_IFDIR | 0755, ino4);
  overlay->saveOverlayDir(kRootNodeId, dir);

  recreate();

  // Rewrite the root (say, after a takeover) without the file.

  DirContents newroot(kPathMapDefaultCaseSensitive);
  newroot.emplace(PathComponentPiece{"d"}, S_IFDIR | 0755, 4_ino);
  overlay->saveOverlayDir(kRootNodeId, newroot);

  recreate(OverlayRestartMode::CLEAN);

  SCOPED_TRACE("Inodes:\n" + debugDumpOverlayInodes(*overlay, kRootNodeId));
  // Ensure an inode in the overlay but not referenced by the previous session
  // counts.
  EXPECT_EQ(5_ino, overlay->getMaxInodeNumber());
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
INSTANTIATE_TEST_CASE_P(
    Clean,
    RawOverlayTest,
    ::testing::Values(OverlayRestartMode::CLEAN));

INSTANTIATE_TEST_CASE_P(
    Unclean,
    RawOverlayTest,
    ::testing::Values(OverlayRestartMode::UNCLEAN));
#pragma clang diagnostic pop

TEST(OverlayInodePath, defaultInodePathIsEmpty) {
  InodePath path;
  EXPECT_STREQ(path.c_str(), "");
}

class DebugDumpOverlayInodesTest : public ::testing::Test {
 public:
  DebugDumpOverlayInodesTest()
      : testDir_{makeTempDir("eden_DebugDumpOverlayInodesTest")},
        overlay{Overlay::create(
            AbsolutePathPiece{testDir_.path().string()},
            kPathMapDefaultCaseSensitive,
            kOverlayType,
            std::make_shared<NullStructuredLogger>(),
            *EdenConfig::createTestEdenConfig())} {
    overlay->initialize(EdenConfig::createTestEdenConfig()).get();
  }

  folly::test::TemporaryDirectory testDir_;
  std::shared_ptr<Overlay> overlay;
};

TEST_F(DebugDumpOverlayInodesTest, dump_empty_directory) {
  auto ino = kRootNodeId;
  EXPECT_EQ(1_ino, ino);

  overlay->saveOverlayDir(ino, DirContents(kPathMapDefaultCaseSensitive));
  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (0 total):\n",
      debugDumpOverlayInodes(*overlay, ino));
}

TEST_F(DebugDumpOverlayInodesTest, dump_directory_with_3_regular_files) {
  auto rootIno = kRootNodeId;
  EXPECT_EQ(1_ino, rootIno);
  auto fileAIno = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, fileAIno);
  auto fileBIno = overlay->allocateInodeNumber();
  EXPECT_EQ(3_ino, fileBIno);
  auto fileCIno = overlay->allocateInodeNumber();
  EXPECT_EQ(4_ino, fileCIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace("file_a"_pc, S_IFREG | 0644, fileAIno);
  root.emplace("file_b"_pc, S_IFREG | 0644, fileBIno);
  root.emplace("file_c"_pc, S_IFREG | 0644, fileCIno);
  overlay->saveOverlayDir(rootIno, root);

  overlay->createOverlayFile(fileAIno, folly::ByteRange{""_sp});
  overlay->createOverlayFile(fileBIno, folly::ByteRange{""_sp});
  overlay->createOverlayFile(fileCIno, folly::ByteRange{""_sp});

  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (3 total):\n"
      "            2 f  644 file_a\n"
      "            3 f  644 file_b\n"
      "            4 f  644 file_c\n",
      debugDumpOverlayInodes(*overlay, rootIno));
}

TEST_F(DebugDumpOverlayInodesTest, dump_directory_with_an_empty_subdirectory) {
  auto rootIno = kRootNodeId;
  EXPECT_EQ(1_ino, rootIno);
  auto subdirIno = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, subdirIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace("subdir"_pc, S_IFDIR | 0755, subdirIno);
  overlay->saveOverlayDir(rootIno, root);

  overlay->saveOverlayDir(subdirIno, DirContents(kPathMapDefaultCaseSensitive));

  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (1 total):\n"
      "            2 d  755 subdir\n"
      "/subdir\n"
      "  Inode number: 2\n"
      "  Entries (0 total):\n",
      debugDumpOverlayInodes(*overlay, rootIno));
}

TEST_F(DebugDumpOverlayInodesTest, dump_directory_with_unsaved_subdirectory) {
  auto rootIno = kRootNodeId;
  EXPECT_EQ(1_ino, rootIno);
  auto directoryDoesNotExistIno = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, directoryDoesNotExistIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace(
      "directory_does_not_exist"_pc, S_IFDIR | 0755, directoryDoesNotExistIno);
  overlay->saveOverlayDir(rootIno, root);

  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (1 total):\n"
      "            2 d  755 directory_does_not_exist\n"
      "/directory_does_not_exist\n"
      "  Inode number: 2\n"
      "  Entries (0 total):\n",
      debugDumpOverlayInodes(*overlay, rootIno));
}

TEST_F(DebugDumpOverlayInodesTest, dump_directory_with_unsaved_regular_file) {
  auto rootIno = kRootNodeId;
  EXPECT_EQ(1_ino, rootIno);
  auto regularFileDoesNotExistIno = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, regularFileDoesNotExistIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace(
      "regular_file_does_not_exist"_pc,
      S_IFREG | 0644,
      regularFileDoesNotExistIno);
  overlay->saveOverlayDir(rootIno, root);

  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (1 total):\n"
      "            2 f  644 regular_file_does_not_exist\n",
      debugDumpOverlayInodes(*overlay, rootIno));
}

TEST_F(DebugDumpOverlayInodesTest, directories_are_dumped_depth_first) {
  auto rootIno = kRootNodeId;
  EXPECT_EQ(1_ino, rootIno);
  auto subdirAIno = overlay->allocateInodeNumber();
  EXPECT_EQ(2_ino, subdirAIno);
  auto subdirAXIno = overlay->allocateInodeNumber();
  EXPECT_EQ(3_ino, subdirAXIno);
  auto subdirAYIno = overlay->allocateInodeNumber();
  EXPECT_EQ(4_ino, subdirAYIno);
  auto subdirBIno = overlay->allocateInodeNumber();
  EXPECT_EQ(5_ino, subdirBIno);
  auto subdirBXIno = overlay->allocateInodeNumber();
  EXPECT_EQ(6_ino, subdirBXIno);

  DirContents root(kPathMapDefaultCaseSensitive);
  root.emplace("subdir_a"_pc, S_IFDIR | 0755, subdirAIno);
  root.emplace("subdir_b"_pc, S_IFDIR | 0755, subdirBIno);
  overlay->saveOverlayDir(rootIno, root);

  DirContents subdirA(kPathMapDefaultCaseSensitive);
  subdirA.emplace("x"_pc, S_IFDIR | 0755, subdirAXIno);
  subdirA.emplace("y"_pc, S_IFDIR | 0755, subdirAYIno);
  overlay->saveOverlayDir(subdirAIno, subdirA);

  DirContents subdirB(kPathMapDefaultCaseSensitive);
  subdirB.emplace("x"_pc, S_IFDIR | 0755, subdirBXIno);
  overlay->saveOverlayDir(subdirBIno, subdirB);

  overlay->saveOverlayDir(
      subdirAXIno, DirContents(kPathMapDefaultCaseSensitive));
  overlay->saveOverlayDir(
      subdirAYIno, DirContents(kPathMapDefaultCaseSensitive));
  overlay->saveOverlayDir(
      subdirBXIno, DirContents(kPathMapDefaultCaseSensitive));

  EXPECT_EQ(
      "/\n"
      "  Inode number: 1\n"
      "  Entries (2 total):\n"
      "            2 d  755 subdir_a\n"
      "            5 d  755 subdir_b\n"
      "/subdir_a\n"
      "  Inode number: 2\n"
      "  Entries (2 total):\n"
      "            3 d  755 x\n"
      "            4 d  755 y\n"
      "/subdir_a/x\n"
      "  Inode number: 3\n"
      "  Entries (0 total):\n"
      "/subdir_a/y\n"
      "  Inode number: 4\n"
      "  Entries (0 total):\n"
      "/subdir_b\n"
      "  Inode number: 5\n"
      "  Entries (1 total):\n"
      "            6 d  755 x\n"
      "/subdir_b/x\n"
      "  Inode number: 6\n"
      "  Entries (0 total):\n",
      debugDumpOverlayInodes(*overlay, rootIno));
}

} // namespace facebook::eden

#endif
