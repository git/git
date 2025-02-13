#ifndef VERSION_H
#define VERSION_H

extern const char git_version_string[];
extern const char git_built_from_commit_string[];

struct strbuf;

const char *git_user_agent(void);
const char *git_user_agent_sanitized(void);

/*
 * Trim and replace each character with ascii code below 32 or above
 * 127 (included) using a dot '.' character.
*/
void redact_non_printables(struct strbuf *buf);

#endif /* VERSION_H */
