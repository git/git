# Agent-Native Git (`git-agent`) — AI Agent Context

This is a fork of the official Git source tree that adds **opt-in agent-native
metadata** to commits, branches, and repository state. Every feature is designed
to be fully backward-compatible with standard Git; agent features are only
visible when you explicitly use the `agent-*` commands or the `refs/agent/`
namespace.

## What makes it agent-native?

1. **Agent trailers** — Eight standard trailers (`Agent-Id`, `Agent-Task`,
   `Agent-Confidence`, `Agent-Intent`, `Agent-Context-Hash`,
   `Agent-Parent-Commit`, `Agent-Autonomy`, `Agent-Tool-Version`) are
   automatically parsed, validated, and stored in every commit that carries
   them.
2. **Annotation refs** — `refs/agent/commits/<sha1>/reasoning`,
   `plan`, `diff-summary`, `context`, and `sessions` store
   per-commit annotations that are too large for trailers or need to be
   updated after the commit is immutable.
3. **Semantic diff JSON** — `git agent-diff` produces a structured JSON diff
   with file-level `semantic_summary` and `token_estimate` fields, making it
   easy for LLMs to consume diffs without parsing patch text.
4. **Session management** — `git agent-session` records agent planning windows
   (start, end, task list, token budget) in a lightweight tree under
   `refs/agent/sessions/`.
5. **Repository orientation** — `git agent-orient` emits a JSON summary of
   repository topology (current branch, dirty files, recent commits,
   `.opencode/memory/` state) so an agent can orient itself in a single call.
6. **Validation** — `git agent-verify` checks that trailers conform to schema
   (confidence is in [0,1], parent-commit is a valid SHA-1, autonomy is one of
   `suggest`, `semi`, or `auto`).

## New commands

| Command | Purpose |
|---------|---------|
| `git agent-commit` | Wrapper around `git commit` that appends `--trailer` args and writes annotation refs |
| `git agent-log` | Log viewer that can show only agent commits or read annotation refs |
| `git agent-diff` | Semantic diff in JSON between two commits or ranges |
| `git agent-orient` | JSON repo summary for agent orientation |
| `git agent-session` | Start, end, list agent sessions |
| `git agent-verify` | Validate agent trailers on a commit object |

## Opt-in design

- Standard `git commit`, `git log`, `git diff`, etc. are **unchanged**.
- Agent metadata is only created when you invoke the `agent-*` commands.
- The `refs/agent/` namespace is ignored by standard Git operations unless
  explicitly requested.
- `git rev-parse --symbolic-full-name HEAD` still returns `refs/heads/main`.

## Documentation

- `Documentation/agent-trailers.txt` — trailer schema, validation rules, and
  examples
- `Documentation/agent-semantic-diff.txt` — JSON schema for `git agent-diff`
- `t/t9900-agent-trailers.sh` — trailer parsing tests
- `t/t9901-agent-refs.sh` — annotation ref store tests
- `t/t9902-agent-session.sh` — session management tests
- `t/t9903-agent-semantic-diff.sh` — semantic diff JSON tests
- `t/t9904-agent-commands.sh` — end-to-end command integration tests

## MCP server

A minimal MCP server wrapper is provided in `contrib/agent-mcp/`. It exposes
repo orientation and semantic diff as MCP resources so that AI editors can
query repository state without shelling out.

## Coding conventions

Follow the standard Git coding guidelines (tabs, 80 columns, `git-compat-util.h`
first include). New code lives in:
- `agent.h` / `agent.c` — shared library
- `builtin/agent-*.c` — command implementations
- `Documentation/agent-*.txt` — user-facing docs
- `contrib/agent-mcp/` — MCP wrapper
