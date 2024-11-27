#include "safety-protocol.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "refs.h"
#include "commit.h"
#include "path.h"
#include "lockfile.h"

/* Critical file patterns with their protection flags */
static const struct safety_pattern critical_patterns[] = {
    /* Build artifacts and dependencies */
    {"node_modules/", SAFETY_PROTECT_BUILD, "Node.js dependencies"},
    {"vendor/", SAFETY_PROTECT_BUILD, "Vendor dependencies"},
    {"build/", SAFETY_PROTECT_BUILD, "Build artifacts"},
    {"dist/", SAFETY_PROTECT_BUILD, "Distribution files"},
    {"target/", SAFETY_PROTECT_BUILD, "Build target directory"},
    {"bin/", SAFETY_PROTECT_BUILD, "Binary files"},
    {"obj/", SAFETY_PROTECT_BUILD, "Object files"},
    
    /* Package manager files */
    {"package-lock.json", SAFETY_PROTECT_BUILD, "NPM lock file"},
    {"yarn.lock", SAFETY_PROTECT_BUILD, "Yarn lock file"},
    {"Gemfile.lock", SAFETY_PROTECT_BUILD, "Ruby gems lock file"},
    {"poetry.lock", SAFETY_PROTECT_BUILD, "Python Poetry lock file"},
    {"Cargo.lock", SAFETY_PROTECT_BUILD, "Rust Cargo lock file"},
    {"composer.lock", SAFETY_PROTECT_BUILD, "PHP Composer lock file"},
    {"pnpm-lock.yaml", SAFETY_PROTECT_BUILD, "PNPM lock file"},
    {"requirements.txt", SAFETY_PROTECT_BUILD, "Python requirements file"},
    {"go.sum", SAFETY_PROTECT_BUILD, "Go modules checksum"},
    
    /* Config files */
    {".env", SAFETY_PROTECT_CONFIG, "Environment variables"},
    {".env.local", SAFETY_PROTECT_CONFIG, "Local environment variables"},
    {".env.development", SAFETY_PROTECT_CONFIG, "Development environment variables"},
    {".env.production", SAFETY_PROTECT_CONFIG, "Production environment variables"},
    {"config/", SAFETY_PROTECT_CONFIG, "Configuration directory"},
    {"settings/", SAFETY_PROTECT_CONFIG, "Settings directory"},
    {".git/config", SAFETY_PROTECT_CONFIG, "Git config"},
    {"wp-config.php", SAFETY_PROTECT_CONFIG, "WordPress config"},
    {"config.php", SAFETY_PROTECT_CONFIG, "PHP config"},
    {"config.yml", SAFETY_PROTECT_CONFIG, "YAML config"},
    {"config.json", SAFETY_PROTECT_CONFIG, "JSON config"},
    
    /* IDE files */
    {".idea/", SAFETY_PROTECT_CONFIG, "IntelliJ IDEA files"},
    {".vscode/", SAFETY_PROTECT_CONFIG, "VSCode files"},
    {".eclipse/", SAFETY_PROTECT_CONFIG, "Eclipse files"},
    {".settings/", SAFETY_PROTECT_CONFIG, "IDE settings"},
    
    /* Important project files */
    {"README", SAFETY_PROTECT_IMPORTANT, "Project documentation"},
    {"LICENSE", SAFETY_PROTECT_IMPORTANT, "License file"},
    {"CONTRIBUTING", SAFETY_PROTECT_IMPORTANT, "Contribution guidelines"},
    {"CHANGELOG", SAFETY_PROTECT_IMPORTANT, "Change log"},
    {"AUTHORS", SAFETY_PROTECT_IMPORTANT, "Authors file"},
    {"SECURITY", SAFETY_PROTECT_IMPORTANT, "Security policy"},
    {"SUPPORT", SAFETY_PROTECT_IMPORTANT, "Support information"},
    {"docs/", SAFETY_PROTECT_IMPORTANT, "Documentation directory"},
    
    /* CI/CD files */
    {".github/", SAFETY_PROTECT_CI, "GitHub workflows"},
    {".gitlab-ci.yml", SAFETY_PROTECT_CI, "GitLab CI config"},
    {"Jenkinsfile", SAFETY_PROTECT_CI, "Jenkins pipeline"},
    {".travis.yml", SAFETY_PROTECT_CI, "Travis CI config"},
    {".circleci/", SAFETY_PROTECT_CI, "CircleCI config"},
    {"azure-pipelines.yml", SAFETY_PROTECT_CI, "Azure Pipelines config"},
    {"bitbucket-pipelines.yml", SAFETY_PROTECT_CI, "Bitbucket Pipelines config"},
    {".drone.yml", SAFETY_PROTECT_CI, "Drone CI config"},
    
    /* Key files */
    {"*.pem", SAFETY_PROTECT_CONFIG, "PEM key file"},
    {"*.key", SAFETY_PROTECT_CONFIG, "Key file"},
    {"*.crt", SAFETY_PROTECT_CONFIG, "Certificate file"},
    {"*.cer", SAFETY_PROTECT_CONFIG, "Certificate file"},
    {"id_rsa", SAFETY_PROTECT_CONFIG, "SSH private key"},
    {"id_dsa", SAFETY_PROTECT_CONFIG, "SSH private key"},
    {"*.keystore", SAFETY_PROTECT_CONFIG, "Java keystore"},
    {"*.jks", SAFETY_PROTECT_CONFIG, "Java keystore"},
    
    /* Database files */
    {"*.sql", SAFETY_PROTECT_IMPORTANT, "SQL database file"},
    {"*.db", SAFETY_PROTECT_IMPORTANT, "Database file"},
    {"*.sqlite", SAFETY_PROTECT_IMPORTANT, "SQLite database"},
    {"*.sqlite3", SAFETY_PROTECT_IMPORTANT, "SQLite3 database"},
    
    {NULL, 0, NULL}  /* List terminator */
};

