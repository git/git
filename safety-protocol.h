#ifndef SAFETY_PROTOCOL_H
#define SAFETY_PROTOCOL_H

/* Required git headers */
#include "git-compat-util.h"
#include "config.h"
#include "repository.h"
#include "string-list.h"
#include "branch.h"
#include "commit.h"
#include "dir.h"
#include "strbuf.h"
#include "prompt.h"

/* Safety risk levels */
enum safety_risk_level {
    RISK_NONE,
    RISK_LOW,
    RISK_MEDIUM,
    RISK_HIGH,
    RISK_CRITICAL
};

/* Safety operation types */
enum safety_op_type {
    SAFETY_OP_NONE,
    SAFETY_OP_CHECKOUT,
    SAFETY_OP_RESET,
    SAFETY_OP_CLEAN,
    SAFETY_OP_RM,
    SAFETY_OP_BRANCH_D,
    SAFETY_OP_STASH_DROP,
    SAFETY_OP_PUSH_FORCE,
    SAFETY_OP_REBASE,
    SAFETY_OP_AMEND
};

/* Safety force levels */
enum safety_force_level {
    SAFETY_FORCE_NONE,
    SAFETY_FORCE_SOFT,
    SAFETY_FORCE_MEDIUM,
    SAFETY_FORCE_HARD,
    SAFETY_FORCE_SINGLE,  /* Single -f for clean */
    SAFETY_FORCE_DOUBLE   /* Double -f for clean */
};

/* Safety protection flags */
#define SAFETY_PROTECT_NONE          0x00000000
#define SAFETY_PROTECT_NESTED        0x00000001  /* Protect nested git repos */
#define SAFETY_PROTECT_UNTRACKED     0x00000002  /* Protect untracked files */
#define SAFETY_PROTECT_MODIFIED      0x00000004  /* Protect modified files */
#define SAFETY_PROTECT_BUILD         0x00000008  /* Protect build artifacts */
#define SAFETY_PROTECT_CONFIG        0x00000010  /* Protect config files */
#define SAFETY_PROTECT_IMPORTANT     0x00000020  /* Protect important files */
#define SAFETY_PROTECT_HISTORY       0x00000040  /* Protect history rewrites */
#define SAFETY_PROTECT_REMOTE        0x00000080  /* Protect remote operations */
#define SAFETY_PROTECT_HOOKS         0x00000100  /* Protect git hooks */
#define SAFETY_PROTECT_SUBMODULES    0x00000200  /* Protect submodules */
#define SAFETY_PROTECT_WORKTREES     0x00000400  /* Protect worktrees */
#define SAFETY_PROTECT_STASH         0x00000800  /* Protect stash */
#define SAFETY_PROTECT_REFLOG        0x00001000  /* Protect reflog */
#define SAFETY_PROTECT_PACKFILES     0x00002000  /* Protect pack files */
#define SAFETY_PROTECT_BRANCHES      0x00004000  /* Protect branches */
#define SAFETY_PROTECT_DEFAULT       0x00008000  /* Protect default branch */
#define SAFETY_PROTECT_UNCOMMITTED   0x00010000  /* Protect uncommitted changes */
#define SAFETY_PROTECT_ALL           0xFFFFFFFF  /* Protect everything */

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

/* Critical file patterns to protect */
struct safety_pattern {
    const char *pattern;
    unsigned int flags;
    const char *description;
};

/* Branch safety information structure */
struct safety_branch_info {
    const char *name;
    const char *path;
    unsigned int is_default:1;
    unsigned int is_protected:1;
    unsigned int has_upstream:1;
    unsigned int has_local_changes:1;
};

/* Core safety tracking structure */
struct safety_state {
    /* Operation metadata */
    enum safety_op_type op_type;
    enum safety_force_level force_level;
    unsigned int protection_flags;
    const char *operation_desc;
    struct repository *repo;
    
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

/* Forward declarations for safety functions */
void safety_state_init(struct safety_state *state, enum safety_op_type op_type,
                      const char *desc, struct repository *repo);
void safety_clear(struct safety_state *state);
int safety_check_path(struct safety_state *state, const char *path);
int safety_check_operation(struct safety_state *state);
int safety_confirm_operation(struct safety_state *state);
void safety_update_risk_level(struct safety_state *state);
const char *safety_risk_level_name(enum safety_risk_level level);
void safety_set_force_level(struct safety_state *state, enum safety_force_level level);
enum safety_risk_level safety_get_risk_level(const struct safety_state *state);
unsigned int safety_get_config_flags(void);
int safety_set_config_flags(unsigned int flags);
void safety_branch_info_init(struct safety_branch_info *info);
void safety_branch_info_release(struct safety_branch_info *info);

#endif /* SAFETY_PROTOCOL_H */
