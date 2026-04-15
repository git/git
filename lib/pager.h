#ifndef PAGER_H
#define PAGER_H

struct child_process;
struct repository;

const char *git_pager(struct repository *r, int stdout_is_tty);
void setup_pager(struct repository *r);
void wait_for_pager(void);
int pager_in_use(void);
int term_columns(void);
void term_clear_line(void);
int decimal_width(uintmax_t);
int check_pager_config(struct repository *r, const char *cmd);
void prepare_pager_args(struct child_process *, const char *pager);

extern int pager_use_color;

#endif /* PAGER_H */
