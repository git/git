#ifndef GIT_EXEC_CMD_H
#define GIT_EXEC_CMD_H

struct strvec;

void git_set_exec_path(const char *exec_path);
void git_resolve_executable_dir(const char *path);
const char *git_exec_path(void);
void setup_path(void);
const char **prepare_git_cmd(struct strvec *out, const char **argv);
int execv_git_cmd(const char **argv); /* NULL terminated */
LAST_ARG_MUST_BE_NULL
int execl_git_cmd(const char *cmd, ...);
char *system_path(const char *path);

#endif /* GIT_EXEC_CMD_H */
