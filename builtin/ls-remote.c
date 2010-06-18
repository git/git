#include "builtin.h"
#include "cache.h"
#include "transport.h"
#include "remote.h"

static const char ls_remote_usage[] =
"git ls-remote [--heads] [--tags]  [-u <exec> | --upload-pack <exec>]\n"
"                     [-q|--quiet] [<repository> [<refs>...]]";

/*
 * Is there one among the list of patterns that match the tail part
 * of the path?
 */
static int tail_match(const char **pattern, const char *path)
{
	const char *p;
	char pathbuf[PATH_MAX];

	if (!pattern)
		return 1; /* no restriction */

	if (snprintf(pathbuf, sizeof(pathbuf), "/%s", path) > sizeof(pathbuf))
		return error("insanely long ref %.*s...", 20, path);
	while ((p = *(pattern++)) != NULL) {
		if (!fnmatch(p, pathbuf, 0))
			return 1;
	}
	return 0;
}

int cmd_ls_remote(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *dest = NULL;
	int nongit;
	unsigned flags = 0;
	int quiet = 0;
	const char *uploadpack = NULL;
	const char **pattern = NULL;

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
			if (!strcmp("--tags", arg) || !strcmp("-t", arg)) {
				flags |= REF_TAGS;
				continue;
			}
			if (!strcmp("--heads", arg) || !strcmp("-h", arg)) {
				flags |= REF_HEADS;
				continue;
			}
			if (!strcmp("--refs", arg)) {
				flags |= REF_NORMAL;
				continue;
			}
			if (!strcmp("--quiet", arg) || !strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			usage(ls_remote_usage);
		}
		dest = arg;
		i++;
		break;
	}

	if (argv[i]) {
		int j;
		pattern = xcalloc(sizeof(const char *), argc - i + 1);
		for (j = i; j < argc; j++) {
			int len = strlen(argv[j]);
			char *p = xmalloc(len + 3);
			sprintf(p, "*/%s", argv[j]);
			pattern[j - i] = p;
		}
	}
	remote = remote_get(dest);
	if (!remote) {
		if (dest)
			die("bad repository '%s'", dest);
		die("No remote configured to list refs from.");
	}
	if (!remote->url_nr)
		die("remote %s has no configured URL", dest);
	transport = transport_get(remote, NULL);
	if (uploadpack != NULL)
		transport_set_option(transport, TRANS_OPT_UPLOADPACK, uploadpack);

	ref = transport_get_remote_refs(transport);
	if (transport_disconnect(transport))
		return 1;

	if (!dest && !quiet)
		fprintf(stderr, "From %s\n", *remote->url);
	for ( ; ref; ref = ref->next) {
		if (!check_ref_type(ref, flags))
			continue;
		if (!tail_match(pattern, ref->name))
			continue;
		printf("%s	%s\n", sha1_to_hex(ref->old_sha1), ref->name);
	}
	return 0;
}
