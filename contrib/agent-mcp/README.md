# Agent MCP Server

A minimal [MCP](https://modelcontextprotocol.io/) server wrapper for
`git-agent`. It exposes repository orientation and semantic diff as MCP
resources so AI editors can query repository state without spawning shells.

## Usage

Run directly for stdio transport (default MCP mode):

```bash
python contrib/agent-mcp/agent_mcp_server.py
```

The server **auto-detects the active repository** from the current working
directory (walking upward until it finds `.git`). It then resolves the
`git-agent` binary by trying, in order:

1. The `git` already in `PATH`
2. `./bin-wrappers/git` inside the detected repo (useful when the repo IS the git-agent source tree)
3. `~/.local/bin/git` (a previous user install)
4. If none of the above work, the server **bootstraps itself** by cloning
   `dutixlf/git-ag` into `~/.cache/git-agent`, building it with
   `NO_CURL=YesPlease NO_RUST=YesPlease`, and using the freshly-built
   `bin-wrappers/git`.

No environment variables required.

## Resources

| URI | Description |
|-----|-------------|
| `repo://orientation` | JSON from `git agent-orient` |
| `repo://semantic_diff` | JSON from `git agent-diff HEAD~1 HEAD` |

## Extending

Add new resources by editing `agent_mcp_server.py` and mapping URIs to
`git agent-*` commands in `handle_resources_read()`.
