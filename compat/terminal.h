#ifndef COMPAT_TERMINAL_H
#define COMPAT_TERMINAL_H

/*
 * Save the terminal attributes so they can be restored later by a
 * call to restore_term(). Note that every successful call to
 * save_term() must be matched by a call to restore_term() even if the
 * attributes have not been changed. Returns 0 on success, -1 on
 * failure.
 */
int save_term(int full_duplex);
/* Restore the terminal attributes that were saved with save_term() */
void restore_term(void);

char *git_terminal_prompt(const char *prompt, int echo);

/* Read a single keystroke, without echoing it to the terminal */
int read_key_without_echo(struct strbuf *buf);

#endif /* COMPAT_TERMINAL_H */
