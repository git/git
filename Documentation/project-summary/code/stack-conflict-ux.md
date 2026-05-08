# Stack Conflict UX Spec

## Purpose

When users run `git ace rebase-stack` or `git ace merge-stack`, Ace iterates over the branch tree and applies operations sequentially. If a conflict occurs midway, the process halts, leaving the user in an intermediate state.

Currently, this relies entirely on Git's native conflict UX, which lacks the broader context of the *stack*. This spec defines how Ace should guide operators through conflicts during multi-branch operations.

## The Problem

If `feature/a` -> `feature/b` -> `feature/c` is being rebased, and `feature/b` conflicts:
- Git drops the user into the working tree with standard `CONFLICT (content)` markers.
- The user resolves the conflict and runs `git rebase --continue`.
- **The Gap**: Once the rebase of `feature/b` finishes, Git stops. The user must manually remember to re-run `git ace rebase-stack` to continue cascading the changes down to `feature/c`.

## Proposed Solution: The Ace Sequencer

Ace requires its own state file, similar to Git's `.git/rebase-merge/`, to track in-progress stack operations.

### 1. State Tracking
When `git ace rebase-stack <root>` starts, it should write a sequence plan to `.git/ace/sequencer/plan`:
```text
rebase feature/b onto feature/a
rebase feature/c onto feature/b
```

### 2. Conflict Halts
If a conflict occurs, Ace intercepts the failure and augments the output:

```text
$ git ace rebase-stack feature/a
Rebasing feature/b onto feature/a...
CONFLICT (content): Merge conflict in parser.c
Ace: Stack rebase halted due to conflicts in feature/b.

To continue:
  1. Resolve the conflicts in the working directory.
  2. Run `git rebase --continue`.
  3. Run `git ace continue` to resume rebasing the rest of the stack (feature/c).

To abort:
  1. Run `git rebase --abort`.
  2. Run `git ace abort`.
```

### 3. Resumption (`git ace continue`)
When the user types `git ace continue`:
1. Ace verifies that no native Git operations (like `rebase`) are actively paused.
2. Ace pops the completed step off `.git/ace/sequencer/plan`.
3. Ace begins executing the next step in the plan (`rebase feature/c onto feature/b`).

## AcreetionOS Philosophy Alignment

This explicitly serves the **Operator-Grade Control** and **Conflict Management As A First-Class UX** pillars outlined in our roadmap. It treats conflicts as expected lifecycle events in dependent branch chains, rather than catastrophic failures.
