# Agent MCP Server

A minimal [MCP](https://modelcontextprotocol.io/) server wrapper for
`git-agent`. It exposes repository orientation and semantic diff as MCP
resources so AI editors can query repository state without spawning shells.

## Usage

Run directly for stdio transport (default MCP mode):

```bash
python contrib/agent-mcp/agent_mcp_server.py
```

Set `GIT_AGENT_REPO` to point at the repository to serve:

```bash
GIT_AGENT_REPO=/path/to/repo python contrib/agent-mcp/agent_mcp_server.py
```

## Resources

| URI | Description |
|-----|-------------|
| `repo://orientation` | JSON from `git agent-orient` |
| `repo://semantic_diff` | JSON from `git agent-diff HEAD~1..HEAD` |

## Extending

Add new resources by editing `agent_mcp_server.py` and mapping URIs to
`git agent-*` commands in `handle_resources_read()`.
