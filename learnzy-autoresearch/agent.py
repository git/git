"""
agent.py — FIXED ORCHESTRATOR (do not modify)

Analog to the training loop orchestrator in karpathy/autoresearch.
One execution of this script = one experiment iteration.

Flow (mirrors autoresearch program.md loop exactly):
  1. Set up git branch for this session tag
  2. Read hypothesis.py + recent results.tsv
  3. Call Claude API → get modified hypothesis.py
  4. Commit modified hypothesis.py (tentative)
  5. Run searches via sources.py (5-min wall-clock budget)
  6. Compute evidence_score via evaluate_evidence()
  7. If improved → keep commit | If not → git reset hypothesis.py
  8. Append row to results.tsv
  9. Print grep-friendly structured output

GitHub Actions runs this script once per cron tick (every 10 min).
Git state (which hypothesis.py won) persists across runs via the repo.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

# ─── PATHS ────────────────────────────────────────────────────────────────────
ROOT = Path(__file__).parent
HYPOTHESIS_FILE = ROOT / "hypothesis.py"
RESULTS_FILE = ROOT / "results.tsv"
PROGRAM_FILE = ROOT / "program.md"
RUN_LOG = ROOT / "run.log"

OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")
AGENT_MODEL = "gpt-4o"  # main reasoning model for hypothesis modification


# ─── GIT HELPERS ──────────────────────────────────────────────────────────────

def git(cmd: str, check: bool = True) -> str:
    result = subprocess.run(
        f"git {cmd}", shell=True, capture_output=True, text=True, cwd=ROOT
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"git {cmd} failed:\n{result.stderr}")
    return result.stdout.strip()


def ensure_branch(tag: str) -> str:
    """Create autoresearch/<tag> branch if it doesn't exist."""
    branch = f"autoresearch/{tag}"
    existing = git("branch --list " + branch)
    if not existing:
        git(f"checkout -b {branch}")
    else:
        git(f"checkout {branch}", check=False)
    return branch


def get_best_score() -> float:
    """Read the best evidence_score seen so far from results.tsv."""
    if not RESULTS_FILE.exists():
        return 0.0
    best = 0.0
    for line in RESULTS_FILE.read_text().splitlines()[1:]:  # skip header
        parts = line.split("\t")
        if len(parts) >= 2:
            try:
                score = float(parts[1])
                best = max(best, score)
            except ValueError:
                pass
    return best


def append_result(commit: str, score: float, total_papers: int,
                  status: str, description: str) -> None:
    """Append a row to results.tsv (not committed to git)."""
    header = "commit\tevidence_score\ttotal_papers\tstatus\tdescription\ttimestamp"
    if not RESULTS_FILE.exists() or RESULTS_FILE.stat().st_size == 0:
        RESULTS_FILE.write_text(header + "\n")
    timestamp = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    row = f"{commit}\t{score:.6f}\t{total_papers}\t{status}\t{description}\t{timestamp}"
    with RESULTS_FILE.open("a") as f:
        f.write(row + "\n")


# ─── LOAD HYPOTHESIS MODULE ───────────────────────────────────────────────────

