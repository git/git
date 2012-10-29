/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "cache.h"
#include "color.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"
#include "tag.h"
#include "reflog-walk.h"
#include "patch-ids.h"
#include "run-command.h"
#include "shortlog.h"
#include "remote.h"
#include "string-list.h"
#include "parse-options.h"
#include "branch.h"
#include "streaming.h"
#include "version.h"

/* Set a default date-time format for git log ("log.date" config variable) */
static const char *default_date_mode = NULL;

static int default_abbrev_commit;
static int default_show_root = 1;
static int decoration_style;
static int decoration_given;
static const char *fmt_patch_subject_prefix = "PATCH";
static const char *fmt_pretty;

static const char * const builtin_log_usage[] = {
	N_("git log [<options>] [<since>..<until>] [[--] <path>...]\n")
	N_("   or: git show [options] <object>..."),
	NULL
};

static int parse_decoration_style(const char *var, const char *value)
{
	switch (git_config_maybe_bool(var, value)) {
	case 1:
		return DECORATE_SHORT_REFS;
	case 0:
		return 0;
	default:
		break;
	}
	if (!strcmp(value, "full"))
		return DECORATE_FULL_REFS;
	else if (!strcmp(value, "short"))
		return DECORATE_SHORT_REFS;
	return -1;
}

static int decorate_callback(const struct option *opt, const char *arg, int unset)
{
	if (unset)
		decoration_style = 0;
	else if (arg)
		decoration_style = parse_decoration_style("command line", arg);
	else
		decoration_style = DECORATE_SHORT_REFS;

	if (decoration_style < 0)
		die("invalid --decorate option: %s", arg);

	decoration_given = 1;

	return 0;
}

static void cmd_log_init_defaults(struct rev_info *rev)
{
	if (fmt_pretty)
		get_commit_format(fmt_pretty, rev);
	rev->verbose_header = 1;
	DIFF_OPT_SET(&rev->diffopt, RECURSIVE);
	rev->diffopt.stat_width = -1; /* use full terminal width */
	rev->diffopt.stat_graph_width = -1; /* respect statGraphWidth config */
	rev->abbrev_commit = default_abbrev_commit;
	rev->show_root_diff = default_show_root;
	rev->subject_prefix = fmt_patch_subject_prefix;
	DIFF_OPT_SET(&rev->diffopt, ALLOW_TEXTCONV);

	if (default_date_mode)
		rev->date_mode = parse_date_format(default_date_mode);
}

static void cmd_log_init_finish(int argc, const char **argv, const char *prefix,
			 struct rev_info *rev, struct setup_revision_opt *opt)
{
	struct userformat_want w;
	int quiet = 0, source = 0;

	const struct option builtin_log_options[] = {
		OPT_BOOLEAN(0, "quiet", &quiet, N_("suppress diff output")),
		OPT_BOOLEAN(0, "source", &source, N_("show source")),
		{ OPTION_CALLBACK, 0, "decorate", NULL, NULL, N_("decorate options"),
		  PARSE_OPT_OPTARG, decorate_callback},
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix,
			     builtin_log_options, builtin_log_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN |
			     PARSE_OPT_KEEP_DASHDASH);

	if (quiet)
		rev->diffopt.output_format |= DIFF_FORMAT_NO_OUTPUT;
	argc = setup_revisions(argc, argv, rev, opt);

	/* Any arguments at this point are not recognized */
	if (argc > 1)
		die("unrecognized argument: %s", argv[1]);

	memset(&w, 0, sizeof(w));
	userformat_find_requirements(NULL, &w);

	if (!rev->show_notes_given && (!rev->pretty_given || w.notes))
		rev->show_notes = 1;
	if (rev->show_notes)
		init_display_notes(&rev->notes_opt);

	if (rev->diffopt.pickaxe || rev->diffopt.filter)
		rev->always_show_header = 0;
	if (DIFF_OPT_TST(&rev->diffopt, FOLLOW_RENAMES)) {
		rev->always_show_header = 0;
		if (rev->diffopt.pathspec.nr != 1)
			usage("git logs can only follow renames on one pathname at a time");
	}

	if (source)
		rev->show_source = 1;

	if (rev->pretty_given && rev->commit_format == CMIT_FMT_RAW) {
		/*
		 * "log --pretty=raw" is special; ignore UI oriented
		 * configuration variables such as decoration.
		 */
		if (!decoration_given)
			decoration_style = 0;
		if (!rev->abbrev_commit_given)
			rev->abbrev_commit = 0;
	}

	if (decoration_style) {
		rev->show_decorations = 1;
		load_ref_decorations(decoration_style);
	}
	setup_pager();
}

static void cmd_log_init(int argc, const char **argv, const char *prefix,
			 struct rev_info *rev, struct setup_revision_opt *opt)
{
	cmd_log_init_defaults(rev);
	cmd_log_init_finish(argc, argv, prefix, rev, opt);
}

/*
 * This gives a rough estimate for how many commits we
 * will print out in the list.
 */
static int estimate_commit_count(struct rev_info *rev, struct commit_list *list)
{
	int n = 0;

	while (list) {
		struct commit *commit = list->item;
		unsigned int flags = commit->object.flags;
		list = list->next;
		if (!(flags & (TREESAME | UNINTERESTING)))
			n++;
	}
	return n;
}

static void show_early_header(struct rev_info *rev, const char *stage, int nr)
{
	if (rev->shown_one) {
		rev->shown_one = 0;
		if (rev->commit_format != CMIT_FMT_ONELINE)
			putchar(rev->diffopt.line_termination);
	}
	printf(_("Final output: %d %s\n"), nr, stage);
}

static struct itimerval early_output_timer;

