/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

#![cfg(test)]
#![allow(non_snake_case)] // For test commits

use std::collections::HashMap;
use std::collections::VecDeque;
use std::str::FromStr;

use anyhow::anyhow;
use anyhow::Error;
use anyhow::Result;
use fbinit::FacebookInit;
use futures::future::try_join_all;
use gitexport_tools::rewrite_partial_changesets;
use gitexport_tools::MASTER_BOOKMARK;
use mononoke_api::BookmarkFreshness;
use mononoke_api::BookmarkKey;
use mononoke_api::ChangesetContext;
use mononoke_api::CoreContext;
use mononoke_api::MononokeError;
use mononoke_types::ChangesetId;
use mononoke_types::NonRootMPath;
use test_utils::build_test_repo;
use test_utils::get_relevant_changesets_from_ids;
use test_utils::GitExportTestRepoOptions;
use test_utils::EXPORT_DIR;
use test_utils::EXPORT_FILE;
use test_utils::FILE_IN_SECOND_EXPORT_DIR;
use test_utils::SECOND_EXPORT_DIR;
use test_utils::SECOND_EXPORT_FILE;

#[fbinit::test]
async fn test_rewrite_partial_changesets(fb: FacebookInit) -> Result<(), Error> {
    let ctx = CoreContext::test_mock(fb);

    let export_dir = NonRootMPath::new(EXPORT_DIR).unwrap();
    let second_export_dir = NonRootMPath::new(SECOND_EXPORT_DIR).unwrap();

    let (source_repo_ctx, changeset_ids) =
        build_test_repo(fb, &ctx, GitExportTestRepoOptions::default()).await?;

    let A = changeset_ids["A"];
    let C = changeset_ids["C"];
    let E = changeset_ids["E"];
    let F = changeset_ids["F"];
    let G = changeset_ids["G"];
    let I = changeset_ids["I"];
    let J = changeset_ids["J"];

    // Test that changesets are rewritten when relevant changesets are given
    // topologically sorted
    let relevant_changeset_ids: Vec<ChangesetId> = vec![A, C, E, F, G, I, J];

    let relevant_changesets: Vec<ChangesetContext> =
        get_relevant_changesets_from_ids(&source_repo_ctx, relevant_changeset_ids).await?;

    let relevant_changeset_parents = HashMap::from([
        (A, vec![]),
        (C, vec![A]),
        (E, vec![C]),
        (F, vec![E]),
        (G, vec![F]),
        (I, vec![G]),
        (J, vec![I]),
    ]);

    let temp_repo_ctx = rewrite_partial_changesets(
        fb,
        source_repo_ctx.clone(),
        relevant_changesets.clone(),
        &relevant_changeset_parents,
        vec![export_dir.clone(), second_export_dir.clone()],
    )
    .await?;

    let temp_repo_master_csc = temp_repo_ctx
        .resolve_bookmark(
            &BookmarkKey::from_str(MASTER_BOOKMARK)?,
            BookmarkFreshness::MostRecent,
        )
        .await?
        .ok_or(anyhow!("Couldn't find master bookmark in temporary repo."))?;

    let mut parents_to_check: VecDeque<ChangesetId> = VecDeque::from([temp_repo_master_csc.id()]);
    let mut target_css = vec![];

    while let Some(changeset_id) = parents_to_check.pop_front() {
        let changeset = temp_repo_ctx
            .changeset(changeset_id)
            .await?
            .ok_or(anyhow!("Changeset not found in target repo"))?;

        changeset
            .parents()
            .await?
            .into_iter()
            .for_each(|parent| parents_to_check.push_back(parent));

        target_css.push(changeset);
    }

    // Order the changesets topologically
    target_css.reverse();

    assert_eq!(
        try_join_all(target_css.iter().map(ChangesetContext::message)).await?,
        try_join_all(relevant_changesets.iter().map(ChangesetContext::message)).await?
    );

    async fn get_msg_and_files_changed(
        cs: &ChangesetContext,
        file_filter: Box<dyn Fn(&NonRootMPath) -> bool>,
    ) -> Result<(String, Vec<NonRootMPath>), MononokeError> {
        let msg = cs.message().await?;
        let fcs = cs.file_changes().await?;

        let files_changed: Vec<NonRootMPath> = fcs
            .into_keys()
            .filter(file_filter)
            .collect::<Vec<NonRootMPath>>();

        Ok((msg, files_changed))
    }

    let result = try_join_all(
        target_css
            .iter()
            .map(|cs| get_msg_and_files_changed(cs, Box::new(|_p| true))),
    )
    .await?;

    fn build_expected_tuple(msg: &str, fpaths: Vec<&str>) -> (String, Vec<NonRootMPath>) {
        (
            String::from(msg),
            fpaths
                .iter()
                .map(|p| NonRootMPath::new(p).unwrap())
                .collect::<Vec<_>>(),
        )
    }

    assert_eq!(result.len(), 7);
    assert_eq!(result[0], build_expected_tuple("A", vec![EXPORT_FILE]));
    assert_eq!(result[1], build_expected_tuple("C", vec![EXPORT_FILE]));
    assert_eq!(result[2], build_expected_tuple("E", vec![EXPORT_FILE]));
    assert_eq!(
        result[3],
        build_expected_tuple("F", vec![FILE_IN_SECOND_EXPORT_DIR])
    );
    assert_eq!(
        result[4],
        build_expected_tuple("G", vec![EXPORT_FILE, FILE_IN_SECOND_EXPORT_DIR])
    );
    assert_eq!(
        result[5],
        build_expected_tuple("I", vec![SECOND_EXPORT_FILE])
    );
    assert_eq!(result[6], build_expected_tuple("J", vec![EXPORT_FILE]));

    Ok(())
}

#[fbinit::test]
async fn test_rewriting_fails_with_irrelevant_changeset(fb: FacebookInit) -> Result<(), Error> {
    let ctx = CoreContext::test_mock(fb);

    let export_dir = NonRootMPath::new(EXPORT_DIR).unwrap();

    let (source_repo_ctx, changeset_ids) =
        build_test_repo(fb, &ctx, GitExportTestRepoOptions::default()).await?;

    let A = changeset_ids["A"];
    let C = changeset_ids["C"];
    let D = changeset_ids["D"];
    let E = changeset_ids["E"];

    // Passing an irrelevant changeset in the list should result in an error
    let broken_changeset_list_ids: Vec<ChangesetId> = vec![A, C, D, E];

    let broken_changeset_list: Vec<ChangesetContext> =
        get_relevant_changesets_from_ids(&source_repo_ctx, broken_changeset_list_ids).await?;

    let broken_changeset_parents =
        HashMap::from([(A, vec![]), (C, vec![A]), (D, vec![C]), (E, vec![D])]);

    let error = rewrite_partial_changesets(
        fb,
        source_repo_ctx.clone(),
        broken_changeset_list.clone(),
        &broken_changeset_parents,
        vec![export_dir.clone()],
    )
    .await
    .unwrap_err();

    assert_eq!(
        error.to_string(),
        "internal error: Commit wasn't rewritten because it had no signficant changes"
    );

    Ok(())
}
