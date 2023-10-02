#include "test-tool.h"
#include "gpg-interface.h"
#include "strbuf.h"


int cmd__delete_gpgsig(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	const char *pattern = "gpgsig";
	const char *bufptr, *tail, *eol;
	int deleting = 0;
	size_t plen;

	if (argc >= 2) {
		pattern = argv[1];
		argv++;
		argc--;
	}

	plen = strlen(pattern);
	strbuf_read(&buf, 0, 0);

	if (!strcmp(pattern, "trailer")) {
		size_t payload_size = parse_signed_buffer(buf.buf, buf.len);
		fwrite(buf.buf, 1, payload_size, stdout);
		fflush(stdout);
		return 0;
	}

	bufptr = buf.buf;
	tail = bufptr + buf.len;

	while (bufptr < tail) {
		/* Find the end of the line */
		eol = memchr(bufptr, '\n', tail - bufptr);
		if (!eol)
			eol = tail;

		/* Drop continuation lines */
		if (deleting && (bufptr < eol) && (bufptr[0] == ' ')) {
			bufptr = eol + 1;
			continue;
		}
		deleting = 0;

		/* Does the line match the prefix? */
		if (((bufptr + plen) < eol) &&
		    !memcmp(bufptr, pattern, plen) &&
		    (bufptr[plen] == ' ')) {
			deleting = 1;
			bufptr = eol + 1;
			continue;
		}

		/* Print all other lines */
		fwrite(bufptr, 1, (eol - bufptr) + 1, stdout);
		bufptr = eol + 1;
	}
	fflush(stdout);

	return 0;
}