void safety_state_init(struct safety_state *state, enum safety_operation_type op_type,
                      const char *desc, struct repository *repo)
{
    memset(state, 0, sizeof(*state));
    state->op_type = op_type;
    state->force_level = SAFETY_FORCE_NONE;
    state->risk_level = SAFETY_RISK_NONE;
    state->protection_flags = 0;
    state->operation_desc = desc;
    state->repo = repo;
    
    /* Set default protection flags based on operation type */
    switch (op_type) {
    case SAFETY_OP_CHECKOUT:
        state->protection_flags |= (SAFETY_PROTECT_MODIFIED | 
                                  SAFETY_PROTECT_UNTRACKED |
                                  SAFETY_PROTECT_IMPORTANT);
        break;
    case SAFETY_OP_RESET:
        state->protection_flags |= (SAFETY_PROTECT_MODIFIED |
                                  SAFETY_PROTECT_UNTRACKED |
                                  SAFETY_PROTECT_IMPORTANT |
                                  SAFETY_PROTECT_HISTORY);
        break;
    case SAFETY_OP_CLEAN:
        state->protection_flags |= (SAFETY_PROTECT_UNTRACKED |
                                  SAFETY_PROTECT_BUILD_ARTIFACTS |
                                  SAFETY_PROTECT_CONFIG |
                                  SAFETY_PROTECT_IMPORTANT);
        break;
    case SAFETY_OP_RM:
        state->protection_flags |= (SAFETY_PROTECT_MODIFIED |
                                  SAFETY_PROTECT_IMPORTANT |
                                  SAFETY_PROTECT_CONFIG);
        break;
    case SAFETY_OP_BRANCH_D:
        state->protection_flags |= (SAFETY_PROTECT_DEFAULT_BRANCH |
                                  SAFETY_PROTECT_BRANCHES);
        break;
    case SAFETY_OP_STASH_DROP:
        state->protection_flags |= SAFETY_PROTECT_STASH;
        break;
    case SAFETY_OP_PUSH_FORCE:
        state->protection_flags |= (SAFETY_PROTECT_REMOTE |
                                  SAFETY_PROTECT_HISTORY |
                                  SAFETY_PROTECT_DEFAULT_BRANCH);
        break;
    case SAFETY_OP_REBASE:
        state->protection_flags |= (SAFETY_PROTECT_HISTORY |
                                  SAFETY_PROTECT_MODIFIED |
                                  SAFETY_PROTECT_DEFAULT_BRANCH);
        break;
    case SAFETY_OP_AMEND:
        state->protection_flags |= (SAFETY_PROTECT_HISTORY |
                                  SAFETY_PROTECT_DEFAULT_BRANCH);
        break;
    }
}

