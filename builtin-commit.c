/*
 * Builtin "git commit"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>
 * Based on git-commit.sh by Junio C Hamano and Linus Torvalds
 */

#include "cache.h"
#include "cache-tree.h"
#include "builtin.h"
#include "diff.h"
#include "diffcore.h"
#include "commit.h"
#include "revision.h"
#include "wt-status.h"
#include "run-command.h"
#include "refs.h"
#include "log-tree.h"
#include "strbuf.h"
#include "utf8.h"
#include "parse-options.h"

static const char * const builtin_commit_usage[] = {
	"git-commit [options] [--] <filepattern>...",
	NULL
};

static unsigned char head_sha1[20], merge_head_sha1[20];
static char *use_message_buffer;
static const char commit_editmsg[] = "COMMIT_EDITMSG";
static struct lock_file lock_file;

static char *logfile, *force_author, *message, *template_file;
static char *edit_message, *use_message;
static int all, edit_flag, also, interactive, only, amend, signoff;
static int quiet, verbose, untracked_files, no_verify;

static int no_edit, initial_commit, in_merge;
const char *only_include_assumed;

static struct option builtin_commit_options[] = {
	OPT__QUIET(&quiet),
	OPT__VERBOSE(&verbose),
	OPT_GROUP("Commit message options"),

	OPT_STRING('F', "file", &logfile, "FILE", "read log from file"),
	OPT_STRING(0, "author", &force_author, "AUTHOR", "override author for commit"),
	OPT_STRING('m', "message", &message, "MESSAGE", "specify commit message"),
	OPT_STRING('c', "reedit-message", &edit_message, "COMMIT", "reuse and edit message from specified commit "),
	OPT_STRING('C', "reuse-message", &use_message, "COMMIT", "reuse message from specified commit"),
	OPT_BOOLEAN('s', "signoff", &signoff, "add Signed-off-by: header"),
	OPT_STRING('t', "template", &template_file, "FILE", "use specified template file"),
	OPT_BOOLEAN('e', "edit", &edit_flag, "force edit of commit"),

	OPT_GROUP("Commit contents options"),
	OPT_BOOLEAN('a', "all", &all, "commit all changed files"),
	OPT_BOOLEAN('i', "include", &also, "add specified files to index for commit"),
	OPT_BOOLEAN(0, "interactive", &interactive, "interactively add files"),
	OPT_BOOLEAN('o', "only", &only, ""),
	OPT_BOOLEAN('n', "no-verify", &no_verify, "bypass pre-commit hook"),
	OPT_BOOLEAN(0, "amend", &amend, "amend previous commit"),
	OPT_BOOLEAN(0, "untracked-files", &untracked_files, "show all untracked files"),

	OPT_END()
};

static char *prepare_index(const char **files, const char *prefix)
{
	int fd;
	struct tree *tree;
	struct lock_file *next_index_lock;

	if (interactive) {
		interactive_add();
		return get_index_file();
	}

	fd = hold_locked_index(&lock_file, 1);
	if (read_cache() < 0)
		die("index file corrupt");

	if (all || also) {
		add_files_to_cache(verbose, also ? prefix : NULL, files);
		refresh_cache(REFRESH_QUIET);
		if (write_cache(fd, active_cache, active_nr) || close(fd))
			die("unable to write new_index file");
		return lock_file.filename;
	}

	if (*files == NULL) {
		/* Commit index as-is. */
		rollback_lock_file(&lock_file);
		return get_index_file();
	}

	/* update the user index file */
	add_files_to_cache(verbose, prefix, files);
	if (write_cache(fd, active_cache, active_nr) || close(fd))
		die("unable to write new_index file");

	if (!initial_commit) {
		tree = parse_tree_indirect(head_sha1);
		if (!tree)
			die("failed to unpack HEAD tree object");
		if (read_tree(tree, 0, NULL))
			die("failed to read HEAD tree object");
	}

	/* Use a lock file to garbage collect the temporary index file. */
	next_index_lock = xmalloc(sizeof(*next_index_lock));
	fd = hold_lock_file_for_update(next_index_lock,
				       git_path("next-index-%d", getpid()), 1);
	add_files_to_cache(verbose, prefix, files);
	refresh_cache(REFRESH_QUIET);
	if (write_cache(fd, active_cache, active_nr) || close(fd))
		die("unable to write new_index file");

	return next_index_lock->filename;
}

