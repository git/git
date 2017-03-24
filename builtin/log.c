/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "cache.h"
#include "refs.h"
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
#include "line-log.h"
#include "branch.h"
#include "streaming.h"
#include "version.h"
#include "mailmap.h"
#include "gpg-interface.h"

/* Set a default date-time format for git log ("log.date" config variable) */
static const char *default_date_mode = NULL;

static int default_abbrev_commit;
static int default_show_root = 1;
static int default_follow;
static int default_show_signature;
static int decoration_style;
static int decoration_given;
static int use_mailmap_config;
static const char *fmt_patch_subject_prefix = "PATCH";
static const char *fmt_pretty;

static const char * const builtin_log_usage[] = {
	N_("git log [<options>] [<revision-range>] [[--] <path>...]"),
	N_("git show [<options>] <object>..."),
	NULL
};

struct line_opt_callback_data {
	struct rev_info *rev;
	const char *prefix;
	struct string_list args;
};

static int auto_decoration_style(void)
{
	return (isatty(1) || pager_in_use()) ? DECORATE_SHORT_REFS : 0;
}

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
	else if (!strcmp(value, "auto"))
		return auto_decoration_style();
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
		die(_("invalid --decorate option: %s"), arg);

	decoration_given = 1;

	return 0;
}

static int log_line_range_callback(const struct option *option, const char *arg, int unset)
{
	struct line_opt_callback_data *data = option->value;

	if (!arg)
		return -1;

	data->rev->line_level_traverse = 1;
	string_list_append(&data->args, arg);

	return 0;
}

static void init_log_defaults(void)
{
	init_grep_defaults();
	init_diff_ui_defaults();
}

static void cmd_log_init_defaults(struct rev_info *rev)
{
	if (fmt_pretty)
		get_commit_format(fmt_pretty, rev);
	if (default_follow)
		DIFF_OPT_SET(&rev->diffopt, DEFAULT_FOLLOW_RENAMES);
	rev->verbose_header = 1;
	DIFF_OPT_SET(&rev->diffopt, RECURSIVE);
	rev->diffopt.stat_width = -1; /* use full terminal width */
	rev->diffopt.stat_graph_width = -1; /* respect statGraphWidth config */
	rev->abbrev_commit = default_abbrev_commit;
	rev->show_root_diff = default_show_root;
	rev->subject_prefix = fmt_patch_subject_prefix;
	rev->show_signature = default_show_signature;
	DIFF_OPT_SET(&rev->diffopt, ALLOW_TEXTCONV);

	if (default_date_mode)
		parse_date_format(default_date_mode, &rev->date_mode);
	rev->diffopt.touched_flags = 0;
}

static void cmd_log_init_finish(int argc, const char **argv, const char *prefix,
			 struct rev_info *rev, struct setup_revision_opt *opt)
{
	struct userformat_want w;
	int quiet = 0, source = 0, mailmap = 0;
	static struct line_opt_callback_data line_cb = {NULL, NULL, STRING_LIST_INIT_DUP};

	const struct option builtin_log_options[] = {
		OPT__QUIET(&quiet, N_("suppress diff output")),
		OPT_BOOL(0, "source", &source, N_("show source")),
		OPT_BOOL(0, "use-mailmap", &mailmap, N_("Use mail map file")),
		{ OPTION_CALLBACK, 0, "decorate", NULL, NULL, N_("decorate options"),
		  PARSE_OPT_OPTARG, decorate_callback},
		OPT_CALLBACK('L', NULL, &line_cb, "n,m:file",
			     N_("Process line range n,m in file, counting from 1"),
			     log_line_range_callback),
		OPT_END()
	};

	line_cb.rev = rev;
	line_cb.prefix = prefix;

	mailmap = use_mailmap_config;
	argc = parse_options(argc, argv, prefix,
			     builtin_log_options, builtin_log_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN |
			     PARSE_OPT_KEEP_DASHDASH);

	if (quiet)
		rev->diffopt.output_format |= DIFF_FORMAT_NO_OUTPUT;
	argc = setup_revisions(argc, argv, rev, opt);

