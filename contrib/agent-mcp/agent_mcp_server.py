#!/usr/bin/env python3
"""
Git-Agent MCP Server — FastMCP implementation.

Exposes TOOLS (callable by the AI):
  get_repo_orientation()   -> text from `git agent-orient`
  get_semantic_diff(base, head) -> JSON from `git agent-diff`
  list_agent_commits(n)    -> recent commits with agent metadata
  read_agent_reasoning(commit) -> reasoning annotation for a commit

Also exposes RESOURCES (for passive context injection):
  repo://orientation
  repo://semantic_diff

Auto-detects repo from CWD and bootstraps git-agent if missing.
"""

import json
import os
import subprocess
import sys
import time
import traceback

# Unbuffered stdout/stderr so opencode sees responses immediately
os.environ["PYTHONUNBUFFERED"] = "1"
sys.stdout = os.fdopen(sys.stdout.fileno(), "w", buffering=1)
sys.stderr = os.fdopen(sys.stderr.fileno(), "w", buffering=1)

# ---------------------------------------------------------------------------
# Crash-safe logging — starts BEFORE any imports that might fail
# ---------------------------------------------------------------------------
LOG_FILE = os.path.expanduser("~/.git-agent-mcp.log")


def _raw_log(msg):
    try:
        with open(LOG_FILE, "a") as f:
            f.write(f"[{time.strftime('%H:%M:%S')}] [PID:{os.getpid()}] {msg}\n")
            f.flush()
    except Exception:
        pass


_raw_log("=== SCRIPT ENTRY ===")
_raw_log(f"cwd={os.getcwd()}  python={sys.executable}  args={sys.argv}")

# Allow running from venv without explicit activation
VENV_SITE = os.path.join(
    os.path.dirname(__file__), ".venv", "lib", "python3.14", "site-packages"
)
if os.path.isdir(VENV_SITE) and VENV_SITE not in sys.path:
    sys.path.insert(0, VENV_SITE)
    _raw_log(f"Added venv site-packages: {VENV_SITE}")

try:
    from mcp.server.fastmcp import FastMCP

    _raw_log("FastMCP imported OK")
except Exception as e:
    _raw_log(f"FAILED to import FastMCP: {e}")
    _raw_log(traceback.format_exc())
    raise


def log(msg):
    with open(LOG_FILE, "a") as f:
        f.write(msg + "\n")
        f.flush()


# ---------------------------------------------------------------------------
# Git-agent discovery / bootstrap
# ---------------------------------------------------------------------------
_active_repo = None
_git_cmd = None


def find_git_root(start="."):
    path = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(path, ".git")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            return os.path.abspath(start)
        path = parent


def resolve_git_agent():
    global _git_cmd

    # 1. PATH git
    try:
        subprocess.check_output(
            ["git", "agent-orient"],
            stderr=subprocess.STDOUT,
            cwd=_active_repo,
        )
        _git_cmd = ["git"]
        return
    except Exception:
        pass

    # 2. bin-wrappers inside repo
    candidate = os.path.join(_active_repo, "bin-wrappers", "git")
    if os.path.isfile(candidate):
        try:
            subprocess.check_output(
                [candidate, "agent-orient"],
                stderr=subprocess.STDOUT,
                cwd=_active_repo,
            )
            _git_cmd = [candidate]
            return
        except Exception:
            pass

    # 3. ~/.local/bin/git
    home_local = os.path.join(os.path.expanduser("~"), ".local", "bin", "git")
    if os.path.isfile(home_local):
        try:
            subprocess.check_output(
                [home_local, "agent-orient"],
                stderr=subprocess.STDOUT,
                cwd=_active_repo,
            )
            _git_cmd = [home_local]
            return
        except Exception:
            pass

    # 4. Bootstrap into ~/.cache/git-agent
    cache_dir = os.path.join(os.path.expanduser("~"), ".cache", "git-agent")
    build_git = os.path.join(cache_dir, "bin-wrappers", "git")
    if not os.path.isfile(build_git):
        os.makedirs(cache_dir, exist_ok=True)
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "https://github.com/dutixlf/git-ag.git",
                cache_dir,
            ],
            check=True,
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )
        subprocess.run(
            [
                "make",
                "-j" + str(os.cpu_count() or 2),
                "NO_CURL=YesPlease",
                "NO_RUST=YesPlease",
            ],
            cwd=cache_dir,
            check=True,
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )
    subprocess.check_output(
        [build_git, "agent-orient"],
        stderr=subprocess.STDOUT,
        cwd=_active_repo,
    )
    _git_cmd = [build_git]


