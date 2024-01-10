#ifndef BASE85_H
#define BASE85_H

int decode_85(char *dst, const char *line, int linelen);
void encode_85(char *buf, const unsigned char *data, int bytes);

#endif /* BASE85_H */
