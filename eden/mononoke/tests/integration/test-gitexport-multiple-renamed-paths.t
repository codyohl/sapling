# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This software may be used and distributed according to the terms of the
# GNU General Public License found in the LICENSE file in the root
# directory of this source tree.

Setting up a simple scenario for the gitexport tool
  $ . "${TEST_FIXTURES}/library.sh"


Setup configuration
  $ REPOTYPE="blob_files"
  $ setup_common_config "$REPOTYPE"
  $ ENABLE_API_WRITES=1 REPOID=1 setup_mononoke_repo_config "temp_repo"
  $ cd $TESTTMP


Set some env vars that will be used frequently

  $ OLD_BAR="old_bar/file.txt"
  $ NEW_BAR="bar/file.txt"
  $ OLD_FOO="old_foo/file.txt"
  $ NEW_FOO="foo/file.txt"

# Scenario where multiple renames could lead to invalid references in the
# `copy_from` field in FileChanges. In this scenario, the `copy_from` should
# be removed and a warning should be printed to the user so they're aware
# of the possible rename and can re-run the tool passing the appropriate args.
  $ testtool_drawdag -R repo --derive-all --no-default-files <<EOF
  > A-B-C-D
  > # modify: A "$OLD_BAR" "first bar"
  > # copy: B "$NEW_BAR" "first bar" A "$OLD_BAR"
  > # delete: B "$OLD_BAR"
  > # modify: C "$OLD_FOO" "first foo"
  > # copy: D "$NEW_FOO" "first foo" C "$OLD_FOO"
  > # delete: D "$OLD_FOO"
  > # bookmark: D master
  > EOF
  A=4611de5cc4c4aebb12fe004b72e4bfb4fe3f6f92ecf4e7e13101aa21ee63f376
  B=4aefc65541bed48aa05912520e72886dc187846900552521fd609684b13bac29
  C=fe89c567605a899a5e59edf16eec50e70085fb989e5c799701285436c723fb0f
  D=3d2e1991a96782483be8a1437ad4e38849152d097c39cc4ec1bfdb5c371b7c79

  $ start_and_wait_for_mononoke_server
  $ hgmn_clone mononoke://$(mononoke_address)/repo repo
  $ cd repo
  $ hg -q co master
  $ SOURCE_REPO_LOG=$TESTTMP/source_repo_log
  $ hg log --git --template "{firstline(desc)}\n{stat()}\n" | sed -E 's/\s+\|\s+([0-9]+).+/ \| \1/' > $SOURCE_REPO_LOG




# -------------------- Use the gitexport tool --------------------


  $ GIT_BUNDLE_OUTPUT=git_bundle
  $ GIT_REPO=git_repo
  $ GIT_REPO_LOG=$TESTTMP/git_repo_log



Run the tool without passing the old name as an export path

  $ gitexport --log-level WARN --repo-name "repo" -B "master" -p "bar" -p "foo" -o "$GIT_BUNDLE_OUTPUT"
  *] Changeset 4aefc65541bed48aa05912520e72886dc187846900552521fd609684b13bac29 might have created the exported path bar by moving/copying files from a commit that might not be exported (id 4611de5cc4c4aebb12fe004b72e4bfb4fe3f6f92ecf4e7e13101aa21ee63f376). (glob)
  *] Changeset 3d2e1991a96782483be8a1437ad4e38849152d097c39cc4ec1bfdb5c371b7c79 might have created the exported path foo by moving/copying files from a commit that might not be exported (id fe89c567605a899a5e59edf16eec50e70085fb989e5c799701285436c723fb0f). (glob)

  $ git clone $GIT_BUNDLE_OUTPUT $GIT_REPO
  Cloning into 'git_repo'...
  $ cd $GIT_REPO

  $ git log --stat --pretty=format:"%s" | sed -E 's/\s+\|\s+([0-9]+).+/ \| \1/' > $GIT_REPO_LOG

  $ diff --old-line-format="- %L" --new-line-format="+ %L" "$SOURCE_REPO_LOG" "$GIT_REPO_LOG"
  D
   foo/file.txt | 1
  -  old_foo/file.txt | 1
  -  2 files changed, 1 insertions(+), 1 deletions(-)
  - 
  - C
  -  old_foo/file.txt | 1
  -  1 files changed, 1 insertions(+), 0 deletions(-)
  +  1 file changed, 1 insertion(+)
  
  B
   bar/file.txt | 1
  -  old_bar/file.txt | 1
  -  2 files changed, 1 insertions(+), 1 deletions(-)
  - 
  - A
  -  old_bar/file.txt | 1
  -  1 files changed, 1 insertions(+), 0 deletions(-)
  - 
  +  1 file changed, 1 insertion(+)
  [1]
