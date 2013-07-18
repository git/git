#ifndef GIT_EXEC_CMD_H
#define GIT_EXEC_CMD_H

extern void git_set_argv_exec_path(const char *exec_path);
extern const char *git_extract_argv0_path(const char *path);
extern const char *git_exec_path(void);
extern void setup_path(void);
extern const char **prepare_git_cmd(const char **argv);
extern int execv_git_cmd(const char **argv); /* NULL terminated */
LAST_ARG_MUST_BE_NULL
extern int execl_git_cmd(const char *cmd, ...);
extern const char *system_path(const char *path);

#endif /* GIT_EXEC_CMD_H */