static int run_status(FILE *fp, const char *index_file, const char *prefix)
{
	struct wt_status s;

	wt_status_prepare(&s);
	s.prefix = prefix;

	if (amend) {
		s.amend = 1;
		s.reference = "HEAD^1";
	}
	s.verbose = verbose;
	s.untracked = untracked_files;
	s.index_file = index_file;
	s.fp = fp;

	wt_status_print(&s);

	return s.commitable;
}

static const char sign_off_header[] = "Signed-off-by: ";

static int prepare_log_message(const char *index_file, const char *prefix)
{
	struct stat statbuf;
	int commitable;
	struct strbuf sb;
	char *buffer;
	FILE *fp;

	strbuf_init(&sb, 0);
	if (message) {
		strbuf_add(&sb, message, strlen(message));
	} else if (logfile && !strcmp(logfile, "-")) {
		if (isatty(0))
			fprintf(stderr, "(reading log message from standard input)\n");
		if (strbuf_read(&sb, 0, 0) < 0)
			die("could not read log from standard input");
	} else if (logfile) {
		if (strbuf_read_file(&sb, logfile, 0) < 0)
			die("could not read log file '%s': %s",
			    logfile, strerror(errno));
	} else if (use_message) {
		buffer = strstr(use_message_buffer, "\n\n");
		if (!buffer || buffer[2] == '\0')
			die("commit has empty message");
		strbuf_add(&sb, buffer + 2, strlen(buffer + 2));
	} else if (!stat(git_path("MERGE_MSG"), &statbuf)) {
		if (strbuf_read_file(&sb, git_path("MERGE_MSG"), 0) < 0)
			die("could not read MERGE_MSG: %s", strerror(errno));
	} else if (!stat(git_path("SQUASH_MSG"), &statbuf)) {
		if (strbuf_read_file(&sb, git_path("SQUASH_MSG"), 0) < 0)
			die("could not read SQUASH_MSG: %s", strerror(errno));
	} else if (template_file && !stat(template_file, &statbuf)) {
		if (strbuf_read_file(&sb, template_file, 0) < 0)
			die("could not read %s: %s",
			    template_file, strerror(errno));
	}

	fp = fopen(git_path(commit_editmsg), "w");
	if (fp == NULL)
		die("could not open %s\n", git_path(commit_editmsg));

	stripspace(&sb, 0);

	if (signoff) {
		struct strbuf sob;
		int i;

		strbuf_init(&sob, 0);
		strbuf_addstr(&sob, sign_off_header);
		strbuf_addstr(&sob, fmt_ident(getenv("GIT_COMMITTER_NAME"),
					      getenv("GIT_COMMITTER_EMAIL"),
					      "", 1));
		strbuf_addch(&sob, '\n');

		for (i = sb.len - 1; i > 0 && sb.buf[i - 1] != '\n'; i--)
			; /* do nothing */
		if (prefixcmp(sb.buf + i, sob.buf))
			strbuf_addbuf(&sb, &sob);
		strbuf_release(&sob);
	}

	if (fwrite(sb.buf, 1, sb.len, fp) < sb.len)
		die("could not write commit template: %s\n",
		    strerror(errno));

	strbuf_release(&sb);

	if (in_merge && !no_edit)
		fprintf(fp,
			"#\n"
			"# It looks like you may be committing a MERGE.\n"
			"# If this is not correct, please remove the file\n"
			"#	%s\n"
			"# and try again.\n"
			"#\n",
			git_path("MERGE_HEAD"));

	fprintf(fp,
		"\n"
		"# Please enter the commit message for your changes.\n"
		"# (Comment lines starting with '#' will not be included)\n");
	if (only_include_assumed)
		fprintf(fp, "# %s\n", only_include_assumed);

	commitable = run_status(fp, index_file, prefix);

	fclose(fp);

	return commitable;
}

