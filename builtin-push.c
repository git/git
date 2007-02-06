/*
 * "git push"
 */
#include "cache.h"
#include "refs.h"
#include "run-command.h"
#include "builtin.h"

#define MAX_URI (16)

static const char push_usage[] = "git-push [--all] [--tags] [--receive-pack=<git-receive-pack>] [--repo=all] [-f | --force] [-v] [<repository> <refspec>...]";

static int all, tags, force, thin = 1, verbose;
static const char *receivepack;

#define BUF_SIZE (2084)
static char buffer[BUF_SIZE];

static const char **refspec;
static int refspec_nr;

static void add_refspec(const char *ref)
{
	int nr = refspec_nr + 1;
	refspec = xrealloc(refspec, nr * sizeof(char *));
	refspec[nr-1] = ref;
	refspec_nr = nr;
}

static int expand_one_ref(const char *ref, const unsigned char *sha1, int flag, void *cb_data)
{
	/* Ignore the "refs/" at the beginning of the refname */
	ref += 5;

	if (!strncmp(ref, "tags/", 5))
		add_refspec(xstrdup(ref));
	return 0;
}

static void expand_refspecs(void)
{
	if (all) {
		if (refspec_nr)
			die("cannot mix '--all' and a refspec");

		/*
		 * No need to expand "--all" - we'll just use
		 * the "--all" flag to send-pack
		 */
		return;
	}
	if (!tags)
		return;
	for_each_ref(expand_one_ref, NULL);
}

struct wildcard_cb {
	const char *from_prefix;
	int from_prefix_len;
	const char *to_prefix;
	int to_prefix_len;
	int force;
};

static int expand_wildcard_ref(const char *ref, const unsigned char *sha1, int flag, void *cb_data)
{
	struct wildcard_cb *cb = cb_data;
	int len = strlen(ref);
	char *expanded, *newref;

	if (len < cb->from_prefix_len ||
	    memcmp(cb->from_prefix, ref, cb->from_prefix_len))
		return 0;
	expanded = xmalloc(len * 2 + cb->force +
			   (cb->to_prefix_len - cb->from_prefix_len) + 2);
	newref = expanded + cb->force;
	if (cb->force)
		expanded[0] = '+';
	memcpy(newref, ref, len);
	newref[len] = ':';
	memcpy(newref + len + 1, cb->to_prefix, cb->to_prefix_len);
	strcpy(newref + len + 1 + cb->to_prefix_len,
	       ref + cb->from_prefix_len);
	add_refspec(expanded);
	return 0;
}

static int wildcard_ref(const char *ref)
{
	int len;
	const char *colon;
	struct wildcard_cb cb;

	memset(&cb, 0, sizeof(cb));
	if (ref[0] == '+') {
		cb.force = 1;
		ref++;
	}
	len = strlen(ref);
	colon = strchr(ref, ':');
	if (! (colon && ref < colon &&
	       colon[-2] == '/' && colon[-1] == '*' &&
	       /* "<mine>/<asterisk>:<yours>/<asterisk>" is at least 7 bytes */
	       7 <= len &&
	       ref[len-2] == '/' && ref[len-1] == '*') )
		return 0 ;
	cb.from_prefix = ref;
	cb.from_prefix_len = colon - ref - 1;
	cb.to_prefix = colon + 1;
	cb.to_prefix_len = len - (colon - ref) - 2;
	for_each_ref(expand_wildcard_ref, &cb);
	return 1;
}

static void set_refspecs(const char **refs, int nr)
{
	if (nr) {
		int i;
		for (i = 0; i < nr; i++) {
			const char *ref = refs[i];
			if (!strcmp("tag", ref)) {
				char *tag;
				int len;
				if (nr <= ++i)
					die("tag shorthand without <tag>");
				len = strlen(refs[i]) + 11;
				tag = xmalloc(len);
				strcpy(tag, "refs/tags/");
				strcat(tag, refs[i]);
				ref = tag;
			}
			else if (wildcard_ref(ref))
				continue;
			add_refspec(ref);
		}
	}
	expand_refspecs();
}

static int get_remotes_uri(const char *repo, const char *uri[MAX_URI])
{
	int n = 0;
	FILE *f = fopen(git_path("remotes/%s", repo), "r");
	int has_explicit_refspec = refspec_nr || all || tags;

	if (!f)
		return -1;
	while (fgets(buffer, BUF_SIZE, f)) {
		int is_refspec;
		char *s, *p;

		if (!strncmp("URL:", buffer, 4)) {
			is_refspec = 0;
			s = buffer + 4;
		} else if (!strncmp("Push:", buffer, 5)) {
			is_refspec = 1;
			s = buffer + 5;
		} else
			continue;

		/* Remove whitespace at the head.. */
		while (isspace(*s))
			s++;
		if (!*s)
			continue;

		/* ..and at the end */
		p = s + strlen(s);
		while (isspace(p[-1]))
			*--p = 0;

		if (!is_refspec) {
			if (n < MAX_URI)
				uri[n++] = xstrdup(s);
			else
				error("more than %d URL's specified, ignoring the rest", MAX_URI);
		}
		else if (is_refspec && !has_explicit_refspec) {
			if (!wildcard_ref(s))
				add_refspec(xstrdup(s));
		}
	}
	fclose(f);
	if (!n)
		die("remote '%s' has no URL", repo);
	return n;
}

static const char **config_uri;
static const char *config_repo;
static int config_repo_len;
static int config_current_uri;
static int config_get_refspecs;
static int config_get_receivepack;

