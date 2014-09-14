#ifndef VARINT_H
#define VARINT_H

extern int encode_varint(uintmax_t, unsigned char *);
extern uintmax_t decode_varint(const unsigned char **);

#endif /* VARINT_H */