/*
 * Find out if the message starting at position 'start' in the strbuf
 * contains only whitespace and Signed-off-by lines.
 */
static int message_is_empty(struct strbuf *sb, int start)
{
	struct strbuf tmpl;
	const char *nl;
	int eol, i;

	/* See if the template is just a prefix of the message. */
	strbuf_init(&tmpl, 0);
	if (template_file && strbuf_read_file(&tmpl, template_file, 0) > 0) {
		stripspace(&tmpl, 1);
		if (start + tmpl.len <= sb->len &&
		    memcmp(tmpl.buf, sb->buf + start, tmpl.len) == 0)
			start += tmpl.len;
	}
	strbuf_release(&tmpl);

	/* Check if the rest is just whitespace and Signed-of-by's. */
	for (i = start; i < sb->len; i++) {
		nl = memchr(sb->buf + i, '\n', sb->len - i);
		if (nl)
			eol = nl - sb->buf;
		else
			eol = sb->len;

		if (strlen(sign_off_header) <= eol - i &&
		    !prefixcmp(sb->buf + i, sign_off_header)) {
			i = eol;
			continue;
		}
		while (i < eol)
			if (!isspace(sb->buf[i++]))
				return 0;
	}

	return 1;
}

static void determine_author_info(struct strbuf *sb)
{
	char *name, *email, *date;

	name = getenv("GIT_AUTHOR_NAME");
	email = getenv("GIT_AUTHOR_EMAIL");
	date = getenv("GIT_AUTHOR_DATE");

	if (use_message) {
		const char *a, *lb, *rb, *eol;

		a = strstr(use_message_buffer, "\nauthor ");
		if (!a)
			die("invalid commit: %s\n", use_message);

		lb = strstr(a + 8, " <");
		rb = strstr(a + 8, "> ");
		eol = strchr(a + 8, '\n');
		if (!lb || !rb || !eol)
			die("invalid commit: %s\n", use_message);

		name = xstrndup(a + 8, lb - (a + 8));
		email = xstrndup(lb + 2, rb - (lb + 2));
		date = xstrndup(rb + 2, eol - (rb + 2));
	}

	if (force_author) {
		const char *lb = strstr(force_author, " <");
		const char *rb = strchr(force_author, '>');

		if (!lb || !rb)
			die("malformed --author parameter\n");
		name = xstrndup(force_author, lb - force_author);
		email = xstrndup(lb + 2, rb - (lb + 2));
	}

	strbuf_addf(sb, "author %s\n", fmt_ident(name, email, date, 1));
}

static int parse_and_validate_options(int argc, const char *argv[])
{
	int f = 0;

	argc = parse_options(argc, argv, builtin_commit_options,
			     builtin_commit_usage, 0);

	if (logfile || message || use_message)
		no_edit = 1;
	if (edit_flag)
		no_edit = 0;

	if (get_sha1("HEAD", head_sha1))
		initial_commit = 1;

	if (!get_sha1("MERGE_HEAD", merge_head_sha1))
		in_merge = 1;

	/* Sanity check options */
	if (amend && initial_commit)
		die("You have nothing to amend.");
	if (amend && in_merge)
		die("You are in the middle of a merger -- cannot amend.");

	if (use_message)
		f++;
	if (edit_message)
		f++;
	if (logfile)
		f++;
	if (f > 1)
		die("Only one of -c/-C/-F can be used.");
	if (message && f > 0)
		die("Option -m cannot be combined with -c/-C/-F.");
	if (edit_message)
		use_message = edit_message;
	if (amend)
		use_message = "HEAD";
	if (use_message) {
		unsigned char sha1[20];
		static char utf8[] = "UTF-8";
		const char *out_enc;
		char *enc, *end;
		struct commit *commit;

		if (get_sha1(use_message, sha1))
			die("could not lookup commit %s", use_message);
		commit = lookup_commit(sha1);
		if (!commit || parse_commit(commit))
			die("could not parse commit %s", use_message);

		enc = strstr(commit->buffer, "\nencoding");
		if (enc) {
			end = strchr(enc + 10, '\n');
			enc = xstrndup(enc + 10, end - (enc + 10));
		} else {
			enc = utf8;
		}
		out_enc = git_commit_encoding ? git_commit_encoding : utf8;

		if (strcmp(out_enc, enc))
			use_message_buffer =
				reencode_string(commit->buffer, out_enc, enc);

		/*
		 * If we failed to reencode the buffer, just copy it
		 * byte for byte so the user can try to fix it up.
		 * This also handles the case where input and output
		 * encodings are identical.
		 */
		if (use_message_buffer == NULL)
			use_message_buffer = xstrdup(commit->buffer);
		if (enc != utf8)
			free(enc);
	}

	if (!!also + !!only + !!all + !!interactive > 1)
		die("Only one of --include/--only/--all/--interactive can be used.");
	if (argc == 0 && (also || (only && !amend)))
		die("No paths with --include/--only does not make sense.");
	if (argc == 0 && only && amend)
		only_include_assumed = "Clever... amending the last one with dirty index.";
	if (argc > 0 && !also && !only) {
		only_include_assumed = "Explicit paths specified without -i nor -o; assuming --only paths...";
		also = 0;
	}

	if (all && argc > 0)
		die("Paths with -a does not make sense.");
	else if (interactive && argc > 0)
		die("Paths with --interactive does not make sense.");

	return argc;
}

