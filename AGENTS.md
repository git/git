# Instructions for AI Agents

This repository contains the Ace project, a Git-native fork of Git with an
additional workflow layer documented under `Documentation/`.

Ace is operated as an open community organization under the name `AcreetionOS`.
The current co-lead developers are `Natalie Cole-Clift Spiva` and `Darren Clift`.

## Project Summary Maintenance

After every material change, update the project summary documentation in
`Documentation/project-summary/` so it continues to reflect the current state of
the repository.

Material changes include:

- source code changes that add, remove, or reshape user-visible behavior
- build, test, or tooling changes
- workflow changes in Ace-specific commands or metadata
- documentation changes that alter how users or developers should understand the project

When updating the project summary:

- keep `README.md` aligned with the current high-level direction of the project
- update `TREE.md` when files or categories change
- update the relevant category folders: `code/`, `manual/`, `user/`, `developer/`
- prefer small, accurate updates over broad rewrites
- do not invent features, workflows, or guarantees not supported by the repository

## Documentation Style

- Keep summaries concise, factual, and tied to the current codebase.
- Prefer references to real paths and commands in the repository.
- Preserve the existing project documentation in `Documentation/`; add summary material without replacing upstream-style manuals.

## Open Community Organization Model

Operate as if Ace is being run like a serious open community organization, not just an informal code fork.

- treat product direction, engineering, design, developer experience, QA, release management, documentation, security, operations, ecosystem, and business strategy as real functions that need explicit ownership
- when documenting plans or proposing major work, account for the relevant organizational functions rather than thinking only in terms of code changes
- do not assume that one person literally holds every role; instead, assume every important function is covered and document responsibilities clearly
- when adding or changing roadmap material in `Documentation/project-summary/`, keep the open-community organizational model current alongside the technical plan

## Delivery And Push Behavior

Operate with the delivery expectations of a professional software developer.

- when work is complete and the normal developer workflow would include pushing the branch, be prepared to push accordingly
- treat pushing as part of end-to-end delivery when the user expects the work to be carried through like a real developer task
- before pushing, make sure the worktree state, documentation updates, and any relevant verification are in good shape
- do not treat `push` as a separate afterthought when the task clearly implies normal developer completion behavior
