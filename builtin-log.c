/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"

/* this is in builtin-diff.c */
void add_head(struct rev_info *revs);

static int cmd_log_wc(int argc, const char **argv, char **envp,
		      struct rev_info *rev)
{
	struct commit *commit;

	rev->abbrev = DEFAULT_ABBREV;
	rev->commit_format = CMIT_FMT_DEFAULT;
	rev->verbose_header = 1;
	argc = setup_revisions(argc, argv, rev, "HEAD");
	if (rev->always_show_header) {
		if (rev->diffopt.pickaxe || rev->diffopt.filter) {
			rev->always_show_header = 0;
			if (rev->diffopt.output_format == DIFF_FORMAT_RAW)
				rev->diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
		}
	}

	if (argc > 1)
		die("unrecognized argument: %s", argv[1]);

	prepare_revision_walk(rev);
	setup_pager();
	while ((commit = get_revision(rev)) != NULL) {
		log_tree_commit(rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	return 0;
}

int cmd_whatchanged(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.simplify_history = 0;
	return cmd_log_wc(argc, argv, envp, &rev);
}

int cmd_show(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.combine_merges = 1;
	rev.dense_combined_merges = 1;
	rev.always_show_header = 1;
	rev.ignore_merges = 0;
	rev.no_walk = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

int cmd_log(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.always_show_header = 1;
	rev.diffopt.recursive = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

static int istitlechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '.' || c == '_';
}

static char *extra_headers = NULL;
static int extra_headers_size = 0;

static int git_format_config(const char *var, const char *value)
{
	if (!strcmp(var, "format.headers")) {
		int len = strlen(value);
		extra_headers_size += len + 1;
		extra_headers = realloc(extra_headers, extra_headers_size);
		extra_headers[extra_headers_size - len - 1] = 0;
		strcat(extra_headers, value);
		return 0;
	}
	return git_default_config(var, value);
}


static FILE *realstdout = NULL;
static const char *output_directory = NULL;

static void reopen_stdout(struct commit *commit, int nr, int keep_subject)
{
	char filename[1024];
	char *sol;
	int len = 0;

	if (output_directory) {
		strlcpy(filename, output_directory, 1010);
		len = strlen(filename);
		if (filename[len - 1] != '/')
			filename[len++] = '/';
	}

	sprintf(filename + len, "%04d", nr);
	len = strlen(filename);

	sol = strstr(commit->buffer, "\n\n");
	if (sol) {
		int j, space = 1;

		sol += 2;
		/* strip [PATCH] or [PATCH blabla] */
		if (!keep_subject && !strncmp(sol, "[PATCH", 6)) {
			char *eos = strchr(sol + 6, ']');
			if (eos) {
				while (isspace(*eos))
					eos++;
				sol = eos;
			}
		}

		for (j = 0; len < 1024 - 6 && sol[j] && sol[j] != '\n'; j++) {
			if (istitlechar(sol[j])) {
				if (space) {
					filename[len++] = '-';
					space = 0;
				}
				filename[len++] = sol[j];
				if (sol[j] == '.')
					while (sol[j + 1] == '.')
						j++;
			} else
				space = 1;
		}
		while (filename[len - 1] == '.' || filename[len - 1] == '-')
			len--;
	}
	strcpy(filename + len, ".txt");
	fprintf(realstdout, "%s\n", filename);
	freopen(filename, "w", stdout);
}

static void reset_all_objects_flags()
{
	int i;

	for (i = 0; i < obj_allocs; i++)
		if (objs[i])
			objs[i]->flags = 0;
}

static int get_patch_id(struct commit *commit, struct diff_options *options,
		unsigned char *sha1)
{
	diff_tree_sha1(commit->parents->item->object.sha1, commit->object.sha1,
			"", options);
	diffcore_std(options);
	return diff_flush_patch_id(options, sha1);
}

static void get_patch_ids(struct rev_info *rev, struct diff_options *options)
{
	struct rev_info check_rev;
	struct commit *commit;
	struct object *o1, *o2;
	unsigned flags1, flags2;
	unsigned char sha1[20];

	if (rev->pending.nr != 2)
		die("Need exactly one range.");

	o1 = rev->pending.objects[0].item;
	flags1 = o1->flags;
	o2 = rev->pending.objects[1].item;
	flags2 = o2->flags;

	if ((flags1 & UNINTERESTING) == (flags2 & UNINTERESTING))
		die("Not a range.");

	diff_setup(options);
	options->recursive = 1;
	if (diff_setup_done(options) < 0)
		die("diff_setup_done failed");

	/* given a range a..b get all patch ids for b..a */
	init_revisions(&check_rev);
	o1->flags ^= UNINTERESTING;
	o2->flags ^= UNINTERESTING;
	add_pending_object(&check_rev, o1, "o1");
	add_pending_object(&check_rev, o2, "o2");
	prepare_revision_walk(&check_rev);

	while ((commit = get_revision(&check_rev)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		if (!get_patch_id(commit, options, sha1))
			created_object(sha1, xcalloc(1, sizeof(struct object)));
	}

	/* reset for next revision walk */
	reset_all_objects_flags();
	o1->flags = flags1;
	o2->flags = flags2;
}

int cmd_format_patch(int argc, const char **argv, char **envp)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	int nr = 0, total, i, j;
	int use_stdout = 0;
	int numbered = 0;
	int start_number = -1;
	int keep_subject = 0;
	int ignore_if_in_upstream = 0;
	struct diff_options patch_id_opts;
	char *add_signoff = NULL;

	init_revisions(&rev);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.diffopt.with_raw = 0;
	rev.diffopt.with_stat = 1;
	rev.combine_merges = 0;
	rev.ignore_merges = 1;
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;

	git_config(git_format_config);
	rev.extra_headers = extra_headers;

	/*
	 * Parse the arguments before setup_revisions(), or something
	 * like "git fmt-patch -o a123 HEAD^.." may fail; a123 is
	 * possibly a valid SHA1.
	 */
	for (i = 1, j = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--stdout"))
			use_stdout = 1;
		else if (!strcmp(argv[i], "-n") ||
				!strcmp(argv[i], "--numbered"))
			numbered = 1;
		else if (!strncmp(argv[i], "--start-number=", 15))
			start_number = strtol(argv[i] + 15, NULL, 10);
		else if (!strcmp(argv[i], "--start-number")) {
			i++;
			if (i == argc)
				die("Need a number for --start-number");
			start_number = strtol(argv[i], NULL, 10);
		}
		else if (!strcmp(argv[i], "-k") ||
				!strcmp(argv[i], "--keep-subject")) {
			keep_subject = 1;
			rev.total = -1;
		}
		else if (!strcmp(argv[i], "--output-directory") ||
			 !strcmp(argv[i], "-o")) {
			i++;
			if (argc <= i)
				die("Which directory?");
			if (output_directory)
				die("Two output directories?");
			output_directory = argv[i];
		}
		else if (!strcmp(argv[i], "--signoff") ||
			 !strcmp(argv[i], "-s")) {
			const char *committer;
			const char *endpos;
			setup_ident();
			committer = git_committer_info(1);
			endpos = strchr(committer, '>');
			if (!endpos)
				die("bogos committer info %s\n", committer);
			add_signoff = xmalloc(endpos - committer + 2);
			memcpy(add_signoff, committer, endpos - committer + 1);
			add_signoff[endpos - committer + 1] = 0;
		}
		else if (!strcmp(argv[i], "--attach"))
			rev.mime_boundary = git_version_string;
		else if (!strncmp(argv[i], "--attach=", 9))
			rev.mime_boundary = argv[i] + 9;
		else if (!strcmp(argv[i], "--ignore-if-in-upstream"))
			ignore_if_in_upstream = 1;
		else
			argv[j++] = argv[i];
	}
	argc = j;

	if (start_number < 0)
		start_number = 1;
	if (numbered && keep_subject)
		die ("-n and -k are mutually exclusive.");

	argc = setup_revisions(argc, argv, &rev, "HEAD");
	if (argc > 1)
		die ("unrecognized argument: %s", argv[1]);

	if (output_directory) {
		if (use_stdout)
			die("standard output, or directory, which one?");
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die("Could not create directory %s",
			    output_directory);
	}

	if (rev.pending.nr == 1) {
		rev.pending.objects[0].item->flags |= UNINTERESTING;
		add_head(&rev);
	}

	if (ignore_if_in_upstream)
		get_patch_ids(&rev, &patch_id_opts);

	if (!use_stdout)
		realstdout = fdopen(dup(1), "w");

	prepare_revision_walk(&rev);
	while ((commit = get_revision(&rev)) != NULL) {
		unsigned char sha1[20];

		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		if (ignore_if_in_upstream &&
				!get_patch_id(commit, &patch_id_opts, sha1) &&
				lookup_object(sha1))
			continue;

		nr++;
		list = realloc(list, nr * sizeof(list[0]));
		list[nr - 1] = commit;
	}
	total = nr;
	if (numbered)
		rev.total = total + start_number - 1;
	rev.add_signoff = add_signoff;
	while (0 <= --nr) {
		int shown;
		commit = list[nr];
		rev.nr = total - nr + (start_number - 1);
		if (!use_stdout)
			reopen_stdout(commit, rev.nr, keep_subject);
		shown = log_tree_commit(&rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;

		/* We put one extra blank line between formatted
		 * patches and this flag is used by log-tree code
		 * to see if it needs to emit a LF before showing
		 * the log; when using one file per patch, we do
		 * not want the extra blank line.
		 */
		if (!use_stdout)
			rev.shown_one = 0;
		if (shown) {
			if (rev.mime_boundary)
				printf("\n--%s%s--\n\n\n",
				       mime_boundary_leader,
				       rev.mime_boundary);
			else
				printf("-- \n%s\n\n", git_version_string);
		}
		if (!use_stdout)
			fclose(stdout);
	}
	free(list);
	return 0;
}

