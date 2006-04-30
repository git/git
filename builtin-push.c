/*
 * "git push"
 */
#include "cache.h"
#include "refs.h"
#include "run-command.h"
#include "builtin.h"

#define MAX_URI (16)

static const char push_usage[] = "git push [--all] [--tags] [--force] <repository> [<refspec>...]";

static int all = 0, tags = 0, force = 0, thin = 1;
static const char *execute = NULL;

#define BUF_SIZE (2084)
static char buffer[BUF_SIZE];

static const char **refspec = NULL;
static int refspec_nr = 0;

static void add_refspec(const char *ref)
{
	int nr = refspec_nr + 1;
	refspec = xrealloc(refspec, nr * sizeof(char *));
	refspec[nr-1] = ref;
	refspec_nr = nr;
}

static int expand_one_ref(const char *ref, const unsigned char *sha1)
{
	/* Ignore the "refs/" at the beginning of the refname */
	ref += 5;

	if (strncmp(ref, "tags/", 5))
		return 0;

	add_refspec(strdup(ref));
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
	for_each_ref(expand_one_ref);
}

static void set_refspecs(const char **refs, int nr)
{
	if (nr) {
		size_t bytes = nr * sizeof(char *);

		refspec = xrealloc(refspec, bytes);
		memcpy(refspec, refs, bytes);
		refspec_nr = nr;
	}
	expand_refspecs();
}

static int get_remotes_uri(const char *repo, const char *uri[MAX_URI])
{
	int n = 0;
	FILE *f = fopen(git_path("remotes/%s", repo), "r");

	if (!f)
		return -1;
	while (fgets(buffer, BUF_SIZE, f)) {
		char *s, *p;

		if (strncmp("URL: ", buffer, 5))
			continue;
		s = buffer + 5;

		/* Remove whitespace at the head.. */
		while (isspace(*s))
			s++;
		if (!*s)
			continue;

		/* ..and at the end */
		p = s + strlen(s);
		while (isspace(p[-1]))
			*--p = 0;

		uri[n++] = strdup(s);
		if (n == MAX_URI)
			break;
	}
	fclose(f);
	if (!n)
		die("remote '%s' has no URL", repo);
	return n;
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

static int get_uri(const char *repo, const char *uri[MAX_URI])
{
	int n;

	if (*repo != '/') {
		n = get_remotes_uri(repo, uri);
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
	int i, n = get_uri(repo, uri);
	int remote;
	const char **argv;
	int argc;

	n = get_uri(repo, uri);
	if (n <= 0)
		die("bad repository '%s'", repo);

	argv = xmalloc((refspec_nr + 10) * sizeof(char *));
	argv[0] = "dummy-send-pack";
	argc = 1;
	if (all)
		argv[argc++] = "--all";
	if (force)
		argv[argc++] = "--force";
	if (execute)
		argv[argc++] = execute;
	if (thin)
		argv[argc++] = "--thin";
	remote = argc;
	argv[argc++] = "dummy-remote";
	while (refspec_nr--)
		argv[argc++] = *refspec++;
	argv[argc] = NULL;

	for (i = 0; i < n; i++) {
		int error;
		const char *dest = uri[i];
		const char *sender = "git-send-pack";
		if (!strncmp(dest, "http://", 7) ||
		    !strncmp(dest, "https://", 8))
			sender = "git-http-push";
		argv[0] = sender;
		argv[remote] = dest;
		error = run_command_v(argc, argv);
		if (!error)
			continue;
		switch (error) {
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
			return -error;
		}
	}
	return 0;
}

int cmd_push(int argc, const char **argv, char **envp)
{
	int i;
	const char *repo = "origin";	// default repository

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-') {
			repo = arg;
			i++;
			break;
		}
		if (!strcmp(arg, "--all")) {
			all = 1;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			tags = 1;
			continue;
		}
		if (!strcmp(arg, "--force")) {
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
		if (!strncmp(arg, "--exec=", 7)) {
			execute = arg;
			continue;
		}
		usage(push_usage);
	}
	set_refspecs(argv + i, argc - i);
	return do_push(repo);
}
