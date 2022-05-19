#ifndef GIT_EXEC_CMD_H
#define GIT_EXEC_CMD_H

struct strvec;

void but_set_exec_path(const char *exec_path);
void but_resolve_executable_dir(const char *path);
const char *but_exec_path(void);
void setup_path(void);
const char **prepare_but_cmd(struct strvec *out, const char **argv);
int execv_but_cmd(const char **argv); /* NULL terminated */
LAST_ARG_MUST_BE_NULL
int execl_but_cmd(const char *cmd, ...);
char *system_path(const char *path);

#endif /* GIT_EXEC_CMD_H */
