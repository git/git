#ifndef SAFETY_PROTOCOL_H
#define SAFETY_PROTOCOL_H

#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "string-list.h"
#include "advice.h"
#include "prompt.h"
#include "read-cache.h"
#include "repository.h"
#include "branch.h"
#include "commit.h"
#include "tree.h"
#include "object-store.h"
#include "git-compat-util.h"
#include "string-list.h"

/* Safety operation types */
enum safety_op_type {
    SAFETY_OP_CHECKOUT = 1,  /* git checkout -- . */
    SAFETY_OP_RESET = 2,     /* git reset --hard */
    SAFETY_OP_CLEAN = 3,     /* git clean */
    SAFETY_OP_RM = 4,        /* git rm */
    SAFETY_OP_BRANCH_D,      /* git branch -D */
    SAFETY_OP_STASH_DROP,    /* git stash drop */
    SAFETY_OP_PUSH_FORCE,    /* git push --force */
    SAFETY_OP_REBASE,        /* git rebase */
    SAFETY_OP_AMEND         /* git commit --amend */
};

/* Force levels */
enum safety_force_level {
    SAFETY_FORCE_NONE = 0,      /* No force flag */
    SAFETY_FORCE_SINGLE = 1,    /* Single -f */
    SAFETY_FORCE_DOUBLE = 2,    /* Double -ff */
    SAFETY_FORCE_IGNORE = 3     /* Force ignore overwrite */
};

/* Protection flags */
#define SAFETY_PROTECT_NESTED_GIT    (1 << 0)  /* Protect nested git repos */
#define SAFETY_PROTECT_BUILD         (1 << 1)  /* Protect build artifacts */
#define SAFETY_PROTECT_CONFIG        (1 << 2)  /* Protect config files */
#define SAFETY_PROTECT_IMPORTANT     (1 << 3)  /* Protect README, LICENSE etc */
#define SAFETY_PROTECT_UNTRACKED     (1 << 4)  /* Protect untracked files */
#define SAFETY_PROTECT_MODIFIED      (1 << 5)  /* Protect modified files */
#define SAFETY_PROTECT_LARGE         (1 << 6)  /* Protect large operations */
#define SAFETY_PROTECT_DEFAULT       (1 << 7)  /* Protect default branches */
#define SAFETY_PROTECT_CI            (1 << 8)  /* Protect CI/CD files */
#define SAFETY_PROTECT_HOOKS         (1 << 9)  /* Protect git hooks */
#define SAFETY_PROTECT_SUBMODULES    (1 << 10) /* Protect submodules */
#define SAFETY_PROTECT_WORKTREES     (1 << 11) /* Protect worktrees */
#define SAFETY_PROTECT_STASH         (1 << 12) /* Protect stash */
#define SAFETY_PROTECT_REFLOG        (1 << 13) /* Protect reflog */
#define SAFETY_PROTECT_PACKFILES     (1 << 14) /* Protect pack files */
#define SAFETY_PROTECT_BRANCHES      (1 << 15) /* Protect branches */
#define SAFETY_PROTECT_HISTORY       (1 << 16) /* Protect commit history */
#define SAFETY_PROTECT_UNCOMMITTED   (1 << 17) /* Protect uncommitted changes */
#define SAFETY_PROTECT_REMOTE        (1 << 18) /* Protect remote operations */
#define SAFETY_PROTECT_ALL           (~0)      /* Protect everything */

/* Size thresholds for warnings */
#define SAFETY_SIZE_WARN_THRESHOLD_SMALL   (10 * 1024 * 1024)   /* 10MB */
#define SAFETY_SIZE_WARN_THRESHOLD_MEDIUM  (50 * 1024 * 1024)   /* 50MB */
#define SAFETY_SIZE_WARN_THRESHOLD_LARGE   (200 * 1024 * 1024)  /* 200MB */
#define SAFETY_SIZE_WARN_THRESHOLD_HUGE    (1024 * 1024 * 1024) /* 1GB */

/* File count thresholds for warnings */
#define SAFETY_FILES_WARN_THRESHOLD_SMALL  100    /* 100 files */
#define SAFETY_FILES_WARN_THRESHOLD_MEDIUM 1000   /* 1000 files */
#define SAFETY_FILES_WARN_THRESHOLD_LARGE  10000  /* 10000 files */
#define SAFETY_FILES_WARN_THRESHOLD_HUGE   100000 /* 100000 files */

/* Time thresholds for warnings (in seconds) */
#define SAFETY_TIME_WARN_THRESHOLD_OLD     (7 * 24 * 60 * 60)   /* 1 week */
#define SAFETY_TIME_WARN_THRESHOLD_ANCIENT (30 * 24 * 60 * 60)  /* 1 month */

/* Risk levels for different operations */
enum safety_risk_level {
    RISK_NONE = 0,
    RISK_LOW,
    RISK_MEDIUM,
    RISK_HIGH,
    RISK_CRITICAL
};

/* Critical file patterns to protect */
struct safety_pattern {
    const char *pattern;
    unsigned int flags;
    const char *description;
};

/* Core safety tracking structure */
struct safety_state {
    /* Operation metadata */
    enum safety_op_type op_type;
    enum safety_force_level force_level;
    unsigned int protection_flags;
    
    /* Risk tracking */
    unsigned int has_nested_git:1;
    unsigned int has_build_artifacts:1;
    unsigned int has_config_files:1;
    unsigned int has_important_files:1;
    unsigned int has_untracked:1;
    unsigned int has_modified:1;
    unsigned int has_ci_config:1;
    unsigned int has_hooks:1;
    unsigned int has_submodules:1;
    unsigned int has_worktrees:1;
    unsigned int has_stash:1;
    unsigned int has_reflog:1;
    unsigned int has_packfiles:1;
    unsigned int has_branches:1;
    unsigned int has_uncommitted:1;
    unsigned int has_history_changes:1;
    unsigned int has_remote_changes:1;
    
    /* Size tracking */
    struct {
        unsigned long total_size;
        unsigned int file_count;
        unsigned int dir_count;
        unsigned int symlink_count;
    } stats;
    
    /* Critical paths tracking */
    struct string_list critical_paths;
    
    /* Risk level */
    enum safety_risk_level risk_level;
};

/* Function declarations */
void safety_state_init(struct safety_state *state, enum safety_op_type op_type,
                      const char *desc, struct repository *repo);
void safety_clear(struct safety_state *state);
int safety_check_path(struct safety_state *state, const char *path);
int safety_check_operation(struct safety_state *state);
int safety_confirm_operation(struct safety_state *state, const char *what);
void safety_update_risk_level(struct safety_state *state);
const char *safety_risk_level_name(enum safety_risk_level level);
void safety_set_force_level(struct safety_state *state, enum safety_force_level level);
enum safety_risk_level safety_get_risk_level(const struct safety_state *state);

#endif /* SAFETY_PROTOCOL_H */