def run_git_agent(*args):
    if _git_cmd is None:
        resolve_git_agent()
    try:
        out = subprocess.check_output(
            _git_cmd + ["agent-" + args[0]] + list(args[1:]),
            cwd=_active_repo,
            stderr=subprocess.STDOUT,
        )
        return out.decode("utf-8", errors="replace")
    except subprocess.CalledProcessError as e:
        return json.dumps({"error": e.output.decode("utf-8", errors="replace")})


REASONING_GUIDE = """
# Agent Reasoning Annotation — Best Practices Guide

## Purpose
Reasoning annotations document WHY a change was made, not WHAT was changed.
They help future agents (and humans) understand the intent, constraints, and
decisions behind a commit.

## What to Include

1. **Problem Statement**
   - What issue or requirement triggered this change?
   - What was broken, missing, or needed improvement?
   - Example: "MCP server timed out because it sent an unsolicited initialize
   response before the client requested it."

2. **Root Cause Analysis**
   - Why did the problem occur?
   - What assumptions were wrong?
   - Example: "The server called mcp_send(handle_initialize()) on startup,
   violating the MCP request-response protocol."

3. **Solution Overview**
   - What approach was chosen and why?
   - What alternatives were considered and rejected?
   - Example: "Removed the unsolicited initialize call and made the server
   wait for client requests. Also made git-agent bootstrap lazy to avoid
   timeouts during server initialization."

4. **Key Decisions**
   - Any trade-offs or non-obvious choices
   - Performance, security, or compatibility implications
   - Example: "Used FastMCP SDK instead of raw stdio to get proper protocol
   handling, accepting the dependency on the mcp PyPI package."

5. **Verification**
   - How was the fix tested?
   - What confirms the problem is resolved?
   - Example: "Manual protocol test: client sends initialize -> server
   responds correctly; client sends resources/list -> server returns list."

## What NOT to Include

1. **No Local Environment Details**
   - Do NOT mention specific local tools, paths, or machine state
   - Bad:  "I used duckduckgo_search to find the MCP spec"
   - Good: "The MCP specification requires servers to wait for client
   initialize requests before responding"

2. **No Personal Narrative**
   - Do NOT use first person or describe your thought process
   - Bad:  "I was confused about why it wasn't working"
   - Good: "The protocol violation caused opencode to timeout waiting
   for a response to its own initialize request"

3. **No Implementation Trivia**
   - Do NOT list every file changed or line edited
   - The diff already shows WHAT changed
   - Focus on WHY the changes were necessary

4. **No Redundant Information**
   - Do NOT repeat information from the commit message
   - Do NOT restate the agent trailers (Agent-Id, Agent-Task, etc.)

## Privacy and Security

- Reasoning annotations are stored in the repository (refs/agent/commits)
- They are fetched by `git clone` (if agent refs are configured)
- They may be read by other agents, CI systems, or humans
- NEVER include: API keys, passwords, internal URLs, proprietary data,
  personal information, or anything that could compromise security

## Formatting Guidelines

- Write in plain text (no markdown required, but ok)
- Use concise paragraphs, not bullet lists unless necessary
- One paragraph per major point
- Total length: 200-800 characters is ideal
- Maximum: 2000 characters (enforced by tooling)

## Examples

### Good Reasoning
"The MCP server was sending an unsolicited initialize response on startup,
violating the MCP protocol which requires servers to wait for client requests.
This caused opencode to interpret the response as garbage and timeout. Fixed
by removing the preemptive initialize call and making git-agent resolution
lazy (only runs when a resource/tool is actually invoked)."

### Bad Reasoning
"I was trying to get the MCP server to work with opencode. I used the
playwright tool to test it and then I used duckduckgo to search for the MCP
specification. I found that the server needs to wait for the client. I made
some changes to agent_mcp_server.py and it seems to work now."

## Quick Checklist

- [ ] Explains WHY, not WHAT
- [ ] Contains no local tool references
- [ ] Contains no personal narrative
- [ ] Contains no sensitive data
- [ ] Focuses on decisions and trade-offs
- [ ] Under 800 characters (ideal)
"""


