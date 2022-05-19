#ifndef VERSION_H
#define VERSION_H

extern const char but_version_string[];
extern const char but_built_from_cummit_string[];

const char *but_user_agent(void);
const char *but_user_agent_sanitized(void);

#endif /* VERSION_H */