static void log_show_early(struct rev_info *revs, struct commit_list *list)
{
	int i = revs->early_output;
	int show_header = 1;

	sort_in_topological_order(&list, revs->lifo);
	while (list && i) {
		struct commit *commit = list->item;
		switch (simplify_commit(revs, commit)) {
		case commit_show:
			if (show_header) {
				int n = estimate_commit_count(revs, list);
				show_early_header(revs, "incomplete", n);
				show_header = 0;
			}
			log_tree_commit(revs, commit);
			i--;
			break;
		case commit_ignore:
			break;
		case commit_error:
			return;
		}
		list = list->next;
	}

	/* Did we already get enough commits for the early output? */
	if (!i)
		return;

	/*
	 * ..if no, then repeat it twice a second until we
	 * do.
	 *
	 * NOTE! We don't use "it_interval", because if the
	 * reader isn't listening, we want our output to be
	 * throttled by the writing, and not have the timer
	 * trigger every second even if we're blocked on a
	 * reader!
	 */
	early_output_timer.it_value.tv_sec = 0;
	early_output_timer.it_value.tv_usec = 500000;
	setitimer(ITIMER_REAL, &early_output_timer, NULL);
}

static void early_output(int signal)
{
	show_early_output = log_show_early;
}

static void setup_early_output(struct rev_info *rev)
{
	struct sigaction sa;

	/*
	 * Set up the signal handler, minimally intrusively:
	 * we only set a single volatile integer word (not
	 * using sigatomic_t - trying to avoid unnecessary
	 * system dependencies and headers), and using
	 * SA_RESTART.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = early_output;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	/*
	 * If we can get the whole output in less than a
	 * tenth of a second, don't even bother doing the
	 * early-output thing..
	 *
	 * This is a one-time-only trigger.
	 */
	early_output_timer.it_value.tv_sec = 0;
	early_output_timer.it_value.tv_usec = 100000;
	setitimer(ITIMER_REAL, &early_output_timer, NULL);
}

static void finish_early_output(struct rev_info *rev)
{
	int n = estimate_commit_count(rev, rev->commits);
	signal(SIGALRM, SIG_IGN);
	show_early_header(rev, "done", n);
}

static int cmd_log_walk(struct rev_info *rev)
{
	struct commit *commit;
	int saved_nrl = 0;
	int saved_dcctc = 0;

	if (rev->early_output)
		setup_early_output(rev);

	if (prepare_revision_walk(rev))
		die(_("revision walk setup failed"));

	if (rev->early_output)
		finish_early_output(rev);

	/*
	 * For --check and --exit-code, the exit code is based on CHECK_FAILED
	 * and HAS_CHANGES being accumulated in rev->diffopt, so be careful to
	 * retain that state information if replacing rev->diffopt in this loop
	 */
	while ((commit = get_revision(rev)) != NULL) {
		if (!log_tree_commit(rev, commit) &&
		    rev->max_count >= 0)
			/*
			 * We decremented max_count in get_revision,
			 * but we didn't actually show the commit.
			 */
			rev->max_count++;
		if (!rev->reflog_info) {
			/* we allow cycles in reflog ancestry */
			free(commit->buffer);
			commit->buffer = NULL;
		}
		free_commit_list(commit->parents);
		commit->parents = NULL;
		if (saved_nrl < rev->diffopt.needed_rename_limit)
			saved_nrl = rev->diffopt.needed_rename_limit;
		if (rev->diffopt.degraded_cc_to_c)
			saved_dcctc = 1;
	}
	rev->diffopt.degraded_cc_to_c = saved_dcctc;
	rev->diffopt.needed_rename_limit = saved_nrl;

	if (rev->diffopt.output_format & DIFF_FORMAT_CHECKDIFF &&
	    DIFF_OPT_TST(&rev->diffopt, CHECK_FAILED)) {
		return 02;
	}
	return diff_result_code(&rev->diffopt, 0);
}

static int git_log_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "format.pretty"))
		return git_config_string(&fmt_pretty, var, value);
	if (!strcmp(var, "format.subjectprefix"))
		return git_config_string(&fmt_patch_subject_prefix, var, value);
	if (!strcmp(var, "log.abbrevcommit")) {
		default_abbrev_commit = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "log.date"))
		return git_config_string(&default_date_mode, var, value);
	if (!strcmp(var, "log.decorate")) {
		decoration_style = parse_decoration_style(var, value);
		if (decoration_style < 0)
			decoration_style = 0; /* maybe warn? */
		return 0;
	}
	if (!strcmp(var, "log.showroot")) {
		default_show_root = git_config_bool(var, value);
		return 0;
	}
	if (!prefixcmp(var, "color.decorate."))
		return parse_decorate_color_config(var, 15, value);
	if (grep_config(var, value, cb) < 0)
		return -1;
	return git_diff_ui_config(var, value, cb);
}

int cmd_whatchanged(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_grep_defaults();
	git_config(git_log_config, NULL);

	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.simplify_history = 0;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.revarg_opt = REVARG_COMMITTISH;
	cmd_log_init(argc, argv, prefix, &rev, &opt);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return cmd_log_walk(&rev);
}

static void show_tagger(char *buf, int len, struct rev_info *rev)
{
	struct strbuf out = STRBUF_INIT;
	struct pretty_print_context pp = {0};

	pp.fmt = rev->commit_format;
	pp.date_mode = rev->date_mode;
	pp_user_info(&pp, "Tagger", &out, buf, get_log_output_encoding());
	printf("%s", out.buf);
	strbuf_release(&out);
}

static int show_blob_object(const unsigned char *sha1, struct rev_info *rev)
{
	fflush(stdout);
	return stream_blob_to_fd(1, sha1, NULL, 0);
}

