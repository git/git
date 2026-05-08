# Future Features

## Super Git Direction

If Ace is going to earn the label "super git", it should not just add commands. It should add missing workflow intelligence while preserving Git's core compatibility.

The standard for a good Ace feature should be:

- it solves a recurring real workflow problem
- it stays Git-native underneath
- it is scriptable and inspectable
- it reduces manual branch, ref, or history bookkeeping
- it respects repository sovereignty rather than hiding important state behind opaque services

## Features Git Still Needs

These are opinionated product directions that fit Ace's current identity.

## 1. First-Class Branch Stacks

Current status:

- partially present through `git ace`

What should improve:

- stack-wide status and health views
- stack-aware conflict summaries
- stack-wide push, pull, and review preparation
- clearer branch dependency diagnostics

Why it matters:

Git still treats related branches as an accident of naming and habit. Ace should treat them as first-class units of work.

## 2. Review-Ready Change Series Management

What Ace should add:

- explicit patch-series or review-series commands
- stack-aware export for mailing-list or PR workflows
- branch-to-series summaries that explain dependency order
- per-branch and per-stack review metadata

Why it matters:

Git is strong at commits and branches, but weak at the "this is a reviewable sequence of related changes" layer.

## 3. Safer History Surgery

Current status:

- partially present through `git history` and `git replay`

What should improve:

- built-in preview mode for history rewrites with human-readable summaries
- explicit safety rails around affected refs and downstream branches
- automatic detection of likely review or publication hazards
- better recovery story after interrupted rewrite sequences

Why it matters:

Git makes powerful history changes possible, but often with too much ceremony and too little clarity.

## 4. Better Repository Introspection

Current status:

- partially present through `git repo`, `git refs`, and `git last-modified`

What should improve:

- a unified repository health dashboard
- fast answers to "why is this repo slow/large/confusing?"
- path hot-spot and ownership summaries
- branch topology summaries for active work, stale work, and risky ref states

Why it matters:

Large repositories are not just version stores. They are operational systems, and Git still underserves that reality.

## 5. Partial Clone That Feels Complete

Current status:

- partially present through `git backfill`

What should improve:

- smarter automatic background hydration
- predictive fetch for likely-next operations
- clearer UX around what data is missing and why
- repository-local policies for aggressive or conservative object hydration

Why it matters:

Partial clone is strategically important, but standard Git still exposes too much of its internal awkwardness to users.

## 6. Agent-Native Workflows

Current status:

- partially present through `git ace agent run`

What should improve:

- richer machine-readable repository and branch context
- standard task handoff formats for coding agents
- branch-scoped repair, review, and migration workflows
- explicit agent permission and safety policies

Why it matters:

If modern development includes AI agents, Git should expose better collaboration surfaces than raw shell access and ad hoc prompts.

Ace is already pointed in that direction.

## 7. Better Ref And Namespace Management

Current status:

- partially present through `git refs` and Ace's virtual-to-real branch mapping

What should improve:

- easier bulk ref operations with dry-run and explanation modes
- better stale-branch cleanup with dependency awareness
- namespace-aware workflows for large teams or multi-tenant repos

Why it matters:

Refs are one of Git's core powers, but the UX for managing them at scale is still rough.

## 8. Conflict Management As A First-Class UX

What Ace should add:

- stack-aware conflict reports
- reusable conflict resolution guidance for dependent branches
- better explanations of why a replay or rebase failed
- structured outputs for tools and agents to help resolve conflicts safely

Why it matters:

Conflicts are one of the most expensive parts of Git workflows, and too much of that cost is still pushed onto human memory.

## 9. Workflow Intent As Data

What Ace should add:

- optional metadata for branch purpose, review state, rollout state, and risk level
- structured links between changes, stacks, and external review systems
- repository-local workflow policies enforced by commands rather than tribal knowledge

Why it matters:

Git stores history very well, but it stores intent poorly. Teams end up rebuilding that layer in spreadsheets, chat, CI naming conventions, and review tools.

## Bottom Line

Ace should become the Git that understands:

- dependency structure
- review structure
- operator needs
- partial clone reality
- agent collaboration
- workflow intent

It should also reflect the AcreetionOS value set visible on `acreetionos.org`:

- independence
- transparency
- stability
- user control
- repository sovereignty

The best version of Ace is not "Git with more commands". It is Git with better models for the way modern teams actually work.
