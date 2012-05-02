#ifndef VARINT_H
#define VARINT_H

#include "git-compat-util.h"

extern int encode_varint(uintmax_t, unsigned char *);
extern uintmax_t decode_varint(const unsigned char **);

#endif /* VARINT_H */