static int show_tag_object(const unsigned char *sha1, struct rev_info *rev)
{
	unsigned long size;
	enum object_type type;
	char *buf = read_sha1_file(sha1, &type, &size);
	int offset = 0;

	if (!buf)
		return error(_("Could not read object %s"), sha1_to_hex(sha1));

	assert(type == OBJ_TAG);
	while (offset < size && buf[offset] != '\n') {
		int new_offset = offset + 1;
		while (new_offset < size && buf[new_offset++] != '\n')
			; /* do nothing */
		if (!prefixcmp(buf + offset, "tagger "))
			show_tagger(buf + offset + 7,
				    new_offset - offset - 7, rev);
		offset = new_offset;
	}

	if (offset < size)
		fwrite(buf + offset, size - offset, 1, stdout);
	free(buf);
	return 0;
}

static int show_tree_object(const unsigned char *sha1,
		const char *base, int baselen,
		const char *pathname, unsigned mode, int stage, void *context)
{
	printf("%s%s\n", pathname, S_ISDIR(mode) ? "/" : "");
	return 0;
}

static void show_rev_tweak_rev(struct rev_info *rev, struct setup_revision_opt *opt)
{
	if (rev->ignore_merges) {
		/* There was no "-m" on the command line */
		rev->ignore_merges = 0;
		if (!rev->first_parent_only && !rev->combine_merges) {
			/* No "--first-parent", "-c", nor "--cc" */
			rev->combine_merges = 1;
			rev->dense_combined_merges = 1;
		}
	}
	if (!rev->diffopt.output_format)
		rev->diffopt.output_format = DIFF_FORMAT_PATCH;
}

int cmd_show(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct object_array_entry *objects;
	struct setup_revision_opt opt;
	struct pathspec match_all;
	int i, count, ret = 0;

	init_grep_defaults();
	git_config(git_log_config, NULL);

	init_pathspec(&match_all, NULL);
	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.always_show_header = 1;
	rev.no_walk = REVISION_WALK_NO_WALK_SORTED;
	rev.diffopt.stat_width = -1; 	/* Scale to real terminal size */

	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.tweak = show_rev_tweak_rev;
	cmd_log_init(argc, argv, prefix, &rev, &opt);

	if (!rev.no_walk)
		return cmd_log_walk(&rev);

	count = rev.pending.nr;
	objects = rev.pending.objects;
	for (i = 0; i < count && !ret; i++) {
		struct object *o = objects[i].item;
		const char *name = objects[i].name;
		switch (o->type) {
		case OBJ_BLOB:
			ret = show_blob_object(o->sha1, NULL);
			break;
		case OBJ_TAG: {
			struct tag *t = (struct tag *)o;

			if (rev.shown_one)
				putchar('\n');
			printf("%stag %s%s\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					t->tag,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			ret = show_tag_object(o->sha1, &rev);
			rev.shown_one = 1;
			if (ret)
				break;
			o = parse_object(t->tagged->sha1);
			if (!o)
				ret = error(_("Could not read object %s"),
					    sha1_to_hex(t->tagged->sha1));
			objects[i].item = o;
			i--;
			break;
		}
		case OBJ_TREE:
			if (rev.shown_one)
				putchar('\n');
			printf("%stree %s%s\n\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					name,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			read_tree_recursive((struct tree *)o, "", 0, 0, &match_all,
					show_tree_object, NULL);
			rev.shown_one = 1;
			break;
		case OBJ_COMMIT:
			rev.pending.nr = rev.pending.alloc = 0;
			rev.pending.objects = NULL;
			add_object_array(o, name, &rev.pending);
			ret = cmd_log_walk(&rev);
			break;
		default:
			ret = error(_("Unknown type: %d"), o->type);
		}
	}
	free(objects);
	return ret;
}

/*
 * This is equivalent to "git log -g --abbrev-commit --pretty=oneline"
 */
int cmd_log_reflog(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_grep_defaults();
	git_config(git_log_config, NULL);

	init_revisions(&rev, prefix);
	init_reflog_walk(&rev.reflog_info);
	rev.verbose_header = 1;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	cmd_log_init_defaults(&rev);
	rev.abbrev_commit = 1;
	rev.commit_format = CMIT_FMT_ONELINE;
	rev.use_terminator = 1;
	rev.always_show_header = 1;
	cmd_log_init_finish(argc, argv, prefix, &rev, &opt);

	return cmd_log_walk(&rev);
}

int cmd_log(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_grep_defaults();
	git_config(git_log_config, NULL);

	init_revisions(&rev, prefix);
	rev.always_show_header = 1;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.revarg_opt = REVARG_COMMITTISH;
	cmd_log_init(argc, argv, prefix, &rev, &opt);
	return cmd_log_walk(&rev);
}

/* format-patch */

static const char *fmt_patch_suffix = ".patch";
static int numbered = 0;
static int auto_number = 1;

static char *default_attach = NULL;

static struct string_list extra_hdr;
static struct string_list extra_to;
static struct string_list extra_cc;

static void add_header(const char *value)
{
	struct string_list_item *item;
	int len = strlen(value);
	while (len && value[len - 1] == '\n')
		len--;

	if (!strncasecmp(value, "to: ", 4)) {
		item = string_list_append(&extra_to, value + 4);
		len -= 4;
	} else if (!strncasecmp(value, "cc: ", 4)) {
		item = string_list_append(&extra_cc, value + 4);
		len -= 4;
	} else {
		item = string_list_append(&extra_hdr, value);
	}

	item->string[len] = '\0';
}

#define THREAD_SHALLOW 1
#define THREAD_DEEP 2
static int thread;
static int do_signoff;
static const char *signature = git_version_string;

static int git_format_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "format.headers")) {
		if (!value)
			die(_("format.headers without value"));
		add_header(value);
		return 0;
	}
	if (!strcmp(var, "format.suffix"))
		return git_config_string(&fmt_patch_suffix, var, value);
	if (!strcmp(var, "format.to")) {
		if (!value)
			return config_error_nonbool(var);
		string_list_append(&extra_to, value);
		return 0;
	}
	if (!strcmp(var, "format.cc")) {
		if (!value)
			return config_error_nonbool(var);
		string_list_append(&extra_cc, value);
		return 0;
	}
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff") ||
	    !strcmp(var, "color.ui")) {
		return 0;
	}
	if (!strcmp(var, "format.numbered")) {
		if (value && !strcasecmp(value, "auto")) {
			auto_number = 1;
			return 0;
		}
		numbered = git_config_bool(var, value);
		auto_number = auto_number && numbered;
		return 0;
	}
	if (!strcmp(var, "format.attach")) {
		if (value && *value)
			default_attach = xstrdup(value);
		else
			default_attach = xstrdup(git_version_string);
		return 0;
	}
	if (!strcmp(var, "format.thread")) {
		if (value && !strcasecmp(value, "deep")) {
			thread = THREAD_DEEP;
			return 0;
		}
		if (value && !strcasecmp(value, "shallow")) {
			thread = THREAD_SHALLOW;
			return 0;
		}
		thread = git_config_bool(var, value) && THREAD_SHALLOW;
		return 0;
	}
	if (!strcmp(var, "format.signoff")) {
		do_signoff = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "format.signature"))
		return git_config_string(&signature, var, value);

	return git_log_config(var, value, cb);
}

