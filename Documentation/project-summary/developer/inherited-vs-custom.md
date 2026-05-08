# Inherited Vs Custom

## Why This Distinction Matters

This repository is not uniformly novel. Contributors need a clear mental model for which parts are mostly inherited Git behavior and which parts define Ace-specific behavior.

That distinction affects:

- how risky a change is
- what compatibility assumptions should hold
- how much documentation must change with the code
- whether a feature should feel experimental or foundational

## Practical Matrix

## Mostly Inherited Git Core

- object model: commits, trees, blobs, object identity
- transport and remote fundamentals
- index, checkout, and worktree fundamentals
- diff, merge, and patch plumbing foundations
- main documentation and testing culture
- large portions of `builtin/` and top-level C internals

How to treat it:

- default to upstream-style expectations
- change cautiously and with strong justification
- assume broad regression potential

## Clearly Custom Ace Surface

- `git-ace.sh`
- `Documentation/git-ace.adoc`
- `.git/ace/branches/` metadata conventions
- agent context export behavior
- Mercurial bookmark and named-branch mapping inside Ace

How to treat it:

- this is the fork's core identity
- changes here should be deliberate, tested, and well documented
- ergonomics matter as much as technical correctness

## Custom But Experimental Command Surface

- `git-history`
- `git-replay`
- `git-repo`
- `git-refs`
- `git-last-modified`
- `git-backfill`
- `git-stage`

How to treat it:

- be explicit about stability level
- prefer precise manuals and examples
- keep command behavior legible and script-friendly

## Interface And Ecosystem Subprojects

- `git-gui/`
- `gitk-git/`
- `gitweb/`
- `contrib/`
- `tools/`

How to treat it:

- important, but secondary to the fork's CLI workflow identity
- preserve local conventions rather than forcing core-Ace assumptions into these areas

## Opinionated Contributor Rules

- If you are touching inherited Git machinery, think `compatibility first`.
- If you are touching Ace workflow behavior, think `clarity and workflow leverage first`.
- If you are touching experimental commands, think `make the feature sharper, not broader`.
- If you cannot explain whether a change belongs to inherited Git or custom Ace, the design probably is not clear enough yet.
