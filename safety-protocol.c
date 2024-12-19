#define USE_THE_REPOSITORY_VARIABLE
#include "safety-protocol.h"
#include "git-compat-util.h"
#include "read-cache.h"
#include "config.h"
#include "gettext.h"
#include "refs.h"
#include "strbuf.h"
#include "dir.h"
#include "environment.h"
#include "prompt.h"
#include "wildmatch.h"
#include "path.h"
#include "repository.h"
#include "string-list.h"
#include "setup.h"
#include "wt-status.h"
#include "pathspec.h"
#include "object-store.h"
#include "submodule.h"
#include <sys/stat.h>
#include <unistd.h>

extern struct repository *the_repository;

/* Forward declarations */
static int check_untracked_files(struct repository *repo);
static int check_modified_files(struct repository *repo);
static int check_nested_git(struct safety_state *state);
static int check_build_artifacts(struct safety_state *state);
static int check_config_files(struct safety_state *state);
static int check_important_files(struct safety_state *state);
static int ask_yes_no_if_possible(const char *prompt);

/* Helper functions */
static int check_path_pattern(const char *path, const char *pattern)
{
    struct strbuf buf = STRBUF_INIT;
    int result = 0;
    
    strbuf_addstr(&buf, path);
    if (wildmatch(pattern, buf.buf, 0) == 0)
        result = 1;
    
    strbuf_release(&buf);
    return result;
}

/* Helper function to check if directory exists */
static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Helper function to get risk level name */
const char *safety_risk_level_name(enum safety_risk_level level)
{
    switch (level) {
    case RISK_NONE:
        return "None";
    case RISK_LOW:
        return "Low";
    case RISK_MEDIUM:
        return "Medium";
    case RISK_HIGH:
        return "High";
    case RISK_CRITICAL:
        return "Critical";
    default:
        return "Unknown";
    }
}

/* Calculate risk level based on state */
static enum safety_risk_level calculate_risk_level(struct safety_state *state)
{
    if (!state)
        return RISK_NONE;
        
    /* Check operation type */
    switch (state->op_type) {
    case SAFETY_OP_CLEAN:
    case SAFETY_OP_RM:
    case SAFETY_OP_PUSH_FORCE:
        return RISK_CRITICAL;
        
    case SAFETY_OP_RESET:
    case SAFETY_OP_BRANCH_D:
    case SAFETY_OP_STASH_DROP:
        return RISK_HIGH;
        
    case SAFETY_OP_REBASE:
    case SAFETY_OP_AMEND:
        return RISK_MEDIUM;
        
    case SAFETY_OP_CHECKOUT:
        return RISK_LOW;
        
    default:
        return RISK_NONE;
    }
}

/* Check for untracked files */
static int check_untracked_files(struct repository *repo)
{
    if (!repo)
        return 0;
        
    /* Check for untracked files */
    return repo->index->cache_changed;
}

/* Check for modified files */
static int check_modified_files(struct repository *repo)
{
    if (!repo)
        return 0;
        
    /* Check for modified files */
    return repo->index->cache_changed;
}

/* Initialize safety state */
void safety_state_init(struct safety_state *state, enum safety_op_type op_type,
                      const char *desc, struct repository *repo)
{
    if (!state)
        return;
        
    /* Initialize state */
    memset(state, 0, sizeof(*state));
    state->op_type = op_type;
    state->risk_level = RISK_NONE;
    state->force_level = SAFETY_FORCE_NONE;
    state->protection_flags = safety_get_config_flags();
    state->operation_desc = desc;
    state->repo = repo;
    
    /* Initialize critical paths list */
    memset(&state->critical_paths, 0, sizeof(state->critical_paths));
    
    /* Set default protection flags based on operation type */
    switch (op_type) {
    case SAFETY_OP_CHECKOUT:
    case SAFETY_OP_RESET:
    case SAFETY_OP_CLEAN:
    case SAFETY_OP_RM:
        state->protection_flags |= SAFETY_PROTECT_UNTRACKED | SAFETY_PROTECT_MODIFIED;
        break;
        
    case SAFETY_OP_BRANCH_D:
    case SAFETY_OP_STASH_DROP:
        state->protection_flags |= SAFETY_PROTECT_MODIFIED;
        break;
        
    case SAFETY_OP_PUSH_FORCE:
    case SAFETY_OP_REBASE:
    case SAFETY_OP_AMEND:
        state->protection_flags |= SAFETY_PROTECT_MODIFIED | SAFETY_PROTECT_BUILD;
        break;
        
    default:
        break;
    }
}

/* Check operation safety */
int safety_check_operation(struct safety_state *state)
{
    unsigned int flags;
    int is_protected = 0;
    
    if (!state || !state->repo)
        return 0;
        
    flags = state->protection_flags;
    
    /* Check for nested git repositories */
    if (flags & SAFETY_PROTECT_NESTED)
        is_protected |= check_nested_git(state);
        
    /* Check for untracked files */
    if (flags & SAFETY_PROTECT_UNTRACKED)
        is_protected |= check_untracked_files(state->repo);
        
    /* Check for modified files */
    if (flags & SAFETY_PROTECT_MODIFIED)
        is_protected |= check_modified_files(state->repo);
        
    /* Check for build artifacts */
    if (flags & SAFETY_PROTECT_BUILD)
        is_protected |= check_build_artifacts(state);
        
    /* Check for config files */
    if (flags & SAFETY_PROTECT_CONFIG)
        is_protected |= check_config_files(state);
        
    /* Check for important files */
    if (flags & SAFETY_PROTECT_IMPORTANT)
        is_protected |= check_important_files(state);
        
    return is_protected;
}

