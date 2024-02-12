#ifndef WS_H
#define WS_H

struct index_state;
struct strbuf;

/*
 * whitespace rules.
 * used by both diff and apply
 * last two digits are tab width
 */
#define WS_BLANK_AT_EOL         0100
#define WS_SPACE_BEFORE_TAB     0200
#define WS_INDENT_WITH_NON_TAB  0400
#define WS_CR_AT_EOL           01000
#define WS_BLANK_AT_EOF        02000
#define WS_TAB_IN_INDENT       04000
#define WS_TRAILING_SPACE      (WS_BLANK_AT_EOL|WS_BLANK_AT_EOF)
#define WS_DEFAULT_RULE (WS_TRAILING_SPACE|WS_SPACE_BEFORE_TAB|8)
#define WS_TAB_WIDTH_MASK        077
/* All WS_* -- when extended, adapt diff.c emit_symbol */
#define WS_RULE_MASK           07777
extern unsigned whitespace_rule_cfg;
unsigned whitespace_rule(struct index_state *, const char *);
unsigned parse_whitespace_rule(const char *);
unsigned ws_check(const char *line, int len, unsigned ws_rule);
void ws_check_emit(const char *line, int len, unsigned ws_rule, FILE *stream, const char *set, const char *reset, const char *ws);
char *whitespace_error_string(unsigned ws);
void ws_fix_copy(struct strbuf *, const char *, int, unsigned, int *);
int ws_blank_line(const char *line, int len);
#define ws_tab_width(rule)     ((rule) & WS_TAB_WIDTH_MASK)

#endif /* WS_H */
