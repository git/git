# Build And Test

## Build Systems

The primary build entry point is the top-level `Makefile`. The repository also contains documentation and discussion around alternative build systems, including Meson and CMake-related support in some areas.

`Documentation/technical/build-systems.adoc` describes build-system requirements such as platform support, auto-detection, IDE integration, out-of-tree builds, and test integration.

Opinionated take:

- The build story is intentionally conservative.
- `make` is still the center of gravity, even if other build systems are discussed or partially supported.
- Any contributor who changes build behavior should assume they are touching portability-sensitive territory.

## Main Developer Commands

- `make`
- `make test`
- targeted test runs in `t/`

## Test Model

`t/README` documents the main regression suite:

- shell-based TAP tests
- support for running all tests or subsets
- direct execution of individual test scripts
- prove-based parallel execution
- options for verbose, debug, valgrind, stress, and chain-lint runs

## What Needs Extra Care

Changes in these areas deserve especially defensive testing and documentation updates:

- refs and ref storage behavior
- history rewriting and replay behavior
- worktree or checkout-affecting commands
- partial clone and object download behavior
- Ace metadata mutation and branch-tree operations

## Documentation Builds

`Documentation/Makefile` builds manpages and HTML documentation from AsciiDoc sources.
