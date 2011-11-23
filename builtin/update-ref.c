#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "parse-options.h"

static const char * const git_update_ref_usage[] = {
	"git update-ref [options] -d <refname> [<oldval>]",
	"git update-ref [options]    <refname> <newval> [<oldval>]",
	NULL
};

int cmd_update_ref(int argc, const char **argv, const char *prefix)
{
	const char *refname, *oldval, *msg = NULL;
	unsigned char sha1[20], oldsha1[20];
	int delete = 0, no_deref = 0, flags = 0;
	struct option options[] = {
		OPT_STRING( 'm', NULL, &msg, "reason", "reason of the update"),
		OPT_BOOLEAN('d', NULL, &delete, "deletes the reference"),
		OPT_BOOLEAN( 0 , "no-deref", &no_deref,
					"update <refname> not the one it points to"),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, git_update_ref_usage,
			     0);
	if (msg && !*msg)
		die("Refusing to perform update with empty message.");

	if (delete) {
		if (argc < 1 || argc > 2)
			usage_with_options(git_update_ref_usage, options);
		refname = argv[0];
		oldval = argv[1];
	} else {
		const char *value;
		if (argc < 2 || argc > 3)
			usage_with_options(git_update_ref_usage, options);
		refname = argv[0];
		value = argv[1];
		oldval = argv[2];
		if (get_sha1(value, sha1))
			die("%s: not a valid SHA1", value);
	}

	hashclr(oldsha1); /* all-zero hash in case oldval is the empty string */
	if (oldval && *oldval && get_sha1(oldval, oldsha1))
		die("%s: not a valid old SHA1", oldval);

	if (no_deref)
		flags = REF_NODEREF;
	if (delete)
		return delete_ref(refname, oldval ? oldsha1 : NULL, flags);
	else
		return update_ref(msg, refname, sha1, oldval ? oldsha1 : NULL,
				  flags, DIE_ON_ERR);
}
