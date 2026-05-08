# Stack-Wide Status and Health Spec

## Purpose

Currently, `git status` only knows about the single branch checked out in the working tree. For a branch-tree workflow to be trusted, users need a fast way to check the health and status of an entire stack of branches at once. 

This spec defines the future `git ace status` (or `git ace stack-status`) command.

## Core Capabilities

1. **Topology View**: Display the current branch, its ancestors up to a stable root, and its direct descendants.
2. **Sync State**: Show whether each branch in the stack is ahead/behind its immediate parent.
3. **Working Tree Integration**: Overlay the standard working tree status (uncommitted/untracked changes) on the currently checked-out node.
4. **Health Diagnostics**: Highlight branches that have drifted, have broken refs, or contain conflicts.

## Proposed UX

```text
$ git ace status
Stack Status:

  main
  │
  ├── feature/base (ahead 2, behind 0) [synced]
  │   │
  │   └── feature/parser (ahead 1, behind 1) [needs rebase]
  │       │
  │       └── feature/parser/validation (ahead 3, behind 0) *CURRENT*
  │             ~ 2 uncommitted changes

Health:
⚠️  feature/parser is out of sync with feature/base. Run `git ace rebase-stack feature/base`.
```

## Implementation Requirements

- **Traversal**: Iterate over the output of `git ace chain` and `git ace children`.
- **Ahead/Behind Calculation**: For each `(parent, child)` pair, compute `git rev-list --count parent..child` (ahead) and `git rev-list --count child..parent` (behind).
- **Performance**: This command must be fast. Bulk `rev-list` operations or using `git get-tar-commit-id` style caching may be necessary for deep stacks.
- **Agent Integration**: Provide a `--format=json` flag so agents and GUI tools can parse the stack health directly without scraping text.

## AcreetionOS Philosophy Alignment

This capability emphasizes **user control** and **transparency**. By making the state of an entire stack visible at a glance, we reduce the cognitive load on the operator, making the system safer and more reliable.
