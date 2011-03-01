#ifndef SVNDUMP_H_
#define SVNDUMP_H_

int svndump_init(const char *filename);
void svndump_read(const char *url);
void svndump_deinit(void);
void svndump_reset(void);

#endif