int safety_check_operation(struct safety_state *state)
{
    int result = 0;
    unsigned int flags = state->protection_flags;
    
    /* Check various protection aspects based on flags */
    if (flags & SAFETY_PROTECT_NESTED_GIT)
        result |= check_nested_git(state);
        
    if (flags & SAFETY_PROTECT_BUILD_ARTIFACTS)
        result |= check_build_artifacts(state);
        
    if (flags & SAFETY_PROTECT_CONFIG)
        result |= check_config_files(state);
        
    if (flags & SAFETY_PROTECT_IMPORTANT)
        result |= check_important_files(state);
        
    if (flags & SAFETY_PROTECT_UNTRACKED)
        result |= check_untracked_files(state);
        
    if (flags & SAFETY_PROTECT_MODIFIED)
        result |= check_modified_files(state);
        
    if (flags & SAFETY_PROTECT_CI_CD)
        result |= check_ci_cd(state);
        
    if (flags & SAFETY_PROTECT_HOOKS)
        result |= check_hooks(state);
        
    if (flags & SAFETY_PROTECT_LARGE_OPS)
        result |= check_large_operation(state);
        
    if (flags & SAFETY_PROTECT_DEFAULT_BRANCH)
        result |= check_default_branch(state);
        
    if (flags & SAFETY_PROTECT_SUBMODULES)
        result |= check_submodules(state);
        
    if (flags & SAFETY_PROTECT_WORKTREES)
        result |= check_worktrees(state);
        
    if (flags & SAFETY_PROTECT_STASH)
        result |= check_stash(state);
        
    if (flags & SAFETY_PROTECT_REFLOG)
        result |= check_reflog(state);
        
    if (flags & SAFETY_PROTECT_PACK_FILES)
        result |= check_packfiles(state);
        
    if (flags & SAFETY_PROTECT_BRANCHES)
        result |= check_branches(state);
        
    if (flags & SAFETY_PROTECT_HISTORY)
        result |= check_history_rewrite(state);
        
    if (flags & SAFETY_PROTECT_UNCOMMITTED)
        result |= check_uncommitted_changes(state);
        
    if (flags & SAFETY_PROTECT_REMOTE)
        result |= check_remote_operation(state);

    /* Calculate final risk level */
    state->risk_level = calculate_risk_level(state);
    
    /* Return 0 if operation is safe, non-zero if protection triggered */
    return result;
}

void safety_set_force_level(struct safety_state *state, enum safety_force_level level)
{
    state->force_level = level;
}

void safety_update_protection_flags(struct safety_state *state, unsigned int flags)
{
    state->protection_flags |= flags;
}

enum safety_risk_level safety_get_risk_level(const struct safety_state *state)
{
    return state->risk_level;
}

void branch_info_init(struct branch_info *info)
{
    memset(info, 0, sizeof(*info));
}

void branch_info_release(struct branch_info *info)
{
    /* Free any dynamically allocated memory */
    if (info->name)
        free((void *)info->name);
    if (info->path)
        free((void *)info->path);
    memset(info, 0, sizeof(*info));
}

/* Initialize safety state for an operation */
void safety_init(struct safety_state *state, enum safety_op_type op_type)
{
    memset(state, 0, sizeof(*state));
    state->op_type = op_type;
    state->force_level = SAFETY_FORCE_NONE;
    state->protection_flags = safety_get_config_flags();
    string_list_init(&state->critical_paths, 1);
}

/* Check if a path matches any critical patterns */
static int path_matches_pattern(const char *path, const struct safety_pattern *pattern)
{
    return strstr(path, pattern->pattern) != NULL;
}

/* Check for git hooks */
static int check_hooks(struct safety_state *state, const char *path)
{
    return starts_with(path, ".git/hooks/");
}

/* Check for git submodules */
static int check_submodules(struct safety_state *state, const char *path)
{
    struct stat st;
    char *submodule_path = mkpathdup("%s/.git", path);
    int is_submodule = 0;
    
    if (!lstat(submodule_path, &st)) {
        if (S_ISREG(st.st_mode)) {
            /* It's a submodule (file contains path to real .git dir) */
            is_submodule = 1;
            state->has_submodules = 1;
        }
    }
    
    free(submodule_path);
    return is_submodule;
}

/* Check for git worktrees */
static int check_worktrees(struct safety_state *state, const char *path)
{
    return starts_with(path, ".git/worktrees/");
}

/* Check for git stash */
static int check_stash(struct safety_state *state, const char *path)
{
    if (starts_with(path, ".git/refs/stash") || 
        strstr(path, "stash@{") != NULL) {
        state->has_stash = 1;
        return 1;
    }
    return 0;
}

/* Check for git reflog */
static int check_reflog(struct safety_state *state, const char *path)
{
    if (starts_with(path, ".git/logs/")) {
        state->has_reflog = 1;
        return 1;
    }
    return 0;
}

/* Check for git pack files */
static int check_packfiles(struct safety_state *state, const char *path)
{
    if (starts_with(path, ".git/objects/pack/")) {
        state->has_packfiles = 1;
        return 1;
    }
    return 0;
}

