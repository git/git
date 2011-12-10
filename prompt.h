#ifndef PROMPT_H
#define PROMPT_H

#define PROMPT_ASKPASS (1<<0)
#define PROMPT_ECHO    (1<<1)

char *git_prompt(const char *prompt, int flags);
char *git_getpass(const char *prompt);

#endif /* PROMPT_H */