static FILE *realstdout = NULL;
static const char *output_directory = NULL;
static int outdir_offset;

static int reopen_stdout(struct commit *commit, const char *subject,
			 struct rev_info *rev, int quiet)
{
	struct strbuf filename = STRBUF_INIT;
	int suffix_len = strlen(fmt_patch_suffix) + 1;

	if (output_directory) {
		strbuf_addstr(&filename, output_directory);
		if (filename.len >=
		    PATH_MAX - FORMAT_PATCH_NAME_MAX - suffix_len)
			return error(_("name of output directory is too long"));
		if (filename.buf[filename.len - 1] != '/')
			strbuf_addch(&filename, '/');
	}

	get_patch_filename(commit, subject, rev->nr, fmt_patch_suffix, &filename);

	if (!quiet)
		fprintf(realstdout, "%s\n", filename.buf + outdir_offset);

	if (freopen(filename.buf, "w", stdout) == NULL)
		return error(_("Cannot open patch file %s"), filename.buf);

	strbuf_release(&filename);
	return 0;
}

static void get_patch_ids(struct rev_info *rev, struct patch_ids *ids)
{
	struct rev_info check_rev;
	struct commit *commit;
	struct object *o1, *o2;
	unsigned flags1, flags2;

	if (rev->pending.nr != 2)
		die(_("Need exactly one range."));

	o1 = rev->pending.objects[0].item;
	flags1 = o1->flags;
	o2 = rev->pending.objects[1].item;
	flags2 = o2->flags;

	if ((flags1 & UNINTERESTING) == (flags2 & UNINTERESTING))
		die(_("Not a range."));

	init_patch_ids(ids);

	/* given a range a..b get all patch ids for b..a */
	init_revisions(&check_rev, rev->prefix);
	check_rev.max_parents = 1;
	o1->flags ^= UNINTERESTING;
	o2->flags ^= UNINTERESTING;
	add_pending_object(&check_rev, o1, "o1");
	add_pending_object(&check_rev, o2, "o2");
	if (prepare_revision_walk(&check_rev))
		die(_("revision walk setup failed"));

	while ((commit = get_revision(&check_rev)) != NULL) {
		add_commit_patch_id(commit, ids);
	}

	/* reset for next revision walk */
	clear_commit_marks((struct commit *)o1,
			SEEN | UNINTERESTING | SHOWN | ADDED);
	clear_commit_marks((struct commit *)o2,
			SEEN | UNINTERESTING | SHOWN | ADDED);
	o1->flags = flags1;
	o2->flags = flags2;
}

static void gen_message_id(struct rev_info *info, char *base)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_addf(&buf, "%s.%lu.git.%s", base,
		    (unsigned long) time(NULL),
		    git_committer_info(IDENT_NO_NAME|IDENT_NO_DATE|IDENT_STRICT));
	info->message_id = strbuf_detach(&buf, NULL);
}

static void print_signature(void)
{
	if (signature && *signature)
		printf("-- \n%s\n\n", signature);
}

static void add_branch_description(struct strbuf *buf, const char *branch_name)
{
	struct strbuf desc = STRBUF_INIT;
	if (!branch_name || !*branch_name)
		return;
	read_branch_desc(&desc, branch_name);
	if (desc.len) {
		strbuf_addch(buf, '\n');
		strbuf_add(buf, desc.buf, desc.len);
		strbuf_addch(buf, '\n');
	}
}

