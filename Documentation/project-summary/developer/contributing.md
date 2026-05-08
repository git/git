# Contributing

## Upstream-Style Flow

The repository follows the Git project's email-centric contribution culture even though it is mirrored on GitHub.

- `.github/CONTRIBUTING.md` explains that contributions are reviewed through the mailing list.
- `Documentation/SubmittingPatches` documents the patch lifecycle and expectations.
- `Documentation/CodingGuidelines` documents style and portability norms.

## Common Expectations

- split logically separate changes into separate commits
- explain the why in commit messages
- add tests for bug fixes and behavior changes
- match the surrounding code style instead of forcing unrelated churn
- keep portability in mind across supported platforms

## Opinionated Guidance For This Fork

- Treat upstream Git behavior as the compatibility baseline unless the fork explicitly intends different behavior.
- Concentrate custom complexity in well-documented surfaces rather than leaking Ace assumptions across unrelated parts of the tree.
- Prefer small, reviewable changes to broad rewrites, especially in shared core code.
- If you add or change Ace behavior, update both the formal manual and the project summary docs.
- If a feature is still experimental, say so plainly in docs rather than implying stability that the code does not yet earn.

## Project Summary Maintenance

In this repository, contributors and agents should also keep `Documentation/project-summary/` current after material changes so the summary remains useful to users and future maintainers.

That summary is most valuable when it is candid. It should highlight what is central, what is experimental, and where the fork is genuinely different from upstream Git.