def load_hypothesis():
    """Dynamically import hypothesis.py to get QUERIES, INCLUSION, SEARCH_DEPTH."""
    spec = importlib.util.spec_from_file_location("hypothesis", HYPOTHESIS_FILE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ─── CLAUDE API CALL (hypothesis modification) ────────────────────────────────

def _claude_api(messages: list, model: str = AGENT_MODEL, max_tokens: int = 4096) -> str:
    payload = json.dumps({
        "model": model,
        "max_tokens": max_tokens,
        "messages": messages,
    }).encode()
    req = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions",
        data=payload,
        headers={
            "Authorization": f"Bearer {OPENAI_API_KEY}",
            "content-type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=60) as r:
        response = json.loads(r.read())
    return response["choices"][0]["message"]["content"]


def propose_hypothesis_modification(
    current_hypothesis: str,
    recent_results: str,
    program_instructions: str,
    best_score: float,
) -> str:
    """
    Ask Claude to propose a modified hypothesis.py based on recent results.
    Returns the full text of the new hypothesis.py.
    """
    messages = [
        {
            "role": "user",
            "content": f"""{program_instructions}

---
CURRENT HYPOTHESIS.PY:
```python
{current_hypothesis}
```

RECENT RESULTS (most recent last):
```
{recent_results}
```

CURRENT BEST EVIDENCE SCORE: {best_score:.6f}

Your task: propose a modification to hypothesis.py that you think will improve the evidence_score.
Focus on the weakest links shown in recent results.
Return ONLY the complete new hypothesis.py file content — no explanation, no markdown fences, just the Python file.
""",
        }
    ]
    return _claude_api(messages)


# ─── MAIN EXPERIMENT LOOP ─────────────────────────────────────────────────────

def main() -> None:
    t_run_start = time.time()

    # 1. Determine session tag from date
    tag = datetime.utcnow().strftime("%Y%m%d")
    print(f"[agent] Starting experiment — tag={tag}", flush=True)

    # 2. Ensure we're on the right branch
    branch = ensure_branch(tag)
    print(f"[agent] Branch: {branch}", flush=True)

    # 3. Read current state
    current_hypothesis = HYPOTHESIS_FILE.read_text()
    program_instructions = PROGRAM_FILE.read_text() if PROGRAM_FILE.exists() else ""

    # Read last 20 rows of results.tsv for context
    recent_results = ""
    if RESULTS_FILE.exists():
        lines = RESULTS_FILE.read_text().splitlines()
        recent_results = "\n".join(lines[-21:])  # header + last 20

    best_score = get_best_score()
    print(f"[agent] Best score so far: {best_score:.6f}", flush=True)

    # 4. Ask Claude to propose modification
    if not OPENAI_API_KEY:
        print("[agent] WARNING: No OPENAI_API_KEY — skipping hypothesis modification", flush=True)
        new_hypothesis = current_hypothesis
        description = "no-api-key (baseline)"
    else:
        print("[agent] Calling Claude to propose hypothesis modification...", flush=True)
        try:
            new_hypothesis = propose_hypothesis_modification(
                current_hypothesis, recent_results, program_instructions, best_score
            )
            # Strip markdown fences if Claude added them
            if new_hypothesis.startswith("```"):
                lines = new_hypothesis.splitlines()
                new_hypothesis = "\n".join(
                    l for l in lines if not l.startswith("```")
                )
            description = "claude-proposed"
        except Exception as e:
            print(f"[agent] Claude API error: {e} — using current hypothesis", flush=True)
            new_hypothesis = current_hypothesis
            description = f"api-error: {e}"

    # 5. Write modified hypothesis.py and commit tentatively
    HYPOTHESIS_FILE.write_text(new_hypothesis)
    git('add hypothesis.py')
    commit_msg = f"experiment {tag}-{int(time.time())}"
    git(f'commit -m "{commit_msg}" --allow-empty')
    commit_hash = git("rev-parse --short HEAD")
    print(f"[agent] Committed: {commit_hash}", flush=True)

    # 6. Import the new hypothesis and run searches
    print("[agent] Running searches...", flush=True)
    t_search_start = time.time()

    # Import sources (fixed infra)
    sys.path.insert(0, str(ROOT))
    import sources
    importlib.reload(sources)

    hyp = load_hypothesis()

    try:
        papers_by_link = sources.run_searches(
            queries_by_link=hyp.QUERIES,
            inclusion=hyp.INCLUSION,
            search_depth=hyp.SEARCH_DEPTH,
        )
        evidence_score = sources.evaluate_evidence(papers_by_link)
        total_papers = sum(len(v) for v in papers_by_link.values())
        search_status = "ok"
    except Exception as e:
        print(f"[agent] Search failed: {e}", flush=True)
        papers_by_link = {l: [] for l in sources.LINK_WEIGHTS}
        evidence_score = 0.0
        total_papers = 0
        search_status = f"error: {e}"

    run_seconds = time.time() - t_run_start

    # 7. Keep or reset based on evidence_score
    if evidence_score > best_score:
        status = "improved"
        print(f"[agent] IMPROVED: {best_score:.6f} → {evidence_score:.6f} — keeping commit", flush=True)
        # In GitHub Actions, the push step in research.yml handles the push
    else:
        status = "no_improvement" if evidence_score > 0 else "failed"
        print(f"[agent] No improvement ({evidence_score:.6f} ≤ {best_score:.6f}) — resetting hypothesis.py", flush=True)
        # Reset hypothesis.py to previous state
        git(f"checkout HEAD~1 -- hypothesis.py", check=False)
        # Amend the commit to restore original hypothesis.py
        git("add hypothesis.py")
        git(f'commit --amend -m "{commit_msg} [reset]" --allow-empty')

    # 8. Append to results.tsv
    append_result(commit_hash, evidence_score, total_papers, status, description)

    # 9. Print structured summary (grep-friendly, analog to train.py summary block)
    sources.print_summary(
        evidence_score=evidence_score,
        papers_by_link=papers_by_link,
        best_score=best_score,
        run_seconds=run_seconds,
        status=status,
    )


if __name__ == "__main__":
    main()
