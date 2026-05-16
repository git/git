#!/usr/bin/env python3
"""
Minimal MCP server wrapper for git-agent.

This server exposes two MCP resources:
  - repo://orientation   -> JSON from git agent-orient
  - repo://semantic_diff  -> JSON from git agent-diff HEAD~1..HEAD

Run directly for stdio transport (default MCP mode):
    python agent_mcp_server.py
"""

import json
import subprocess
import sys
import os

def mcp_send(msg):
    payload = json.dumps(msg)
    sys.stdout.write(f"Content-Length: {len(payload)}\r\n\r\n{payload}")
    sys.stdout.flush()

def mcp_recv():
    headers = {}
    while True:
        line = sys.stdin.readline()
        if line == "\r\n":
            break
        k, v = line.strip().split(": ", 1)
        headers[k] = v
    length = int(headers.get("Content-Length", 0))
    if length:
        body = sys.stdin.read(length)
        return json.loads(body)
    return None

def run_git_agent(*args):
    try:
        out = subprocess.check_output(
            ["git", "agent-" + args[0]] + list(args[1:]),
            cwd=os.environ.get("GIT_AGENT_REPO", "."),
            stderr=subprocess.STDOUT,
        )
        return out.decode("utf-8", errors="replace")
    except subprocess.CalledProcessError as e:
        return json.dumps({"error": e.output.decode("utf-8", errors="replace")})

def handle_initialize(params):
    return {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "serverInfo": {"name": "git-agent-mcp", "version": "0.1.0"},
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
        contents = run_git_agent("diff", "HEAD~1..HEAD")
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

def main():
    mcp_send(handle_initialize({}))
    while True:
        req = mcp_recv()
        if req is None:
            break
        method = req.get("method", "")
        params = req.get("params", {})
        handler = METHODS.get(method)
        if handler:
            result = handler(params)
        else:
            result = {}
        mcp_send({"jsonrpc": "2.0", "id": req.get("id"), "result": result})

if __name__ == "__main__":
    main()
