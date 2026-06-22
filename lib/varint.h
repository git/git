#ifndef VARINT_H
#define VARINT_H

uint8_t encode_varint(uint64_t, unsigned char *);
uint64_t decode_varint(const unsigned char **);

#endif /* VARINT_H */
