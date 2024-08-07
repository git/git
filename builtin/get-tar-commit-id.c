/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include "builtin.h"
#include "commit.h"
#include "tar.h"

static const char builtin_get_tar_commit_id_usage[] =
"git get-tar-commit-id";

/* ustar header + extended global header content */
#define RECORDSIZE	(512)
#define HEADERSIZE (2 * RECORDSIZE)

int cmd_get_tar_commit_id(int argc, const char **argv UNUSED, const char *prefix)
{
	char buffer[HEADERSIZE];
	struct ustar_header *header = (struct ustar_header *)buffer;
	char *content = buffer + RECORDSIZE;
	const char *comment;
	ssize_t n;
	long len;
	char *end;

	BUG_ON_NON_EMPTY_PREFIX(prefix);

	if (argc != 1)
		usage(builtin_get_tar_commit_id_usage);

	n = read_in_full(0, buffer, HEADERSIZE);
	if (n < 0)
		die_errno("git get-tar-commit-id: read error");
	if (n != HEADERSIZE)
		die_errno("git get-tar-commit-id: EOF before reading tar header");
	if (header->typeflag[0] != TYPEFLAG_GLOBAL_HEADER)
		return 1;

	len = strtol(content, &end, 10);
	if (errno == ERANGE || end == content || len < 0)
		return 1;
	if (!skip_prefix(end, " comment=", &comment))
		return 1;
	len -= comment - content;
	if (len < 1 || !(len % 2) ||
	    hash_algo_by_length((len - 1) / 2) == GIT_HASH_UNKNOWN)
		return 1;

	if (write_in_full(1, comment, len) < 0)
		die_errno("git get-tar-commit-id: write error");

	return 0;
}