/* Check for git branches */
static int check_branches(struct safety_state *state, const char *path)
{
    if (starts_with(path, ".git/refs/heads/") ||
        starts_with(path, ".git/refs/remotes/")) {
        state->has_branches = 1;
        return 1;
    }
    return 0;
}

/* Check for uncommitted changes */
static int check_uncommitted_changes(struct safety_state *state)
{
    struct rev_info rev;
    int has_changes = 0;
    
    /* Check index for uncommitted changes */
    if (has_uncommitted_changes(the_repository)) {
        state->has_uncommitted = 1;
        has_changes = 1;
    }
    
    /* Check for staged changes */
    if (has_staged_changes(the_repository)) {
        state->has_uncommitted = 1;
        has_changes = 1;
    }
    
    return has_changes;
}

/* Check for history rewrite risks */
static int check_history_rewrite(struct safety_state *state, const char *ref)
{
    struct commit *commit;
    struct object_id oid;
    int is_dangerous = 0;
    
    /* Check if we're operating on published history */
    if (get_oid(ref, &oid))
        return 0;
    
    commit = lookup_commit_reference(the_repository, &oid);
    if (!commit)
        return 0;
    
    /* Check if commit is referenced by any remote */
    if (commit_is_published(commit)) {
        state->has_history_changes = 1;
        is_dangerous = 1;
    }
    
    return is_dangerous;
}

/* Check for remote operation risks */
static int check_remote_operation(struct safety_state *state, const char *ref)
{
    struct branch *branch;
    int is_dangerous = 0;
    
    /* Get current branch */
    branch = branch_get(ref);
    if (!branch)
        return 0;
    
    /* Check if we're on default branch */
    if (is_default_branch(branch)) {
        state->has_remote_changes = 1;
        is_dangerous = 1;
    }
    
    /* Check if branch is protected */
    if (branch_is_protected(branch)) {
        state->has_remote_changes = 1;
        is_dangerous = 1;
    }
    
    return is_dangerous;
}

/* Calculate risk level based on state */
static enum safety_risk_level calculate_risk_level(struct safety_state *state)
{
    enum safety_risk_level risk = RISK_LOW;
    
    /* Check for critical conditions */
    if (state->has_nested_git || state->has_submodules ||
        (state->has_remote_changes && state->op_type == SAFETY_OP_PUSH_FORCE))
        risk = RISK_CRITICAL;
    
    /* Check for high-risk conditions */
    else if (state->has_config_files || state->has_important_files ||
             state->has_history_changes || state->has_uncommitted ||
             state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_HUGE ||
             state->stats.file_count > SAFETY_FILES_WARN_THRESHOLD_HUGE)
        risk = RISK_HIGH;
    
    /* Check for medium-risk conditions */
    else if (state->has_build_artifacts || state->has_ci_config ||
             state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_LARGE ||
             state->stats.file_count > SAFETY_FILES_WARN_THRESHOLD_LARGE)
        risk = RISK_MEDIUM;
    
    return risk;
}

