# Roadmap

## Roadmap Principle

Ace should be planned like a serious open community organization, not just a patch queue. That means every major initiative should answer four questions:

- what user problem it solves
- what technical leverage it creates
- how it will be tested and documented
- which organizational functions need to support it

The AcreetionOS public site adds a fifth constraint: the roadmap should preserve independence, transparency, and repository sovereignty rather than drifting toward opaque hosted-workflow assumptions.

## Near-Term Priorities

These are the most natural next steps because they build directly on current Ace capabilities.

## 1. Make `git ace` Operationally Complete

Goals:

- add stack-wide status and health reporting
- add clearer diagnostics for invalid or drifting Ace metadata
- improve conflict reporting for `rebase-stack` and `merge-stack`
- add more test coverage around rename, delete, and parent mutation edge cases

Why near-term:

- `git ace` is already the project identity
- the biggest payoff comes from making the core feature set feel dependable

Functions involved:

- Product
- Core Engineering
- QA
- Documentation
- Developer Experience

## 2. Sharpen Experimental History Tools

Goals:

- add preview and explanation modes to `git history` and `git replay`
- improve safety rails around affected refs and downstream branches
- clarify stability expectations in manuals

Why near-term:

- these commands already exist and are close to being signature differentiators
- they solve real problems Git users regularly hit

Functions involved:

- Product
- Core Engineering
- QA
- Documentation
- Security

## 3. Improve Repository Introspection

Goals:

- expand `git repo`, `git refs`, and `git last-modified`
- make repository health and branch topology easier to query
- improve machine-readable outputs for tools and agents

Why near-term:

- the fork already has the seeds of an operator-grade Git
- introspection features support both humans and automation

Functions involved:

- Product
- Core Engineering
- Developer Experience
- QA
- Agent Platform

## Medium-Term Priorities

These features move Ace from "interesting fork" toward "workflow platform".

## 4. First-Class Review Series Management

Goals:

- represent a reviewable stack as more than an incidental set of branches
- add stack-aware export and summary commands
- support mailing-list and pull-request preparation from one model

Why medium-term:

- it extends the Ace branch-tree idea into the code review layer
- it requires clearer metadata and workflow design than the current repo exposes

Functions involved:

- Product
- Core Engineering
- Developer Experience
- Documentation
- Ecosystem

## 5. Smarter Partial Clone UX

Goals:

- build on `git backfill` with predictive and policy-driven hydration
- reduce user confusion around missing data
- improve performance defaults for large repositories

Why medium-term:

- strategically valuable, especially for large-team or monorepo-style use
- requires thoughtful performance and telemetry-oriented design

Functions involved:

- Product
- Performance Engineering
- Core Engineering
- QA
- Operations

## 6. Agent-Native Collaboration Surface

Goals:

- extend agent context beyond the current environment variables
- define standard task handoff and result formats
- support branch-scoped repair, review, and migration flows

Why medium-term:

- the current agent hook is promising but minimal
- a stronger model would make Ace meaningfully better for AI-assisted development

Functions involved:

- Product
- Agent Platform
- Core Engineering
- Security
- Documentation
- Developer Experience

## Long-Term Bets

These are the areas that would make Ace feel like a category-defining product rather than an enhanced Git fork.

## 7. Workflow Intent As Data

Goals:

- encode branch purpose, review state, rollout state, and risk level as first-class metadata
- let commands enforce workflow policy instead of relying on naming conventions and tribal knowledge

Why long-term:

- this changes the product model, not just individual commands
- it requires careful compatibility and UX design

Functions involved:

- Product
- Core Engineering
- Design
- Security
- Documentation
- Ecosystem

## 8. Conflict Management As A First-Class System

Goals:

- stack-aware conflict reporting
- reusable resolution context for dependent branches
- structured outputs for agents and tools

Why long-term:

- conflict handling is one of Git's hardest UX problems
- solving it well requires design, data modeling, and deep integration work

Functions involved:

- Product
- Core Engineering
- QA
- Design
- Agent Platform

## 9. Operator-Grade Repository Platform

Goals:

- repository health dashboards
- ref lifecycle management at scale
- policy-aware automation for large repos and teams

Why long-term:

- this would move Ace beyond developer convenience into serious repository operations tooling

Functions involved:

- Product
- Operations
- Performance Engineering
- Core Engineering
- Security
- Ecosystem

## Execution Notes

- Near-term work should mostly strengthen existing commands.
- Medium-term work should connect existing commands into more coherent workflows.
- Long-term work should only proceed when the core Ace branch-tree layer feels stable and well tested.

## Bottom Line

Ace should ship like a disciplined open community organization with a product strategy:

- stabilize the signature feature first
- turn adjacent experiments into coherent tools
- then invest in workflow models Git has historically lacked
