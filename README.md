# git-agent — Agent-Native Git

[![Agent Build](https://github.com/dutixlf/git-ag/actions/workflows/agent-build.yml/badge.svg)](https://github.com/dutixlf/git-ag/actions/workflows/agent-build.yml)
[![Upstream Sync](https://github.com/dutixlf/git-ag/actions/workflows/upstream-sync.yml/badge.svg)](https://github.com/dutixlf/git-ag/actions/workflows/upstream-sync.yml)
[![Release](https://github.com/dutixlf/git-ag/actions/workflows/release.yml/badge.svg)](https://github.com/dutixlf/git-ag/releases)

A backward-compatible fork of Git that adds **opt-in agent metadata** to commits, branches, and repository state. Agent features are invisible to standard Git unless you explicitly invoke the `agent-*` commands or the `refs/agent/` namespace.

## Why?

When an AI agent works on a codebase, it produces context that is usually lost:
- reasoning chains
- confidence scores
- task identifiers
- planning artifacts
- session boundaries

git-agent stores this context **inside the repository**, so it travels with the code through clone, push, and fork.

## Quick start

### One-liner install (auto-download binary or build from source)

```bash
curl -sL https://raw.githubusercontent.com/dutixlf/git-ag/master/install.sh | sh
```

Or grab a pre-built binary from [Releases](https://github.com/dutixlf/git-ag/releases):

```bash
# Linux
wget https://github.com/dutixlf/git-ag/releases/latest/download/git-agent-linux-x86_64.tar.gz
tar xzf git-agent-linux-x86_64.tar.gz -C $HOME/.local --strip-components=1

# macOS
wget https://github.com/dutixlf/git-ag/releases/latest/download/git-agent-macos-universal.tar.gz
tar xzf git-agent-macos-universal.tar.gz -C $HOME/.local --strip-components=1

# Windows: download .tar.gz, extract to C:\Program Files\Git\agent\
```

Or build from source:

```bash
git clone https://github.com/dutixlf/git-ag.git
cd git-ag
make -j$(nproc) NO_CURL=YesPlease NO_RUST=YesPlease
make prefix=$HOME/.local install
export PATH="$HOME/.local/bin:$PATH"
```

No installation required for development — you can run directly from `bin-wrappers/git`.

### Make an agent commit

```bash
git agent-commit -m "feat: add caching layer" \
  --agent-id="claude" \
  --agent-task="perf-42" \
  --agent-confidence="0.92" \
  --agent-autonomy="semi" \
  --reasoning="The cache invalidation is conservative: only evict on explicit write."
```

This creates a normal commit with standard trailers:

```
feat: add caching layer

Agent-Id: claude
Agent-Task: perf-42
Agent-Confidence: 0.920000
Agent-Autonomy: semi
```

And stores the reasoning annotation under `refs/agent/commits/<sha>/reasoning`.

### Read agent context

```bash
git agent-log --reasoning -5 HEAD
```

Outputs:

```
a1b2c3d...   claude   perf-42   0.920000   (none)   semi
reasoning:
The cache invalidation is conservative: only evict on explicit write.
```

### Repository orientation (one-shot self-orient)

```bash
git agent-orient
```

Emits JSON:

```json
{
  "repo": "my-project",
  "current_branch": "main",
  "dirty_files": ["src/cache.c"],
  "recent_commits": [...],
  "agent_commits_present": true
}
```

An AI agent can run this on clone and immediately understand the repository state.

## Agent workflow

### 1. Orient

When the agent starts (or resumes), run:

```bash
git agent-orient
```

This tells the agent:
- what branch it is on
- what files are dirty
- whether prior agent commits exist
- recent activity (so it can see what happened since its last session)

### 2. Session boundary

Start a planning window:

```bash
git agent-session --start --task="implement-auth"
```

This creates a lightweight session record under `refs/agent/sessions/`. Every subsequent `git agent-commit` with `--session-id=<id>` is logged into that session transcript.

End the session when done:

```bash
git agent-session --end --session-id=<id>
```

### 3. Commit with metadata

Always use `git agent-commit` instead of `git commit` when the agent is making the change. This:
- appends standard trailers (`Agent-Id`, `Agent-Task`, `Agent-Confidence`, etc.)
- stores reasoning / plan / context as composite blob annotations
- logs the commit into the active session (if `--session-id` is given)

### 4. Validate

Before accepting an external PR or reviewing agent work:

```bash
git agent-verify HEAD~5..HEAD
```

Checks that all agent trailers conform to schema (confidence in [0,1], autonomy is `suggest`/`semi`/`auto`/`full`, parent-commit is valid SHA-1).

### 5. Semantic diff

For LLM consumption, avoid parsing unified patch text:

```bash
git agent-diff HEAD~1 HEAD
```

Produces structured JSON:

```json
{
  "schema": "1",
  "base": "HEAD~1",
  "head": "HEAD",
  "changes": [
    {
      "type": "modify",
      "from": "src/cache.c",
      "to": "src/cache.c",
      "semantic_summary": "adds cache invalidation",
      "token_estimate": 150
    }
  ]
}
```

## Integration patterns for AI agents

### Pattern A: Persistent context across clones

Because `refs/agent/*` is automatically pushed and fetched, a repository cloned on a new machine contains the full agent history:

```bash
git clone git@github.com:owner/repo.git
cd repo
git agent-log --reasoning -10 HEAD   # reasoning from prior sessions visible immediately
```

### Pattern B: Multi-agent collaboration

Different agents use different `--agent-id` values. `git agent-log` shows which agent made which commit and why:

```bash
git agent-log --format="%H %ai %cI" HEAD~20..HEAD | awk '{print $2}' | sort | uniq -c
```

### Pattern C: Checkpoint / resume

Agents can mark checkpoint commits:

```bash
git agent-commit -m "checkpoint before refactor" \
  --agent-checkpoint \
  --agent-task="refactor-auth" \
  --context="Current state: 3/7 files migrated"
```

Later, another agent (or the same one after restart) can read the context annotation and resume seamlessly.

## Auto-sync behavior

### Clone

When you `git clone` an agent-enabled repository, `refs/agent/*` is automatically detected and fetched. No manual refspec configuration needed.

### Push

When you `git push` from an agent-enabled repository, `refs/agent/*` is automatically pushed alongside your branches. No extra flags needed.

## Commands reference

| Command | What it does |
|---------|-------------|
| `git agent-commit` | Commit with agent trailers and store annotations |
| `git agent-log` | View agent metadata and read reasoning/plan/context annotations |
| `git agent-diff` | Semantic diff in JSON (LLM-friendly) |
| `git agent-orient` | JSON repo snapshot for agent self-orientation |
| `git agent-session` | Start / end / log agent sessions |
| `git agent-verify` | Validate agent trailer schema |

## Standard trailers

| Trailer | Type | Description |
|---------|------|-------------|
| `Agent-Id` | string | Agent identifier (e.g. `claude`, `gpt-4`, `opencodes`) |
| `Agent-Task` | string | Ticket or task ID |
| `Agent-Confidence` | float [0,1] | Confidence in the change |
| `Agent-Intent` | string | One-line summary (max 120 chars) |
| `Agent-Context-Hash` | sha256 | Hash of the prompt/context blob |
| `Agent-Parent-Commit` | sha1 | Prior commit this reasoning branches from |
| `Agent-Autonomy` | enum | `suggest`, `semi`, `auto`, `full` |
| `Agent-Tool-Version` | string | Version string of the agent tool |

## Backward compatibility

- Standard `git commit`, `git log`, `git diff` are **unchanged**.
- Agent metadata is stored in standard Git trailers and `refs/agent/*` namespace.
- Repositories without agent features work exactly the same.
- Agent metadata only appears when you explicitly use `agent-*` commands.

## MCP server

A minimal stdio MCP server is provided in `contrib/agent-mcp/`:

```bash
python3 contrib/agent-mcp/agent_mcp_server.py
```

Exposes:
- `repo://orientation` → `git agent-orient` JSON
- `repo://semantic_diff` → `git agent-diff` JSON

See [`contrib/agent-mcp/SETUP.md`](contrib/agent-mcp/SETUP.md) for VS Code, Cursor, Claude Desktop, and Nix/Home Manager configuration.

AI editors can query repository state without shelling out.

## More documentation

- `Documentation/agent-trailers.txt` — trailer schema and validation rules
- `Documentation/agent-semantic-diff.txt` — JSON schema for semantic diff
- `AGENTS.md` — internal architecture notes
- `t/t9900-*.sh` — integration tests

## CI/CD

This fork uses **two lightweight workflows** instead of the upstream heavy matrix:

| Workflow | Trigger | What it does |
|----------|---------|-------------|
| `agent-build.yml` | push / PR to `master` | builds with `NO_CURL=YesPlease NO_RUST=YesPlease`, runs all 5 agent test suites, smoke-tests `git agent-orient` |
| `upstream-sync.yml` | weekly (Mon 06:00 UTC) + manual | fast-forwards `master` from `git/git`; creates a PR if FF is not possible |
| `release.yml` | tag `v*` push + manual | builds Linux/macOS/Windows binaries, uploads to [GitHub Releases](https://github.com/dutixlf/git-ag/releases) |

The upstream `main.yml` (Windows builds, fuzzing, Coverity, dockrized tests) is **disabled by default** because it requires infrastructure this fork does not have. To re-enable it, edit `.github/workflows/main.yml` and uncomment the `push` / `pull_request` triggers.

### Required repository settings

1. **Actions permissions**: `Settings → Actions → General → Workflow permissions` → choose **"Read and write permissions"** (needed for the upstream-sync workflow to push).
2. **Secrets**: none required for basic build/sync. Coverity scans require `COVERITY_SCAN_TOKEN` and `COVERITY_SCAN_EMAIL` if you re-enable `coverity.yml`.

## License

Same as Git: GPLv2 with some parts under compatible licenses.
