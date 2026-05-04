/*
 * Minimal stub for the "git ace" command.
 *
 * This implementation adds an optional "--ai" flag. When the flag is
 * supplied, the command forwards the remaining arguments to the existing
 * Ace agent integration ("git ace agent run opencode ..."). For all other
 * invocations the arguments are passed unchanged to the real Ace binary
 * (which may be provided later).
 *
 * The code follows the style of other built‑ins in the repository (e.g.
 * builtin/stripspace.c). It is deliberately lightweight – the goal is to
 * expose the new front‑end without altering the core Ace logic.
 */

#include "builtin.h"
#include "cache.h"
#include "quote.h"
#include "run-command.h"
#include "strbuf.h"

static const char * const ace_usage[] = {
    "git ace [--ai] <subcommand> [<args>...]",
    NULL,
};

int cmd_ace(int argc, const char **argv, const char *prefix)
{
    int ai_mode = 0;
    const char **new_argv;
    int new_argc = 0;
    int i;

    /* Simple parsing: detect "--ai" anywhere before the subcommand. */
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--ai")) {
            ai_mode = 1;
            continue; /* skip this token */
        }
        /* Preserve all other arguments. */
        new_argc++;
    }

    new_argv = xcalloc(new_argc + 1, sizeof(char *));
    new_argc = 0;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--ai"))
            continue;
        new_argv[new_argc++] = argv[i];
    }
    new_argv[new_argc] = NULL;

    if (ai_mode) {
        /* Build a new argument vector that calls the existing agent runner.
         * The expected form is:
         *   git ace agent run opencode <subcommand> [<args>...]
         */
        struct child_process cp = CHILD_PROCESS_INIT;
        const char **cp_argv;
        int cp_argc = 0;
        int j;

        /* Count needed slots: "git", "ace", "agent", "run", "opencode" + new_argv */
        cp_argc = 5 + new_argc;
        cp_argv = xcalloc(cp_argc + 1, sizeof(char *));
        j = 0;
        cp_argv[j++] = "git";
        cp_argv[j++] = "ace";
        cp_argv[j++] = "agent";
        cp_argv[j++] = "run";
        cp_argv[j++] = "opencode";
        for (i = 0; i < new_argc; i++)
            cp_argv[j++] = new_argv[i];
        cp_argv[j] = NULL;

        cp.argv = cp_argv;
        cp.use_shell = 0;
        cp.no_stdin = 0;
        cp.stdout_to_stderr = 0;
        cp.err = STDERR_FILENO;
        if (run_process(&cp))
            return -1;
        free(cp_argv);
        free(new_argv);
        return 0;
    }

    /* No --ai: forward to the real implementation (if any). For now we simply
     * invoke the same binary without modification, which will result in "git
     * ace: command not found" unless a full implementation exists elsewhere.
     */
    if (new_argc) {
        struct child_process cp = CHILD_PROCESS_INIT;
        const char **cp_argv;
        int cp_argc = new_argc + 1; /* prepend "git" */
        int k;
        cp_argv = xcalloc(cp_argc + 1, sizeof(char *));
        k = 0;
        cp_argv[k++] = "git";
        for (i = 0; i < new_argc; i++)
            cp_argv[k++] = new_argv[i];
        cp_argv[k] = NULL;
        cp.argv = cp_argv;
        cp.use_shell = 0;
        cp.no_stdin = 0;
        cp.stdout_to_stderr = 0;
        cp.err = STDERR_FILENO;
        run_process(&cp);
        free(cp_argv);
    }
    free(new_argv);
    return 0;
}

/* Register the command with Git's builtin table (see builtin.c). */

static const struct command builtin_ace = {
    "ace",
    cmd_ace,
    "git ace [--ai] <subcommand> [<args>...]",
    ace_usage,
};

/* The builtin.c file discovers commands via the symbol name "builtin_<cmd>". */

static const struct command *builtin_commands[] = { &builtin_ace, NULL };

/* Export the command table for the main program. */

const struct command **get_builtin_commands(void)
{
    return builtin_commands;
}
