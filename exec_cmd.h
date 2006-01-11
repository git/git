#ifndef __GIT_EXEC_CMD_H_
#define __GIT_EXEC_CMD_H_

extern void git_set_exec_path(const char *exec_path);
extern const char* git_exec_path(void);
extern int execv_git_cmd(char **argv); /* NULL terminated */
extern int execl_git_cmd(char *cmd, ...);


#endif /* __GIT_EXEC_CMD_H_ */
