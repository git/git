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
	}
	return 0;
}

int cmd_whatchanged(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
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

static FILE *realstdout = NULL;
static char *output_directory = NULL;

static void reopen_stdout(struct commit *commit, int nr, int keep_subject)
{
	char filename[1024];
	char *sol;
	int len = 0;

	if (output_directory) {
		strncpy(filename, output_directory, 1010);
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

int cmd_format_patch(int argc, const char **argv, char **envp)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	int nr = 0, total, i, j;
	int use_stdout = 0;
	int numbered = 0;
	int keep_subject = 0;

	init_revisions(&rev);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.diffopt.with_raw = 0;
	rev.diffopt.with_stat = 1;
	rev.combine_merges = 0;
	rev.ignore_merges = 1;
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;

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
		else if (!strcmp(argv[i], "-k") ||
				!strcmp(argv[i], "--keep-subject")) {
			keep_subject = 1;
			rev.total = -1;
		} else if (!strcmp(argv[i], "-o")) {
			if (argc < 3)
				die ("Which directory?");
			if (mkdir(argv[i + 1], 0777) < 0 && errno != EEXIST)
				die("Could not create directory %s",
						argv[i + 1]);
			output_directory = strdup(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "--attach"))
			rev.mime_boundary = git_version_string;
		else if (!strncmp(argv[i], "--attach=", 9))
			rev.mime_boundary = argv[i] + 9;
		else
			argv[j++] = argv[i];
	}
	argc = j;

	if (numbered && keep_subject < 0)
		die ("-n and -k are mutually exclusive.");

	argc = setup_revisions(argc, argv, &rev, "HEAD");
	if (argc > 1)
		die ("unrecognized argument: %s", argv[1]);

	if (rev.pending_objects && rev.pending_objects->next == NULL) {
		rev.pending_objects->item->flags |= UNINTERESTING;
		add_head(&rev);
	}

	if (!use_stdout)
		realstdout = fdopen(dup(1), "w");

	prepare_revision_walk(&rev);
	while ((commit = get_revision(&rev)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;
		nr++;
		list = realloc(list, nr * sizeof(list[0]));
		list[nr - 1] = commit;
	}
	total = nr;
	if (numbered)
		rev.total = total;
	while (0 <= --nr) {
		int shown;
		commit = list[nr];
		rev.nr = total - nr;
		if (!use_stdout)
			reopen_stdout(commit, rev.nr, keep_subject);
		shown = log_tree_commit(&rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;
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
	if (output_directory)
		free(output_directory);
	free(list);
	return 0;
}

