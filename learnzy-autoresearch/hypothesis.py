"""
hypothesis.py — THE AGENT'S ONLY MUTABLE FILE

Analog to train.py in karpathy/autoresearch.

Everything in this file is fair game for the agent to modify each run.
The agent's goal: maximize evidence_score by finding higher-quality,
more relevant, larger-sample papers for each hypothesis link.

DO NOT modify sources.py, agent.py, or the GitHub Actions workflow.
"""

# ─── SEARCH QUERIES ───────────────────────────────────────────────────────────
# One list per hypothesis link. The agent modifies these to discover better papers.
# Fixed validation queries in sources.py ALWAYS run in addition to these.
#
# Hypothesis links:
#   A_hrv_cognition    : HRV → executive function / memory / attention
#   B_sleep_cognition  : Sleep → memory consolidation / learning / retention
#   C_cognition_grades : Cognition → academic grades / exam performance
#   D1_focus_depression: HRV + sleep → depression (PHQ-9)
#   D2_focus_anxiety   : HRV + sleep → anxiety (GAD-7)
#   D3_focus_insomnia  : HRV + sleep → insomnia (ISI)

QUERIES = {
    "A_hrv_cognition": [
        '"heart rate variability" AND "working memory" AND "students"',
        '"vagal tone" AND "executive function" AND "university"',
        '"RMSSD" AND "cognitive performance" AND "healthy"',
        '"parasympathetic" AND "attention" AND "learning"',
    ],

    "B_sleep_cognition": [
        '"sleep deprivation" AND "memory encoding" AND "hippocampus"',
        '"sleep quality" AND "academic grades" AND "prospective"',
        '"slow wave sleep" AND "declarative memory"',
        '"sleep duration" AND "GPA" AND "university students"',
    ],

    "C_cognition_grades": [
        '"cognitive performance" AND "exam scores" AND "longitudinal"',
        '"working memory" AND "academic achievement" AND "students"',
        '"executive function" AND "academic performance" AND "adolescent"',
        '"attention" AND "exam performance" AND "university"',
    ],

    "D1_focus_depression": [
        '"HRV" AND "PHQ-9" AND "students"',
        '"sleep quality" AND "depression" AND "wearable" AND "students"',
        '"heart rate variability" AND "major depressive disorder" AND "resting"',
        '"RMSSD" AND "depression" AND "young adults"',
    ],

    "D2_focus_anxiety": [
        '"heart rate variability" AND "GAD-7" AND "anxiety"',
        '"autonomic nervous system" AND "anxiety disorder" AND "HRV"',
        '"sleep disturbance" AND "anxiety" AND "students" AND "wearable"',
        '"HRV biofeedback" AND "anxiety" AND "randomized"',
    ],

    "D3_focus_insomnia": [
        '"HRV" AND "insomnia severity index" AND "wearable"',
        '"autonomic" AND "insomnia" AND "heart rate variability"',
        '"sleep efficiency" AND "HRV" AND "mental health"',
        '"nocturnal HRV" AND "insomnia" AND "students"',
    ],
}

# ─── INCLUSION CRITERIA ────────────────────────────────────────────────────────
# Filters applied AFTER LLM extraction of paper metadata.
# Agent can tighten or loosen these to improve signal quality.

INCLUSION = {
    "min_sample_size": 20,      # exclude very small studies (n < 20)
    "min_year": 2010,           # focus on recent evidence
    "max_results_per_query": 50,
    "study_types": [            # which study designs to include
        "meta_analysis",
        "systematic_review",
        "rct",
        "prospective_cohort",
        "cross_sectional",
        "case_control",
    ],
}

# ─── SEARCH DEPTH PER LINK ────────────────────────────────────────────────────
# Controls how many results to fetch per query for each link.
# Agent should allocate more depth to weaker links.
# Total across all links ideally stays under ~300 API calls to fit in TIME_BUDGET.

SEARCH_DEPTH = {
    "A_hrv_cognition":    50,
    "B_sleep_cognition":  50,
    "C_cognition_grades": 40,
    "D1_focus_depression": 40,
    "D2_focus_anxiety":   40,
    "D3_focus_insomnia":  30,
}
