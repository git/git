#ifndef STRING_POOL_H_
#define STRING_POOL_H_

uint32_t pool_intern(const char *key);
const char *pool_fetch(uint32_t entry);
uint32_t pool_tok_r(char *str, const char *delim, char **saveptr);
void pool_print_seq(uint32_t len, const uint32_t *seq, char delim, FILE *stream);
void pool_print_seq_q(uint32_t len, const uint32_t *seq, char delim, FILE *stream);
uint32_t pool_tok_seq(uint32_t sz, uint32_t *seq, const char *delim, char *str);
void pool_reset(void);

#endif
