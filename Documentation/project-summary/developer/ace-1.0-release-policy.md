# Ace 1.0 Release Policy

## Purpose

Ace 1.0 should mean more than "the code exists". It should mean the core identity of the project is usable, explainable, and trustworthy for real users.

## What Ace 1.0 Is About

Ace 1.0 should primarily stand for:

- a credible `git ace` workflow
- dependable Git-native compatibility
- clear documentation of what is stable versus experimental
- a project identity aligned with AcreetionOS values of sovereignty, transparency, stability, and user control

Ace 1.0 should not require every experimental command in the fork to be fully mature.

## Required 1.0 Release Conditions

## 1. `git ace` Must Be Dependable

Before 1.0, the core Ace workflow should have:

- reliable create, resolve, rename, delete, parent, children, chain, and tree behavior
- clear safety behavior for subtree deletion and rename operations
- documented and understandable handling for metadata drift or misuse
- meaningful regression coverage for key branch-tree invariants

## 2. Documentation Must Be Honest And Complete

Before 1.0, the project should have:

- an up-to-date `README.md`
- an accurate `Documentation/git-ace.adoc`
- current `Documentation/project-summary/` docs
- clear labeling of experimental commands outside the core `git ace` promise

## 3. Compatibility Must Be Preserved

Before 1.0, Ace should still clearly behave as a Git-native layer:

- history remains in normal Git commits
- Ace branches remain backed by normal Git refs
- repository contents remain intelligible to normal Git tooling
- no core Ace workflow should require opaque hosted infrastructure

## 4. Operational Safety Must Be Good Enough

Before 1.0, high-impact workflows should be judged on safety, not only capability:

- destructive actions should be clearly gated
- metadata-altering behavior should be tested
- history and ref-affecting behavior should have understandable failure modes
- agent-related surfaces should be documented with realistic expectations

## 5. Release Messaging Must Be Precise

For Ace 1.0, release messaging should say:

- what is the core promise
- what is stable
- what is still experimental
- what kinds of teams or users Ace is most suited for

It should not imply that every custom command in the repository is equally mature.

## What Can Still Be Experimental At 1.0

The following areas may still be experimental at 1.0 if clearly documented as such:

- `git history`
- `git replay`
- `git last-modified`
- `git backfill`
- richer agent-native workflows beyond the current branch-context model

## Suggested 1.0 Narrative

Ace 1.0 should be presented as:

- a serious first release of the Ace branch-tree workflow layer
- a Git-native workflow platform from AcreetionOS
- a stable foundation for future "super git" capabilities

## Push And Release Standard

No branch should be pushed as a 1.0-ready release candidate unless:

- the worktree is clean enough to reason about
- relevant tests or verification have run
- docs are current
- maturity claims are accurate
- the release story is stronger than "we have implemented something interesting"
