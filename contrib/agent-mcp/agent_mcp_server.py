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


if __name__ == "__main__":
    try:
        _raw_log("=== FastMCP server starting mcp.run() ===")
        mcp.run()
        _raw_log("=== FastMCP server mcp.run() returned normally ===")
    except Exception as e:
        _raw_log(f"=== FastMCP server crashed: {e} ===")
        _raw_log(traceback.format_exc())
        raise