/* Confirm operation */
int safety_confirm_operation(struct safety_state *state)
{
    struct strbuf msg = STRBUF_INIT;
    int result = 0;
    
    if (!state)
        return 0;
        
    /* Build confirmation message */
    strbuf_addstr(&msg, _("Safety Check:\n"));
    if (state->operation_desc)
        strbuf_addf(&msg, _("Operation: %s\n"), state->operation_desc);
    strbuf_addf(&msg, _("Risk Level: %s\n"), safety_risk_level_name(state->risk_level));
    
    /* Ask for confirmation based on risk level */
    switch (state->risk_level) {
    case RISK_CRITICAL:
        if (!ask_yes_no_if_possible(_("Are you ABSOLUTELY sure you want to proceed? Type 'yes' to confirm: "))) {
            result = 1;
            goto cleanup;
        }
        break;
        
    case RISK_HIGH:
        if (!ask_yes_no_if_possible(_("This operation has high risk. Are you sure? Type 'yes' to confirm: "))) {
            result = 1;
            goto cleanup;
        }
        break;
        
    case RISK_MEDIUM:
        if (!ask_yes_no_if_possible(_("Proceed with this operation? [y/N]: "))) {
            result = 1;
            goto cleanup;
        }
        break;
        
    default:
        break;
    }
    
cleanup:
    strbuf_release(&msg);
    return result;
}

/* Clean up safety state */
void safety_clear(struct safety_state *state)
{
    if (!state)
        return;
        
    string_list_clear(&state->critical_paths, 0);
}

/* Check for nested git repositories */
static int check_nested_git(struct safety_state *state)
{
    if (!state || !state->repo)
        return 0;
        
    return is_inside_git_dir();
}

/* Check for build artifacts */
static int check_build_artifacts(struct safety_state *state)
{
    if (!state || !state->repo)
        return 0;
        
    /* Check common build directories and files */
    return (is_dir("build") ||
            is_dir("target") ||
            is_dir("dist") ||
            is_dir("node_modules"));
}

/* Check for config files */
static int check_config_files(struct safety_state *state)
{
    if (!state || !state->repo)
        return 0;
        
    /* Check for common config files */
    return (access(".gitconfig", F_OK) == 0 ||
            access(".env", F_OK) == 0 ||
            access("config.json", F_OK) == 0 ||
            access("config.yaml", F_OK) == 0 ||
            access("config.yml", F_OK) == 0);
}

/* Check for important files */
static int check_important_files(struct safety_state *state)
{
    if (!state || !state->repo)
        return 0;
        
    /* Check for important project files */
    return (access("README.md", F_OK) == 0 ||
            access("LICENSE", F_OK) == 0 ||
            access("CHANGELOG.md", F_OK) == 0 ||
            access("package.json", F_OK) == 0 ||
            access("Cargo.toml", F_OK) == 0 ||
            access("requirements.txt", F_OK) == 0);
}

/* Helper function to ask yes/no questions */
static int ask_yes_no_if_possible(const char *prompt)
{
    struct strbuf buf = STRBUF_INIT;
    int result;
    
    if (!isatty(0))
        return 0;
        
    strbuf_addstr(&buf, prompt);
    strbuf_addstr(&buf, " [y/N]: ");
    
    if (strbuf_getline(&buf, stdin) == EOF) {
        result = 0;
        goto cleanup;
    }
    
    result = (buf.buf[0] == 'y' || buf.buf[0] == 'Y');
    
cleanup:
    strbuf_release(&buf);
    return result;
}

/* Update risk level */
void safety_update_risk_level(struct safety_state *state)
{
    if (!state)
        return;
        
    state->risk_level = calculate_risk_level(state);
}

/* Get risk level */
enum safety_risk_level safety_get_risk_level(const struct safety_state *state)
{
    if (!state)
        return RISK_NONE;
        
    return state->risk_level;
}

/* Safety configuration flags */
unsigned int safety_get_config_flags(void)
{
    unsigned int flags = SAFETY_PROTECT_NONE;
    int bool_val;

    /* Read safety protection flags from git config */
    if (!git_config_get_bool("safety.protectNestedGit", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_NESTED;
    if (!git_config_get_bool("safety.protectUntracked", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_UNTRACKED;
    if (!git_config_get_bool("safety.protectModified", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_MODIFIED;
    if (!git_config_get_bool("safety.protectBuild", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_BUILD;
    if (!git_config_get_bool("safety.protectConfig", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_CONFIG;
    if (!git_config_get_bool("safety.protectImportant", &bool_val) && bool_val)
        flags |= SAFETY_PROTECT_IMPORTANT;

    return flags;
}

/* Set force level */
void safety_set_force_level(struct safety_state *state, enum safety_force_level level)
{
    if (!state)
        return;
    state->force_level = level;
}

/* Check path safety */
int safety_check_path(struct safety_state *state, const char *path)
{
    struct string_list_item *item;
    int is_protected = 0;
    
    if (!state || !path)
        return 0;
    
    /* Check if path is in critical paths list */
    for_each_string_list_item(item, &state->critical_paths) {
        if (check_path_pattern(path, item->string)) {
            is_protected = 1;
            break;
        }
    }
    
    /* Check based on protection flags */
    if (state->protection_flags & SAFETY_PROTECT_CONFIG)
        is_protected |= check_config_files(state);
    if (state->protection_flags & SAFETY_PROTECT_IMPORTANT)
        is_protected |= check_important_files(state);
    
    return !is_protected;
}
