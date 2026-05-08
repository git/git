# Release Criteria

## Why This Exists

Ace should not treat every implemented feature as equally mature. A serious open community organization needs clear release expectations so users, contributors, and maintainers understand what is experimental, what is promising, and what is ready to depend on.

## Maturity Levels

## Experimental

Use this label when:

- the command or behavior is still changing materially
- failure modes are not yet well mapped
- tests are partial or narrow
- manuals describe capability but not long-term guarantees

Required conditions:

- the docs say it is experimental plainly
- there is at least basic test coverage for the core path
- known limitations are documented

Examples in this repo likely include command surfaces such as:

- `git history`
- `git replay`
- `git last-modified`
- `git backfill`

## Supported But Evolving

Use this label when:

- the feature is real and useful now
- the main workflows are stable enough for normal use
- there is meaningful test coverage
- the interface may still expand, but behavior should not change casually

Required conditions:

- the primary workflows are documented with examples
- failure cases are reasonably understandable
- regression coverage exists for the key invariants

`git ace` should move toward this category before Ace claims broad workflow leadership.

## Stable

Use this label when:

- the workflow is well understood
- the semantics are unlikely to change without strong reason
- edge cases are tested, documented, and operationally legible
- downstream users and tools can build assumptions on it safely

Required conditions:

- clear manuals
- strong regression coverage
- compatibility expectations documented
- release messaging that does not overstate or understate the feature

## Feature Release Checklist

Before presenting a feature as ready for normal users, verify:

- code path is implemented end-to-end
- tests cover the main path and the highest-risk edge cases
- manuals are updated
- `Documentation/project-summary/` is updated when the change is material
- user-visible failure modes are understandable
- compatibility expectations are clear
- if the feature affects refs, history, or metadata, safety concerns are reviewed explicitly

## Push And Release Gate

Before pushing work that a normal software developer would consider delivery-ready, check:

- worktree is in good shape
- docs are current
- relevant tests or verification have run, or the reason they did not run is known
- the change is described at the correct maturity level

## Opinionated Rule

- Do not market experimental features as if they were stable.
- Do not hold back clearly useful stable behavior just because the whole product is ambitious.
- Be accurate first. Credibility compounds.