/* Check if a path should be protected */
int safety_check_path(struct safety_state *state, const char *path)
{
    struct stat st;
    const struct safety_pattern *pattern;
    int is_protected = 0;
    
    /* Operation-specific checks */
    switch (state->op_type) {
        case SAFETY_OP_CHECKOUT:
        case SAFETY_OP_RESET:
            if ((state->protection_flags & SAFETY_PROTECT_UNCOMMITTED) &&
                check_uncommitted_changes(state))
                is_protected = 1;
            break;
            
        case SAFETY_OP_REBASE:
        case SAFETY_OP_AMEND:
            if ((state->protection_flags & SAFETY_PROTECT_HISTORY) &&
                check_history_rewrite(state, path))
                is_protected = 1;
            break;
            
        case SAFETY_OP_PUSH_FORCE:
            if ((state->protection_flags & SAFETY_PROTECT_REMOTE) &&
                check_remote_operation(state, path))
                is_protected = 1;
            break;
            
        default:
            break;
    }
    
    /* Check for nested git repository */
    if ((state->protection_flags & SAFETY_PROTECT_NESTED_GIT) && 
        is_nonbare_repository_dir(path)) {
        state->has_nested_git = 1;
        is_protected = 1;
    }
    
    /* Check for git hooks */
    if ((state->protection_flags & SAFETY_PROTECT_HOOKS) &&
        check_hooks(state, path)) {
        state->has_hooks = 1;
        is_protected = 1;
    }
    
    /* Check for git submodules */
    if ((state->protection_flags & SAFETY_PROTECT_SUBMODULES) &&
        check_submodules(state, path)) {
        state->has_submodules = 1;
        is_protected = 1;
    }
    
    /* Check for git worktrees */
    if ((state->protection_flags & SAFETY_PROTECT_WORKTREES) &&
        check_worktrees(state, path)) {
        state->has_worktrees = 1;
        is_protected = 1;
    }
    
    /* Check for git stash */
    if ((state->protection_flags & SAFETY_PROTECT_STASH) &&
        check_stash(state, path)) {
        state->has_stash = 1;
        is_protected = 1;
    }
    
    /* Check for git reflog */
    if ((state->protection_flags & SAFETY_PROTECT_REFLOG) &&
        check_reflog(state, path)) {
        state->has_reflog = 1;
        is_protected = 1;
    }
    
    /* Check for git pack files */
    if ((state->protection_flags & SAFETY_PROTECT_PACKFILES) &&
        check_packfiles(state, path)) {
        state->has_packfiles = 1;
        is_protected = 1;
    }
    
    /* Check for git branches */
    if ((state->protection_flags & SAFETY_PROTECT_BRANCHES) &&
        check_branches(state, path)) {
        state->has_branches = 1;
        is_protected = 1;
    }
    
    /* Check against critical patterns */
    for (pattern = critical_patterns; pattern->pattern; pattern++) {
        if ((state->protection_flags & pattern->flags) && 
            path_matches_pattern(path, pattern)) {
            if (pattern->flags & SAFETY_PROTECT_BUILD)
                state->has_build_artifacts = 1;
            if (pattern->flags & SAFETY_PROTECT_CONFIG)
                state->has_config_files = 1;
            if (pattern->flags & SAFETY_PROTECT_IMPORTANT)
                state->has_important_files = 1;
            if (pattern->flags & SAFETY_PROTECT_CI)
                state->has_ci_config = 1;
            
            string_list_append(&state->critical_paths, path);
            is_protected = 1;
        }
    }
    
    /* Track size and stats information */
    if (lstat(path, &st) == 0) {
        state->stats.total_size += st.st_size;
        state->stats.file_count++;
        
        if (S_ISDIR(st.st_mode))
            state->stats.dir_count++;
        else if (S_ISLNK(st.st_mode))
            state->stats.symlink_count++;
            
        /* Check size thresholds */
        if ((state->protection_flags & SAFETY_PROTECT_LARGE) && 
            (state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_MEDIUM || 
             state->stats.file_count > SAFETY_FILES_WARN_THRESHOLD_MEDIUM)) {
            is_protected = 1;
        }
    }
    
    /* Calculate risk level */
    state->risk_level = calculate_risk_level(state);
    
    return is_protected;
}

