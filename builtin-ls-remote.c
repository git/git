#include "builtin.h"
#include "cache.h"
#include "transport.h"
#include "remote.h"

static const char ls_remote_usage[] =
"git-ls-remote [--upload-pack=<git-upload-pack>] [<host>:]<directory>";

/*
 * pattern is a list of tail-part of accepted refnames.  Is there one
 * among them that is a suffix of the path?  Directory boundary must
 * be honored when checking this match.  IOW, patterns "master" and
 * "sa/master" both match path "refs/hold/sa/master".  On the other
 * hand, path "refs/hold/foosa/master" is matched by "master" but not
 * by "sa/master".
 */

static int tail_match(const char **pattern, const char *path)
{
	int pathlen;
	const char *p;

	if (!*pattern)
		return 1; /* no restriction */

	for (pathlen = strlen(path); (p = *pattern); pattern++) {
		int pfxlen = pathlen - strlen(p);
		if (pfxlen < 0)
			continue; /* pattern is longer, will never match */
		if (strcmp(path + pfxlen, p))
			continue; /* no tail match */
		if (!pfxlen || path[pfxlen - 1] == '/')
			return 1; /* fully match at directory boundary */
	}
	return 0;
}

int cmd_ls_remote(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *dest = NULL;
	int nongit = 0;
	unsigned flags = 0;
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

	if (!dest)
		usage(ls_remote_usage);
	pattern = argv + i + 1;
	remote = nongit ? NULL : remote_get(dest);
	if (remote && !remote->url_nr)
		die("remote %s has no configured URL", dest);
	transport = transport_get(remote, remote ? remote->url[0] : dest);
	if (uploadpack != NULL)
		transport_set_option(transport, TRANS_OPT_UPLOADPACK, uploadpack);

	ref = transport_get_remote_refs(transport);

	if (!ref)
		return 1;

	for ( ; ref; ref = ref->next) {
		if (!check_ref_type(ref, flags))
			continue;
		if (!tail_match(pattern, ref->name))
			continue;
		printf("%s	%s\n", sha1_to_hex(ref->old_sha1), ref->name);
	}
	return 0;
}