static int get_remote_config(const char* key, const char* value)
{
	if (!strncmp(key, "remote.", 7) &&
	    !strncmp(key + 7, config_repo, config_repo_len)) {
		if (!strcmp(key + 7 + config_repo_len, ".url")) {
			if (config_current_uri < MAX_URI)
				config_uri[config_current_uri++] = xstrdup(value);
			else
				error("more than %d URL's specified, ignoring the rest", MAX_URI);
		}
		else if (config_get_refspecs &&
			 !strcmp(key + 7 + config_repo_len, ".push")) {
			if (!wildcard_ref(value))
				add_refspec(xstrdup(value));
		}
		else if (config_get_receivepack &&
			 !strcmp(key + 7 + config_repo_len, ".receivepack")) {
			if (!receivepack) {
				char *rp = xmalloc(strlen(value) + 16);
				sprintf(rp, "--receive-pack=%s", value);
				receivepack = rp;
			} else
				error("more than one receivepack given, using the first");
		}
	}
	return 0;
}

static int get_config_remotes_uri(const char *repo, const char *uri[MAX_URI])
{
	config_repo_len = strlen(repo);
	config_repo = repo;
	config_current_uri = 0;
	config_uri = uri;
	config_get_refspecs = !(refspec_nr || all || tags);
	config_get_receivepack = (receivepack == NULL);

	git_config(get_remote_config);
	return config_current_uri;
}

static int get_branches_uri(const char *repo, const char *uri[MAX_URI])
{
	const char *slash = strchr(repo, '/');
	int n = slash ? slash - repo : 1000;
	FILE *f = fopen(git_path("branches/%.*s", n, repo), "r");
	char *s, *p;
	int len;

	if (!f)
		return 0;
	s = fgets(buffer, BUF_SIZE, f);
	fclose(f);
	if (!s)
		return 0;
	while (isspace(*s))
		s++;
	if (!*s)
		return 0;
	p = s + strlen(s);
	while (isspace(p[-1]))
		*--p = 0;
	len = p - s;
	if (slash)
		len += strlen(slash);
	p = xmalloc(len + 1);
	strcpy(p, s);
	if (slash)
		strcat(p, slash);
	uri[0] = p;
	return 1;
}

/*
 * Read remotes and branches file, fill the push target URI
 * list.  If there is no command line refspecs, read Push: lines
 * to set up the *refspec list as well.
 * return the number of push target URIs
 */
static int read_config(const char *repo, const char *uri[MAX_URI])
{
	int n;

	if (*repo != '/') {
		n = get_remotes_uri(repo, uri);
		if (n > 0)
			return n;

		n = get_config_remotes_uri(repo, uri);
		if (n > 0)
			return n;

		n = get_branches_uri(repo, uri);
		if (n > 0)
			return n;
	}

	uri[0] = repo;
	return 1;
}

static int do_push(const char *repo)
{
	const char *uri[MAX_URI];
	int i, n;
	int common_argc;
	const char **argv;
	int argc;

	n = read_config(repo, uri);
	if (n <= 0)
		die("bad repository '%s'", repo);

	argv = xmalloc((refspec_nr + 10) * sizeof(char *));
	argv[0] = "dummy-send-pack";
	argc = 1;
	if (all)
		argv[argc++] = "--all";
	if (force)
		argv[argc++] = "--force";
	if (receivepack)
		argv[argc++] = receivepack;
	common_argc = argc;

	for (i = 0; i < n; i++) {
		int err;
		int dest_argc = common_argc;
		int dest_refspec_nr = refspec_nr;
		const char **dest_refspec = refspec;
		const char *dest = uri[i];
		const char *sender = "git-send-pack";
		if (!strncmp(dest, "http://", 7) ||
		    !strncmp(dest, "https://", 8))
			sender = "git-http-push";
		else if (thin)
			argv[dest_argc++] = "--thin";
		argv[0] = sender;
		argv[dest_argc++] = dest;
		while (dest_refspec_nr--)
			argv[dest_argc++] = *dest_refspec++;
		argv[dest_argc] = NULL;
		if (verbose)
			fprintf(stderr, "Pushing to %s\n", dest);
		err = run_command_v(argv);
		if (!err)
			continue;
		switch (err) {
		case -ERR_RUN_COMMAND_FORK:
			die("unable to fork for %s", sender);
		case -ERR_RUN_COMMAND_EXEC:
			die("unable to exec %s", sender);
		case -ERR_RUN_COMMAND_WAITPID:
		case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
			die("%s died with strange error", sender);
		default:
			return -err;
		}
	}
	return 0;
}

int cmd_push(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *repo = "origin";	/* default repository */

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-') {
			repo = arg;
			i++;
			break;
		}
		if (!strcmp(arg, "-v")) {
			verbose=1;
			continue;
		}
		if (!strncmp(arg, "--repo=", 7)) {
			repo = arg+7;
			continue;
		}
		if (!strcmp(arg, "--all")) {
			all = 1;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			tags = 1;
			continue;
		}
		if (!strcmp(arg, "--force") || !strcmp(arg, "-f")) {
			force = 1;
			continue;
		}
		if (!strcmp(arg, "--thin")) {
			thin = 1;
			continue;
		}
		if (!strcmp(arg, "--no-thin")) {
			thin = 0;
			continue;
		}
		if (!strncmp(arg, "--receive-pack=", 15)) {
			receivepack = arg;
			continue;
		}
		if (!strncmp(arg, "--exec=", 7)) {
			receivepack = arg;
			continue;
		}
		usage(push_usage);
	}
	set_refspecs(argv + i, argc - i);
	return do_push(repo);
}
