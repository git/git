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

static const char * const verify_tag_usage[] = {
		"git verify-tag [-v|--verbose] <tag>...",
		NULL
};

#define PGP_SIGNATURE "-----BEGIN PGP SIGNATURE-----"

static int run_gpg_verify(const char *buf, unsigned long size, int verbose)
{
	struct child_process gpg;
	const char *args_gpg[] = {"gpg", "--verify", "FILE", "-", NULL};
	char path[PATH_MAX], *eol;
	size_t len;
	int fd, ret;

	fd = git_mkstemp(path, PATH_MAX, ".git_vtag_tmpXXXXXX");
	if (fd < 0)
		return error("could not create temporary file '%s': %s",
						path, strerror(errno));
	if (write_in_full(fd, buf, size) < 0)
		return error("failed writing temporary file '%s': %s",
						path, strerror(errno));
	close(fd);

	/* find the length without signature */
	len = 0;
	while (len < size && prefixcmp(buf + len, PGP_SIGNATURE)) {
		eol = memchr(buf + len, '\n', size - len);
		len += eol ? eol - (buf + len) + 1 : size - len;
	}
	if (verbose)
		write_in_full(1, buf, len);

	memset(&gpg, 0, sizeof(gpg));
	gpg.argv = args_gpg;
	gpg.in = -1;
	args_gpg[2] = path;
	if (start_command(&gpg)) {
		unlink(path);
		return error("could not run gpg.");
	}

	write_in_full(gpg.in, buf, len);
	close(gpg.in);
	ret = finish_command(&gpg);

	unlink_or_warn(path);

	return ret;
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
		OPT__VERBOSE(&verbose),
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
