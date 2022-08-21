#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "run-command.h"
#include "parse-options.h"

#define VERIFY_PACK_VERBOSE 01
#define VERIFY_PACK_STAT_ONLY 02

static int verify_one_pack(const char *path, unsigned int flags, const char *hash_algo)
{
	struct child_process index_pack = CHILD_PROCESS_INIT;
	struct strvec *argv = &index_pack.args;
	struct strbuf arg = STRBUF_INIT;
	int verbose = flags & VERIFY_PACK_VERBOSE;
	int stat_only = flags & VERIFY_PACK_STAT_ONLY;
	int err;

	strvec_push(argv, "index-pack");

	if (stat_only)
		strvec_push(argv, "--verify-stat-only");
	else if (verbose)
		strvec_push(argv, "--verify-stat");
	else
		strvec_push(argv, "--verify");

	if (hash_algo)
		strvec_pushf(argv, "--object-format=%s", hash_algo);

	/*
	 * In addition to "foo.pack" we accept "foo.idx" and "foo";
	 * normalize these forms to "foo.pack" for "index-pack --verify".
	 */
	strbuf_addstr(&arg, path);
	if (strbuf_strip_suffix(&arg, ".idx") ||
	    !ends_with(arg.buf, ".pack"))
		strbuf_addstr(&arg, ".pack");
	strvec_push(argv, arg.buf);

	index_pack.git_cmd = 1;

	err = run_command(&index_pack);

	if (verbose || stat_only) {
		if (err)
			printf("%s: bad\n", arg.buf);
		else {
			if (!stat_only)
				printf("%s: ok\n", arg.buf);
		}
	}
	strbuf_release(&arg);

	return err;
}

static const char * const verify_pack_usage[] = {
	N_("git verify-pack [-v | --verbose] [-s | --stat-only] <pack>..."),
	NULL
};

int cmd_verify_pack(int argc, const char **argv, const char *prefix)
{
	int err = 0;
	unsigned int flags = 0;
	const char *object_format = NULL;
	int i;
	const struct option verify_pack_options[] = {
		OPT_BIT('v', "verbose", &flags, N_("verbose"),
			VERIFY_PACK_VERBOSE),
		OPT_BIT('s', "stat-only", &flags, N_("show statistics only"),
			VERIFY_PACK_STAT_ONLY),
		OPT_STRING(0, "object-format", &object_format, N_("hash"),
			   N_("specify the hash algorithm to use")),
		OPT_END()
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, verify_pack_options,
			     verify_pack_usage, 0);
	if (argc < 1)
		usage_with_options(verify_pack_usage, verify_pack_options);
	for (i = 0; i < argc; i++) {
		if (verify_one_pack(argv[i], flags, object_format))
			err = 1;
	}

	return err;
}
