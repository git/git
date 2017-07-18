#ifndef SIDEBAND_H
#define SIDEBAND_H

#define SIDEBAND_PROTOCOL_ERROR -2
#define SIDEBAND_REMOTE_ERROR -1

int recv_sideband(const char *me, int in_stream, int out);
ssize_t send_sideband(int fd, int band, const char *data, ssize_t sz, int packet_max);

#endif