/* Print warning and get confirmation if needed */
int safety_confirm_operation(struct safety_state *state, const char *op_desc)
{
    struct strbuf msg = STRBUF_INIT;
    int is_dangerous = 0;
    struct string_list_item *item;
    
    strbuf_addf(&msg, _("WARNING: You are about to %s:\n"), op_desc);
    
    /* Report risk level */
    switch (state->risk_level) {
        case RISK_CRITICAL:
            strbuf_addstr(&msg, _("  !!! CRITICAL RISK OPERATION !!!\n"));
            is_dangerous = 1;
            break;
        case RISK_HIGH:
            strbuf_addstr(&msg, _("  !! HIGH RISK OPERATION !!\n"));
            is_dangerous = 1;
            break;
        case RISK_MEDIUM:
            strbuf_addstr(&msg, _("  ! MEDIUM RISK OPERATION !\n"));
            is_dangerous = 1;
            break;
        case RISK_LOW:
            strbuf_addstr(&msg, _("  Low risk operation\n"));
            break;
        default:
            break;
    }
    
    /* Report specific dangers */
    if (state->has_nested_git) {
        strbuf_addstr(&msg, _("  ! DANGER: Will affect nested Git repositories!\n"));
        is_dangerous = 1;
    }
    
    if (state->has_submodules) {
        strbuf_addstr(&msg, _("  ! DANGER: Will affect Git submodules!\n"));
        is_dangerous = 1;
    }
    
    if (state->has_hooks) {
        strbuf_addstr(&msg, _("  ! Will affect Git hooks\n"));
        is_dangerous = 1;
    }
    
    if (state->has_worktrees) {
        strbuf_addstr(&msg, _("  ! Will affect Git worktrees\n"));
        is_dangerous = 1;
    }
    
    if (state->has_stash) {
        strbuf_addstr(&msg, _("  ! Will affect Git stash\n"));
        is_dangerous = 1;
    }
    
    if (state->has_reflog) {
        strbuf_addstr(&msg, _("  ! Will affect Git reflog\n"));
        is_dangerous = 1;
    }
    
    if (state->has_packfiles) {
        strbuf_addstr(&msg, _("  ! Will affect Git pack files\n"));
        is_dangerous = 1;
    }
    
    if (state->has_branches) {
        strbuf_addstr(&msg, _("  ! Will affect Git branches\n"));
        is_dangerous = 1;
    }
    
    if (state->has_build_artifacts) {
        strbuf_addstr(&msg, _("  ! Will affect build artifacts and dependencies\n"));
        is_dangerous = 1;
    }
    
    if (state->has_config_files) {
        strbuf_addstr(&msg, _("  ! Will affect configuration files\n"));
        is_dangerous = 1;
    }
    
    if (state->has_important_files) {
        strbuf_addstr(&msg, _("  ! Will affect important project files\n"));
        is_dangerous = 1;
    }
    
    if (state->has_ci_config) {
        strbuf_addstr(&msg, _("  ! Will affect CI/CD configuration\n"));
        is_dangerous = 1;
    }
    
    /* Operation-specific warnings */
    switch (state->op_type) {
        case SAFETY_OP_CHECKOUT:
        case SAFETY_OP_RESET:
            if (state->has_uncommitted) {
                strbuf_addstr(&msg, _("  ! WARNING: You have uncommitted changes that will be lost!\n"));
                is_dangerous = 1;
            }
            break;
            
        case SAFETY_OP_REBASE:
        case SAFETY_OP_AMEND:
            if (state->has_history_changes) {
                strbuf_addstr(&msg, _("  ! WARNING: This will rewrite published history!\n"));
                is_dangerous = 1;
            }
            break;
            
        case SAFETY_OP_PUSH_FORCE:
            if (state->has_remote_changes) {
                strbuf_addstr(&msg, _("  ! WARNING: Force pushing to protected branch!\n"));
                is_dangerous = 1;
            }
            break;
            
        default:
            break;
    }
    
    /* List critical paths */
    if (state->critical_paths.nr > 0) {
        strbuf_addstr(&msg, _("\nCritical paths affected:\n"));
        for_each_string_list_item(item, &state->critical_paths) {
            strbuf_addf(&msg, "  - %s\n", item->string);
        }
    }
    
    /* Size and stats warnings */
    strbuf_addstr(&msg, _("\nOperation statistics:\n"));
    strbuf_addf(&msg, _("  * Files: %d\n"), state->stats.file_count);
    strbuf_addf(&msg, _("  * Directories: %d\n"), state->stats.dir_count);
    strbuf_addf(&msg, _("  * Symlinks: %d\n"), state->stats.symlink_count);
    strbuf_addf(&msg, _("  * Total size: %lu bytes\n"), state->stats.total_size);
    
    if (state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_HUGE) {
        strbuf_addstr(&msg, _("  ! HUGE operation warning (>1GB)\n"));
        is_dangerous = 1;
    } else if (state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_LARGE) {
        strbuf_addstr(&msg, _("  ! Large operation warning (>200MB)\n"));
        is_dangerous = 1;
    } else if (state->stats.total_size > SAFETY_SIZE_WARN_THRESHOLD_MEDIUM) {
        strbuf_addstr(&msg, _("  ! Medium operation warning (>50MB)\n"));
        is_dangerous = 1;
    }
    
    /* Force level requirements */
    if (is_dangerous && state->force_level < SAFETY_FORCE_DOUBLE) {
        strbuf_addstr(&msg, _("\nThis operation requires -ff (double force) due to dangerous content\n"));
        fprintf(stderr, "%s", msg.buf);
        strbuf_release(&msg);
        return 0;
    }
    
    /* Print warning */
    fprintf(stderr, "%s\n", msg.buf);
    strbuf_release(&msg);
    
    /* Get confirmation for dangerous operations */
    if (is_dangerous) {
        if (!isatty(0)) {
            error(_("Refusing dangerous operation in non-interactive mode.\nUse -ff to override or run in terminal"));
            return 0;
        }
        
        if (!ask(_("Are you ABSOLUTELY sure you want to proceed? Type 'yes' to confirm: "), 0)) {
            error(_("Operation aborted by user"));
            return 0;
        }
    }
    
    return 1;
}

/* Clean up safety state */
void safety_clear(struct safety_state *state)
{
    string_list_clear(&state->critical_paths, 0);
}

