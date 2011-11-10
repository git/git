#include "cache.h"
#include "walker.h"

int cmd_http_fetch(int argc, const char **argv, const char *prefix)
{
	struct walker *walker;
	int commits_on_stdin = 0;
	int commits;
	const char **write_ref = NULL;
	char **commit_id;
	const char *url;
	char *rewritten_url = NULL;
	int arg = 1;
	int rc = 0;
	int get_tree = 0;
	int get_history = 0;
	int get_all = 0;
	int get_verbosely = 0;
	int get_recover = 0;

	git_config(git_default_config, NULL);

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = &argv[arg + 1];
			arg++;
		} else if (!strcmp(argv[arg], "--recover")) {
			get_recover = 1;
		} else if (!strcmp(argv[arg], "--stdin")) {
			commits_on_stdin = 1;
		}
		arg++;
	}
	if (argc < arg + 2 - commits_on_stdin) {
		usage("git http-fetch [-c] [-t] [-a] [-v] [--recover] [-w ref] [--stdin] commit-id url");
		return 1;
	}
	if (commits_on_stdin) {
		commits = walker_targets_stdin(&commit_id, &write_ref);
	} else {
		commit_id = (char **) &argv[arg++];
		commits = 1;
	}
	url = argv[arg];
	if (url && url[strlen(url)-1] != '/') {
		rewritten_url = xmalloc(strlen(url)+2);
		strcpy(rewritten_url, url);
		strcat(rewritten_url, "/");
		url = rewritten_url;
	}

	walker = get_http_walker(url, NULL);
	walker->get_tree = get_tree;
	walker->get_history = get_history;
	walker->get_all = get_all;
	walker->get_verbosely = get_verbosely;
	walker->get_recover = get_recover;

	rc = walker_fetch(walker, commits, commit_id, write_ref, url);

	if (commits_on_stdin)
		walker_targets_free(commits, commit_id, write_ref);

	if (walker->corrupt_object_found) {
		fprintf(stderr,
"Some loose object were found to be corrupt, but they might be just\n"
"a false '404 Not Found' error message sent with incorrect HTTP\n"
"status code.  Suggest running 'git fsck'.\n");
	}

	walker_free(walker);

	free(rewritten_url);

	return rc;
}
