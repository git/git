# User Summary

## What Users Get

Ace preserves the normal Git experience while adding an optional workflow layer through `git ace`.

## Standard Entry Points

- Start with `README.md` for project positioning.
- Use `Documentation/gittutorial.adoc` and `Documentation/giteveryday.adoc` for core Git workflows.
- Use `Documentation/git-<command>.adoc` for command reference details.

## Ace Workflow Layer

The main user-facing Ace command is documented in `Documentation/git-ace.adoc`. It introduces:

- nested virtual branch names
- explicit parent-child branch relationships
- tree inspection commands such as `parent`, `children`, `chain`, and `tree`
- stack operations such as `rebase-stack` and `merge-stack`
- Mercurial bookmark and named-branch import/export
- agent execution tied to branch context

## Best Short Description

For end users, this project is still Git first, with Ace acting as a compatibility-focused workflow extension rather than a replacement for Git repositories or history.

Additional detail:

- `getting-started.md`: where a new user should begin.
- `ace-workflows.md`: what Ace changes for stacked and tree-based workflows.
- `future-features.md`: what Ace should grow into if it is going to be a true "super git".
- `roadmap.md`: how that vision breaks down into near-, medium-, and long-term execution.
