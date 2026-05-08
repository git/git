# Code Components

## Repository Root

- Top-level C files implement shared internals and many command backends.
- Top-level shell scripts provide scripted commands and wrappers.
- `Makefile` orchestrates builds, tests, installation, and documentation generation.

## `builtin/`

Contains compiled command entry points for many Git subcommands. This is where built-in porcelain and plumbing commands are wired into the main executable.

## `git-ace.sh`

Implements the Ace workflow command. Key capabilities include:

- initializing Ace metadata
- creating and resolving virtual branches
- renaming and deleting Ace branch subtrees
- querying parent, children, chain, and tree relationships
- stack-aware rebase and merge operations
- Mercurial bookmark and named-branch import/export
- launching external agents with Ace branch context

Opinionated take:

- This file is the most important single custom implementation artifact in the repository.
- It is also intentionally simple in technology choice: shell script plus normal Git commands plus metadata files.
- That simplicity is good for portability and inspectability, but it also means correctness depends heavily on careful edge-case handling around branch naming, metadata updates, and command sequencing.

Important implementation details visible in the script:

- Ace stores virtual branch metadata under `.git/ace/branches/`.
- Each virtual branch records a backing ref in a `ref` file.
- Parent-child relationships are stored in `parent` files.
- Agent execution exports Ace-specific environment variables such as `ACE_BRANCH`, `ACE_REAL_BRANCH`, `ACE_PARENT_BRANCH`, and `ACE_INTEROP_TARGETS`.
- Mercurial interoperability is file-oriented and branch-pointer-oriented rather than storage-oriented.

## `t/`

Holds the primary regression suite. Tests are organized as shell scripts with TAP output and broad coverage across repository setup, refs, merge, diff, transport, partial clone, submodules, and other behavior.

Opinionated take:

- In a repository this old and behaviorally dense, tests are not just protection; they are part of the design documentation.
- Any Ace-specific feature that mutates refs, history, or metadata should be treated as incomplete until it has realistic test coverage.

## `contrib/` And `tools/`

- `contrib/` contains optional helpers, integrations, and experimental extras.
- `tools/` contains developer tooling related to the build infrastructure and manual workflows.

## Interface Subprojects

- `git-gui/`: Tcl/Tk graphical interface for staging, committing, browsing, and blame/citool workflows.
- `gitk-git/`: graphical history browser.
- `gitweb/`: web interface for browsing repositories.

These matter, but they are not where this fork's primary identity lives. The main story is still CLI workflows and repository behavior.
