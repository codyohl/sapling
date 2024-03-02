/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#pragma once

#include "eden/common/utils/ImmediateFuture.h"
#include "eden/common/utils/PathFuncs.h"
#include "eden/fs/store/ObjectFetchContext.h"

namespace facebook::eden {

class ObjectStore;
class Tree;

ImmediateFuture<std::shared_ptr<const Tree>> resolveTree(
    ObjectStore& objectStore,
    const ObjectFetchContextPtr& fetchContext,
    std::shared_ptr<const Tree> root,
    RelativePathPiece path);

} // namespace facebook::eden
