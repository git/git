#include "builtin.h"
#include "cache.h"
#include "transport.h"
#include "remote.h"

static const char ls_remote_usage[] =
"git ls-remote [--heads] [--tags]  [--upload-pack=<exec>]\n"
"                     [-q | --quiet] [--exit-code] [--get-url] [<repository> [<refs>...]]";

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
		if (!wildmatch(p, pathbuf, 0, NULL))
			return 1;
	}
	return 0;
}

int cmd_ls_remote(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *dest = NULL;
	unsigned flags = 0;
	int get_url = 0;
	int quiet = 0;
	int status = 0;
	const char *uploadpack = NULL;
	const char **pattern = NULL;

	struct remote *remote;
	struct transport *transport;
	const struct ref *ref;

	if (argc == 2 && !strcmp("-h", argv[1]))
		usage(ls_remote_usage);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (starts_with(arg, "--upload-pack=")) {
				uploadpack = arg + 14;
				continue;
			}
			if (starts_with(arg, "--exec=")) {
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
			if (!strcmp("--get-url", arg)) {
				get_url = 1;
				continue;
			}
			if (!strcmp("--exit-code", arg)) {
				/* return this code if no refs are reported */
				status = 2;
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
		pattern = xcalloc(argc - i + 1, sizeof(const char *));
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

	if (get_url) {
		printf("%s\n", *remote->url);
		return 0;
	}

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
		status = 0; /* we found something */
	}
	return status;
}
