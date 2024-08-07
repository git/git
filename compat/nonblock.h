#ifndef COMPAT_NONBLOCK_H
#define COMPAT_NONBLOCK_H

/*
 * Enable non-blocking I/O for the pipe specified by the passed-in descriptor.
 */
int enable_pipe_nonblock(int fd);

#endif
