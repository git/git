# Metadata Drift Detection Spec

## Purpose

Ace operates by layering virtual metadata (`.git/ace/branches/`) on top of Git's standard refs (`refs/heads/ace/...`). Because Git allows users to directly mutate refs (e.g., `git branch -D`, `git rebase`, `git commit --amend`), the Ace metadata can fall out of sync with the underlying repository state. 

This spec defines how Ace should detect, explain, and help repair this "metadata drift".

## Types of Drift

1. **Dangling Virtual Branch (Missing Backing Ref)**
   - **Cause**: A user runs `git branch -D ace/feature-xyz` directly.
   - **Detection**: The file `.git/ace/branches/feature-xyz/ref` exists, but `git show-ref $(cat .../ref)` returns empty.
   
2. **Orphaned Backing Ref (Missing Metadata)**
   - **Cause**: The user deletes the `.git/ace/branches/` directory manually, or a file-system error occurs.
   - **Detection**: A branch matches `refs/heads/ace/*` but no corresponding virtual branch maps to it via `git ace resolve`.

3. **Topology Drift (Ancestry Broken)**
   - **Cause**: A user forces a reset or hard rebase on a parent branch, dropping commits that a child branch depends on.
   - **Detection**: `git merge-base parent child` does not equal `git rev-parse parent`. (The parent is no longer an ancestor of the child).

## Proposed UX & Explanations

Ace must never silently crash when encountering drift. It should explain the failure in Git-native terms.

**Example 1: Dangling Virtual Branch**
```text
$ git ace checkout feature/ui
Error: The virtual branch 'feature/ui' points to a backing Git ref that no longer exists (ace/feature_ui-a1b2c3).
Hint: If the branch was deleted manually, you can clear the Ace metadata using `git ace delete --force feature/ui`.
```

**Example 2: Topology Drift during Stack Operations**
```text
$ git ace status
Warning: Topology drift detected. 'feature/parser' no longer contains the history of its parent 'feature/base'.
Hint: This usually happens if 'feature/base' was rewritten. Use `git ace rebase-stack feature/base` to resync the stack.
```

## Repair Mechanisms

A future `git ace repair` command should be introduced:
- `git ace repair --clean-dangling`: Removes virtual branch metadata that has no backing Git ref.
- `git ace repair --adopt-orphans`: Scans `refs/heads/ace/*` and attempts to reconstruct virtual branches based on commit hashes and branch slugs.

## Testing Strategy

To validate drift detection:
1. Setup a valid Ace stack.
2. Shell out to `rm .git/refs/heads/ace/...` or `git branch -D`.
3. Assert that `git ace tree` and `git ace status` output the expected warnings and do not crash with generic shell errors.
