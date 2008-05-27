#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "parse-options.h"

static const char * const git_update_ref_usage[] = {
	"git-update-ref [options] -d <refname> <oldval>",
	"git-update-ref [options]    <refname> <newval> [<oldval>]",
	NULL
};

int cmd_update_ref(int argc, const char **argv, const char *prefix)
{
	const char *refname, *value, *oldval, *msg=NULL;
	unsigned char sha1[20], oldsha1[20];
	int delete = 0, no_deref = 0;
	struct option options[] = {
		OPT_STRING( 'm', NULL, &msg, "reason", "reason of the update"),
		OPT_BOOLEAN('d', NULL, &delete, "deletes the reference"),
		OPT_BOOLEAN( 0 , "no-deref", &no_deref,
					"update <refname> not the one it points to"),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, options, git_update_ref_usage, 0);
	if (msg && !*msg)
		die("Refusing to perform update with empty message.");

	if (argc < 2 || argc > 3)
		usage_with_options(git_update_ref_usage, options);
	refname = argv[0];
	value   = argv[1];
	oldval  = argv[2];

	if (get_sha1(value, sha1))
		die("%s: not a valid SHA1", value);

	if (delete) {
		if (oldval)
			usage_with_options(git_update_ref_usage, options);
		return delete_ref(refname, sha1);
	}

	hashclr(oldsha1);
	if (oldval && *oldval && get_sha1(oldval, oldsha1))
		die("%s: not a valid old SHA1", oldval);

	return update_ref(msg, refname, sha1, oldval ? oldsha1 : NULL,
			  no_deref ? REF_NODEREF : 0, DIE_ON_ERR);
}
