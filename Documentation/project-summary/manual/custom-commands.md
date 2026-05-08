# Custom Commands

## What Actually Makes This Fork Different

Most of the repository is still Git. The fork feels distinct because of a relatively small set of commands and manuals that push workflow, ref management, and repository introspection further than standard Git.

## Primary Identity Command

### `git ace`

The defining command of the fork.

- Adds explicit branch-tree metadata
- Supports nested virtual branch names
- Adds stack-aware operations
- Bridges to agent tooling and Mercurial branch-pointer workflows

If a newcomer asks what Ace is, this command is the shortest honest answer.

## Experimental Or Distinctive Workflow Commands

### `git history`

An opinionated history editing interface for rewording and splitting commits.

Why it matters:

- it aims to be simpler than general-purpose interactive rebase for some tasks
- it works in bare repositories
- it updates descendant branches by default

Opinionated take: this is the kind of command Git has long needed for common history surgery tasks.

### `git replay`

Replay or revert commit ranges onto a new base while leaving the worktree untouched.

Why it matters:

- it is automation-friendly
- it works for bare repository workflows
- it treats ref updates as first-class, including atomic update behavior

Opinionated take: this is a strong primitive for server-side and tooling-driven history manipulation.

### `git repo`

Repository metadata and structure inspection.

Why it matters:

- gathers repository facts behind a structured command surface
- exposes information that tools and operators often need but Git scatters across lower-level commands

### `git refs`

Grouped interface for ref migration, verification, listing, existence checks, and optimization.

Why it matters:

- it makes ref administration more coherent
- it surfaces operations that are real but often too fragmented in stock Git UX

### `git last-modified`

Shows which commit last modified files or tree entries.

Why it matters:

- helps answer path-oriented history questions faster than composing lower-level commands manually

### `git backfill`

Hydrates missing blobs in partial clones in larger batches.

Why it matters:

- directly addresses partial-clone pain around repeated tiny blob fetches

### `git stage`

Alias for `git add`.

Why it matters:

- small feature, but it improves discoverability and naming clarity for users who think in terms of staging rather than adding

## Overall Assessment

The fork's custom commands fall into three clusters:

- branch and stack workflows: `git ace`, `git history`, `git replay`
- repository/ref introspection and administration: `git repo`, `git refs`, `git last-modified`
- ergonomics and data movement: `git backfill`, `git stage`

That combination makes Ace feel more like an operator's Git and a workflow architect's Git, not just a cosmetic rebrand.
