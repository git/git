#include "cache.h"
#include "refs.h"

static const char git_update_ref_usage[] =
"git-update-ref <refname> <value> [<oldval>] [-m <reason>]";

int main(int argc, char **argv)
{
	const char *refname=NULL, *value=NULL, *oldval=NULL, *msg=NULL;
	struct ref_lock *lock;
	unsigned char sha1[20], oldsha1[20];
	int i;

	setup_git_directory();
	git_config(git_default_config);

	for (i = 1; i < argc; i++) {
		if (!strcmp("-m", argv[i])) {
			if (i+1 >= argc)
				usage(git_update_ref_usage);
			msg = argv[++i];
			if (!*msg)
				die("Refusing to perform update with empty message.");
			if (strchr(msg, '\n'))
				die("Refusing to perform update with \\n in message.");
			continue;
		}
		if (!refname) {
			refname = argv[i];
			continue;
		}
		if (!value) {
			value = argv[i];
			continue;
		}
		if (!oldval) {
			oldval = argv[i];
			continue;
		}
	}
	if (!refname || !value)
		usage(git_update_ref_usage);

	if (get_sha1(value, sha1))
		die("%s: not a valid SHA1", value);
	memset(oldsha1, 0, 20);
	if (oldval && get_sha1(oldval, oldsha1))
		die("%s: not a valid old SHA1", oldval);

	lock = lock_any_ref_for_update(refname, oldval ? oldsha1 : NULL, 0);
	if (!lock)
		return 1;
	if (write_ref_sha1(lock, sha1, msg) < 0)
		return 1;
	return 0;
}