	/* Any arguments at this point are not recognized */
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);

	memset(&w, 0, sizeof(w));
	userformat_find_requirements(NULL, &w);

	if (!rev->show_notes_given && (!rev->pretty_given || w.notes))
		rev->show_notes = 1;
	if (rev->show_notes)
		init_display_notes(&rev->notes_opt);

	if (rev->diffopt.pickaxe || rev->diffopt.filter ||
	    DIFF_OPT_TST(&rev->diffopt, FOLLOW_RENAMES))
		rev->always_show_header = 0;

	if (source)
		rev->show_source = 1;

	if (mailmap) {
		rev->mailmap = xcalloc(1, sizeof(struct string_list));
		read_mailmap(rev->mailmap, NULL);
	}

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

	if (rev->line_level_traverse)
		line_log_init(rev, line_cb.prefix, &line_cb.args);

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
	fprintf(rev->diffopt.file, _("Final output: %d %s\n"), nr, stage);
}

static struct itimerval early_output_timer;

static void log_show_early(struct rev_info *revs, struct commit_list *list)
{
	int i = revs->early_output, close_file = revs->diffopt.close_file;
	int show_header = 1;

	revs->diffopt.close_file = 0;
	sort_in_topological_order(&list, revs->sort_order);
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
			if (close_file)
				fclose(revs->diffopt.file);
			return;
		}
		list = list->next;
	}

	/* Did we already get enough commits for the early output? */
	if (!i) {
		if (close_file)
			fclose(revs->diffopt.file);
		return;
	}

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
	int saved_dcctc = 0, close_file = rev->diffopt.close_file;

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
	rev->diffopt.close_file = 0;
	while ((commit = get_revision(rev)) != NULL) {
		if (!log_tree_commit(rev, commit) && rev->max_count >= 0)
			/*
			 * We decremented max_count in get_revision,
			 * but we didn't actually show the commit.
			 */
			rev->max_count++;
		if (!rev->reflog_info) {
			/* we allow cycles in reflog ancestry */
			free_commit_buffer(commit);
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
	if (close_file)
		fclose(rev->diffopt.file);

	if (rev->diffopt.output_format & DIFF_FORMAT_CHECKDIFF &&
	    DIFF_OPT_TST(&rev->diffopt, CHECK_FAILED)) {
		return 02;
	}
	return diff_result_code(&rev->diffopt, 0);
}

static int git_log_config(const char *var, const char *value, void *cb)
{
	const char *slot_name;

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
	} else {
		decoration_style = auto_decoration_style();
	}
	if (!strcmp(var, "log.showroot")) {
		default_show_root = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "log.follow")) {
		default_follow = git_config_bool(var, value);
		return 0;
	}
	if (skip_prefix(var, "color.decorate.", &slot_name))
		return parse_decorate_color_config(var, slot_name, value);
	if (!strcmp(var, "log.mailmap")) {
		use_mailmap_config = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "log.showsignature")) {
		default_show_signature = git_config_bool(var, value);
		return 0;
	}

	if (grep_config(var, value, cb) < 0)
		return -1;
	if (git_gpg_config(var, value, cb) < 0)
		return -1;
	return git_diff_ui_config(var, value, cb);
}

int cmd_whatchanged(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_log_defaults();
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
	fprintf(rev->diffopt.file, "%s", out.buf);
	strbuf_release(&out);
}

static int show_blob_object(const struct object_id *oid, struct rev_info *rev, const char *obj_name)
{
	struct object_id oidc;
	struct object_context obj_context;
	char *buf;
	unsigned long size;

	fflush(rev->diffopt.file);
	if (!DIFF_OPT_TOUCHED(&rev->diffopt, ALLOW_TEXTCONV) ||
	    !DIFF_OPT_TST(&rev->diffopt, ALLOW_TEXTCONV))
		return stream_blob_to_fd(1, oid, NULL, 0);

	if (get_sha1_with_context(obj_name, 0, oidc.hash, &obj_context))
		die(_("Not a valid object name %s"), obj_name);
	if (!obj_context.path[0] ||
	    !textconv_object(obj_context.path, obj_context.mode, &oidc, 1, &buf, &size))
		return stream_blob_to_fd(1, oid, NULL, 0);

	if (!buf)
		die(_("git show %s: bad file"), obj_name);

	write_or_die(1, buf, size);
	return 0;
}

static int show_tag_object(const struct object_id *oid, struct rev_info *rev)
{
	unsigned long size;
	enum object_type type;
	char *buf = read_sha1_file(oid->hash, &type, &size);
	int offset = 0;

	if (!buf)
		return error(_("Could not read object %s"), oid_to_hex(oid));

	assert(type == OBJ_TAG);
	while (offset < size && buf[offset] != '\n') {
		int new_offset = offset + 1;
		while (new_offset < size && buf[new_offset++] != '\n')
			; /* do nothing */
		if (starts_with(buf + offset, "tagger "))
			show_tagger(buf + offset + 7,
				    new_offset - offset - 7, rev);
		offset = new_offset;
	}

	if (offset < size)
		fwrite(buf + offset, size - offset, 1, rev->diffopt.file);
	free(buf);
	return 0;
}

