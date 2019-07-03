#include "cache.h"
#include "config.h"
#include "exec-cmd.h"
#include "http.h"
#include "walker.h"

static const char http_fetch_usage[] = "git http-fetch "
"[-c] [-t] [-a] [-v] [--recover] [-w ref] [--stdin | --packfile | commit-id] url";

int cmd_main(int argc, const char **argv)
{
	struct walker *walker;
	int commits_on_stdin = 0;
	int commits;
	const char **write_ref = NULL;
	char **commit_id;
	char *url = NULL;
	int arg = 1;
	int rc = 0;
	int get_verbosely = 0;
	int get_recover = 0;
	int packfile = 0;

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
		} else if (argv[arg][1] == 'c') {
		} else if (argv[arg][1] == 'a') {
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = &argv[arg + 1];
			arg++;
		} else if (argv[arg][1] == 'h') {
			usage(http_fetch_usage);
		} else if (!strcmp(argv[arg], "--recover")) {
			get_recover = 1;
		} else if (!strcmp(argv[arg], "--stdin")) {
			commits_on_stdin = 1;
		} else if (!strcmp(argv[arg], "--packfile")) {
			packfile = 1;
		}
		arg++;
	}
	if (argc != arg + 2 - (commits_on_stdin || packfile))
		usage(http_fetch_usage);
	if (commits_on_stdin) {
		commits = walker_targets_stdin(&commit_id, &write_ref);
	} else if (packfile) {
		/* URL will be set later */
	} else {
		commit_id = (char **) &argv[arg++];
		commits = 1;
	}

	if (packfile) {
		url = xstrdup(argv[arg]);
	} else {
		if (argv[arg])
			str_end_url_with_slash(argv[arg], &url);
	}

	setup_git_directory();

	git_config(git_default_config, NULL);

	http_init(NULL, url, 0);

	if (packfile) {
		struct http_pack_request *preq;
		struct slot_results results;
		int ret;

		preq = new_http_pack_request(NULL, url);
		if (preq == NULL)
			die("couldn't create http pack request");
		preq->slot->results = &results;
		preq->generate_keep = 1;

		if (start_active_slot(preq->slot)) {
			run_active_slot(preq->slot);
			if (results.curl_result != CURLE_OK) {
				die("Unable to get pack file %s\n%s", preq->url,
				    curl_errorstr);
			}
		} else {
			die("Unable to start request");
		}

		if ((ret = finish_http_pack_request(preq)))
			die("finish_http_pack_request gave result %d", ret);
		release_http_pack_request(preq);
		rc = 0;
	} else {
		walker = get_http_walker(url);
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
	}

	http_cleanup();

	free(url);

	return rc;
}
