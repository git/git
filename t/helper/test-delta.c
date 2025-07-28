/*
 * test-delta.c: test code to exercise diff-delta.c and patch-delta.c
 *
 * (C) 2005 Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "test-tool.h"
#include "git-compat-util.h"
#include "delta.h"
#include "strbuf.h"

static const char usage_str[] =
	"test-tool delta (-d|-p) <from_file> <data_file> <out_file>";

int cmd__delta(int argc, const char **argv)
{
	int fd;
	struct strbuf from = STRBUF_INIT, data = STRBUF_INIT;
	char *out_buf;
	unsigned long out_size;

	if (argc != 5 || (strcmp(argv[1], "-d") && strcmp(argv[1], "-p")))
		usage(usage_str);

	if (strbuf_read_file(&from, argv[2], 0) < 0)
		die_errno("unable to read '%s'", argv[2]);
	if (strbuf_read_file(&data, argv[3], 0) < 0)
		die_errno("unable to read '%s'", argv[3]);

	if (argv[1][1] == 'd')
		out_buf = diff_delta(from.buf, from.len,
				     data.buf, data.len,
				     &out_size, 0);
	else
		out_buf = patch_delta(from.buf, from.len,
				      data.buf, data.len,
				      &out_size);
	if (!out_buf)
		die("delta operation failed (returned NULL)");

	fd = xopen(argv[4], O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (write_in_full(fd, out_buf, out_size) < 0)
		die_errno("write(%s)", argv[4]);
	if (close(fd) < 0)
		die_errno("close(%s)", argv[4]);

	strbuf_release(&from);
	strbuf_release(&data);
	free(out_buf);

	return 0;
}