/* Get protection flags from git config */
unsigned int safety_get_config_flags(void)
{
    const char *value;
    unsigned int flags = 0;
    
    if (!git_config_get_string("safety.protectNestedGit", &value) && git_config_bool("safety.protectNestedGit", value))
        flags |= SAFETY_PROTECT_NESTED_GIT;
        
    if (!git_config_get_string("safety.protectBuild", &value) && git_config_bool("safety.protectBuild", value))
        flags |= SAFETY_PROTECT_BUILD;
        
    if (!git_config_get_string("safety.protectConfig", &value) && git_config_bool("safety.protectConfig", value))
        flags |= SAFETY_PROTECT_CONFIG;
        
    if (!git_config_get_string("safety.protectImportant", &value) && git_config_bool("safety.protectImportant", value))
        flags |= SAFETY_PROTECT_IMPORTANT;
        
    if (!git_config_get_string("safety.protectUntracked", &value) && git_config_bool("safety.protectUntracked", value))
        flags |= SAFETY_PROTECT_UNTRACKED;
        
    if (!git_config_get_string("safety.protectModified", &value) && git_config_bool("safety.protectModified", value))
        flags |= SAFETY_PROTECT_MODIFIED;
        
    if (!git_config_get_string("safety.protectLarge", &value) && git_config_bool("safety.protectLarge", value))
        flags |= SAFETY_PROTECT_LARGE;
        
    if (!git_config_get_string("safety.protectDefault", &value) && git_config_bool("safety.protectDefault", value))
        flags |= SAFETY_PROTECT_DEFAULT;
        
    if (!git_config_get_string("safety.protectCI", &value) && git_config_bool("safety.protectCI", value))
        flags |= SAFETY_PROTECT_CI;
        
    if (!git_config_get_string("safety.protectHooks", &value) && git_config_bool("safety.protectHooks", value))
        flags |= SAFETY_PROTECT_HOOKS;
        
    if (!git_config_get_string("safety.protectSubmodules", &value) && git_config_bool("safety.protectSubmodules", value))
        flags |= SAFETY_PROTECT_SUBMODULES;
        
    if (!git_config_get_string("safety.protectWorktrees", &value) && git_config_bool("safety.protectWorktrees", value))
        flags |= SAFETY_PROTECT_WORKTREES;
        
    if (!git_config_get_string("safety.protectStash", &value) && git_config_bool("safety.protectStash", value))
        flags |= SAFETY_PROTECT_STASH;
        
    if (!git_config_get_string("safety.protectReflog", &value) && git_config_bool("safety.protectReflog", value))
        flags |= SAFETY_PROTECT_REFLOG;
        
    if (!git_config_get_string("safety.protectPackfiles", &value) && git_config_bool("safety.protectPackfiles", value))
        flags |= SAFETY_PROTECT_PACKFILES;
        
    if (!git_config_get_string("safety.protectBranches", &value) && git_config_bool("safety.protectBranches", value))
        flags |= SAFETY_PROTECT_BRANCHES;
    
    return flags ? flags : SAFETY_PROTECT_ALL;  /* Default to all protections if none configured */
}

/* Set protection flags in git config */
int safety_set_config_flags(unsigned int flags)
{
    int ret = 0;
    
    ret |= git_config_set_in_file_gently("safety.protectNestedGit", 
        flags & SAFETY_PROTECT_NESTED_GIT ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectBuild",
        flags & SAFETY_PROTECT_BUILD ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectConfig",
        flags & SAFETY_PROTECT_CONFIG ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectImportant",
        flags & SAFETY_PROTECT_IMPORTANT ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectUntracked",
        flags & SAFETY_PROTECT_UNTRACKED ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectModified",
        flags & SAFETY_PROTECT_MODIFIED ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectLarge",
        flags & SAFETY_PROTECT_LARGE ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectDefault",
        flags & SAFETY_PROTECT_DEFAULT ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectCI",
        flags & SAFETY_PROTECT_CI ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectHooks",
        flags & SAFETY_PROTECT_HOOKS ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectSubmodules",
        flags & SAFETY_PROTECT_SUBMODULES ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectWorktrees",
        flags & SAFETY_PROTECT_WORKTREES ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectStash",
        flags & SAFETY_PROTECT_STASH ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectReflog",
        flags & SAFETY_PROTECT_REFLOG ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectPackfiles",
        flags & SAFETY_PROTECT_PACKFILES ? "true" : "false");
    ret |= git_config_set_in_file_gently("safety.protectBranches",
        flags & SAFETY_PROTECT_BRANCHES ? "true" : "false");
    
    return ret;
}