# ---------------------------------------------------------------------------
# FastMCP Server
# ---------------------------------------------------------------------------
_active_repo = find_git_root()
log(f"=== FastMCP server start: repo={_active_repo} ===")

mcp = FastMCP("git-agent")


# ---------------------------------------------------------------------------
# TOOLS — callable by the AI assistant
# ---------------------------------------------------------------------------
@mcp.tool()
def get_repo_orientation() -> str:
    """Return repository orientation: branch, recent commits, agent metadata."""
    log("TOOL: get_repo_orientation called")
    return run_git_agent("orient")


@mcp.tool()
def get_semantic_diff(base: str = "HEAD~1", head: str = "HEAD") -> str:
    """Return semantic diff JSON between two commits (default HEAD~1..HEAD)."""
    log(f"TOOL: get_semantic_diff called base={base} head={head}")
    return run_git_agent("diff", base, head)


@mcp.tool()
def list_agent_commits(n: int = 5) -> str:
    """List recent agent commits with metadata. Returns formatted text."""
    log(f"TOOL: list_agent_commits called n={n}")
    return run_git_agent("log", "-" + str(n), "HEAD")


@mcp.tool()
def read_agent_reasoning(commit: str = "HEAD") -> str:
    """Read the agent reasoning annotation for a commit (default HEAD)."""
    log(f"TOOL: read_agent_reasoning called commit={commit}")
    return run_git_agent("log", "--reasoning", "-1", commit)


@mcp.tool()
def read_agent_plan(commit: str = "HEAD") -> str:
    """Read the agent plan annotation for a commit (default HEAD)."""
    log(f"TOOL: read_agent_plan called commit={commit}")
    return run_git_agent("log", "--plan", "-1", commit)


@mcp.tool()
def read_agent_context(commit: str = "HEAD") -> str:
    """Read the agent context annotation for a commit (default HEAD)."""
    log(f"TOOL: read_agent_context called commit={commit}")
    return run_git_agent("log", "--context", "-1", commit)


@mcp.tool()
def verify_agent_trailers(commit: str = "HEAD") -> str:
    """Verify agent trailers on a commit and return JSON report."""
    log(f"TOOL: verify_agent_trailers called commit={commit}")
    return run_git_agent("verify", commit)


@mcp.tool()
def start_agent_session(session_id: str, task: str = "") -> str:
    """Start a new agent session with optional task description."""
    log(f"TOOL: start_agent_session called session_id={session_id}")
    args = ["--start", "--session-id=" + session_id]
    if task:
        args.append("--task=" + task)
    return run_git_agent("session", *args)


@mcp.tool()
def end_agent_session(session_id: str) -> str:
    """End an agent session and return its log."""
    log(f"TOOL: end_agent_session called session_id={session_id}")
    return run_git_agent("session", "--end", "--session-id=" + session_id)


@mcp.tool()
def get_reasoning_guide() -> str:
    """Return the best practices guide for agent reasoning annotations."""
    log("TOOL: get_reasoning_guide called")
    return REASONING_GUIDE


# ---------------------------------------------------------------------------
# RESOURCES — passive data for context injection
# ---------------------------------------------------------------------------
@mcp.resource("repo://orientation")
def get_orientation_resource() -> str:
    """Repository orientation text (passive resource)."""
    log("RESOURCE: get_orientation called")
    return run_git_agent("orient")


@mcp.resource("repo://semantic_diff", mime_type="application/json")
def get_semantic_diff_resource() -> str:
    """Semantic diff JSON (HEAD~1..HEAD) (passive resource)."""
    log("RESOURCE: get_semantic_diff called")
    return run_git_agent("diff", "HEAD~1", "HEAD")


@mcp.resource("repo://reasoning_guide")
def get_reasoning_guide_resource() -> str:
    """Best practices guide for agent reasoning annotations."""
    log("RESOURCE: get_reasoning_guide called")
    return REASONING_GUIDE


if __name__ == "__main__":
    try:
        _raw_log("=== FastMCP server starting mcp.run() ===")
        mcp.run()
        _raw_log("=== FastMCP server mcp.run() returned normally ===")
    except Exception as e:
        _raw_log(f"=== FastMCP server crashed: {e} ===")
        _raw_log(traceback.format_exc())
        raise
