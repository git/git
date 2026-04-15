#ifndef VERSION_H
#define VERSION_H

extern const char git_version_string[];
extern const char git_built_from_commit_string[];

const char *git_user_agent(void);
const char *git_user_agent_sanitized(void);

/*
  Try to get information about the system using uname(2).
  Return -1 and put an error message into 'buf' in case of uname()
  error. Return 0 and put uname info into 'buf' otherwise.
*/
int get_uname_info(struct strbuf *buf, unsigned int full);


#endif /* VERSION_H */