int cmd_status(int argc, const char **argv, const char *prefix)
{
	const char *index_file;
	int commitable;

	git_config(git_status_config);

	argc = parse_and_validate_options(argc, argv);

	index_file = prepare_index(argv, prefix);

	commitable = run_status(stdout, index_file, prefix);

	rollback_lock_file(&lock_file);

	return commitable ? 0 : 1;
}

static int run_hook(const char *index_file, const char *name, const char *arg)
{
	struct child_process hook;
	const char *argv[3], *env[2];
	char index[PATH_MAX];

	argv[0] = git_path("hooks/%s", name);
	argv[1] = arg;
	argv[2] = NULL;
	snprintf(index, sizeof(index), "GIT_INDEX_FILE=%s", index_file);
	env[0] = index;
	env[1] = NULL;

	if (access(argv[0], X_OK) < 0)
		return 0;

	memset(&hook, 0, sizeof(hook));
	hook.argv = argv;
	hook.no_stdin = 1;
	hook.stdout_to_stderr = 1;
	hook.env = env;

	return run_command(&hook);
}

static void print_summary(const char *prefix, const unsigned char *sha1)
{
	struct rev_info rev;
	struct commit *commit;

	commit = lookup_commit(sha1);
	if (!commit)
		die("couldn't look up newly created commit\n");
	if (!commit || parse_commit(commit))
		die("could not parse newly created commit");

	init_revisions(&rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);

	rev.abbrev = 0;
	rev.diff = 1;
	rev.diffopt.output_format =
		DIFF_FORMAT_SHORTSTAT | DIFF_FORMAT_SUMMARY;

	rev.verbose_header = 1;
	rev.show_root_diff = 1;
	rev.commit_format = get_commit_format("format:%h: %s");
	rev.always_show_header = 1;

	printf("Created %scommit ", initial_commit ? "initial " : "");

	log_tree_commit(&rev, commit);
}

int git_commit_config(const char *k, const char *v)
{
	if (!strcmp(k, "commit.template")) {
		template_file = xstrdup(v);
		return 0;
	}

	return git_status_config(k, v);
}

static const char commit_utf8_warn[] =
"Warning: commit message does not conform to UTF-8.\n"
"You may want to amend it after fixing the message, or set the config\n"
"variable i18n.commitencoding to the encoding your project uses.\n";

