#!/usr/bin/env python3
"""Debug wrapper for git-agent MCP server - logs all stdin/stdout to a file."""
import os
import sys
import subprocess

LOG_FILE = os.path.expanduser("~/.git-agent-mcp-debug.log")

with open(LOG_FILE, "a") as log:
    log.write(f"\n{'='*60}\n")
    log.write(f"STARTED: pid={os.getpid()}, cwd={os.getcwd()}\n")
    log.write(f"args={sys.argv}\n")
    log.write(f"env PATH={os.environ.get('PATH', 'NOTSET')[:200]}\n")
    log.flush()

    server_path = os.path.join(os.path.dirname(__file__), "agent_mcp_server.py")
    proc = subprocess.Popen(
        [sys.executable, server_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    import threading
    def reader(pipe, label):
        while True:
            try:
                data = pipe.read(1024)
                if not data:
                    break
                if isinstance(data, bytes):
                    data = data.decode('utf-8', errors='replace')
                log.write(f"[{label}] {repr(data)}\n")
                log.flush()
                if label == "OUT":
                    sys.stdout.write(data)
                    sys.stdout.flush()
                elif label == "ERR":
                    sys.stderr.write(data)
                    sys.stderr.flush()
            except Exception as e:
                log.write(f"[{label}] ERROR: {e}\n")
                log.flush()
                break

    out_thread = threading.Thread(target=reader, args=(proc.stdout, "OUT"))
    err_thread = threading.Thread(target=reader, args=(proc.stderr, "ERR"))
    out_thread.start()
    err_thread.start()

    while True:
        try:
            data = sys.stdin.read(1024)
            if not data:
                break
            log.write(f"[IN] {repr(data)}\n")
            log.flush()
            proc.stdin.write(data.encode('utf-8') if isinstance(data, str) else data)
            proc.stdin.flush()
        except Exception as e:
            log.write(f"[IN] ERROR: {e}\n")
            log.flush()
            break

    proc.stdin.close()
    proc.wait()
    log.write(f"EXITED: rc={proc.returncode}\n")
    log.flush()
