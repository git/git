#ifndef SIDEBAND_H
#define SIDEBAND_H

#define SIDEBAND_PROTOCOL_ERROR -2
#define SIDEBAND_REMOTE_ERROR -1

#define DEFAULT_PACKET_MAX 1000

int recv_sideband(const char *me, int in_stream, int out, int err, char *, int);

#endif
