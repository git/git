#ifndef PROMPT_H
#define PROMPT_H

#define PROMPT_ASKPASS (1<<0)
#define PROMPT_ECHO    (1<<1)

char *git_prompt(const char *prompt, int flags);

int git_read_line_interactively(struct strbuf *line);

#endif /* PROMPT_H */
