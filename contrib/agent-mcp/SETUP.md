# MCP Server Setup Guide

The `agent_mcp_server.py` script is a self-contained MCP (Model Context Protocol) server that exposes git-agent features to any MCP client (e.g., Claude, Cursor, VS Code Copilot).

## Quick Start (3 steps)

### 1. Install git-agent

**One-liner (Linux/macOS):**
```bash
curl -sL https://raw.githubusercontent.com/dutixlf/git-ag/master/install.sh | sh
```

**With system git replacement:**
```bash
curl -sL https://raw.githubusercontent.com/dutixlf/git-ag/master/install.sh | REPLACE_SYSTEM=1 sudo sh
```

**Windows:** Download `git-agent-windows-x86_64.tar.gz` from [Releases](https://github.com/dutixlf/git-ag/releases), extract to `C:\Program Files\Git\agent\`, and prepend to PATH.

### 2. Configure your MCP client

**VS Code / Cursor** (`~/.cursor/mcp.json` or `.vscode/mcp.json`):
```json
{
  "mcpServers": {
    "git-agent": {
      "command": "python3",
      "args": [
        "/path/to/git-ag/contrib/agent-mcp/agent_mcp_server.py"
      ],
      "env": {
        "PATH": "/home/you/.local/bin:/usr/bin:/usr/local/bin"
      }
    }
  }
}
```

**Claude Desktop** (`~/Library/Application Support/Claude/claude_desktop_config.json` on macOS, `%APPDATA%\Claude\claude_desktop_config.json` on Windows):
```json
{
  "mcpServers": {
    "git-agent": {
      "command": "python3",
      "args": [
        "/path/to/git-ag/contrib/agent-mcp/agent_mcp_server.py"
      ],
      "env": {
        "PATH": "/home/you/.local/bin"
      }
    }
  }
}
```

**Nix / Home Manager:**
```nix
programs.claude.mcpServers.git-agent = {
  command = "${pkgs.python3}/bin/python3";
  args = [ "${inputs.git-ag}/contrib/agent-mcp/agent_mcp_server.py" ];
};
```

### 3. Test it

Open any MCP client, navigate to a git repository, and ask:

> "What is the current state of this repo?"

The client will call `repo://orientation` and receive:
```json
{
  "repo": "my-project",
  "current_branch": "feature/agent-stuff",
  "dirty_files": ["agent.c", "agent.h"],
  "recent_commits": 5,
  "agent_commits": 3,
  "last_agent_session": "session-abc123..."
}
```

## How it works

The server is **zero-config** after step 1:

1. **Auto-detects repo**: Walks up from CWD until it finds `.git/`
2. **Auto-finds git-agent**: Tries in order:
   - `git` in PATH (if it already has agent commands)
   - `./bin-wrappers/git` inside the detected repo (for development)
   - `~/.local/bin/git` (default install location)
   - `~/.cache/git-agent/bin-wrappers/git` (auto-downloaded fallback)
3. **If missing**: Clones `dutixlf/git-ag`, builds with `NO_CURL=YesPlease NO_RUST=YesPlease`, caches in `~/.cache/git-agent/`

## Resources exposed

| URI | What it returns | Example use |
|-----|----------------|-------------|
| `repo://orientation` | JSON repo summary | "What's the status?" |
| `repo://semantic_diff?base=HEAD~1&head=HEAD` | JSON diff with token estimates | "Show me what changed" |

## Troubleshooting

### "git: unknown command 'agent-orient'"
The server auto-bootstrapped but the build failed. Check:
```bash
ls -la ~/.cache/git-agent/bin-wrappers/git
~/.cache/git-agent/bin-wrappers/git agent-orient
```
If missing, force rebuild:
```bash
rm -rf ~/.cache/git-agent
python3 /path/to/agent_mcp_server.py
```

### "No .git directory found"
The MCP client's CWD is not inside a git repository. Make sure you opened a project folder that contains `.git/`.

### Windows PATH issues
On Windows, use the full path to Python:
```json
{
  "command": "C:\\Python311\\python.exe",
  "args": ["C:\\path\\to\\agent_mcp_server.py"]
}
```

## CI Integration

You can also use the MCP server in CI to auto-generate release notes:

```yaml
# .github/workflows/release-notes.yml
- uses: actions/checkout@v4
- run: |
    python3 contrib/agent-mcp/agent_mcp_server.py << 'MCP'
    {"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"repo://semantic_diff?base=v1.0.0&head=HEAD"}}
    MCP
```

## Architecture

```
┌─────────────┐      stdin/stdout      ┌─────────────────────────┐
│  MCP Client │  ◄──────────────────►  │  agent_mcp_server.py    │
│  (Claude)   │                        │  - find_git_root()      │
└─────────────┘                        │  - resolve_git_agent()  │
                                     │  - shell out to git-    │
                                     │    agent commands         │
                                     └─────────────────────────┘
```

No background daemon, no ports, no configuration files.
