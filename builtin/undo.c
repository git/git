#include "builtin.h"
#include "gettext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function prototypes
void undo_commit(void);
void undo_merge(void);
void undo_push(void);
void undo_file_changes(const char *file);
int cmd_undo(int argc, const char **argv, const char *prefix, struct repository *repo);

// Undo the last commit
void undo_commit(void) {
    int ret;
    printf(_("Undoing the last commit...\n"));
    ret = system("git reset --soft HEAD~1");
    if (ret != 0) {
        fprintf(stderr, _("Error: Failed to undo the last commit.\n"));
    } else {
        // Unstage the changes to simulate complete undo
        printf(_("Unstaging changes...\n"));
        ret = system("git restore --staged .");
        if (ret != 0) {
            fprintf(stderr, _("Error: Failed to unstage changes.\n"));
        }
    }
}

// Undo the last merge
void undo_merge(void) {
    int ret;
    printf(_("Undoing the merge...\n"));
    ret = system("git reset --hard ORIG_HEAD");
    if (ret != 0) {
        fprintf(stderr, _("Error: Failed to undo the merge.\n"));
    }
}

// Undo the last push
void undo_push(void) {
    int ret;
    printf(_("Undoing the last commit locally...\n"));
    ret = system("git reset --hard HEAD~1");
    if (ret != 0) {
        fprintf(stderr, _("Error: Failed to reset the last commit locally.\n"));
        return;
    }

    printf(_("Undoing the push...\n"));
    ret = system("git push --force");
    if (ret != 0) {
        fprintf(stderr, _("Error: Failed to undo the push.\n"));
    } else {
        printf(_("Push undone successfully.\n"));
    }
}


// Undo changes to a specific file
void undo_file_changes(const char *file) {
    int ret;
    char command[1024];
    printf(_("Undoing changes for file: %s\n"), file);
    snprintf(command, sizeof(command), "git restore %s", file);
    ret = system(command);
    if (ret != 0) {
        fprintf(stderr, _("Error: Failed to undo changes for file: %s\n"), file);
    }
}

// Main undo command
int cmd_undo(int argc, const char **argv, const char *prefix, struct repository *repo) {
    (void)prefix; // Mark prefix as unused
    (void)repo; // Mark repo as unused

    if (argc < 2) {
        printf(_("Please specify an action to undo (commit, merge, push, file_changes <file>)\n"));
        return 1;
    }

    if (strcmp(argv[1], "commit") == 0) {
        undo_commit();
    } else if (strcmp(argv[1], "merge") == 0) {
        undo_merge();
    } else if (strcmp(argv[1], "push") == 0) {
        undo_push();
    } else if (strcmp(argv[1], "file_changes") == 0 && argc == 3) {
        undo_file_changes(argv[2]);
    } else {
        printf(_("Unknown action: %s\n"), argv[1]);
        return 1;
    }

    return 0;
}
