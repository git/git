#ifndef BASE85_H
#define BASE85_H

/*
 * We pick 52, a multiple of 4 so there are no partial groups and it
 * fits in the A-Za-z length prefix encoding.
 */
#define BASE85_CHUNK_SIZE 52

typedef void each_encoded_line_fn(char *line, int len, void *cb_data);

int decode_85(char *dst, const char *line, int linelen);
void chunk_and_encode85(const unsigned char *data, unsigned long len,
			each_encoded_line_fn output, void *output_data);

#endif /* BASE85_H */