/* Check for nested git repositories */
static int check_nested_git(struct safety_state *state)
{
    struct strbuf path = STRBUF_INIT;
    int found = 0;
    
    strbuf_addstr(&path, state->repo->gitdir);
    strbuf_addstr(&path, "/.git");
    
    if (is_directory(path.buf))
        found = 1;
        
    strbuf_release(&path);
    return found;
}

/* Check for build artifacts */
static int check_build_artifacts(struct safety_state *state)
{
    const struct safety_pattern *pattern;
    int found = 0;
    
    for (pattern = critical_patterns; pattern->pattern; pattern++) {
        if (pattern->flags & SAFETY_PROTECT_BUILD) {
            if (path_matches_pattern(state->repo->worktree, pattern))
                found = 1;
        }
    }
    
    return found;
}

/* Check for important configuration files */
static int check_config_files(struct safety_state *state)
{
    const struct safety_pattern *pattern;
    int found = 0;
    
    for (pattern = critical_patterns; pattern->pattern; pattern++) {
        if (pattern->flags & SAFETY_PROTECT_CONFIG) {
            if (path_matches_pattern(state->repo->worktree, pattern))
                found = 1;
        }
    }
    
    return found;
}

/* Check for important project files */
static int check_important_files(struct safety_state *state)
{
    const struct safety_pattern *pattern;
    int found = 0;
    
    for (pattern = critical_patterns; pattern->pattern; pattern++) {
        if (pattern->flags & SAFETY_PROTECT_IMPORTANT) {
            if (path_matches_pattern(state->repo->worktree, pattern))
                found = 1;
        }
    }
    
    return found;
}

/* Check for untracked files */
static int check_untracked_files(struct safety_state *state)
{
    struct strbuf buf = STRBUF_INIT;
    int found = 0;
    
    /* Use git status to check for untracked files */
    strbuf_addstr(&buf, "git -C ");
    strbuf_addstr(&buf, state->repo->worktree);
    strbuf_addstr(&buf, " ls-files --others --exclude-standard");
    
    if (pipe_command(buf.buf, NULL, 0, NULL, 0, NULL, 0) == 0)
        found = 1;
        
    strbuf_release(&buf);
    return found;
}

/* Check for modified files */
static int check_modified_files(struct safety_state *state)
{
    struct strbuf buf = STRBUF_INIT;
    int found = 0;
    
    /* Use git status to check for modified files */
    strbuf_addstr(&buf, "git -C ");
    strbuf_addstr(&buf, state->repo->worktree);
    strbuf_addstr(&buf, " diff-index --quiet HEAD --");
    
    if (pipe_command(buf.buf, NULL, 0, NULL, 0, NULL, 0) != 0)
        found = 1;
        
    strbuf_release(&buf);
    return found;
}

/* Check for CI/CD configuration files */
static int check_ci_cd(struct safety_state *state)
{
    const struct safety_pattern *pattern;
    int found = 0;
    
    for (pattern = critical_patterns; pattern->pattern; pattern++) {
        if (pattern->flags & SAFETY_PROTECT_CI) {
            if (path_matches_pattern(state->repo->worktree, pattern))
                found = 1;
        }
    }
    
    return found;
}

/* Check for large operations */
static int check_large_operation(struct safety_state *state)
{
    struct strbuf buf = STRBUF_INIT;
    int count = 0;
    int found = 0;
    
    /* Count number of files affected */
    strbuf_addstr(&buf, "git -C ");
    strbuf_addstr(&buf, state->repo->worktree);
    strbuf_addstr(&buf, " status --porcelain | wc -l");
    
    if (pipe_command_to_int(&count, buf.buf, NULL, 0) == 0) {
        if (count > 100)  /* Consider operations affecting >100 files as large */
            found = 1;
    }
    
    strbuf_release(&buf);
    return found;
}

/* Check if operation affects default branch */
static int check_default_branch(struct safety_state *state)
{
    struct strbuf buf = STRBUF_INIT;
    struct strbuf default_branch = STRBUF_INIT;
    int found = 0;
    
    /* Get default branch name */
    strbuf_addstr(&buf, "git -C ");
    strbuf_addstr(&buf, state->repo->worktree);
    strbuf_addstr(&buf, " symbolic-ref refs/remotes/origin/HEAD | sed 's@^refs/remotes/origin/@@'");
    
    if (pipe_command_to_buffer(&default_branch, buf.buf, NULL, 0) == 0) {
        /* Check if operation affects default branch */
        if (state->operation_desc && 
            strstr(state->operation_desc, default_branch.buf))
            found = 1;
    }
    
    strbuf_release(&buf);
    strbuf_release(&default_branch);
    return found;
}
