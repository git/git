#ifndef BASE85_H
#define BASE85_H

typedef void each_encoded_line_fn(char *line, int len, void *cb_data);

int decode_85(char *dst, const char *line, int linelen);
void chunk_and_encode85(const unsigned char *data, unsigned long len,
			each_encoded_line_fn output, void *output_data);

#endif /* BASE85_H */
