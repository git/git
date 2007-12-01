#include "builtin.h"
#include "cache.h"
#include "transport.h"
#include "remote.h"

static const char ls_remote_usage[] =
"git-ls-remote [--upload-pack=<git-upload-pack>] [<host>:]<directory>";

int cmd_ls_remote(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *dest = NULL;
	int nongit = 0;
	unsigned flags = 0;
	const char *uploadpack = NULL;

	struct remote *remote;
	struct transport *transport;
	const struct ref *ref;

	setup_git_directory_gently(&nongit);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!prefixcmp(arg, "--upload-pack=")) {
				uploadpack = arg + 14;
				continue;
			}
			if (!prefixcmp(arg, "--exec=")) {
				uploadpack = arg + 7;
				continue;
			}
			if (!strcmp("--tags", arg)) {
				flags |= REF_TAGS;
				continue;
			}
			if (!strcmp("--heads", arg)) {
				flags |= REF_HEADS;
				continue;
			}
			if (!strcmp("--refs", arg)) {
				flags |= REF_NORMAL;
				continue;
			}
			usage(ls_remote_usage);
		}
		dest = arg;
		break;
	}

	if (!dest || i != argc - 1)
		usage(ls_remote_usage);

	remote = nongit ? NULL : remote_get(dest);
	if (remote && !remote->url_nr)
		die("remote %s has no configured URL", dest);
	transport = transport_get(remote, remote ? remote->url[0] : dest);
	if (uploadpack != NULL)
		transport_set_option(transport, TRANS_OPT_UPLOADPACK, uploadpack);

	ref = transport_get_remote_refs(transport);

	if (!ref)
		return 1;

	while (ref) {
		if (check_ref_type(ref, flags))
			printf("%s	%s\n", sha1_to_hex(ref->old_sha1), ref->name);
		ref = ref->next;
	}
	return 0;
}
