# Ace Workflows

## Main Idea

Ace adds a virtual branch-tree model on top of normal Git branches. Instead of treating branches as a flat list, Ace lets users model explicit parent-child relationships and operate on entire stacks.

The strongest idea here is that branch hierarchy should be first-class workflow data rather than an informal naming convention.

## Core User Operations

- `git ace create`
- `git ace checkout`
- `git ace parent`
- `git ace children`
- `git ace chain`
- `git ace tree`
- `git ace rebase-stack`
- `git ace merge-stack`

These commands are more opinionated than stock Git. They assume that once branch relationships are declared, the tool should help users operate on the whole structure instead of forcing them to manually repeat branch-by-branch maintenance.

## Why It Exists

According to the project README, Ace focuses on:

- deeply nested branch trees
- stack-aware workflows
- compatibility with standard Git repositories
- interoperability with Mercurial bookmark workflows
- agent-driven collaboration

## Important User Constraint

Ace metadata is authoritative for Ace-owned branch hierarchy. Direct low-level Git branch mutations outside of Ace can leave that metadata out of sync.

## Opinionated Assessment

- This workflow is most compelling for stacked changes, long-lived topic trees, and teams that already think in dependency chains.
- It is less compelling for very small repositories or teams that mostly work with one short-lived feature branch at a time.
- The Mercurial import/export and agent hooks suggest Ace is trying to be a bridge technology: modern workflow help without abandoning Git's interoperability.
