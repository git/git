#ifndef SVNDUMP_H
#define SVNDUMP_H

int svndump_init(const char *filename);
int svndump_init_fd(int in_fd, int back_fd);
void svndump_read(const char *url, const char *local_ref, const char *notes_ref);
void svndump_deinit(void);
void svndump_reset(void);

#endif