static int show_tree_object(const unsigned char *sha1,
		struct strbuf *base,
		const char *pathname, unsigned mode, int stage, void *context)
{
	FILE *file = context;
	fprintf(file, "%s%s\n", pathname, S_ISDIR(mode) ? "/" : "");
	return 0;
}

static void show_setup_revisions_tweak(struct rev_info *rev,
				       struct setup_revision_opt *opt)
{
	if (rev->ignore_merges) {
		/* There was no "-m" on the command line */
		rev->ignore_merges = 0;
		if (!rev->first_parent_only && !rev->combine_merges) {
			/* No "--first-parent", "-c", or "--cc" */
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

	init_log_defaults();
	git_config(git_log_config, NULL);

	memset(&match_all, 0, sizeof(match_all));
	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.always_show_header = 1;
	rev.no_walk = REVISION_WALK_NO_WALK_SORTED;
	rev.diffopt.stat_width = -1; 	/* Scale to real terminal size */

	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.tweak = show_setup_revisions_tweak;
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
			ret = show_blob_object(&o->oid, &rev, name);
			break;
		case OBJ_TAG: {
			struct tag *t = (struct tag *)o;

			if (rev.shown_one)
				putchar('\n');
			fprintf(rev.diffopt.file, "%stag %s%s\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					t->tag,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			ret = show_tag_object(&o->oid, &rev);
			rev.shown_one = 1;
			if (ret)
				break;
			o = parse_object(t->tagged->oid.hash);
			if (!o)
				ret = error(_("Could not read object %s"),
					    oid_to_hex(&t->tagged->oid));
			objects[i].item = o;
			i--;
			break;
		}
		case OBJ_TREE:
			if (rev.shown_one)
				putchar('\n');
			fprintf(rev.diffopt.file, "%stree %s%s\n\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					name,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			read_tree_recursive((struct tree *)o, "", 0, 0, &match_all,
					show_tree_object, rev.diffopt.file);
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

	init_log_defaults();
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

static void log_setup_revisions_tweak(struct rev_info *rev,
				      struct setup_revision_opt *opt)
{
	if (DIFF_OPT_TST(&rev->diffopt, DEFAULT_FOLLOW_RENAMES) &&
	    rev->prune_data.nr == 1)
		DIFF_OPT_SET(&rev->diffopt, FOLLOW_RENAMES);

	/* Turn --cc/-c into -p --cc/-c when -p was not given */
	if (!rev->diffopt.output_format && rev->combine_merges)
		rev->diffopt.output_format = DIFF_FORMAT_PATCH;

	/* Turn -m on when --cc/-c was given */
	if (rev->combine_merges)
		rev->ignore_merges = 0;
}

int cmd_log(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_log_defaults();
	git_config(git_log_config, NULL);

	init_revisions(&rev, prefix);
	rev.always_show_header = 1;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.revarg_opt = REVARG_COMMITTISH;
	opt.tweak = log_setup_revisions_tweak;
	cmd_log_init(argc, argv, prefix, &rev, &opt);
	return cmd_log_walk(&rev);
}

/* format-patch */

static const char *fmt_patch_suffix = ".patch";
static int numbered = 0;
static int auto_number = 1;

static char *default_attach = NULL;

static struct string_list extra_hdr = STRING_LIST_INIT_NODUP;
static struct string_list extra_to = STRING_LIST_INIT_NODUP;
static struct string_list extra_cc = STRING_LIST_INIT_NODUP;

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
static int base_auto;
static char *from;
static const char *signature = git_version_string;
static const char *signature_file;
static int config_cover_letter;
static const char *config_output_directory;

enum {
	COVER_UNSET,
	COVER_OFF,
	COVER_ON,
	COVER_AUTO
};

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
	    !strcmp(var, "color.ui") || !strcmp(var, "diff.submodule")) {
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
	if (!strcmp(var, "format.signaturefile"))
		return git_config_pathname(&signature_file, var, value);
	if (!strcmp(var, "format.coverletter")) {
		if (value && !strcasecmp(value, "auto")) {
			config_cover_letter = COVER_AUTO;
			return 0;
		}
		config_cover_letter = git_config_bool(var, value) ? COVER_ON : COVER_OFF;
		return 0;
	}
	if (!strcmp(var, "format.outputdirectory"))
		return git_config_string(&config_output_directory, var, value);
	if (!strcmp(var, "format.useautobase")) {
		base_auto = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "format.from")) {
		int b = git_config_maybe_bool(var, value);
		free(from);
		if (b < 0)
			from = xstrdup(value);
		else if (b)
			from = xstrdup(git_committer_info(IDENT_NO_DATE));
		else
			from = NULL;
		return 0;
	}

	return git_log_config(var, value, cb);
}

static const char *output_directory = NULL;
static int outdir_offset;

static int open_next_file(struct commit *commit, const char *subject,
			 struct rev_info *rev, int quiet)
{
	struct strbuf filename = STRBUF_INIT;
	int suffix_len = strlen(rev->patch_suffix) + 1;

	if (output_directory) {
		strbuf_addstr(&filename, output_directory);
		if (filename.len >=
		    PATH_MAX - FORMAT_PATCH_NAME_MAX - suffix_len)
			return error(_("name of output directory is too long"));
		strbuf_complete(&filename, '/');
	}

	if (rev->numbered_files)
		strbuf_addf(&filename, "%d", rev->nr);
	else if (commit)
		fmt_output_commit(&filename, commit, rev);
	else
		fmt_output_subject(&filename, subject, rev);

	if (!quiet)
		printf("%s\n", filename.buf + outdir_offset);

	if ((rev->diffopt.file = fopen(filename.buf, "w")) == NULL)
		return error(_("Cannot open patch file %s"), filename.buf);

	strbuf_release(&filename);
	return 0;
}

static void get_patch_ids(struct rev_info *rev, struct patch_ids *ids)
{
	struct rev_info check_rev;
	struct commit *commit, *c1, *c2;
	struct object *o1, *o2;
	unsigned flags1, flags2;

	if (rev->pending.nr != 2)
		die(_("Need exactly one range."));

	o1 = rev->pending.objects[0].item;
	o2 = rev->pending.objects[1].item;
	flags1 = o1->flags;
	flags2 = o2->flags;
	c1 = lookup_commit_reference(o1->oid.hash);
	c2 = lookup_commit_reference(o2->oid.hash);

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
	clear_commit_marks(c1, SEEN | UNINTERESTING | SHOWN | ADDED);
	clear_commit_marks(c2, SEEN | UNINTERESTING | SHOWN | ADDED);
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

static void print_signature(FILE *file)
{
	if (!signature || !*signature)
		return;

	fprintf(file, "-- \n%s", signature);
	if (signature[strlen(signature)-1] != '\n')
		putc('\n', file);
	putc('\n', file);
}

static void add_branch_description(struct strbuf *buf, const char *branch_name)
{
	struct strbuf desc = STRBUF_INIT;
	if (!branch_name || !*branch_name)
		return;
	read_branch_desc(&desc, branch_name);
	if (desc.len) {
		strbuf_addch(buf, '\n');
		strbuf_addbuf(buf, &desc);
		strbuf_addch(buf, '\n');
	}
	strbuf_release(&desc);
}

static char *find_branch_name(struct rev_info *rev)
{
	int i, positive = -1;
	struct object_id branch_oid;
	const struct object_id *tip_oid;
	const char *ref, *v;
	char *full_ref, *branch = NULL;

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
	ref = rev->cmdline.rev[positive].name;
	tip_oid = &rev->cmdline.rev[positive].item->oid;
	if (dwim_ref(ref, strlen(ref), branch_oid.hash, &full_ref) &&
	    skip_prefix(full_ref, "refs/heads/", &v) &&
	    !oidcmp(tip_oid, &branch_oid))
		branch = xstrdup(v);
	free(full_ref);
	return branch;
}

static void make_cover_letter(struct rev_info *rev, int use_stdout,
			      struct commit *origin,
			      int nr, struct commit **list,
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
	struct commit *head = list[0];

	if (!cmit_fmt_is_mail(rev->commit_format))
		die(_("Cover letter needs email format"));

	committer = git_committer_info(0);

	if (!use_stdout &&
	    open_next_file(NULL, rev->numbered_files ? NULL : "cover-letter", rev, quiet))
		return;

	log_write_email_headers(rev, head, &pp.after_subject, &need_8bit_cte);

	for (i = 0; !need_8bit_cte && i < nr; i++) {
		const char *buf = get_commit_buffer(list[i], NULL);
		if (has_non_ascii(buf))
			need_8bit_cte = 1;
		unuse_commit_buffer(list[i], buf);
	}

	if (!branch_name)
		branch_name = find_branch_name(rev);

	msg = body;
	pp.fmt = CMIT_FMT_EMAIL;
	pp.date_mode.type = DATE_RFC2822;
	pp.rev = rev;
	pp.print_email_subject = 1;
	pp_user_info(&pp, NULL, &sb, committer, encoding);
	pp_title_line(&pp, &msg, &sb, encoding, need_8bit_cte);
	pp_remainder(&pp, &msg, &sb, 0);
	add_branch_description(&sb, branch_name);
	fprintf(rev->diffopt.file, "%s\n", sb.buf);

	strbuf_release(&sb);

	shortlog_init(&log);
	log.wrap_lines = 1;
	log.wrap = 72;
	log.in1 = 2;
	log.in2 = 4;
	log.file = rev->diffopt.file;
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

	diff_tree_sha1(origin->tree->object.oid.hash,
		       head->tree->object.oid.hash,
		       "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);

	fprintf(rev->diffopt.file, "\n");
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

	return prefix_filename(prefix, output_directory);
}

static const char * const builtin_format_patch_usage[] = {
	N_("git format-patch [<options>] [<since> | <revision-range>]"),
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

static int rfc_callback(const struct option *opt, const char *arg, int unset)
{
	return subject_prefix_callback(opt, "RFC PATCH", unset);
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

static int from_callback(const struct option *opt, const char *arg, int unset)
{
	char **from = opt->value;

	free(*from);

	if (unset)
		*from = NULL;
	else if (arg)
		*from = xstrdup(arg);
	else
		*from = xstrdup(git_committer_info(IDENT_NO_DATE));
	return 0;
}

struct base_tree_info {
	struct object_id base_commit;
	int nr_patch_id, alloc_patch_id;
	struct object_id *patch_id;
};

static struct commit *get_base_commit(const char *base_commit,
				      struct commit **list,
				      int total)
{
	struct commit *base = NULL;
	struct commit **rev;
	int i = 0, rev_nr = 0;

	if (base_commit && strcmp(base_commit, "auto")) {
		base = lookup_commit_reference_by_name(base_commit);
		if (!base)
			die(_("Unknown commit %s"), base_commit);
	} else if ((base_commit && !strcmp(base_commit, "auto")) || base_auto) {
		struct branch *curr_branch = branch_get(NULL);
		const char *upstream = branch_get_upstream(curr_branch, NULL);
		if (upstream) {
			struct commit_list *base_list;
			struct commit *commit;
			struct object_id oid;

			if (get_oid(upstream, &oid))
				die(_("Failed to resolve '%s' as a valid ref."), upstream);
			commit = lookup_commit_or_die(oid.hash, "upstream base");
			base_list = get_merge_bases_many(commit, total, list);
			/* There should be one and only one merge base. */
			if (!base_list || base_list->next)
				die(_("Could not find exact merge base."));
			base = base_list->item;
			free_commit_list(base_list);
		} else {
			die(_("Failed to get upstream, if you want to record base commit automatically,\n"
			      "please use git branch --set-upstream-to to track a remote branch.\n"
			      "Or you could specify base commit by --base=<base-commit-id> manually."));
		}
	}

	ALLOC_ARRAY(rev, total);
	for (i = 0; i < total; i++)
		rev[i] = list[i];

	rev_nr = total;
	/*
	 * Get merge base through pair-wise computations
	 * and store it in rev[0].
	 */
	while (rev_nr > 1) {
		for (i = 0; i < rev_nr / 2; i++) {
			struct commit_list *merge_base;
			merge_base = get_merge_bases(rev[2 * i], rev[2 * i + 1]);
			if (!merge_base || merge_base->next)
				die(_("Failed to find exact merge base"));

			rev[i] = merge_base->item;
		}

		if (rev_nr % 2)
			rev[i] = rev[2 * i];
		rev_nr = (rev_nr + 1) / 2;
	}

	if (!in_merge_bases(base, rev[0]))
		die(_("base commit should be the ancestor of revision list"));

	for (i = 0; i < total; i++) {
		if (base == list[i])
			die(_("base commit shouldn't be in revision list"));
	}

	free(rev);
	return base;
}

static void prepare_bases(struct base_tree_info *bases,
			  struct commit *base,
			  struct commit **list,
			  int total)
{
	struct commit *commit;
	struct rev_info revs;
	struct diff_options diffopt;
	int i;

	if (!base)
		return;

	diff_setup(&diffopt);
	DIFF_OPT_SET(&diffopt, RECURSIVE);
	diff_setup_done(&diffopt);

	oidcpy(&bases->base_commit, &base->object.oid);

	init_revisions(&revs, NULL);
	revs.max_parents = 1;
	revs.topo_order = 1;
	for (i = 0; i < total; i++) {
		list[i]->object.flags &= ~UNINTERESTING;
		add_pending_object(&revs, &list[i]->object, "rev_list");
		list[i]->util = (void *)1;
	}
	base->object.flags |= UNINTERESTING;
	add_pending_object(&revs, &base->object, "base");

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));
	/*
	 * Traverse the commits list, get prerequisite patch ids
	 * and stuff them in bases structure.
	 */
	while ((commit = get_revision(&revs)) != NULL) {
		struct object_id oid;
		struct object_id *patch_id;
		if (commit->util)
			continue;
		if (commit_patch_id(commit, &diffopt, oid.hash, 0))
			die(_("cannot get patch id"));
		ALLOC_GROW(bases->patch_id, bases->nr_patch_id + 1, bases->alloc_patch_id);
		patch_id = bases->patch_id + bases->nr_patch_id;
		oidcpy(patch_id, &oid);
		bases->nr_patch_id++;
	}
}

static void print_bases(struct base_tree_info *bases, FILE *file)
{
	int i;

	/* Only do this once, either for the cover or for the first one */
	if (is_null_oid(&bases->base_commit))
		return;

	/* Show the base commit */
	fprintf(file, "\nbase-commit: %s\n", oid_to_hex(&bases->base_commit));

	/* Show the prerequisite patches */
	for (i = bases->nr_patch_id - 1; i >= 0; i--)
		fprintf(file, "prerequisite-patch-id: %s\n", oid_to_hex(&bases->patch_id[i]));

	free(bases->patch_id);
	bases->nr_patch_id = 0;
	bases->alloc_patch_id = 0;
	oidclr(&bases->base_commit);
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
	int just_numbers = 0;
	int ignore_if_in_upstream = 0;
	int cover_letter = -1;
	int boundary_count = 0;
	int no_binary_diff = 0;
	int zero_commit = 0;
	struct commit *origin = NULL;
	const char *in_reply_to = NULL;
	struct patch_ids ids;
	struct strbuf buf = STRBUF_INIT;
	int use_patch_format = 0;
	int quiet = 0;
	int reroll_count = -1;
	char *branch_name = NULL;
	char *base_commit = NULL;
	struct base_tree_info bases;

	const struct option builtin_format_patch_options[] = {
		{ OPTION_CALLBACK, 'n', "numbered", &numbered, NULL,
			    N_("use [PATCH n/m] even with a single patch"),
			    PARSE_OPT_NOARG, numbered_callback },
		{ OPTION_CALLBACK, 'N', "no-numbered", &numbered, NULL,
			    N_("use [PATCH] even with multiple patches"),
			    PARSE_OPT_NOARG, no_numbered_callback },
		OPT_BOOL('s', "signoff", &do_signoff, N_("add Signed-off-by:")),
		OPT_BOOL(0, "stdout", &use_stdout,
			    N_("print patches to standard out")),
		OPT_BOOL(0, "cover-letter", &cover_letter,
			    N_("generate a cover letter")),
		OPT_BOOL(0, "numbered-files", &just_numbers,
			    N_("use simple number sequence for output file names")),
		OPT_STRING(0, "suffix", &fmt_patch_suffix, N_("sfx"),
			    N_("use <sfx> instead of '.patch'")),
		OPT_INTEGER(0, "start-number", &start_number,
			    N_("start numbering patches at <n> instead of 1")),
		OPT_INTEGER('v', "reroll-count", &reroll_count,
			    N_("mark the series as Nth re-roll")),
		{ OPTION_CALLBACK, 0, "rfc", &rev, NULL,
			    N_("Use [RFC PATCH] instead of [PATCH]"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, rfc_callback },
		{ OPTION_CALLBACK, 0, "subject-prefix", &rev, N_("prefix"),
			    N_("Use [<prefix>] instead of [PATCH]"),
			    PARSE_OPT_NONEG, subject_prefix_callback },
		{ OPTION_CALLBACK, 'o', "output-directory", &output_directory,
			    N_("dir"), N_("store resulting files in <dir>"),
			    PARSE_OPT_NONEG, output_directory_callback },
		{ OPTION_CALLBACK, 'k', "keep-subject", &rev, NULL,
			    N_("don't strip/add [PATCH]"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, keep_callback },
		OPT_BOOL(0, "no-binary", &no_binary_diff,
			 N_("don't output binary diffs")),
		OPT_BOOL(0, "zero-commit", &zero_commit,
			 N_("output all-zero hash in From header")),
		OPT_BOOL(0, "ignore-if-in-upstream", &ignore_if_in_upstream,
			 N_("don't include a patch matching a commit upstream")),
		{ OPTION_SET_INT, 'p', "no-stat", &use_patch_format, NULL,
		  N_("show patch format instead of default (patch + stat)"),
		  PARSE_OPT_NONEG | PARSE_OPT_NOARG, NULL, 1},
		OPT_GROUP(N_("Messaging")),
		{ OPTION_CALLBACK, 0, "add-header", NULL, N_("header"),
			    N_("add email header"), 0, header_callback },
		{ OPTION_CALLBACK, 0, "to", NULL, N_("email"), N_("add To: header"),
			    0, to_callback },
		{ OPTION_CALLBACK, 0, "cc", NULL, N_("email"), N_("add Cc: header"),
			    0, cc_callback },
		{ OPTION_CALLBACK, 0, "from", &from, N_("ident"),
			    N_("set From address to <ident> (or committer ident if absent)"),
			    PARSE_OPT_OPTARG, from_callback },
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
		OPT_STRING(0, "base", &base_commit, N_("base-commit"),
			   N_("add prerequisite tree info to the patch series")),
		OPT_FILENAME(0, "signature-file", &signature_file,
				N_("add a signature from a file")),
		OPT__QUIET(&quiet, N_("don't print the patch filenames")),
		OPT_END()
	};

	extra_hdr.strdup_strings = 1;
	extra_to.strdup_strings = 1;
	extra_cc.strdup_strings = 1;
	init_log_defaults();
	git_config(git_format_config, NULL);
	init_revisions(&rev, prefix);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.expand_tabs_in_log_default = 0;
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

	if (0 < reroll_count) {
		struct strbuf sprefix = STRBUF_INIT;
		strbuf_addf(&sprefix, "%s v%d",
			    rev.subject_prefix, reroll_count);
		rev.reroll_count = reroll_count;
		rev.subject_prefix = strbuf_detach(&sprefix, NULL);
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

	if (from) {
		if (split_ident_line(&rev.from_ident, from, strlen(from)))
			die(_("invalid ident line: %s"), from);
	}

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
		die (_("--subject-prefix/--rfc and -k are mutually exclusive."));
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

	rev.zero_commit = zero_commit;

	if (!DIFF_OPT_TST(&rev.diffopt, TEXT) && !no_binary_diff)
		DIFF_OPT_SET(&rev.diffopt, BINARY);

	if (rev.show_notes)
		init_display_notes(&rev.notes_opt);

	if (!output_directory && !use_stdout)
		output_directory = config_output_directory;

	if (!use_stdout)
		output_directory = set_outdir(prefix, output_directory);
	else
		setup_pager();

	if (output_directory) {
		if (rev.diffopt.use_color != GIT_COLOR_ALWAYS)
			rev.diffopt.use_color = GIT_COLOR_NEVER;
		if (use_stdout)
			die(_("standard output, or directory, which one?"));
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die_errno(_("Could not create directory '%s'"),
				  output_directory);
	}

	if (rev.pending.nr == 1) {
		int check_head = 0;

		if (rev.max_count < 0 && !rev.show_root_diff) {
			/*
			 * This is traditional behaviour of "git format-patch
			 * origin" that prepares what the origin side still
			 * does not have.
			 */
			rev.pending.objects[0].item->flags |= UNINTERESTING;
			add_head_to_pending(&rev);
			check_head = 1;
		}
		/*
		 * Otherwise, it is "format-patch -22 HEAD", and/or
		 * "format-patch --root HEAD".  The user wants
		 * get_revision() to do the usual traversal.
		 */

		if (!strcmp(rev.pending.objects[0].name, "HEAD"))
			check_head = 1;

		if (check_head) {
			struct object_id oid;
			const char *ref, *v;
			ref = resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
						 oid.hash, NULL);
			if (ref && skip_prefix(ref, "refs/heads/", &v))
				branch_name = xstrdup(v);
			else
				branch_name = xstrdup(""); /* no branch */
		}
	}

	/*
	 * We cannot move this anywhere earlier because we do want to
	 * know if --root was given explicitly from the command line.
	 */
	rev.show_root_diff = 1;

	if (ignore_if_in_upstream) {
		/* Don't say anything if head and upstream are the same. */
		if (rev.pending.nr == 2) {
			struct object_array_entry *o = rev.pending.objects;
			if (oidcmp(&o[0].item->oid, &o[1].item->oid) == 0)
				return 0;
		}
		get_patch_ids(&rev, &ids);
	}

	if (prepare_revision_walk(&rev))
		die(_("revision walk setup failed"));
	rev.boundary = 1;
	while ((commit = get_revision(&rev)) != NULL) {
		if (commit->object.flags & BOUNDARY) {
			boundary_count++;
			origin = (boundary_count == 1) ? commit : NULL;
			continue;
		}

		if (ignore_if_in_upstream && has_commit_patch_id(commit, &ids))
			continue;

		nr++;
		REALLOC_ARRAY(list, nr);
		list[nr - 1] = commit;
	}
	if (nr == 0)
		/* nothing to do */
		return 0;
	total = nr;
	if (cover_letter == -1) {
		if (config_cover_letter == COVER_AUTO)
			cover_letter = (total > 1);
		else
			cover_letter = (config_cover_letter == COVER_ON);
	}
	if (!keep_subject && auto_number && (total > 1 || cover_letter))
		numbered = 1;
	if (numbered)
		rev.total = total + start_number - 1;

	if (!signature) {
		; /* --no-signature inhibits all signatures */
	} else if (signature && signature != git_version_string) {
		; /* non-default signature already set */
	} else if (signature_file) {
		struct strbuf buf = STRBUF_INIT;

		if (strbuf_read_file(&buf, signature_file, 128) < 0)
			die_errno(_("unable to read signature file '%s'"), signature_file);
		signature = strbuf_detach(&buf, NULL);
	}

	memset(&bases, 0, sizeof(bases));
	if (base_commit || base_auto) {
		struct commit *base = get_base_commit(base_commit, list, nr);
		reset_revision_walk();
		prepare_bases(&bases, base, list, nr);
	}

	if (in_reply_to || thread || cover_letter)
		rev.ref_message_ids = xcalloc(1, sizeof(struct string_list));
	if (in_reply_to) {
		const char *msgid = clean_message_id(in_reply_to);
		string_list_append(rev.ref_message_ids, msgid);
	}
	rev.numbered_files = just_numbers;
	rev.patch_suffix = fmt_patch_suffix;
	if (cover_letter) {
		if (thread)
			gen_message_id(&rev, "cover");
		make_cover_letter(&rev, use_stdout,
				  origin, nr, list, branch_name, quiet);
		print_bases(&bases, rev.diffopt.file);
		print_signature(rev.diffopt.file);
		total++;
		start_number--;
	}
	rev.add_signoff = do_signoff;
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
			gen_message_id(&rev, oid_to_hex(&commit->object.oid));
		}

		if (!use_stdout &&
		    open_next_file(rev.numbered_files ? NULL : commit, NULL, &rev, quiet))
			die(_("Failed to create output files"));
		shown = log_tree_commit(&rev, commit);
		free_commit_buffer(commit);

		/* We put one extra blank line between formatted
		 * patches and this flag is used by log-tree code
		 * to see if it needs to emit a LF before showing
		 * the log; when using one file per patch, we do
		 * not want the extra blank line.
		 */
		if (!use_stdout)
			rev.shown_one = 0;
		if (shown) {
			print_bases(&bases, rev.diffopt.file);
			if (rev.mime_boundary)
				fprintf(rev.diffopt.file, "\n--%s%s--\n\n\n",
				       mime_boundary_leader,
				       rev.mime_boundary);
			else
				print_signature(rev.diffopt.file);
		}
		if (!use_stdout)
			fclose(rev.diffopt.file);
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
	struct object_id oid;
	if (get_oid(arg, &oid) == 0) {
		struct commit *commit = lookup_commit_reference(oid.hash);
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
			 int abbrev, FILE *file)
{
	if (!verbose) {
		fprintf(file, "%c %s\n", sign,
		       find_unique_abbrev(commit->object.oid.hash, abbrev));
	} else {
		struct strbuf buf = STRBUF_INIT;
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &buf);
		fprintf(file, "%c %s %s\n", sign,
		       find_unique_abbrev(commit->object.oid.hash, abbrev),
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
		upstream = branch_get_upstream(current_branch, NULL);
		if (!upstream) {
			fprintf(stderr, _("Could not find a tracked"
					" remote branch, please"
					" specify <upstream> manually.\n"));
			usage_with_options(cherry_usage, options);
		}
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
		if (oidcmp(&o[0].item->oid, &o[1].item->oid) == 0)
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
		print_commit(sign, commit, verbose, abbrev, revs.diffopt.file);
		list = list->next;
	}

	free_patch_ids(&ids);
	return 0;
}
