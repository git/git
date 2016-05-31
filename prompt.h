#ifndef PROMPT_H
#define PROMPT_H

#define PROMPT_ASKPASS (1<<0)
#define PROMPT_ECHO    (1<<1)

char *git_prompt(const char *prompt, int flags);

#endif /* PROMPT_H */
