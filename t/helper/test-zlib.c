#include "test-tool.h"
#include "git-zlib.h"
#include "strbuf.h"

static const char *zlib_usage = "test-tool zlib [inflate|deflate]";

static void do_zlib(struct git_zstream *stream,
		    int (*zlib_func)(git_zstream *, int),
		    int fd_in, int fd_out)
{
	struct strbuf buf_in = STRBUF_INIT;
	int status = Z_OK;

	if (strbuf_read(&buf_in, fd_in, 0) < 0)
		die_errno("read error");

	stream->next_in = (unsigned char *)buf_in.buf;
	stream->avail_in = buf_in.len;

	while (status == Z_OK ||
	       (status == Z_BUF_ERROR && !stream->avail_out)) {
		unsigned char buf_out[4096];

		stream->next_out = buf_out;
		stream->avail_out = sizeof(buf_out);

		status = zlib_func(stream, Z_FINISH);
		if (write_in_full(fd_out, buf_out,
				  sizeof(buf_out) - stream->avail_out) < 0)
			die_errno("write error");
	}

	if (status != Z_STREAM_END)
		die("zlib error %d", status);

	strbuf_release(&buf_in);
}

int cmd__zlib(int argc, const char **argv)
{
	git_zstream stream;

	if (argc != 2)
		usage(zlib_usage);

	memset(&stream, 0, sizeof(stream));

	if (!strcmp(argv[1], "inflate")) {
		git_inflate_init(&stream);
		do_zlib(&stream, git_inflate, 0, 1);
		git_inflate_end(&stream);
	} else if (!strcmp(argv[1], "deflate")) {
		git_deflate_init(&stream, Z_DEFAULT_COMPRESSION);
		do_zlib(&stream, git_deflate, 0, 1);
		git_deflate_end(&stream);
	} else {
		error("unknown mode: %s", argv[1]);
		usage(zlib_usage);
	}

	return 0;
}