static void make_cover_letter(struct rev_info *rev, int use_stdout,
			      int numbered, int numbered_files,
			      struct commit *origin,
			      int nr, struct commit **list, struct commit *head,
			      const char *branch_name,
			      int quiet)
{
	const char *committer;
	const char *body = "*** SUBJECT HERE ***\n\n*** BLURB HERE ***\n";
	const char *msg;
	struct shortlog log;
	struct strbuf sb = STRBUF_INIT;
	int i;
	const char *encoding = "UTF-8";
	struct diff_options opts;
	int need_8bit_cte = 0;
	struct pretty_print_context pp = {0};

	if (rev->commit_format != CMIT_FMT_EMAIL)
		die(_("Cover letter needs email format"));

	committer = git_committer_info(0);

	if (!use_stdout &&
	    reopen_stdout(NULL, numbered_files ? NULL : "cover-letter", rev, quiet))
		return;

	log_write_email_headers(rev, head, &pp.subject, &pp.after_subject,
				&need_8bit_cte);

	for (i = 0; !need_8bit_cte && i < nr; i++)
		if (has_non_ascii(list[i]->buffer))
			need_8bit_cte = 1;

	msg = body;
	pp.fmt = CMIT_FMT_EMAIL;
	pp.date_mode = DATE_RFC2822;
	pp_user_info(&pp, NULL, &sb, committer, encoding);
	pp_title_line(&pp, &msg, &sb, encoding, need_8bit_cte);
	pp_remainder(&pp, &msg, &sb, 0);
	add_branch_description(&sb, branch_name);
	printf("%s\n", sb.buf);

	strbuf_release(&sb);

	shortlog_init(&log);
	log.wrap_lines = 1;
	log.wrap = 72;
	log.in1 = 2;
	log.in2 = 4;
	for (i = 0; i < nr; i++)
		shortlog_add_commit(&log, list[i]);

	shortlog_output(&log);

	/*
	 * We can only do diffstat with a unique reference point
	 */
	if (!origin)
		return;

	memcpy(&opts, &rev->diffopt, sizeof(opts));
	opts.output_format = DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;

	diff_setup_done(&opts);

	diff_tree_sha1(origin->tree->object.sha1,
		       head->tree->object.sha1,
		       "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);

	printf("\n");
	print_signature();
}

static const char *clean_message_id(const char *msg_id)
{
	char ch;
	const char *a, *z, *m;

	m = msg_id;
	while ((ch = *m) && (isspace(ch) || (ch == '<')))
		m++;
	a = m;
	z = NULL;
	while ((ch = *m)) {
		if (!isspace(ch) && (ch != '>'))
			z = m;
		m++;
	}
	if (!z)
		die(_("insane in-reply-to: %s"), msg_id);
	if (++z == m)
		return a;
	return xmemdupz(a, z - a);
}

static const char *set_outdir(const char *prefix, const char *output_directory)
{
	if (output_directory && is_absolute_path(output_directory))
		return output_directory;

	if (!prefix || !*prefix) {
		if (output_directory)
			return output_directory;
		/* The user did not explicitly ask for "./" */
		outdir_offset = 2;
		return "./";
	}

	outdir_offset = strlen(prefix);
	if (!output_directory)
		return prefix;

	return xstrdup(prefix_filename(prefix, outdir_offset,
				       output_directory));
}

static const char * const builtin_format_patch_usage[] = {
	N_("git format-patch [options] [<since> | <revision range>]"),
	NULL
};

static int keep_subject = 0;

static int keep_callback(const struct option *opt, const char *arg, int unset)
{
	((struct rev_info *)opt->value)->total = -1;
	keep_subject = 1;
	return 0;
}

static int subject_prefix = 0;

static int subject_prefix_callback(const struct option *opt, const char *arg,
			    int unset)
{
	subject_prefix = 1;
	((struct rev_info *)opt->value)->subject_prefix = arg;
	return 0;
}

static int numbered_cmdline_opt = 0;

static int numbered_callback(const struct option *opt, const char *arg,
			     int unset)
{
	*(int *)opt->value = numbered_cmdline_opt = unset ? 0 : 1;
	if (unset)
		auto_number =  0;
	return 0;
}

static int no_numbered_callback(const struct option *opt, const char *arg,
				int unset)
{
	return numbered_callback(opt, arg, 1);
}

static int output_directory_callback(const struct option *opt, const char *arg,
			      int unset)
{
	const char **dir = (const char **)opt->value;
	if (*dir)
		die(_("Two output directories?"));
	*dir = arg;
	return 0;
}

static int thread_callback(const struct option *opt, const char *arg, int unset)
{
	int *thread = (int *)opt->value;
	if (unset)
		*thread = 0;
	else if (!arg || !strcmp(arg, "shallow"))
		*thread = THREAD_SHALLOW;
	else if (!strcmp(arg, "deep"))
		*thread = THREAD_DEEP;
	else
		return 1;
	return 0;
}

static int attach_callback(const struct option *opt, const char *arg, int unset)
{
	struct rev_info *rev = (struct rev_info *)opt->value;
	if (unset)
		rev->mime_boundary = NULL;
	else if (arg)
		rev->mime_boundary = arg;
	else
		rev->mime_boundary = git_version_string;
	rev->no_inline = unset ? 0 : 1;
	return 0;
}

static int inline_callback(const struct option *opt, const char *arg, int unset)
{
	struct rev_info *rev = (struct rev_info *)opt->value;
	if (unset)
		rev->mime_boundary = NULL;
	else if (arg)
		rev->mime_boundary = arg;
	else
		rev->mime_boundary = git_version_string;
	rev->no_inline = 0;
	return 0;
}

static int header_callback(const struct option *opt, const char *arg, int unset)
{
	if (unset) {
		string_list_clear(&extra_hdr, 0);
		string_list_clear(&extra_to, 0);
		string_list_clear(&extra_cc, 0);
	} else {
	    add_header(arg);
	}
	return 0;
}

static int to_callback(const struct option *opt, const char *arg, int unset)
{
	if (unset)
		string_list_clear(&extra_to, 0);
	else
		string_list_append(&extra_to, arg);
	return 0;
}

static int cc_callback(const struct option *opt, const char *arg, int unset)
{
	if (unset)
		string_list_clear(&extra_cc, 0);
	else
		string_list_append(&extra_cc, arg);
	return 0;
}

static char *find_branch_name(struct rev_info *rev)
{
	int i, positive = -1;
	unsigned char branch_sha1[20];
	struct strbuf buf = STRBUF_INIT;
	const char *branch;

	for (i = 0; i < rev->cmdline.nr; i++) {
		if (rev->cmdline.rev[i].flags & UNINTERESTING)
			continue;
		if (positive < 0)
			positive = i;
		else
			return NULL;
	}
	if (positive < 0)
		return NULL;
	strbuf_addf(&buf, "refs/heads/%s", rev->cmdline.rev[positive].name);
	branch = resolve_ref_unsafe(buf.buf, branch_sha1, 1, NULL);
	if (!branch ||
	    prefixcmp(branch, "refs/heads/") ||
	    hashcmp(rev->cmdline.rev[positive].item->sha1, branch_sha1))
		branch = NULL;
	strbuf_release(&buf);
	if (branch)
		return xstrdup(rev->cmdline.rev[positive].name);
	return NULL;
}

int cmd_format_patch(int argc, const char **argv, const char *prefix)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	struct setup_revision_opt s_r_opt;
	int nr = 0, total, i;
	int use_stdout = 0;
	int start_number = -1;
	int numbered_files = 0;		/* _just_ numbers */
	int ignore_if_in_upstream = 0;
	int cover_letter = 0;
	int boundary_count = 0;
	int no_binary_diff = 0;
	struct commit *origin = NULL, *head = NULL;
	const char *in_reply_to = NULL;
	struct patch_ids ids;
	char *add_signoff = NULL;
	struct strbuf buf = STRBUF_INIT;
	int use_patch_format = 0;
	int quiet = 0;
	char *branch_name = NULL;
	const struct option builtin_format_patch_options[] = {
		{ OPTION_CALLBACK, 'n', "numbered", &numbered, NULL,
			    N_("use [PATCH n/m] even with a single patch"),
			    PARSE_OPT_NOARG, numbered_callback },
		{ OPTION_CALLBACK, 'N', "no-numbered", &numbered, NULL,
			    N_("use [PATCH] even with multiple patches"),
			    PARSE_OPT_NOARG, no_numbered_callback },
		OPT_BOOLEAN('s', "signoff", &do_signoff, N_("add Signed-off-by:")),
		OPT_BOOLEAN(0, "stdout", &use_stdout,
			    N_("print patches to standard out")),
		OPT_BOOLEAN(0, "cover-letter", &cover_letter,
			    N_("generate a cover letter")),
		OPT_BOOLEAN(0, "numbered-files", &numbered_files,
			    N_("use simple number sequence for output file names")),
		OPT_STRING(0, "suffix", &fmt_patch_suffix, N_("sfx"),
			    N_("use <sfx> instead of '.patch'")),
		OPT_INTEGER(0, "start-number", &start_number,
			    N_("start numbering patches at <n> instead of 1")),
		{ OPTION_CALLBACK, 0, "subject-prefix", &rev, N_("prefix"),
			    N_("Use [<prefix>] instead of [PATCH]"),
			    PARSE_OPT_NONEG, subject_prefix_callback },
		{ OPTION_CALLBACK, 'o', "output-directory", &output_directory,
			    N_("dir"), N_("store resulting files in <dir>"),
			    PARSE_OPT_NONEG, output_directory_callback },
		{ OPTION_CALLBACK, 'k', "keep-subject", &rev, NULL,
			    N_("don't strip/add [PATCH]"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, keep_callback },
		OPT_BOOLEAN(0, "no-binary", &no_binary_diff,
			    N_("don't output binary diffs")),
		OPT_BOOLEAN(0, "ignore-if-in-upstream", &ignore_if_in_upstream,
			    N_("don't include a patch matching a commit upstream")),
		{ OPTION_BOOLEAN, 'p', "no-stat", &use_patch_format, NULL,
		  N_("show patch format instead of default (patch + stat)"),
		  PARSE_OPT_NONEG | PARSE_OPT_NOARG },
		OPT_GROUP(N_("Messaging")),
		{ OPTION_CALLBACK, 0, "add-header", NULL, N_("header"),
			    N_("add email header"), 0, header_callback },
		{ OPTION_CALLBACK, 0, "to", NULL, N_("email"), N_("add To: header"),
			    0, to_callback },
		{ OPTION_CALLBACK, 0, "cc", NULL, N_("email"), N_("add Cc: header"),
			    0, cc_callback },
		OPT_STRING(0, "in-reply-to", &in_reply_to, N_("message-id"),
			    N_("make first mail a reply to <message-id>")),
		{ OPTION_CALLBACK, 0, "attach", &rev, N_("boundary"),
			    N_("attach the patch"), PARSE_OPT_OPTARG,
			    attach_callback },
		{ OPTION_CALLBACK, 0, "inline", &rev, N_("boundary"),
			    N_("inline the patch"),
			    PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			    inline_callback },
		{ OPTION_CALLBACK, 0, "thread", &thread, N_("style"),
			    N_("enable message threading, styles: shallow, deep"),
			    PARSE_OPT_OPTARG, thread_callback },
		OPT_STRING(0, "signature", &signature, N_("signature"),
			    N_("add a signature")),
		OPT_BOOLEAN(0, "quiet", &quiet,
			    N_("don't print the patch filenames")),
		OPT_END()
	};

	extra_hdr.strdup_strings = 1;
	extra_to.strdup_strings = 1;
	extra_cc.strdup_strings = 1;
	init_grep_defaults();
	git_config(git_format_config, NULL);
	init_revisions(&rev, prefix);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.max_parents = 1;
	DIFF_OPT_SET(&rev.diffopt, RECURSIVE);
	rev.subject_prefix = fmt_patch_subject_prefix;
	memset(&s_r_opt, 0, sizeof(s_r_opt));
	s_r_opt.def = "HEAD";
	s_r_opt.revarg_opt = REVARG_COMMITTISH;

	if (default_attach) {
		rev.mime_boundary = default_attach;
		rev.no_inline = 1;
	}

	/*
	 * Parse the arguments before setup_revisions(), or something
	 * like "git format-patch -o a123 HEAD^.." may fail; a123 is
	 * possibly a valid SHA1.
	 */
	argc = parse_options(argc, argv, prefix, builtin_format_patch_options,
			     builtin_format_patch_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN |
			     PARSE_OPT_KEEP_DASHDASH);

	if (do_signoff) {
		const char *committer;
		const char *endpos;
		committer = git_committer_info(IDENT_STRICT);
		endpos = strchr(committer, '>');
		if (!endpos)
			die(_("bogus committer info %s"), committer);
		add_signoff = xmemdupz(committer, endpos - committer + 1);
	}

	for (i = 0; i < extra_hdr.nr; i++) {
		strbuf_addstr(&buf, extra_hdr.items[i].string);
		strbuf_addch(&buf, '\n');
	}

	if (extra_to.nr)
		strbuf_addstr(&buf, "To: ");
	for (i = 0; i < extra_to.nr; i++) {
		if (i)
			strbuf_addstr(&buf, "    ");
		strbuf_addstr(&buf, extra_to.items[i].string);
		if (i + 1 < extra_to.nr)
			strbuf_addch(&buf, ',');
		strbuf_addch(&buf, '\n');
	}

	if (extra_cc.nr)
		strbuf_addstr(&buf, "Cc: ");
	for (i = 0; i < extra_cc.nr; i++) {
		if (i)
			strbuf_addstr(&buf, "    ");
		strbuf_addstr(&buf, extra_cc.items[i].string);
		if (i + 1 < extra_cc.nr)
			strbuf_addch(&buf, ',');
		strbuf_addch(&buf, '\n');
	}

	rev.extra_headers = strbuf_detach(&buf, NULL);

	if (start_number < 0)
		start_number = 1;

	/*
	 * If numbered is set solely due to format.numbered in config,
	 * and it would conflict with --keep-subject (-k) from the
	 * command line, reset "numbered".
	 */
	if (numbered && keep_subject && !numbered_cmdline_opt)
		numbered = 0;

	if (numbered && keep_subject)
		die (_("-n and -k are mutually exclusive."));
	if (keep_subject && subject_prefix)
		die (_("--subject-prefix and -k are mutually exclusive."));
	rev.preserve_subject = keep_subject;

	argc = setup_revisions(argc, argv, &rev, &s_r_opt);
	if (argc > 1)
		die (_("unrecognized argument: %s"), argv[1]);

	if (rev.diffopt.output_format & DIFF_FORMAT_NAME)
		die(_("--name-only does not make sense"));
	if (rev.diffopt.output_format & DIFF_FORMAT_NAME_STATUS)
		die(_("--name-status does not make sense"));
	if (rev.diffopt.output_format & DIFF_FORMAT_CHECKDIFF)
		die(_("--check does not make sense"));

	if (!use_patch_format &&
		(!rev.diffopt.output_format ||
		 rev.diffopt.output_format == DIFF_FORMAT_PATCH))
		rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_SUMMARY;

	/* Always generate a patch */
	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;

	if (!DIFF_OPT_TST(&rev.diffopt, TEXT) && !no_binary_diff)
		DIFF_OPT_SET(&rev.diffopt, BINARY);

	if (rev.show_notes)
		init_display_notes(&rev.notes_opt);

	if (!use_stdout)
		output_directory = set_outdir(prefix, output_directory);
	else
		setup_pager();

	if (output_directory) {
		if (use_stdout)
			die(_("standard output, or directory, which one?"));
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die_errno(_("Could not create directory '%s'"),
				  output_directory);
	}

	if (rev.pending.nr == 1) {
		if (rev.max_count < 0 && !rev.show_root_diff) {
			/*
			 * This is traditional behaviour of "git format-patch
			 * origin" that prepares what the origin side still
			 * does not have.
			 */
			unsigned char sha1[20];
			const char *ref;

			rev.pending.objects[0].item->flags |= UNINTERESTING;
			add_head_to_pending(&rev);
			ref = resolve_ref_unsafe("HEAD", sha1, 1, NULL);
			if (ref && !prefixcmp(ref, "refs/heads/"))
				branch_name = xstrdup(ref + strlen("refs/heads/"));
			else
				branch_name = xstrdup(""); /* no branch */
		}
		/*
		 * Otherwise, it is "format-patch -22 HEAD", and/or
		 * "format-patch --root HEAD".  The user wants
		 * get_revision() to do the usual traversal.
		 */
	}

	/*
	 * We cannot move this anywhere earlier because we do want to
	 * know if --root was given explicitly from the command line.
	 */
	rev.show_root_diff = 1;

	if (cover_letter) {
		/*
		 * NEEDSWORK:randomly pick one positive commit to show
		 * diffstat; this is often the tip and the command
		 * happens to do the right thing in most cases, but a
		 * complex command like "--cover-letter a b c ^bottom"
		 * picks "c" and shows diffstat between bottom..c
		 * which may not match what the series represents at
		 * all and totally broken.
		 */
		int i;
		for (i = 0; i < rev.pending.nr; i++) {
			struct object *o = rev.pending.objects[i].item;
			if (!(o->flags & UNINTERESTING))
				head = (struct commit *)o;
		}
		/* There is nothing to show; it is not an error, though. */
		if (!head)
			return 0;
		if (!branch_name)
			branch_name = find_branch_name(&rev);
	}

	if (ignore_if_in_upstream) {
		/* Don't say anything if head and upstream are the same. */
		if (rev.pending.nr == 2) {
			struct object_array_entry *o = rev.pending.objects;
			if (hashcmp(o[0].item->sha1, o[1].item->sha1) == 0)
				return 0;
		}
		get_patch_ids(&rev, &ids);
	}

	if (!use_stdout)
		realstdout = xfdopen(xdup(1), "w");

	if (prepare_revision_walk(&rev))
		die(_("revision walk setup failed"));
	rev.boundary = 1;
	while ((commit = get_revision(&rev)) != NULL) {
		if (commit->object.flags & BOUNDARY) {
			boundary_count++;
			origin = (boundary_count == 1) ? commit : NULL;
			continue;
		}

		if (ignore_if_in_upstream &&
				has_commit_patch_id(commit, &ids))
			continue;

		nr++;
		list = xrealloc(list, nr * sizeof(list[0]));
		list[nr - 1] = commit;
	}
	total = nr;
	if (!keep_subject && auto_number && total > 1)
		numbered = 1;
	if (numbered)
		rev.total = total + start_number - 1;
	if (in_reply_to || thread || cover_letter)
		rev.ref_message_ids = xcalloc(1, sizeof(struct string_list));
	if (in_reply_to) {
		const char *msgid = clean_message_id(in_reply_to);
		string_list_append(rev.ref_message_ids, msgid);
	}
	rev.numbered_files = numbered_files;
	rev.patch_suffix = fmt_patch_suffix;
	if (cover_letter) {
		if (thread)
			gen_message_id(&rev, "cover");
		make_cover_letter(&rev, use_stdout, numbered, numbered_files,
				  origin, nr, list, head, branch_name, quiet);
		total++;
		start_number--;
	}
	rev.add_signoff = add_signoff;
	while (0 <= --nr) {
		int shown;
		commit = list[nr];
		rev.nr = total - nr + (start_number - 1);
		/* Make the second and subsequent mails replies to the first */
		if (thread) {
			/* Have we already had a message ID? */
			if (rev.message_id) {
				/*
				 * For deep threading: make every mail
				 * a reply to the previous one, no
				 * matter what other options are set.
				 *
				 * For shallow threading:
				 *
				 * Without --cover-letter and
				 * --in-reply-to, make every mail a
				 * reply to the one before.
				 *
				 * With --in-reply-to but no
				 * --cover-letter, make every mail a
				 * reply to the <reply-to>.
				 *
				 * With --cover-letter, make every
				 * mail but the cover letter a reply
				 * to the cover letter.  The cover
				 * letter is a reply to the
				 * --in-reply-to, if specified.
				 */
				if (thread == THREAD_SHALLOW
				    && rev.ref_message_ids->nr > 0
				    && (!cover_letter || rev.nr > 1))
					free(rev.message_id);
				else
					string_list_append(rev.ref_message_ids,
							   rev.message_id);
			}
			gen_message_id(&rev, sha1_to_hex(commit->object.sha1));
		}

		if (!use_stdout &&
		    reopen_stdout(numbered_files ? NULL : commit, NULL, &rev, quiet))
			die(_("Failed to create output files"));
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
				print_signature();
		}
		if (!use_stdout)
			fclose(stdout);
	}
	free(list);
	free(branch_name);
	string_list_clear(&extra_to, 0);
	string_list_clear(&extra_cc, 0);
	string_list_clear(&extra_hdr, 0);
	if (ignore_if_in_upstream)
		free_patch_ids(&ids);
	return 0;
}

