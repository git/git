# Code Summary

## Primary Languages

- C: the main implementation language for core Git and most command internals.
- POSIX shell: command wrappers, scripted porcelain, test helpers, and the Ace workflow command.
- Tcl/Perl/JavaScript: support code for `git-gui`, maintenance scripts, and web tooling.
- Rust: a small `Cargo.toml`/`build.rs` footprint exists, but the repository is still predominantly C-based.

## Source Layout

- Top-level `*.c` and `*.h` files implement shared subsystems such as refs, diff, config, transport, object handling, and repository state.
- `builtin/` contains built-in command entry points.
- `compat/` and related portability files adapt the codebase across platforms.
- `git-ace.sh` implements Ace-specific branch-tree workflows on top of normal Git branches.
- `git-gui/`, `gitk-git/`, and `gitweb/` provide interface layers beyond the CLI.

## Ace-Specific Behavior

`git-ace.sh` adds a virtual branch model where human-facing branch names can be nested while backing refs remain safe normal Git branch names. Metadata is stored under `.git/ace/branches/` and powers commands such as `create`, `tree`, `rebase-stack`, `merge-stack`, and `agent run`.

Additional detail:

- `ace-internals.md`: Ace metadata layout, execution model, and operational constraints.
- `architecture.md`: top-level implementation model and main subsystems.
- `components.md`: notable project areas and what they do.
- `history-safety-design.md`: preview and safety design notes for history rewriting commands.
