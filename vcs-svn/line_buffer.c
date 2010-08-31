/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "line_buffer.h"
#include "obj_pool.h"

#define LINE_BUFFER_LEN 10000
#define COPY_BUFFER_LEN 4096

/* Create memory pool for char sequence of known length */
obj_pool_gen(blob, char, 4096)

static char line_buffer[LINE_BUFFER_LEN];
static char byte_buffer[COPY_BUFFER_LEN];
static FILE *infile;

int buffer_init(const char *filename)
{
	infile = filename ? fopen(filename, "r") : stdin;
	if (!infile)
		return -1;
	return 0;
}

int buffer_deinit(void)
{
	int err;
	if (infile == stdin)
		return ferror(infile);
	err = ferror(infile);
	err |= fclose(infile);
	return err;
}

/* Read a line without trailing newline. */
char *buffer_read_line(void)
{
	char *end;
	if (!fgets(line_buffer, sizeof(line_buffer), infile))
		/* Error or data exhausted. */
		return NULL;
	end = line_buffer + strlen(line_buffer);
	if (end[-1] == '\n')
		end[-1] = '\0';
	else if (feof(infile))
		; /* No newline at end of file.  That's fine. */
	else
		/*
		 * Line was too long.
		 * There is probably a saner way to deal with this,
		 * but for now let's return an error.
		 */
		return NULL;
	return line_buffer;
}

char *buffer_read_string(uint32_t len)
{
	char *s;
	blob_free(blob_pool.size);
	s = blob_pointer(blob_alloc(len + 1));
	s[fread(s, 1, len, infile)] = '\0';
	return ferror(infile) ? NULL : s;
}

void buffer_copy_bytes(uint32_t len)
{
	uint32_t in;
	while (len > 0 && !feof(infile) && !ferror(infile)) {
		in = len < COPY_BUFFER_LEN ? len : COPY_BUFFER_LEN;
		in = fread(byte_buffer, 1, in, infile);
		len -= in;
		fwrite(byte_buffer, 1, in, stdout);
		if (ferror(stdout)) {
			buffer_skip_bytes(len);
			return;
		}
	}
}

void buffer_skip_bytes(uint32_t len)
{
	uint32_t in;
	while (len > 0 && !feof(infile) && !ferror(infile)) {
		in = len < COPY_BUFFER_LEN ? len : COPY_BUFFER_LEN;
		in = fread(byte_buffer, 1, in, infile);
		len -= in;
	}
}

void buffer_reset(void)
{
	blob_reset();
}
