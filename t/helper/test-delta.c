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

static const char usage_str[] =
	"test-tool delta (-d|-p) <from_file> <data_file> <out_file>";

int cmd__delta(int argc, const char **argv)
{
	int fd;
	struct stat st;
	void *from_buf = NULL, *data_buf = NULL, *out_buf = NULL;
	unsigned long from_size, data_size, out_size;

	if (argc != 5 || (strcmp(argv[1], "-d") && strcmp(argv[1], "-p")))
		usage(usage_str);

	fd = xopen(argv[2], O_RDONLY);
	if (fstat(fd, &st) < 0)
		die_errno("fstat(%s)", argv[2]);
	from_size = st.st_size;
	from_buf = xmalloc(from_size);
	if (read_in_full(fd, from_buf, from_size) < 0)
		die_errno("read(%s)", argv[2]);
	close(fd);

	fd = xopen(argv[3], O_RDONLY);
	if (fstat(fd, &st) < 0)
		die_errno("fstat(%s)", argv[3]);
	data_size = st.st_size;
	data_buf = xmalloc(data_size);
	if (read_in_full(fd, data_buf, data_size) < 0)
		die_errno("read(%s)", argv[3]);
	close(fd);

	if (argv[1][1] == 'd')
		out_buf = diff_delta(from_buf, from_size,
				     data_buf, data_size,
				     &out_size, 0);
	else
		out_buf = patch_delta(from_buf, from_size,
				      data_buf, data_size,
				      &out_size);
	if (!out_buf)
		die("delta operation failed (returned NULL)");

	fd = xopen(argv[4], O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (write_in_full(fd, out_buf, out_size) < 0)
		die_errno("write(%s)", argv[4]);

	free(from_buf);
	free(data_buf);
	free(out_buf);

	return 0;
}
