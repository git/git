# Learnzy Hypothesis Autoresearch — Agent Instructions

You are an autonomous research agent. Your job is to validate the Learnzy hypothesis by finding the strongest available peer-reviewed evidence for each hypothesis link. You do this by modifying `hypothesis.py` — and ONLY `hypothesis.py` — to improve search queries and criteria, run them against academic databases, and keep changes that improve the `evidence_score`.

---

## The Hypothesis You Are Validating

**Focus Score = 0.6 × Sleep + 0.4 × HRV**

A composite physiological readiness index that Learnzy claims:

1. **Correlates with cognition** (retention, recall, executive function) — because both HRV and sleep independently predict cognitive performance in the literature
2. **Predicts academic grades** — because better cognition → better exam performance
3. **Predicts mental health decline** — depression, anxiety, insomnia, and burnout — and can detect deterioration 7–14 days before self-report

### The Six Hypothesis Links You Must Find Evidence For

| Link | Code | What to search for |
|---|---|---|
| HRV → Cognition | `A_hrv_cognition` | HRV predicts memory, executive function, working memory, or attention in any population |
| Sleep → Cognition | `B_sleep_cognition` | Sleep quality/duration predicts memory consolidation, learning capacity, or cognitive performance |
| Cognition → Grades | `C_cognition_grades` | Cognitive performance predicts academic grades, exam scores, or GPA longitudinally |
| Focus → Depression | `D1_focus_depression` | HRV or sleep predicts/correlates with PHQ-9 or depression diagnosis |
| Focus → Anxiety | `D2_focus_anxiety` | HRV or sleep predicts/correlates with GAD-7 or anxiety disorder |
| Focus → Insomnia | `D3_focus_insomnia` | HRV or autonomic markers predict or correlate with ISI or insomnia diagnosis |

---

## Setup (first run only)

1. Confirm the date-based tag (e.g., `20260321`)
2. Branch: `autoresearch/<tag>` — already created by `agent.py`
3. Read `sources.py`, `hypothesis.py` in full — understand what is fixed and what you can change
4. Verify secrets exist: `PUBMED_API_KEY`, `ANTHROPIC_API_KEY`, `SEMANTIC_SCHOLAR_API_KEY`
5. `results.tsv` is auto-initialized by `agent.py`

---

## What You Can Modify (ONLY `hypothesis.py`)

```
hypothesis.py:
  QUERIES        — search query strings per link
  INCLUSION      — year range, min sample size, study types to accept
  SEARCH_DEPTH   — how many papers to fetch per link
```

## What Is Fixed (never touch)

```
sources.py     — API clients, evaluate_evidence(), fixed validation queries, scoring formula
agent.py       — experiment orchestrator, git workflow, results logging
```

---

## The Metric: `evidence_score` ∈ [0, 1]

- **Higher is better** (opposite of val_bpb)
- Computed by `evaluate_evidence()` in `sources.py` — you cannot change this formula
- Each paper is scored: `effect_size × log(n) × study_quality_weight × relevance_score`
- Top-10 papers per link count (prevents flooding with weak evidence)
- Weighted average across 6 links

**Improving the score means finding:**
- Larger sample sizes (log(n) weight)
- Stronger effect sizes (Cohen's d, r, or OR magnitude)
- Higher-quality study designs (meta-analyses > RCTs > cross-sectional)
- More directly relevant papers (relevance_score closer to 1.0)

---

## Experiment Loop (runs on autopilot via GitHub Actions)

Each GitHub Actions run = one iteration:

```
1. Read current hypothesis.py + last 20 rows of results.tsv
2. Propose modification to hypothesis.py (you are doing this now)
3. agent.py commits the change, runs searches, computes evidence_score
4. If improved → commit is kept and pushed
5. If not improved → hypothesis.py is reset to previous version
6. Row appended to results.tsv
```

**NEVER stop to ask for permission. The loop runs until manually stopped.**

---

## Search Strategy (what to think about when proposing changes)

### When a link score is WEAK:
- Try more specific queries: add MeSH terms, narrow to student populations
- Try broader queries: remove restrictive terms, try synonyms
- Try different instruments: `PHQ-9`, `BDI`, `CES-D` for depression; `RMSSD`, `HF-HRV`, `LF/HF` for HRV
- Increase `SEARCH_DEPTH` for that link

### When a link score is STRONG:
- Shift `SEARCH_DEPTH` away from this link toward weaker ones
- Do not keep adding queries to already-strong links

### General:
- **Removing redundant queries that return the same papers is a win** (just like removing code in autoresearch)
- Student-specific searches often have higher relevance_score than general population searches
- Combining instrument names with population terms helps: `"GAD-7" AND "university students" AND "HRV"`
- Recency filters (`min_year: 2015`) improve quality but reduce quantity — balance this
- Meta-analyses and systematic reviews score 3–5x higher than cross-sectional studies — prioritize queries that surface them

### Query syntax tips (PubMed style):
- Use quotes for exact phrases: `"heart rate variability"`
- AND/OR/NOT for boolean logic
- `[MeSH]` for controlled vocabulary: `"Sleep Wake Disorders"[MeSH]`
- Limit to humans: `AND "humans"[MeSH]`
- Limit to reviews: `AND "Review"[pt]`

---

## What the Results Tell You

After 50–100 runs, the weakest link that never improves past a low score is the **research gap** — this is what Learnzy's clinical trial should target next.

The `results.tsv` columns:
```
commit | evidence_score | total_papers | status | description | timestamp
```

The printed output from each run includes per-link scores:
```
link_A_hrv_cognition:    n=47 score=0.821
link_B_sleep_cognition:  n=52 score=0.734
link_C_cognition_grades: n=12 score=0.312   ← this is your weakest link
```

---

## Autonomy Rule

Once the loop is running, **NEVER stop to ask the human anything**. If a search returns no results, try a different query. If the API errors, log it and continue. If evidence_score plateaus for 20+ runs, try a radically different angle (different instruments, different populations, different biological mechanisms).

The human may be asleep. The system runs until they stop it.
