/*
 * Builtin "but cummit-cummit"
 *
 * Copyright (c) 2014 Michael J Gruber <but@drmicha.warpmail.net>
 *
 * Based on but-verify-tag
 */
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "object-store.h"
#include "repository.h"
#include "cummit.h"
#include "run-command.h"
#include "parse-options.h"
#include "gpg-interface.h"

static const char * const verify_cummit_usage[] = {
		N_("but verify-cummit [-v | --verbose] <cummit>..."),
		NULL
};

static int run_gpg_verify(struct cummit *cummit, unsigned flags)
{
	struct signature_check signature_check;
	int ret;

	memset(&signature_check, 0, sizeof(signature_check));

	ret = check_cummit_signature(cummit, &signature_check);
	print_signature_buffer(&signature_check, flags);

	signature_check_clear(&signature_check);
	return ret;
}

static int verify_cummit(const char *name, unsigned flags)
{
	struct object_id oid;
	struct object *obj;

	if (get_oid(name, &oid))
		return error("cummit '%s' not found.", name);

	obj = parse_object(the_repository, &oid);
	if (!obj)
		return error("%s: unable to read file.", name);
	if (obj->type != OBJ_CUMMIT)
		return error("%s: cannot verify a non-cummit object of type %s.",
				name, type_name(obj->type));

	return run_gpg_verify((struct cummit *)obj, flags);
}

static int but_verify_cummit_config(const char *var, const char *value, void *cb)
{
	int status = but_gpg_config(var, value, cb);
	if (status)
		return status;
	return but_default_config(var, value, cb);
}

int cmd_verify_cummit(int argc, const char **argv, const char *prefix)
{
	int i = 1, verbose = 0, had_error = 0;
	unsigned flags = 0;
	const struct option verify_cummit_options[] = {
		OPT__VERBOSE(&verbose, N_("print cummit contents")),
		OPT_BIT(0, "raw", &flags, N_("print raw gpg status output"), GPG_VERIFY_RAW),
		OPT_END()
	};

	but_config(but_verify_cummit_config, NULL);

	argc = parse_options(argc, argv, prefix, verify_cummit_options,
			     verify_cummit_usage, PARSE_OPT_KEEP_ARGV0);
	if (argc <= i)
		usage_with_options(verify_cummit_usage, verify_cummit_options);

	if (verbose)
		flags |= GPG_VERIFY_VERBOSE;

	/* sometimes the program was terminated because this signal
	 * was received in the process of writing the gpg input: */
	signal(SIGPIPE, SIG_IGN);
	while (i < argc)
		if (verify_cummit(argv[i++], flags))
			had_error = 1;
	return had_error;
}
