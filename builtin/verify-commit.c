/*
 * Builtin "git commit-commit"
 *
 * Copyright (c) 2014 Michael J Gruber <git@drmicha.warpmail.net>
 *
 * Based on git-verify-tag
 */
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "run-command.h"
#include <signal.h>
#include "parse-options.h"
#include "gpg-interface.h"

static const char * const verify_commit_usage[] = {
		N_("git verify-commit [-v | --verbose] <commit>..."),
		NULL
};

static int run_gpg_verify(const struct object_id *oid, const char *buf, unsigned long size, unsigned flags)
{
	struct signature_check signature_check;
	int ret;

	memset(&signature_check, 0, sizeof(signature_check));

	ret = check_commit_signature(lookup_commit(the_repository, oid),
				     &signature_check);
	print_signature_buffer(&signature_check, flags);

	signature_check_clear(&signature_check);
	return ret;
}

static int verify_commit(const char *name, unsigned flags)
{
	enum object_type type;
	struct object_id oid;
	char *buf;
	unsigned long size;
	int ret;

	if (get_oid(name, &oid))
		return error("commit '%s' not found.", name);

	buf = read_object_file(&oid, &type, &size);
	if (!buf)
		return error("%s: unable to read file.", name);
	if (type != OBJ_COMMIT)
		return error("%s: cannot verify a non-commit object of type %s.",
				name, type_name(type));

	ret = run_gpg_verify(&oid, buf, size, flags);

	free(buf);
	return ret;
}

static int git_verify_commit_config(const char *var, const char *value, void *cb)
{
	int status = git_gpg_config(var, value, cb);
	if (status)
		return status;
	return git_default_config(var, value, cb);
}

int cmd_verify_commit(int argc, const char **argv, const char *prefix)
{
	int i = 1, verbose = 0, had_error = 0;
	unsigned flags = 0;
	const struct option verify_commit_options[] = {
		OPT__VERBOSE(&verbose, N_("print commit contents")),
		OPT_BIT(0, "raw", &flags, N_("print raw gpg status output"), GPG_VERIFY_RAW),
		OPT_END()
	};

	git_config(git_verify_commit_config, NULL);

	argc = parse_options(argc, argv, prefix, verify_commit_options,
			     verify_commit_usage, PARSE_OPT_KEEP_ARGV0);
	if (argc <= i)
		usage_with_options(verify_commit_usage, verify_commit_options);

	if (verbose)
		flags |= GPG_VERIFY_VERBOSE;

	/* sometimes the program was terminated because this signal
	 * was received in the process of writing the gpg input: */
	signal(SIGPIPE, SIG_IGN);
	while (i < argc)
		if (verify_commit(argv[i++], flags))
			had_error = 1;
	return had_error;
}
