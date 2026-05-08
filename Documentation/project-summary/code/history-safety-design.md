# History and Replay Safety Design Notes

## Purpose

`git history` and `git replay` are powerful experimental commands. Because they rewrite history and directly update refs, they represent high-risk surfaces for user data loss. This document outlines the safety and preview design notes that must guide their stabilization.

## Preview and Dry-Run Mechanics

A major barrier to safe history rewriting is the inability to see the outcome before committing to it.

### `git history`

- **Interactive Previews:** Before rewriting descendants, `git history` should optionally display a simulated commit graph comparing the old topology with the proposed new topology.
- **Dry-Run Mode (`--dry-run`):** Currently supported, but must be expanded to show exactly which branch refs will be repointed.
- **Diff Summaries:** When splitting commits, the UI should provide an explicit warning if the split leaves a commit semantically empty or causes downstream conflict markers in automated re-applications.

### `git replay`

- **`--ref-action=print`:** This is the foundational safety feature. It outputs `update-ref` commands without mutating the repository.
- **Future Pipeline Previews:** Tools building on `git replay` should parse the `--ref-action=print` output to render visual previews for operators before confirming the atomic transaction.

## Atomic Updates and Fallbacks

Both commands must ensure all-or-nothing ref updates.

- **Atomic Transactions:** If updating 5 branch refs due to a `git history` rewrite, and the 5th fails, all previous 4 must roll back to their original OIDs.
- **Conflict Halts:** If `git replay` encounters a conflict, it must halt immediately with exit status `1` and perform zero ref updates. It must not leave the repository in a partially replayed state.

## Reflog and Recovery

- **Dangling Commits:** Rewritten commits must not immediately be pruned. They must remain discoverable via `git reflog`.
- **Explicit Undo:** `git history` should leave a breadcrumb in the reflog (e.g., `history: split commit X`) so that a standard `git reset --hard @{1}` cleanly undoes the entire operation.

## Summary

We do not want "super git" to mean "faster ways to destroy your repository." `git history` and `git replay` will only move from *Experimental* to *Supported But Evolving* once these safety preview mechanisms are hardened and fully tested.
