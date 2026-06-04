#ifndef TR2_CMD_NAME_H
#define TR2_CMD_NAME_H

/*
 * Append the current command name to the list being maintained
 * in the environment.
 *
 * The hierarchy for a top-level git command is just the current
 * command name.  For a child git process, the hierarchy includes the
 * names of the parent processes.
 *
 * The hierarchy for the current process will be exported to the
 * environment and inherited by child processes.
 */
void tr2_cmd_name_append_hierarchy(const char *name);

/*
 * Get the command name hierarchy for the current process.
 */
const char *tr2_cmd_name_get_hierarchy(void);

void tr2_cmd_name_release(void);

#endif /* TR2_CMD_NAME_H */