int cmd_commit(int argc, const char **argv, const char *prefix)
{
	int header_len, parent_count = 0;
	struct strbuf sb;
	const char *index_file, *reflog_msg;
	char *nl;
	unsigned char commit_sha1[20];
	struct ref_lock *ref_lock;

	git_config(git_commit_config);

	argc = parse_and_validate_options(argc, argv);

	index_file = prepare_index(argv, prefix);

	if (!no_verify && run_hook(index_file, "pre-commit", NULL))
		exit(1);

	if (!prepare_log_message(index_file, prefix) && !in_merge) {
		run_status(stdout, index_file, prefix);
		unlink(commit_editmsg);
		return 1;
	}

	strbuf_init(&sb, 0);

	/* Start building up the commit header */
	read_cache_from(index_file);
	active_cache_tree = cache_tree();
	if (cache_tree_update(active_cache_tree,
			      active_cache, active_nr, 0, 0) < 0)
		die("Error building trees");
	strbuf_addf(&sb, "tree %s\n",
		    sha1_to_hex(active_cache_tree->sha1));

	/* Determine parents */
	if (initial_commit) {
		reflog_msg = "commit (initial)";
		parent_count = 0;
	} else if (amend) {
		struct commit_list *c;
		struct commit *commit;

		reflog_msg = "commit (amend)";
		commit = lookup_commit(head_sha1);
		if (!commit || parse_commit(commit))
			die("could not parse HEAD commit");

		for (c = commit->parents; c; c = c->next)
			strbuf_addf(&sb, "parent %s\n",
				      sha1_to_hex(c->item->object.sha1));
	} else if (in_merge) {
		struct strbuf m;
		FILE *fp;

		reflog_msg = "commit (merge)";
		strbuf_addf(&sb, "parent %s\n", sha1_to_hex(head_sha1));
		strbuf_init(&m, 0);
		fp = fopen(git_path("MERGE_HEAD"), "r");
		if (fp == NULL)
			die("could not open %s for reading: %s",
			    git_path("MERGE_HEAD"), strerror(errno));
		while (strbuf_getline(&m, fp, '\n') != EOF)
			strbuf_addf(&sb, "parent %s\n", m.buf);
		fclose(fp);
		strbuf_release(&m);
	} else {
		reflog_msg = "commit";
		strbuf_addf(&sb, "parent %s\n", sha1_to_hex(head_sha1));
	}

	determine_author_info(&sb);
	strbuf_addf(&sb, "committer %s\n", git_committer_info(1));
	if (!is_encoding_utf8(git_commit_encoding))
		strbuf_addf(&sb, "encoding %s\n", git_commit_encoding);
	strbuf_addch(&sb, '\n');

	/* Get the commit message and validate it */
	header_len = sb.len;
	if (!no_edit) {
		fprintf(stderr, "launching editor, log %s\n", logfile);
		launch_editor(git_path(commit_editmsg), &sb);
	} else if (strbuf_read_file(&sb, git_path(commit_editmsg), 0) < 0)
		die("could not read commit message\n");
	if (run_hook(index_file, "commit-msg", commit_editmsg))
		exit(1);
	stripspace(&sb, 1);
	if (sb.len < header_len ||
	    message_is_empty(&sb, header_len))
		die("* no commit message?  aborting commit.");
	strbuf_addch(&sb, '\0');
	if (is_encoding_utf8(git_commit_encoding) && !is_utf8(sb.buf))
		fprintf(stderr, commit_utf8_warn);

	if (write_sha1_file(sb.buf, sb.len - 1, commit_type, commit_sha1))
		die("failed to write commit object");

	ref_lock = lock_any_ref_for_update("HEAD",
					   initial_commit ? NULL : head_sha1,
					   0);

	nl = strchr(sb.buf + header_len, '\n');
	if (nl)
		strbuf_setlen(&sb, nl + 1 - sb.buf);
	else
		strbuf_addch(&sb, '\n');
	strbuf_remove(&sb, 0, header_len);
	strbuf_insert(&sb, 0, reflog_msg, strlen(reflog_msg));
	strbuf_insert(&sb, strlen(reflog_msg), ": ", 2);

	if (!ref_lock)
		die("cannot lock HEAD ref");
	if (write_ref_sha1(ref_lock, commit_sha1, sb.buf) < 0)
		die("cannot update HEAD ref");

	unlink(git_path("MERGE_HEAD"));
	unlink(git_path("MERGE_MSG"));

	if (lock_file.filename[0] && commit_locked_index(&lock_file))
		die("failed to write new index");

	rerere();

	run_hook(index_file, "post-commit", NULL);

	if (!quiet)
		print_summary(prefix, commit_sha1);

	return 0;
}
