/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include "cache.h"
#include "commit.h"
#include "tar.h"
#include "builtin.h"
#include "quote.h"

static const char builtin_get_tar_commit_id_usage[] =
"git get-tar-commit-id < <tarfile>";

/* ustar header + extended global header content */
#define RECORDSIZE	(512)
#define HEADERSIZE (2 * RECORDSIZE)

int cmd_get_tar_commit_id(int argc, const char **argv, const char *prefix)
{
	char buffer[HEADERSIZE];
	struct ustar_header *header = (struct ustar_header *)buffer;
	char *content = buffer + RECORDSIZE;
	ssize_t n;

	if (argc != 1)
		usage(builtin_get_tar_commit_id_usage);

	n = read_in_full(0, buffer, HEADERSIZE);
	if (n < HEADERSIZE)
		die("git get-tar-commit-id: read error");
	if (header->typeflag[0] != 'g')
		return 1;
	if (memcmp(content, "52 comment=", 11))
		return 1;

	n = write_in_full(1, content + 11, 41);
	if (n < 41)
		die_errno("git get-tar-commit-id: write error");

	return 0;
}
