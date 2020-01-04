#ifndef VERSION_H
#define VERSION_H

extern const char git_version_string[];
extern const char git_built_from_commit_string[];

const char *git_user_agent(void);
const char *git_user_agent_sanitized(void);

#endif /* VERSION_H */
