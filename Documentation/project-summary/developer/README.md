# Developer Summary

## Build And Verification

- Main build entry point: `make`
- Test guidance: `t/README`
- Documentation build rules: `Documentation/Makefile`

The repository includes a large shell-based TAP test suite, optional interface components, and extensive portability logic for many platforms.

## Contribution Guidance

- Patch process: `Documentation/SubmittingPatches`
- Code style: `Documentation/CodingGuidelines`
- GitHub mirror note: `.github/CONTRIBUTING.md` explains that the upstream contribution flow is mailing-list based.

## Developer Priorities In This Repo

- preserve compatibility with native Git repositories
- keep changes portable and aligned with local code style
- add tests for bug fixes and behavior changes
- document commands and workflows in `Documentation/`

## Ace-Focused Development Notes

Ace extends the upstream Git codebase rather than replacing it. The clearest custom surface area is the `git ace` workflow, its metadata under `.git/ace/branches/`, and the associated documentation in `Documentation/git-ace.adoc`.

Additional detail:

- `ace-1.0-release-policy.md`: a concrete release policy for what Ace 1.0 should require before being presented as a dependable release.
- `build-and-test.md`: build systems and test entry points.
- `execution-board.md`: a tracked execution view derived from the current quarter plan.
- `governance-summary.md`: how decision-making, leadership, and repository sovereignty should be understood in this repo.
- `milestones.md`: milestone-level targets for Ace 1.0, 1.1, and 2.0.
- `open-community-operating-model.md`: open-community functional ownership across product, engineering, QA, docs, security, release, and ecosystem work.
- `organization-profile.md`: AcreetionOS identity, leadership, values, and public positioning.
- `q2-2026-plan.md`: a filled-in current-quarter execution plan.
- `quarterly-planning-template.md`: a reusable template for roadmap and execution planning.
- `release-criteria.md`: what should count as experimental, ready, or stable before release and push decisions.
- `roles-and-owners.md`: named ownership lanes for the current co-lead developers.
- `testing-strategy.md`: testing priorities for `git ace`, `git history`, and `git replay`.
- `contributing.md`: patch flow, review, and style expectations.
- `inherited-vs-custom.md`: which areas should be treated as upstream baseline versus fork-specific behavior.
