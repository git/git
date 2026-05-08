# Testing Strategy

## Purpose

Ace needs stronger testing discipline in the command surfaces where small mistakes can create outsized damage. The three highest-risk custom areas are:

- `git ace`
- `git history`
- `git replay`

## Core Principle

The more a feature touches refs, history, branch metadata, or user trust, the less acceptable it is to rely on manual confidence alone.

## `git ace`

Highest-risk areas:

- virtual-to-real branch resolution
- subtree rename behavior
- subtree deletion behavior
- parent assignment and cycle prevention
- rebase-stack and merge-stack sequencing
- descendant command traversal
- metadata drift or misuse scenarios
- agent environment variable isolation and formatting

Minimum testing expectations:

- branch creation with inferred parent and explicit parent
- rename of a branch with descendants
- delete refusal when descendants exist
- delete with descendants and current-branch fallback behavior
- cycle rejection in `set-parent`
- tree/chain/children output consistency
- mock agent execution verifying the environment contract (all `ACE_*` variables)

Recommended test style:

- shell-based integration tests in `t/`
- explicit assertions about refs and metadata layout after operations
- cases that simulate user mistakes, not only happy paths

## `git history`

Highest-risk areas:

- commit reword correctness
- split behavior and resulting parent relationships
- ref update behavior for descendant branches
- dry-run output correctness
- limitations around merges and conflicts

Minimum testing expectations:

- reword of a single commit with expected commit-message change only
- split of a simple commit into two logical commits
- dry-run output that matches expected ref-update form
- descendant branch updates when history is rewritten

Recommended test style:

- integration tests that assert commit graph shape before and after
- explicit checks that unaffected commits remain unchanged

## `git replay`

Highest-risk areas:

- `--onto`, `--advance`, and `--revert` semantics
- atomic ref update behavior
- `--ref-action=print` correctness
- drop behavior when commits are already present
- exit-code behavior on success, conflict, and hard failure

Minimum testing expectations:

- replay onto a new base
- advance an existing branch
- revert a commit range onto a branch
- printed ref updates in the expected format
- exit status assertions for conflict and non-conflict cases

Recommended test style:

- graph-shape assertions
- explicit ref movement checks
- tests that cover both direct update and print-only paths

## Documentation As Part Of Testing

For Ace, docs and tests should move together.

- if a command gains a new invariant, document it
- if a command is still experimental, test the known boundaries and describe them
- if a command becomes stable, its tests should justify that claim

## Execution Priority

If testing work must be sequenced, prioritize in this order:

1. `git ace`
2. `git replay`
3. `git history`

That order reflects current project identity first, then high-risk history/ref operations.
