#!/usr/bin/env python3
"""
Minimal MCP server wrapper for git-agent.

This server exposes two MCP resources:
  - repo://orientation   -> JSON from git agent-orient
  - repo://semantic_diff  -> JSON from git agent-diff HEAD~1..HEAD

Run directly for stdio transport (default MCP mode):
    python agent_mcp_server.py

The server auto-detects the active repository (cwd or nearest git root)
and downloads/builds git-agent if it is not found in PATH.
"""

import json
import os
import subprocess
import sys

LOG_FILE = os.path.expanduser("~/.git-agent-mcp.log")

def log(msg):
    with open(LOG_FILE, "a") as f:
        f.write(msg + "\n")
        f.flush()

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

_active_repo = None          # absolute path to the repository being served
_git_cmd = None              # resolved path to the git-agent binary

def find_git_root(start="."):
    """Walk upward until we hit a .git directory."""
    path = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(path, ".git")):
            log(f"find_git_root: found {path}")
            return path
        parent = os.path.dirname(path)
        if parent == path:
            log(f"find_git_root: no .git found, using {os.path.abspath(start)}")
            return os.path.abspath(start)
        path = parent

def resolve_git_agent():
    """Find or bootstrap a git-agent binary."""
    global _git_cmd

    # 1. Try the git already in PATH.
    try:
        subprocess.check_output(
            ["git", "agent-orient"],
            stderr=subprocess.STDOUT,
            cwd=_active_repo,
        )
        _git_cmd = ["git"]
        return
    except subprocess.CalledProcessError:
        pass
    except FileNotFoundError:
        pass

    # 2. If the active repo IS the git-agent source tree, use bin-wrappers.
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
        except subprocess.CalledProcessError:
            pass

    # 3. Check a user-local install location.
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
        except subprocess.CalledProcessError:
            pass

    # 4. Bootstrap: clone + build into ~/.cache/git-agent.
    cache_dir = os.path.join(os.path.expanduser("~"), ".cache", "git-agent")
    build_git = os.path.join(cache_dir, "bin-wrappers", "git")

    if not os.path.isfile(build_git):
        print(
            "git-agent not found in PATH — bootstrapping into ~/.cache/git-agent ...",
            file=sys.stderr,
            flush=True,
        )
        os.makedirs(cache_dir, exist_ok=True)
        # shallow clone
        subprocess.run(
            [
                "git", "clone", "--depth", "1",
                "https://github.com/dutixlf/git-ag.git",
                cache_dir,
            ],
            check=True,
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )
        # build
        subprocess.run(
            ["make", "-j" + str(os.cpu_count() or 2),
             "NO_CURL=YesPlease", "NO_RUST=YesPlease"],
            cwd=cache_dir,
            check=True,
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )

    # verify the freshly-built binary
    try:
        subprocess.check_output(
            [build_git, "agent-orient"],
            stderr=subprocess.STDOUT,
            cwd=_active_repo,
        )
        _git_cmd = [build_git]
    except subprocess.CalledProcessError as e:
        sys.exit("Bootstrapped git-agent does not respond: " + str(e))

def run_git_agent(*args):
    """Run a git-agent subcommand in the active repository."""
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
# MCP stdio helpers
# ---------------------------------------------------------------------------

def mcp_send(msg):
    payload = json.dumps(msg)
    out = f"Content-Length: {len(payload)}\r\n\r\n{payload}"
    log(f"mcp_send: {payload[:200]}")
    sys.stdout.write(out)
    sys.stdout.flush()

def mcp_recv():
    headers = {}
    while True:
        line = sys.stdin.readline()
        if not line:
            log("mcp_recv: EOF on stdin (readline)")
            raise EOFError
        if line == "\r\n" or line == "\n":
            break
        if ": " not in line:
            continue
        k, v = line.strip().split(": ", 1)
        headers[k] = v
    length = int(headers.get("Content-Length", 0))
    log(f"mcp_recv: Content-Length={length}")
    if length:
        body = sys.stdin.read(length)
        if not body:
            log("mcp_recv: EOF on stdin (read body)")
            raise EOFError
        log(f"mcp_recv: body={body[:200]}")
        return json.loads(body)
    return None

# ---------------------------------------------------------------------------
# MCP method handlers
# ---------------------------------------------------------------------------

def handle_initialize(params):
    # Mirror the client's requested protocol version (or fall back to ours).
    requested = params.get("protocolVersion", "2024-11-05") if params else "2024-11-05"
    return {
        "protocolVersion": requested,
        "capabilities": {},
        "serverInfo": {"name": "git-agent-mcp", "version": "0.2.0"},
    }

def handle_resources_list(params):
    return {
        "resources": [
            {
                "uri": "repo://orientation",
                "name": "Repository orientation",
                "mimeType": "application/json",
            },
            {
                "uri": "repo://semantic_diff",
                "name": "Semantic diff (HEAD~1..HEAD)",
                "mimeType": "application/json",
            },
        ]
    }

def handle_resources_read(params):
    uri = params.get("uri", "")
    if uri == "repo://orientation":
        contents = run_git_agent("orient")
    elif uri == "repo://semantic_diff":
        contents = run_git_agent("diff", "HEAD~1", "HEAD")
    else:
        contents = json.dumps({"error": "unknown resource"})
    return {
        "contents": [
            {"uri": uri, "mimeType": "application/json", "text": contents}
        ]
    }

METHODS = {
    "initialize": handle_initialize,
    "resources/list": handle_resources_list,
    "resources/read": handle_resources_read,
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    global _active_repo
    _active_repo = find_git_root()
    log(f"main: active_repo={_active_repo}, pid={os.getpid()}")

    while True:
        try:
            req = mcp_recv()
        except EOFError:
            log("main: EOFError, exiting")
            break
        if req is None:
            log("main: req is None, exiting")
            break
        method = req.get("method", "")
        log(f"main: method={method}")
        params = req.get("params", {})
        handler = METHODS.get(method)
        if handler:
            result = handler(params)
        else:
            result = {}
        # Notifications (no "id") do not get a response per MCP spec
        if "id" in req:
            mcp_send({"jsonrpc": "2.0", "id": req["id"], "result": result})

if __name__ == "__main__":
    log("=== server start ===")
    main()
    log("=== server exit ===")
