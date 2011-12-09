/*
 * Builtin "git verify-tag"
 *
 * Copyright (c) 2007 Carlos Rica <jasampler@gmail.com>
 *
 * Based on git-verify-tag.sh
 */
#include "cache.h"
#include "builtin.h"
#include "tag.h"
#include "run-command.h"
#include <signal.h>
#include "parse-options.h"
#include "gpg-interface.h"

static const char * const verify_tag_usage[] = {
		"git verify-tag [-v|--verbose] <tag>...",
		NULL
};

static int run_gpg_verify(const char *buf, unsigned long size, int verbose)
{
	int len;

	len = parse_signature(buf, size);
	if (verbose)
		write_in_full(1, buf, len);

	if (size == len)
		return error("no signature found");

	return verify_signed_buffer(buf, len, buf + len, size - len, NULL);
}

static int verify_tag(const char *name, int verbose)
{
	enum object_type type;
	unsigned char sha1[20];
	char *buf;
	unsigned long size;
	int ret;

	if (get_sha1(name, sha1))
		return error("tag '%s' not found.", name);

	type = sha1_object_info(sha1, NULL);
	if (type != OBJ_TAG)
		return error("%s: cannot verify a non-tag object of type %s.",
				name, typename(type));

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		return error("%s: unable to read file.", name);

	ret = run_gpg_verify(buf, size, verbose);

	free(buf);
	return ret;
}

int cmd_verify_tag(int argc, const char **argv, const char *prefix)
{
	int i = 1, verbose = 0, had_error = 0;
	const struct option verify_tag_options[] = {
		OPT__VERBOSE(&verbose, "print tag contents"),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, verify_tag_options,
			     verify_tag_usage, PARSE_OPT_KEEP_ARGV0);
	if (argc <= i)
		usage_with_options(verify_tag_usage, verify_tag_options);

	/* sometimes the program was terminated because this signal
	 * was received in the process of writing the gpg input: */
	signal(SIGPIPE, SIG_IGN);
	while (i < argc)
		if (verify_tag(argv[i++], verbose))
			had_error = 1;
	return had_error;
}
