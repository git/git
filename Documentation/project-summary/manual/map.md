# Manual Map

## Start Here

- `README.md`: project positioning and Ace direction.
- `Documentation/gittutorial.adoc`: beginner tutorial.
- `Documentation/giteveryday.adoc`: practical day-to-day commands.
- `Documentation/user-manual.adoc`: longer conceptual guide.

## Command Reference

- `Documentation/git.adoc`: top-level Git command overview.
- `Documentation/git-<command>.adoc`: individual command reference pages.
- `Documentation/git-ace.adoc`: Ace-specific command reference.

Notable fork-specific or fork-distinct manuals worth reading early:

- `Documentation/git-history.adoc`
- `Documentation/git-replay.adoc`
- `Documentation/git-repo.adoc`
- `Documentation/git-refs.adoc`
- `Documentation/git-last-modified.adoc`
- `Documentation/git-backfill.adoc`
- `Documentation/git-stage.adoc`

## Technical Deep Dives

- `Documentation/technical/`: internal formats, protocols, APIs, build systems, and implementation details.

Representative topics include:

- build systems
- partial clone
- commit graph
- sparse checkout and sparse index
- pack and protocol internals

Opinionated take:

- If you are changing behavior, read the closest command manual first.
- If you are changing architecture, build logic, or storage/ref behavior, read both the command manual and the relevant technical note.
- If a custom Ace behavior has no close technical note yet, that gap is worth noticing.

## Contribution And Process Docs

- `Documentation/SubmittingPatches`
- `Documentation/CodingGuidelines`
- `Documentation/ReviewingGuidelines.adoc`
- `.github/CONTRIBUTING.md`
