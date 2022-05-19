#ifndef PROMPT_H
#define PROMPT_H

#define PROMPT_ASKPASS (1<<0)
#define PROMPT_ECHO    (1<<1)

char *but_prompt(const char *prompt, int flags);

int but_read_line_interactively(struct strbuf *line);

#endif /* PROMPT_H */
