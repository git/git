#ifndef GIT_EXEC_CMD_H
#define GIT_EXEC_CMD_H

extern void git_set_exec_path(const char *exec_path);
extern const char* git_exec_path(void);
extern int execv_git_cmd(const char **argv); /* NULL terminated */
extern int execl_git_cmd(const char *cmd, ...);


#endif /* GIT_EXEC_CMD_H */
