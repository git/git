# Code Architecture

## Core Model

Ace inherits Git's architecture: a command-oriented executable backed by shared C libraries and helper scripts. The repository stores history in native Git objects and refs, while commands provide both porcelain and plumbing interfaces on top of that storage.

The important practical implication is that most engineering risk in this repository still looks like Git risk: changes to refs, history rewriting, transport, checkout, merge behavior, or repository metadata can have wide blast radius even when the custom feature being worked on seems small.

## Main Layers

- Command layer: built-in commands in `builtin/` plus script-based commands at the repository root.
- Core subsystems: top-level `*.c` and `*.h` files for objects, refs, diff, merge, transport, config, index, and worktree behavior.
- Portability layer: `compat/` and platform-detection build logic.
- Interface layer: `git-gui/`, `gitk-git/`, and `gitweb/`.
- Test layer: shell-based TAP tests in `t/` and unit-test support under `t/unit-tests/`.

## Ace Extension Model

Ace is implemented as an extension on top of normal Git repository mechanics, not a replacement for them.

- Virtual branch names are resolved to safe backing branch names.
- Parent-child branch metadata is stored under `.git/ace/branches/`.
- Stack operations such as `rebase-stack` and `merge-stack` coordinate normal Git operations across that metadata.
- Agent integrations run against ordinary Git state with extra Ace branch context.

This is a conservative design, and that is a strength. The fork is most convincing when it uses Git as stable infrastructure and layers workflow semantics above it.

## Where The Fork Actually Differs

From the docs and command inventory, the fork's custom identity is concentrated in a relatively small set of surfaces:

- `git-ace.sh` and `Documentation/git-ace.adoc`
- custom or experimental command manuals such as `git-history`, `git-replay`, `git-repo`, `git-refs`, `git-last-modified`, and `git-backfill`
- command registration in `command-list.txt`
- any tests and supporting code that exercise those behaviors

That means contributors should resist treating the whole repository as equally custom. Most of it is inherited Git machinery that should be changed carefully and for clear reasons.

## Important Constraint

The codebase aims to preserve compatibility with standard Git repositories and tools. That design choice is visible in both the Ace README and the `git-ace` manual.

If a design choice would make Ace easier to implement but would make the repository less legible to normal Git tooling, that choice is probably in tension with the project's stated direction.
