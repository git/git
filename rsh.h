#ifndef RSH_H
#define RSH_H

int setup_connection(int *fd_in, int *fd_out, const char *remote_prog,
		     char *url, int rmt_argc, char **rmt_argv);

#endif
