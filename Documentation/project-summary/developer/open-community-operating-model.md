# Open Community Operating Model

## Operating Assumption

Ace should be planned and documented as if it is being built by a serious open community organization with all major functions covered.

Organization name: `AcreetionOS`

Current co-lead developers:

- `Natalie Cole-Clift Spiva`
- `Darren Clift`

Public organization context from `acreetionos.org` also matters here:

- AcreetionOS describes itself as independent and community-focused
- it emphasizes transparency and integrity
- it treats repository sovereignty as a governance principle, not just an infrastructure detail
- it presents itself as a small team with broad responsibilities across development, infrastructure, support, and logistics

That does not mean one literal employee per title. It means every critical function has an explicit owner and is not left as an afterthought.

## Required Organizational Functions

## 1. Executive / Strategy

Responsibility:

- define the long-term identity of Ace
- decide where Ace should remain Git-compatible versus where it should differentiate
- prioritize product bets and sequencing

In this repo, this function mainly shows up in:

- `Documentation/project-summary/user/future-features.md`
- `Documentation/project-summary/user/roadmap.md`
- top-level `README.md`

## 2. Product Management

Responsibility:

- identify real user problems
- shape feature scope
- decide what should be near-term, medium-term, or long-term

Key concerns:

- branch stacks
- history ergonomics
- review workflows
- repository introspection
- agent collaboration

## 3. Design / UX

Responsibility:

- make command UX understandable
- ensure features are discoverable and explainable
- reduce Git-style incidental complexity where possible

In Ace, UX is not just visual. It includes:

- command naming
- option design
- dry-run and preview behavior
- readable error messages
- machine-readable output formats

## 4. Core Engineering

Responsibility:

- implement repository behavior safely
- preserve Git-native compatibility
- keep internals coherent and testable

Primary surfaces:

- `git-ace.sh`
- command implementations and supporting internals
- ref and history behavior

## 5. Developer Experience

Responsibility:

- reduce friction for daily users and contributors
- improve command discoverability
- improve docs, examples, and ergonomics

Examples:

- `git stage`
- better structured outputs
- easier review-series preparation

## 6. QA / Test Engineering

Responsibility:

- define expected behavior
- expand coverage for edge cases and regressions
- validate safety around refs, history rewriting, and metadata mutations

Especially important in Ace for:

- branch-tree rename/delete operations
- replay/rewrite flows
- partial-clone behavior
- compatibility-sensitive changes

## 7. Documentation

Responsibility:

- keep official manuals current
- keep `Documentation/project-summary/` current
- explain what is stable, what is experimental, and why features exist

This is a first-class function in Ace, not polish work.

## 8. Release Management

Responsibility:

- decide when features are mature enough to ship
- track experimental versus stable surfaces
- keep release messaging aligned with real behavior

Practical implication:

- not every implemented command should be presented as equally mature

## 9. Security

Responsibility:

- review agent execution surfaces
- review ref update and history rewrite safety
- review import/export and automation interfaces
- reduce footguns in destructive or high-impact workflows

## 10. Operations / Reliability

Responsibility:

- think about large repositories, performance, and maintainability
- improve introspection and health reporting
- support operator-grade workflows rather than only local developer workflows

## 11. Performance Engineering

Responsibility:

- improve performance-sensitive workflows
- prioritize partial clone, ref scaling, and large-repo ergonomics

## 12. Ecosystem / Integrations

Responsibility:

- keep Git interoperability strong
- support Mercurial interop where valuable
- support agents, review tools, and automation ecosystems

Constraint:

- integrations should strengthen sovereignty and user control rather than forcing dependence on one hosted platform

## 13. Community / Developer Relations

Responsibility:

- explain the project clearly to contributors and users
- lower the barrier to understanding what Ace is and is not
- keep contribution guidance and project narrative coherent

## 14. Sustainability / Strategy

Responsibility:

- decide what kinds of teams or repository scales Ace is optimized for
- justify why the fork exists and who should adopt it
- keep the project viable without diluting its identity

Even in an open-source project, this function matters. A project without a target user and a reason to exist usually becomes noisy rather than valuable.

For AcreetionOS specifically, this function should also preserve the public identity of the project as independent, community-facing, and control-oriented rather than generic or purely trend-driven.

## Practical Rule For Future Work

For any significant initiative, the plan should be strong enough that each of these functions could answer:

- why this matters
- what success looks like
- what risks it creates
- what docs/tests/release expectations change

## Bottom Line

Ace should operate like a serious open community organization whose product happens to be a Git-native workflow platform. The code matters, but so do product thinking, UX, QA, docs, security, release discipline, and ecosystem strategy.
