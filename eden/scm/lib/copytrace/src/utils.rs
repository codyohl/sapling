/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This software may be used and distributed according to the terms of the
 * GNU General Public License version 2.
 */

use types::RepoPath;

/// Contants for path similarity score. The actually value does not matter
/// here, we are more care about the ordering. Based on Git community's data,
/// ~70%+ of renames have the same basename:
/// https://github.com/git/git/blob/74cc1aa55f30ed76424a0e7226ab519aa6265061/diffcore-rename.c#L904-L907

/// Computes the similarity score between file paths.
///
/// The score will be in range [0.0, 1.0]. Higher number means more similar.
/// The algorithm here is a simple diff algorithm to calculate the similarity
/// bettween two file paths, which should cover below cases, and it will have
/// better result for common files like 'lib.rs', '__init__.py', etc.
///   - moving from one directory to antoher (same basename)
///   - moving inside a directory (same directory name)
pub(crate) fn file_path_similarity(p1: &RepoPath, p2: &RepoPath) -> f64 {
    let p1_chars: Vec<char> = p1.as_str().chars().collect();
    let p2_chars: Vec<char> = p2.as_str().chars().collect();

    let mut start = 0;
    while start < p1_chars.len() && start < p2_chars.len() && p1_chars[start] == p2_chars[start] {
        start += 1;
    }

    let mut end = 0;
    while end < p1_chars.len() - start
        && end < p2_chars.len() - start
        && p1_chars[p1_chars.len() - 1 - end] == p2_chars[p2_chars.len() - 1 - end]
    {
        end += 1;
    }

    (start + end) as f64 * 2.0 / (p1_chars.len() + p2_chars.len()) as f64
}

#[cfg(test)]
mod tests {
    use super::*;

    fn get_most_similar_path<'a>(candidates: &[&'a str], target: &'a str) -> &'a str {
        let mut candidates: Vec<&RepoPath> = candidates
            .iter()
            .map(|x| RepoPath::from_str(x).unwrap())
            .collect();
        let target = RepoPath::from_str(target).unwrap();
        candidates.sort_by_key(|path| {
            let score = (file_path_similarity(path, target) * 1000.0) as i32;
            (-score, path.to_owned())
        });
        let first = candidates.first().unwrap();
        first.as_str()
    }

    #[test]
    fn test_directory_move() {
        // rename 'edenscm' -> 'sapling'
        let candidates = vec![
            "ab/cd/edenscm/1/lib.rs",
            "ab/cd/edenscm/2/lib.rs",
            "ab/cd/edenscm/3/lib.rs",
            "ab/cd/edenscm/4/lib.rs",
            "ab/cd/edenscm/1/rename.rs",
            "a.txt",
        ];

        assert_eq!(
            get_most_similar_path(&candidates, "ab/cd/sapling/1/lib.rs"),
            "ab/cd/edenscm/1/lib.rs",
        );
        assert_eq!(
            get_most_similar_path(&candidates, "ab/cd/sapling/4/lib.rs"),
            "ab/cd/edenscm/4/lib.rs",
        );
        assert_eq!(
            get_most_similar_path(&candidates, "ab/cd/sapling/1/rename.rs"),
            "ab/cd/edenscm/1/rename.rs",
        );
        assert_eq!(get_most_similar_path(&candidates, "b.txt"), "a.txt",);
    }

    #[test]
    fn test_batch_moves() {
        // rename *.txt to *.md
        let candidates = vec!["a/b/4.txt", "a/b/1.txt", "a/b/2.txt", "a/b/3.txt"];

        assert_eq!(get_most_similar_path(&candidates, "a/b/1.md"), "a/b/1.txt",);
        assert_eq!(get_most_similar_path(&candidates, "a/b/2.md"), "a/b/2.txt",);
        assert_eq!(get_most_similar_path(&candidates, "a/b/3.md"), "a/b/3.txt",);
    }
}