static int add_pending_commit(const char *arg, struct rev_info *revs, int flags)
{
	unsigned char sha1[20];
	if (get_sha1(arg, sha1) == 0) {
		struct commit *commit = lookup_commit_reference(sha1);
		if (commit) {
			commit->object.flags |= flags;
			add_pending_object(revs, &commit->object, arg);
			return 0;
		}
	}
	return -1;
}

static const char * const cherry_usage[] = {
	N_("git cherry [-v] [<upstream> [<head> [<limit>]]]"),
	NULL
};

static void print_commit(char sign, struct commit *commit, int verbose,
			 int abbrev)
{
	if (!verbose) {
		printf("%c %s\n", sign,
		       find_unique_abbrev(commit->object.sha1, abbrev));
	} else {
		struct strbuf buf = STRBUF_INIT;
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &buf);
		printf("%c %s %s\n", sign,
		       find_unique_abbrev(commit->object.sha1, abbrev),
		       buf.buf);
		strbuf_release(&buf);
	}
}

int cmd_cherry(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct patch_ids ids;
	struct commit *commit;
	struct commit_list *list = NULL;
	struct branch *current_branch;
	const char *upstream;
	const char *head = "HEAD";
	const char *limit = NULL;
	int verbose = 0, abbrev = 0;

	struct option options[] = {
		OPT__ABBREV(&abbrev),
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, cherry_usage, 0);

	switch (argc) {
	case 3:
		limit = argv[2];
		/* FALLTHROUGH */
	case 2:
		head = argv[1];
		/* FALLTHROUGH */
	case 1:
		upstream = argv[0];
		break;
	default:
		current_branch = branch_get(NULL);
		if (!current_branch || !current_branch->merge
					|| !current_branch->merge[0]
					|| !current_branch->merge[0]->dst) {
			fprintf(stderr, _("Could not find a tracked"
					" remote branch, please"
					" specify <upstream> manually.\n"));
			usage_with_options(cherry_usage, options);
		}

		upstream = current_branch->merge[0]->dst;
	}

	init_revisions(&revs, prefix);
	revs.max_parents = 1;

	if (add_pending_commit(head, &revs, 0))
		die(_("Unknown commit %s"), head);
	if (add_pending_commit(upstream, &revs, UNINTERESTING))
		die(_("Unknown commit %s"), upstream);

	/* Don't say anything if head and upstream are the same. */
	if (revs.pending.nr == 2) {
		struct object_array_entry *o = revs.pending.objects;
		if (hashcmp(o[0].item->sha1, o[1].item->sha1) == 0)
			return 0;
	}

	get_patch_ids(&revs, &ids);

	if (limit && add_pending_commit(limit, &revs, UNINTERESTING))
		die(_("Unknown commit %s"), limit);

	/* reverse the list of commits */
	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));
	while ((commit = get_revision(&revs)) != NULL) {
		commit_list_insert(commit, &list);
	}

	while (list) {
		char sign = '+';

		commit = list->item;
		if (has_commit_patch_id(commit, &ids))
			sign = '-';
		print_commit(sign, commit, verbose, abbrev);
		list = list->next;
	}

	free_patch_ids(&ids);
	return 0;
}
