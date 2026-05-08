# Ace Project Summary

Ace is being developed under the open community organization identity `AcreetionOS`.
The current co-lead developers are `Natalie Cole-Clift Spiva` and `Darren Clift`.

Public organization context from `acreetionos.org` frames AcreetionOS as an
independent, community-focused organization that values stability,
transparency, repository sovereignty, and practical user control.

Ace should be understood as two projects living in one tree:

1. the upstream-style Git codebase, which remains the overwhelming majority of the implementation
2. the Ace workflow layer, which gives this fork its reason to exist

Ace is not trying to replace Git's repository model. Its most important opinion is that better workflow ergonomics should be added on top of native Git repositories, not by inventing a new storage backend.

## What This Project Is

- Base project: the Git source tree, largely written in C with POSIX shell scripts and a long-established documentation and test suite.
- Ace layer: a workflow extension centered on `git ace`, which adds virtual branch trees, explicit parent-child branch metadata, stack-aware operations, and agent integration.
- Compatibility goal: keep repositories, commits, trees, blobs, and refs compatible with standard Git tooling.

## Opinionated Reading Of The Repo

- The most important custom feature is `git ace`, not the existence of a fork by itself.
- The value proposition is workflow structure, not storage innovation.
- The safest way to work in this codebase is to treat upstream Git behavior as the baseline and Ace behavior as an additive layer that must not casually break that baseline.
- The custom commands outside `git ace` matter, but they read more like a growing set of power-user and experimental extensions than a separate platform.
- The AcreetionOS framing suggests this project should care not just about developer ergonomics, but also about sovereignty, independence, and operator-grade control.

## Main Areas

- `builtin/`, top-level `*.c`, `*.h`: core command implementations and shared internals.
- `git-ace.sh`: the main Ace workflow command implemented as a shell script.
- `Documentation/`: manpages, tutorials, technical notes, and contributor guidance.
- `t/`: the primary regression and integration test suite.
- `git-gui/`, `gitk-git/`, `gitweb/`: optional interfaces and companion tools.
- `contrib/`, `tools/`: helper utilities, experiments, and maintenance tooling.

## Build And Test Model

- Primary build system: `make` with a large top-level `Makefile`.
- Documentation build: `Documentation/Makefile` converts AsciiDoc/Asciidoctor sources into manpages and HTML.
- Tests: `make` or targeted runs in `t/`, with TAP-style shell tests described in `t/README`.

## Ace-Specific Direction

From `README.md` and `Documentation/git-ace.adoc`, Ace currently focuses on:

- infinitely nested virtual branch trees
- explicit branch parent-child metadata under `.git/ace/branches/`
- stack-aware rebase and merge workflows
- Mercurial bookmark and named-branch import/export
- agent integration points for tools such as OpenCode and Claude Code

## Other Notable Fork Additions

The fork also carries several non-upstream-style or explicitly experimental commands that help explain where maintainers are exploring Git UX:

- `git history`: opinionated history rewriting for rewording and splitting commits
- `git replay`: replay or revert commit ranges without touching the worktree
- `git repo`: repository metadata and structure inspection
- `git refs`: a grouped interface for ref migration, verification, listing, and optimization
- `git last-modified`: path-oriented history lookup
- `git stage`: a user-facing synonym for `git add`
- `git backfill`: partial-clone blob hydration in larger batches

These commands make the fork feel more workflow- and operator-oriented than stock Git, but `git ace` is still the clearest expression of the project's identity.

## Summary Tree

See `TREE.md` for the category tree and the folder summaries under:

- `code/`
- `manual/`
- `user/`
- `developer/`

Especially important deeper reads:

- `code/ace-internals.md`: how Ace virtual branches are represented and manipulated
- `manual/custom-commands.md`: which commands make this fork meaningfully different from stock Git
- `user/roadmap.md`: the practical near-, medium-, and long-term roadmap for Ace
- `developer/inherited-vs-custom.md`: where contributors should assume upstream Git semantics versus fork-specific behavior
- `developer/company-operating-model.md`: the company-style ownership model and function map
- `user/future-features.md`: the stronger product vision for Ace as a "super git"
