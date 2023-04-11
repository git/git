/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "git-compat-util.h"
#include "abspath.h"
#include "alloc.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "refs.h"
#include "object-name.h"
#include "object-store.h"
#include "color.h"
#include "commit.h"
#include "diff.h"
#include "diff-merges.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"
#include "oid-array.h"
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
#include "progress.h"
#include "commit-slab.h"
#include "repository.h"
#include "commit-reach.h"
#include "range-diff.h"
#include "tmp-objdir.h"
#include "write-or-die.h"

#define MAIL_DEFAULT_WRAP 72
#define COVER_FROM_AUTO_MAX_SUBJECT_LEN 100
#define FORMAT_PATCH_NAME_MAX_DEFAULT 64

/* Set a default date-time format for git log ("log.date" config variable) */
static const char *default_date_mode = NULL;

static int default_abbrev_commit;
static int default_show_root = 1;
static int default_follow;
static int default_show_signature;
static int default_encode_email_headers = 1;
static int decoration_style;
static int decoration_given;
static int use_mailmap_config = 1;
static unsigned int force_in_body_from;
static int stdout_mboxrd;
static const char *fmt_patch_subject_prefix = "PATCH";
static int fmt_patch_name_max = FORMAT_PATCH_NAME_MAX_DEFAULT;
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

static int session_is_interactive(void)
{
	return isatty(1) || pager_in_use();
}

static int auto_decoration_style(void)
{
	return session_is_interactive() ? DECORATE_SHORT_REFS : 0;
}

static int parse_decoration_style(const char *value)
{
	switch (git_parse_maybe_bool(value)) {
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
	/*
	 * Please update _git_log() in git-completion.bash when you
	 * add new decoration styles.
	 */
	return -1;
}

static int use_default_decoration_filter = 1;
static struct string_list decorate_refs_exclude = STRING_LIST_INIT_NODUP;
static struct string_list decorate_refs_exclude_config = STRING_LIST_INIT_NODUP;
static struct string_list decorate_refs_include = STRING_LIST_INIT_NODUP;

static int clear_decorations_callback(const struct option *opt,
					    const char *arg, int unset)
{
	string_list_clear(&decorate_refs_include, 0);
	string_list_clear(&decorate_refs_exclude, 0);
	use_default_decoration_filter = 0;
	return 0;
}

static int decorate_callback(const struct option *opt, const char *arg, int unset)
{
	if (unset)
		decoration_style = 0;
	else if (arg)
		decoration_style = parse_decoration_style(arg);
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

	BUG_ON_OPT_NEG(unset);

	if (!arg)
		return -1;

	data->rev->line_level_traverse = 1;
	string_list_append(&data->args, arg);

	return 0;
}

static void init_log_defaults(void)
{
	init_diff_ui_defaults();

	decoration_style = auto_decoration_style();
}

static void cmd_log_init_defaults(struct rev_info *rev)
{
	if (fmt_pretty)
		get_commit_format(fmt_pretty, rev);
	if (default_follow)
		rev->diffopt.flags.default_follow_renames = 1;
	rev->verbose_header = 1;
	rev->diffopt.flags.recursive = 1;
	rev->diffopt.stat_width = -1; /* use full terminal width */
	rev->diffopt.stat_graph_width = -1; /* respect statGraphWidth config */
	rev->abbrev_commit = default_abbrev_commit;
	rev->show_root_diff = default_show_root;
	rev->subject_prefix = fmt_patch_subject_prefix;
	rev->patch_name_max = fmt_patch_name_max;
	rev->show_signature = default_show_signature;
	rev->encode_email_headers = default_encode_email_headers;
	rev->diffopt.flags.allow_textconv = 1;

	if (default_date_mode)
		parse_date_format(default_date_mode, &rev->date_mode);
}

static void set_default_decoration_filter(struct decoration_filter *decoration_filter)
{
	int i;
	char *value = NULL;
	struct string_list *include = decoration_filter->include_ref_pattern;
	const struct string_list *config_exclude =
			git_config_get_value_multi("log.excludeDecoration");

	if (config_exclude) {
		struct string_list_item *item;
		for_each_string_list_item(item, config_exclude)
			string_list_append(decoration_filter->exclude_ref_config_pattern,
					   item->string);
	}

	/*
	 * By default, decorate_all is disabled. Enable it if
	 * log.initialDecorationSet=all. Don't ever disable it by config,
	 * since the command-line takes precedent.
	 */
	if (use_default_decoration_filter &&
	    !git_config_get_string("log.initialdecorationset", &value) &&
	    !strcmp("all", value))
		use_default_decoration_filter = 0;
	free(value);

	if (!use_default_decoration_filter ||
	    decoration_filter->exclude_ref_pattern->nr ||
	    decoration_filter->include_ref_pattern->nr ||
	    decoration_filter->exclude_ref_config_pattern->nr)
		return;

	/*
	 * No command-line or config options were given, so
	 * populate with sensible defaults.
	 */
	for (i = 0; i < ARRAY_SIZE(ref_namespace); i++) {
		if (!ref_namespace[i].decoration)
			continue;

		string_list_append(include, ref_namespace[i].ref);
	}
}

static void cmd_log_init_finish(int argc, const char **argv, const char *prefix,
			 struct rev_info *rev, struct setup_revision_opt *opt)
{
	struct userformat_want w;
	int quiet = 0, source = 0, mailmap;
	static struct line_opt_callback_data line_cb = {NULL, NULL, STRING_LIST_INIT_DUP};
	struct decoration_filter decoration_filter = {
		.exclude_ref_pattern = &decorate_refs_exclude,
		.include_ref_pattern = &decorate_refs_include,
		.exclude_ref_config_pattern = &decorate_refs_exclude_config,
	};
	static struct revision_sources revision_sources;

	const struct option builtin_log_options[] = {
		OPT__QUIET(&quiet, N_("suppress diff output")),
		OPT_BOOL(0, "source", &source, N_("show source")),
		OPT_BOOL(0, "use-mailmap", &mailmap, N_("use mail map file")),
		OPT_ALIAS(0, "mailmap", "use-mailmap"),
		OPT_CALLBACK_F(0, "clear-decorations", NULL, NULL,
			       N_("clear all previously-defined decoration filters"),
			       PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			       clear_decorations_callback),
		OPT_STRING_LIST(0, "decorate-refs", &decorate_refs_include,
				N_("pattern"), N_("only decorate refs that match <pattern>")),
		OPT_STRING_LIST(0, "decorate-refs-exclude", &decorate_refs_exclude,
				N_("pattern"), N_("do not decorate refs that match <pattern>")),
		OPT_CALLBACK_F(0, "decorate", NULL, NULL, N_("decorate options"),
			       PARSE_OPT_OPTARG, decorate_callback),
		OPT_CALLBACK('L', NULL, &line_cb, "range:file",
			     N_("trace the evolution of line range <start>,<end> or function :<funcname> in <file>"),
			     log_line_range_callback),
		OPT_END()
	};

	line_cb.rev = rev;
	line_cb.prefix = prefix;

	mailmap = use_mailmap_config;
	argc = parse_options(argc, argv, prefix,
			     builtin_log_options, builtin_log_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	if (quiet)
		rev->diffopt.output_format |= DIFF_FORMAT_NO_OUTPUT;
	argc = setup_revisions(argc, argv, rev, opt);

	/* Any arguments at this point are not recognized */
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);

	if (rev->line_level_traverse && rev->prune_data.nr)
		die(_("-L<range>:<file> cannot be used with pathspec"));

	memset(&w, 0, sizeof(w));
	userformat_find_requirements(NULL, &w);

	if (!rev->show_notes_given && (!rev->pretty_given || w.notes))
		rev->show_notes = 1;
	if (rev->show_notes)
		load_display_notes(&rev->notes_opt);

	if ((rev->diffopt.pickaxe_opts & DIFF_PICKAXE_KINDS_MASK) ||
	    rev->diffopt.filter || rev->diffopt.flags.follow_renames)
		rev->always_show_header = 0;

	if (source || w.source) {
		init_revision_sources(&revision_sources);
		rev->sources = &revision_sources;
	}

	if (mailmap) {
		rev->mailmap = xmalloc(sizeof(struct string_list));
		string_list_init_nodup(rev->mailmap);
		read_mailmap(rev->mailmap);
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

	if (rev->commit_format == CMIT_FMT_USERFORMAT) {
		if (!w.decorate) {
			/*
			 * Disable decoration loading if the format will not
			 * show them anyway.
			 */
			decoration_style = 0;
		} else if (!decoration_style) {
			/*
			 * If we are going to show them, make sure we do load
			 * them here, but taking care not to override a
			 * specific style set by config or --decorate.
			 */
			decoration_style = DECORATE_SHORT_REFS;
		}
	}

	if (decoration_style || rev->simplify_by_decoration) {
		set_default_decoration_filter(&decoration_filter);

		if (decoration_style)
			rev->show_decorations = 1;

		load_ref_decorations(&decoration_filter, decoration_style);
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

static int cmd_log_deinit(int ret, struct rev_info *rev)
{
	release_revisions(rev);
	return ret;
}

/*
 * This gives a rough estimate for how many commits we
 * will print out in the list.
 */
static int estimate_commit_count(struct commit_list *list)
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
	int i = revs->early_output;
	int show_header = 1;
	int no_free = revs->diffopt.no_free;

	revs->diffopt.no_free = 0;
	sort_in_topological_order(&list, revs->sort_order);
	while (list && i) {
		struct commit *commit = list->item;
		switch (simplify_commit(revs, commit)) {
		case commit_show:
			if (show_header) {
				int n = estimate_commit_count(list);
				show_early_header(revs, "incomplete", n);
				show_header = 0;
			}
			log_tree_commit(revs, commit);
			i--;
			break;
		case commit_ignore:
			break;
		case commit_error:
			revs->diffopt.no_free = no_free;
			diff_free(&revs->diffopt);
			return;
		}
		list = list->next;
	}

	/* Did we already get enough commits for the early output? */
	if (!i) {
		revs->diffopt.no_free = 0;
		diff_free(&revs->diffopt);
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

static void early_output(int signal UNUSED)
{
	show_early_output = log_show_early;
}

static void setup_early_output(void)
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
	int n = estimate_commit_count(rev->commits);
	signal(SIGALRM, SIG_IGN);
	show_early_header(rev, "done", n);
}

static int cmd_log_walk_no_free(struct rev_info *rev)
{
	struct commit *commit;
	int saved_nrl = 0;
	int saved_dcctc = 0;

	if (rev->remerge_diff) {
		rev->remerge_objdir = tmp_objdir_create("remerge-diff");
		if (!rev->remerge_objdir)
			die(_("unable to create temporary object directory"));
		tmp_objdir_replace_primary_odb(rev->remerge_objdir, 1);
	}

	if (rev->early_output)
		setup_early_output();

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
		if (!log_tree_commit(rev, commit) && rev->max_count >= 0)
			/*
			 * We decremented max_count in get_revision,
			 * but we didn't actually show the commit.
			 */
			rev->max_count++;
		if (!rev->reflog_info) {
			/*
			 * We may show a given commit multiple times when
			 * walking the reflogs.
			 */
			free_commit_buffer(the_repository->parsed_objects,
					   commit);
			free_commit_list(commit->parents);
			commit->parents = NULL;
		}
		if (saved_nrl < rev->diffopt.needed_rename_limit)
			saved_nrl = rev->diffopt.needed_rename_limit;
		if (rev->diffopt.degraded_cc_to_c)
			saved_dcctc = 1;
	}
	rev->diffopt.degraded_cc_to_c = saved_dcctc;
	rev->diffopt.needed_rename_limit = saved_nrl;

	if (rev->remerge_diff) {
		tmp_objdir_destroy(rev->remerge_objdir);
		rev->remerge_objdir = NULL;
	}

	if (rev->diffopt.output_format & DIFF_FORMAT_CHECKDIFF &&
	    rev->diffopt.flags.check_failed) {
		return 02;
	}
	return diff_result_code(&rev->diffopt, 0);
}

static int cmd_log_walk(struct rev_info *rev)
{
	int retval;

	rev->diffopt.no_free = 1;
	retval = cmd_log_walk_no_free(rev);
	rev->diffopt.no_free = 0;
	diff_free(&rev->diffopt);
	return retval;
}

static int git_log_config(const char *var, const char *value, void *cb)
{
	const char *slot_name;

	if (!strcmp(var, "format.pretty"))
		return git_config_string(&fmt_pretty, var, value);
	if (!strcmp(var, "format.subjectprefix"))
		return git_config_string(&fmt_patch_subject_prefix, var, value);
	if (!strcmp(var, "format.filenamemaxlength")) {
		fmt_patch_name_max = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "format.encodeemailheaders")) {
		default_encode_email_headers = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "log.abbrevcommit")) {
		default_abbrev_commit = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "log.date"))
		return git_config_string(&default_date_mode, var, value);
	if (!strcmp(var, "log.decorate")) {
		decoration_style = parse_decoration_style(value);
		if (decoration_style < 0)
			decoration_style = 0; /* maybe warn? */
		return 0;
	}
	if (!strcmp(var, "log.diffmerges"))
		return diff_merges_config(value);
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

	return git_diff_ui_config(var, value, cb);
}

int cmd_whatchanged(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_log_defaults();
	git_config(git_log_config, NULL);

	repo_init_revisions(the_repository, &rev, prefix);
	git_config(grep_config, &rev.grep_filter);

	rev.diff = 1;
	rev.simplify_history = 0;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.revarg_opt = REVARG_COMMITTISH;
	cmd_log_init(argc, argv, prefix, &rev, &opt);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return cmd_log_deinit(cmd_log_walk(&rev), &rev);
}

static void show_tagger(const char *buf, struct rev_info *rev)
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
	if (!rev->diffopt.flags.textconv_set_via_cmdline ||
	    !rev->diffopt.flags.allow_textconv)
		return stream_blob_to_fd(1, oid, NULL, 0);

	if (get_oid_with_context(the_repository, obj_name,
				 GET_OID_RECORD_PATH,
				 &oidc, &obj_context))
		die(_("not a valid object name %s"), obj_name);
	if (!obj_context.path ||
	    !textconv_object(the_repository, obj_context.path,
			     obj_context.mode, &oidc, 1, &buf, &size)) {
		free(obj_context.path);
		return stream_blob_to_fd(1, oid, NULL, 0);
	}

	if (!buf)
		die(_("git show %s: bad file"), obj_name);

	write_or_die(1, buf, size);
	free(obj_context.path);
	return 0;
}

static int show_tag_object(const struct object_id *oid, struct rev_info *rev)
{
	unsigned long size;
	enum object_type type;
	char *buf = repo_read_object_file(the_repository, oid, &type, &size);
	int offset = 0;

	if (!buf)
		return error(_("could not read object %s"), oid_to_hex(oid));

	assert(type == OBJ_TAG);
	while (offset < size && buf[offset] != '\n') {
		int new_offset = offset + 1;
		const char *ident;
		while (new_offset < size && buf[new_offset++] != '\n')
			; /* do nothing */
		if (skip_prefix(buf + offset, "tagger ", &ident))
			show_tagger(ident, rev);
		offset = new_offset;
	}

	if (offset < size)
		fwrite(buf + offset, size - offset, 1, rev->diffopt.file);
	free(buf);
	return 0;
}

static int show_tree_object(const struct object_id *oid UNUSED,
			    struct strbuf *base UNUSED,
			    const char *pathname, unsigned mode,
			    void *context)
{
	FILE *file = context;
	fprintf(file, "%s%s\n", pathname, S_ISDIR(mode) ? "/" : "");
	return 0;
}

static void show_setup_revisions_tweak(struct rev_info *rev,
				       struct setup_revision_opt *opt)
{
	if (rev->first_parent_only)
		diff_merges_default_to_first_parent(rev);
	else
		diff_merges_default_to_dense_combined(rev);
	if (!rev->diffopt.output_format)
		rev->diffopt.output_format = DIFF_FORMAT_PATCH;
}

int cmd_show(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	unsigned int i;
	struct setup_revision_opt opt;
	struct pathspec match_all;
	int ret = 0;

	init_log_defaults();
	git_config(git_log_config, NULL);

	if (the_repository->gitdir) {
		prepare_repo_settings(the_repository);
		the_repository->settings.command_requires_full_index = 0;
	}

	memset(&match_all, 0, sizeof(match_all));
	repo_init_revisions(the_repository, &rev, prefix);
	git_config(grep_config, &rev.grep_filter);

	rev.diff = 1;
	rev.always_show_header = 1;
	rev.no_walk = 1;
	rev.diffopt.stat_width = -1; 	/* Scale to real terminal size */

	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.tweak = show_setup_revisions_tweak;
	cmd_log_init(argc, argv, prefix, &rev, &opt);

	if (!rev.no_walk)
		return cmd_log_deinit(cmd_log_walk(&rev), &rev);

	rev.diffopt.no_free = 1;
	for (i = 0; i < rev.pending.nr && !ret; i++) {
		struct object *o = rev.pending.objects[i].item;
		const char *name = rev.pending.objects[i].name;
		switch (o->type) {
		case OBJ_BLOB:
			ret = show_blob_object(&o->oid, &rev, name);
			break;
		case OBJ_TAG: {
			struct tag *t = (struct tag *)o;
			struct object_id *oid = get_tagged_oid(t);

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
			o = parse_object(the_repository, oid);
			if (!o)
				ret = error(_("could not read object %s"),
					    oid_to_hex(oid));
			rev.pending.objects[i].item = o;
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
			read_tree(the_repository, (struct tree *)o,
				  &match_all, show_tree_object,
				  rev.diffopt.file);
			rev.shown_one = 1;
			break;
		case OBJ_COMMIT:
		{
			struct object_array old;
			struct object_array blank = OBJECT_ARRAY_INIT;

			memcpy(&old, &rev.pending, sizeof(old));
			memcpy(&rev.pending, &blank, sizeof(rev.pending));

			add_object_array(o, name, &rev.pending);
			ret = cmd_log_walk_no_free(&rev);

			/*
			 * No need for
			 * object_array_clear(&pending). It was
			 * cleared already in prepare_revision_walk()
			 */
			memcpy(&rev.pending, &old, sizeof(rev.pending));
			break;
		}
		default:
			ret = error(_("unknown type: %d"), o->type);
		}
	}

	rev.diffopt.no_free = 0;
	diff_free(&rev.diffopt);

	return cmd_log_deinit(ret, &rev);
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

	repo_init_revisions(the_repository, &rev, prefix);
	init_reflog_walk(&rev.reflog_info);
	git_config(grep_config, &rev.grep_filter);

	rev.verbose_header = 1;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	cmd_log_init_defaults(&rev);
	rev.abbrev_commit = 1;
	rev.commit_format = CMIT_FMT_ONELINE;
	rev.use_terminator = 1;
	rev.always_show_header = 1;
	cmd_log_init_finish(argc, argv, prefix, &rev, &opt);

	return cmd_log_deinit(cmd_log_walk(&rev), &rev);
}

static void log_setup_revisions_tweak(struct rev_info *rev,
				      struct setup_revision_opt *opt)
{
	if (rev->diffopt.flags.default_follow_renames &&
	    rev->prune_data.nr == 1)
		rev->diffopt.flags.follow_renames = 1;

	if (rev->first_parent_only)
		diff_merges_default_to_first_parent(rev);
}

int cmd_log(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_log_defaults();
	git_config(git_log_config, NULL);

	repo_init_revisions(the_repository, &rev, prefix);
	git_config(grep_config, &rev.grep_filter);

	rev.always_show_header = 1;
	memset(&opt, 0, sizeof(opt));
	opt.def = "HEAD";
	opt.revarg_opt = REVARG_COMMITTISH;
	opt.tweak = log_setup_revisions_tweak;
	cmd_log_init(argc, argv, prefix, &rev, &opt);
	return cmd_log_deinit(cmd_log_walk(&rev), &rev);
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

enum cover_setting {
	COVER_UNSET,
	COVER_OFF,
	COVER_ON,
	COVER_AUTO
};

enum thread_level {
	THREAD_UNSET,
	THREAD_SHALLOW,
	THREAD_DEEP
};

enum cover_from_description {
	COVER_FROM_NONE,
	COVER_FROM_MESSAGE,
	COVER_FROM_SUBJECT,
	COVER_FROM_AUTO
};

enum auto_base_setting {
	AUTO_BASE_NEVER,
	AUTO_BASE_ALWAYS,
	AUTO_BASE_WHEN_ABLE
};

static enum thread_level thread;
static int do_signoff;
static enum auto_base_setting auto_base;
static char *from;
static const char *signature = git_version_string;
static const char *signature_file;
static enum cover_setting config_cover_letter;
static const char *config_output_directory;
static enum cover_from_description cover_from_description_mode = COVER_FROM_MESSAGE;
static int show_notes;
static struct display_notes_opt notes_opt;

static enum cover_from_description parse_cover_from_description(const char *arg)
{
	if (!arg || !strcmp(arg, "default"))
		return COVER_FROM_MESSAGE;
	else if (!strcmp(arg, "none"))
		return COVER_FROM_NONE;
	else if (!strcmp(arg, "message"))
		return COVER_FROM_MESSAGE;
	else if (!strcmp(arg, "subject"))
		return COVER_FROM_SUBJECT;
	else if (!strcmp(arg, "auto"))
		return COVER_FROM_AUTO;
	else
		die(_("%s: invalid cover from description mode"), arg);
}

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
		else if (value && !*value)
			FREE_AND_NULL(default_attach);
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
		thread = git_config_bool(var, value) ? THREAD_SHALLOW : THREAD_UNSET;
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
		if (value && !strcasecmp(value, "whenAble")) {
			auto_base = AUTO_BASE_WHEN_ABLE;
			return 0;
		}
		auto_base = git_config_bool(var, value) ? AUTO_BASE_ALWAYS : AUTO_BASE_NEVER;
		return 0;
	}
	if (!strcmp(var, "format.from")) {
		int b = git_parse_maybe_bool(value);
		free(from);
		if (b < 0)
			from = xstrdup(value);
		else if (b)
			from = xstrdup(git_committer_info(IDENT_NO_DATE));
		else
			from = NULL;
		return 0;
	}
	if (!strcmp(var, "format.forceinbodyfrom")) {
		force_in_body_from = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "format.notes")) {
		int b = git_parse_maybe_bool(value);
		if (b < 0)
			enable_ref_display_notes(&notes_opt, &show_notes, value);
		else if (b)
			enable_default_display_notes(&notes_opt, &show_notes);
		else
			disable_display_notes(&notes_opt, &show_notes);
		return 0;
	}
	if (!strcmp(var, "format.coverfromdescription")) {
		cover_from_description_mode = parse_cover_from_description(value);
		return 0;
	}
	if (!strcmp(var, "format.mboxrd")) {
		stdout_mboxrd = git_config_bool(var, value);
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

	if (output_directory) {
		strbuf_addstr(&filename, output_directory);
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

	if (!(rev->diffopt.file = fopen(filename.buf, "w"))) {
		error_errno(_("cannot open patch file %s"), filename.buf);
		strbuf_release(&filename);
		return -1;
	}

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
		die(_("need exactly one range"));

	o1 = rev->pending.objects[0].item;
	o2 = rev->pending.objects[1].item;
	flags1 = o1->flags;
	flags2 = o2->flags;
	c1 = lookup_commit_reference(the_repository, &o1->oid);
	c2 = lookup_commit_reference(the_repository, &o2->oid);

	if ((flags1 & UNINTERESTING) == (flags2 & UNINTERESTING))
		die(_("not a range"));

	init_patch_ids(the_repository, ids);

	/* given a range a..b get all patch ids for b..a */
	repo_init_revisions(the_repository, &check_rev, rev->prefix);
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
	strbuf_addf(&buf, "%s.%"PRItime".git.%s", base,
		    (timestamp_t) time(NULL),
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
	if (repo_dwim_ref(the_repository, ref, strlen(ref), &branch_oid,
			  &full_ref, 0) &&
	    skip_prefix(full_ref, "refs/heads/", &v) &&
	    oideq(tip_oid, &branch_oid))
		branch = xstrdup(v);
	free(full_ref);
	return branch;
}

static void show_diffstat(struct rev_info *rev,
			  struct commit *origin, struct commit *head)
{
	struct diff_options opts;

	memcpy(&opts, &rev->diffopt, sizeof(opts));
	opts.output_format = DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;
	diff_setup_done(&opts);

	diff_tree_oid(get_commit_tree_oid(origin),
		      get_commit_tree_oid(head),
		      "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);

	fprintf(rev->diffopt.file, "\n");
}

static void prepare_cover_text(struct pretty_print_context *pp,
			       const char *branch_name,
			       struct strbuf *sb,
			       const char *encoding,
			       int need_8bit_cte)
{
	const char *subject = "*** SUBJECT HERE ***";
	const char *body = "*** BLURB HERE ***";
	struct strbuf description_sb = STRBUF_INIT;
	struct strbuf subject_sb = STRBUF_INIT;

	if (cover_from_description_mode == COVER_FROM_NONE)
		goto do_pp;

	if (branch_name && *branch_name)
		read_branch_desc(&description_sb, branch_name);
	if (!description_sb.len)
		goto do_pp;

	if (cover_from_description_mode == COVER_FROM_SUBJECT ||
			cover_from_description_mode == COVER_FROM_AUTO)
		body = format_subject(&subject_sb, description_sb.buf, " ");

	if (cover_from_description_mode == COVER_FROM_MESSAGE ||
			(cover_from_description_mode == COVER_FROM_AUTO &&
			 subject_sb.len > COVER_FROM_AUTO_MAX_SUBJECT_LEN))
		body = description_sb.buf;
	else
		subject = subject_sb.buf;

do_pp:
	pp_title_line(pp, &subject, sb, encoding, need_8bit_cte);
	pp_remainder(pp, &body, sb, 0);

	strbuf_release(&description_sb);
	strbuf_release(&subject_sb);
}

static int get_notes_refs(struct string_list_item *item, void *arg)
{
	strvec_pushf(arg, "--notes=%s", item->string);
	return 0;
}

static void get_notes_args(struct strvec *arg, struct rev_info *rev)
{
	if (!rev->show_notes) {
		strvec_push(arg, "--no-notes");
	} else if (rev->notes_opt.use_default_notes > 0 ||
		   (rev->notes_opt.use_default_notes == -1 &&
		    !rev->notes_opt.extra_notes_refs.nr)) {
		strvec_push(arg, "--notes");
	} else {
		for_each_string_list(&rev->notes_opt.extra_notes_refs, get_notes_refs, arg);
	}
}

static void make_cover_letter(struct rev_info *rev, int use_separate_file,
			      struct commit *origin,
			      int nr, struct commit **list,
			      const char *branch_name,
			      int quiet)
{
	const char *committer;
	struct shortlog log;
	struct strbuf sb = STRBUF_INIT;
	int i;
	const char *encoding = "UTF-8";
	int need_8bit_cte = 0;
	struct pretty_print_context pp = {0};
	struct commit *head = list[0];

	if (!cmit_fmt_is_mail(rev->commit_format))
		die(_("cover letter needs email format"));

	committer = git_committer_info(0);

	if (use_separate_file &&
	    open_next_file(NULL, rev->numbered_files ? NULL : "cover-letter", rev, quiet))
		die(_("failed to create cover-letter file"));

	log_write_email_headers(rev, head, &pp.after_subject, &need_8bit_cte, 0);

	for (i = 0; !need_8bit_cte && i < nr; i++) {
		const char *buf = repo_get_commit_buffer(the_repository,
							 list[i], NULL);
		if (has_non_ascii(buf))
			need_8bit_cte = 1;
		repo_unuse_commit_buffer(the_repository, list[i], buf);
	}

	if (!branch_name)
		branch_name = find_branch_name(rev);

	pp.fmt = CMIT_FMT_EMAIL;
	pp.date_mode.type = DATE_RFC2822;
	pp.rev = rev;
	pp.print_email_subject = 1;
	pp_user_info(&pp, NULL, &sb, committer, encoding);
	prepare_cover_text(&pp, branch_name, &sb, encoding, need_8bit_cte);
	fprintf(rev->diffopt.file, "%s\n", sb.buf);

	strbuf_release(&sb);

	shortlog_init(&log);
	log.wrap_lines = 1;
	log.wrap = MAIL_DEFAULT_WRAP;
	log.in1 = 2;
	log.in2 = 4;
	log.file = rev->diffopt.file;
	log.groups = SHORTLOG_GROUP_AUTHOR;
	shortlog_finish_setup(&log);
	for (i = 0; i < nr; i++)
		shortlog_add_commit(&log, list[i]);

	shortlog_output(&log);

	/* We can only do diffstat with a unique reference point */
	if (origin)
		show_diffstat(rev, origin, head);

	if (rev->idiff_oid1) {
		fprintf_ln(rev->diffopt.file, "%s", rev->idiff_title);
		show_interdiff(rev->idiff_oid1, rev->idiff_oid2, 0,
			       &rev->diffopt);
	}

	if (rev->rdiff1) {
		/*
		 * Pass minimum required diff-options to range-diff; others
		 * can be added later if deemed desirable.
		 */
		struct diff_options opts;
		struct strvec other_arg = STRVEC_INIT;
		struct range_diff_options range_diff_opts = {
			.creation_factor = rev->creation_factor,
			.dual_color = 1,
			.diffopt = &opts,
			.other_arg = &other_arg
		};

		repo_diff_setup(the_repository, &opts);
		opts.file = rev->diffopt.file;
		opts.use_color = rev->diffopt.use_color;
		diff_setup_done(&opts);
		fprintf_ln(rev->diffopt.file, "%s", rev->rdiff_title);
		get_notes_args(&other_arg, rev);
		show_range_diff(rev->rdiff1, rev->rdiff2, &range_diff_opts);
		strvec_clear(&other_arg);
	}
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
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	((struct rev_info *)opt->value)->total = -1;
	keep_subject = 1;
	return 0;
}

static int subject_prefix = 0;

static int subject_prefix_callback(const struct option *opt, const char *arg,
			    int unset)
{
	BUG_ON_OPT_NEG(unset);
	subject_prefix = 1;
	((struct rev_info *)opt->value)->subject_prefix = arg;
	return 0;
}

static int rfc_callback(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return subject_prefix_callback(opt, "RFC PATCH", unset);
}

static int numbered_cmdline_opt = 0;

static int numbered_callback(const struct option *opt, const char *arg,
			     int unset)
{
	BUG_ON_OPT_ARG(arg);
	*(int *)opt->value = numbered_cmdline_opt = unset ? 0 : 1;
	if (unset)
		auto_number =  0;
	return 0;
}

static int no_numbered_callback(const struct option *opt, const char *arg,
				int unset)
{
	BUG_ON_OPT_NEG(unset);
	return numbered_callback(opt, arg, 1);
}

static int output_directory_callback(const struct option *opt, const char *arg,
			      int unset)
{
	const char **dir = (const char **)opt->value;
	BUG_ON_OPT_NEG(unset);
	if (*dir)
		die(_("two output directories?"));
	*dir = arg;
	return 0;
}

static int thread_callback(const struct option *opt, const char *arg, int unset)
{
	enum thread_level *thread = (enum thread_level *)opt->value;
	if (unset)
		*thread = THREAD_UNSET;
	else if (!arg || !strcmp(arg, "shallow"))
		*thread = THREAD_SHALLOW;
	else if (!strcmp(arg, "deep"))
		*thread = THREAD_DEEP;
	/*
	 * Please update _git_formatpatch() in git-completion.bash
	 * when you add new options.
	 */
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

static int base_callback(const struct option *opt, const char *arg, int unset)
{
	const char **base_commit = opt->value;

	if (unset) {
		auto_base = AUTO_BASE_NEVER;
		*base_commit = NULL;
	} else if (!strcmp(arg, "auto")) {
		auto_base = AUTO_BASE_ALWAYS;
		*base_commit = NULL;
	} else {
		auto_base = AUTO_BASE_NEVER;
		*base_commit = arg;
	}
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
	int i = 0, rev_nr = 0, auto_select, die_on_failure;

	switch (auto_base) {
	case AUTO_BASE_NEVER:
		if (base_commit) {
			auto_select = 0;
			die_on_failure = 1;
		} else {
			/* no base information is requested */
			return NULL;
		}
		break;
	case AUTO_BASE_ALWAYS:
	case AUTO_BASE_WHEN_ABLE:
		if (base_commit) {
			BUG("requested automatic base selection but a commit was provided");
		} else {
			auto_select = 1;
			die_on_failure = auto_base == AUTO_BASE_ALWAYS;
		}
		break;
	default:
		BUG("unexpected automatic base selection method");
	}

	if (!auto_select) {
		base = lookup_commit_reference_by_name(base_commit);
		if (!base)
			die(_("unknown commit %s"), base_commit);
	} else {
		struct branch *curr_branch = branch_get(NULL);
		const char *upstream = branch_get_upstream(curr_branch, NULL);
		if (upstream) {
			struct commit_list *base_list;
			struct commit *commit;
			struct object_id oid;

			if (repo_get_oid(the_repository, upstream, &oid)) {
				if (die_on_failure)
					die(_("failed to resolve '%s' as a valid ref"), upstream);
				else
					return NULL;
			}
			commit = lookup_commit_or_die(&oid, "upstream base");
			base_list = repo_get_merge_bases_many(the_repository,
							      commit, total,
							      list);
			/* There should be one and only one merge base. */
			if (!base_list || base_list->next) {
				if (die_on_failure) {
					die(_("could not find exact merge base"));
				} else {
					free_commit_list(base_list);
					return NULL;
				}
			}
			base = base_list->item;
			free_commit_list(base_list);
		} else {
			if (die_on_failure)
				die(_("failed to get upstream, if you want to record base commit automatically,\n"
				      "please use git branch --set-upstream-to to track a remote branch.\n"
				      "Or you could specify base commit by --base=<base-commit-id> manually"));
			else
				return NULL;
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
			merge_base = repo_get_merge_bases(the_repository,
							  rev[2 * i],
							  rev[2 * i + 1]);
			if (!merge_base || merge_base->next) {
				if (die_on_failure) {
					die(_("failed to find exact merge base"));
				} else {
					free(rev);
					return NULL;
				}
			}

			rev[i] = merge_base->item;
		}

		if (rev_nr % 2)
			rev[i] = rev[2 * i];
		rev_nr = DIV_ROUND_UP(rev_nr, 2);
	}

	if (!repo_in_merge_bases(the_repository, base, rev[0])) {
		if (die_on_failure) {
			die(_("base commit should be the ancestor of revision list"));
		} else {
			free(rev);
			return NULL;
		}
	}

	for (i = 0; i < total; i++) {
		if (base == list[i]) {
			if (die_on_failure) {
				die(_("base commit shouldn't be in revision list"));
			} else {
				free(rev);
				return NULL;
			}
		}
	}

	free(rev);
	return base;
}

define_commit_slab(commit_base, int);

static void prepare_bases(struct base_tree_info *bases,
			  struct commit *base,
			  struct commit **list,
			  int total)
{
	struct commit *commit;
	struct rev_info revs;
	struct diff_options diffopt;
	struct commit_base commit_base;
	int i;

	if (!base)
		return;

	init_commit_base(&commit_base);
	repo_diff_setup(the_repository, &diffopt);
	diffopt.flags.recursive = 1;
	diff_setup_done(&diffopt);

	oidcpy(&bases->base_commit, &base->object.oid);

	repo_init_revisions(the_repository, &revs, NULL);
	revs.max_parents = 1;
	revs.topo_order = 1;
	for (i = 0; i < total; i++) {
		list[i]->object.flags &= ~UNINTERESTING;
		add_pending_object(&revs, &list[i]->object, "rev_list");
		*commit_base_at(&commit_base, list[i]) = 1;
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
		if (*commit_base_at(&commit_base, commit))
			continue;
		if (commit_patch_id(commit, &diffopt, &oid, 0))
			die(_("cannot get patch id"));
		ALLOC_GROW(bases->patch_id, bases->nr_patch_id + 1, bases->alloc_patch_id);
		patch_id = bases->patch_id + bases->nr_patch_id;
		oidcpy(patch_id, &oid);
		bases->nr_patch_id++;
	}
	clear_commit_base(&commit_base);
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

static const char *diff_title(struct strbuf *sb,
			      const char *reroll_count,
			      const char *generic,
			      const char *rerolled)
{
	int v;

	/* RFC may be v0, so allow -v1 to diff against v0 */
	if (reroll_count && !strtol_i(reroll_count, 10, &v) &&
	    v >= 1)
		strbuf_addf(sb, rerolled, v - 1);
	else
		strbuf_addstr(sb, generic);
	return sb->buf;
}

static void infer_range_diff_ranges(struct strbuf *r1,
				    struct strbuf *r2,
				    const char *prev,
				    struct commit *origin,
				    struct commit *head)
{
	const char *head_oid = oid_to_hex(&head->object.oid);
	int prev_is_range = is_range_diff_range(prev);

	if (prev_is_range)
		strbuf_addstr(r1, prev);
	else
		strbuf_addf(r1, "%s..%s", head_oid, prev);

	if (origin)
		strbuf_addf(r2, "%s..%s", oid_to_hex(&origin->object.oid), head_oid);
	else if (prev_is_range)
		die(_("failed to infer range-diff origin of current series"));
	else {
		warning(_("using '%s' as range-diff origin of current series"), prev);
		strbuf_addf(r2, "%s..%s", prev, head_oid);
	}
}

int cmd_format_patch(int argc, const char **argv, const char *prefix)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	char *to_free = NULL;
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
	const char *reroll_count = NULL;
	char *cover_from_description_arg = NULL;
	char *branch_name = NULL;
	char *base_commit = NULL;
	struct base_tree_info bases;
	struct commit *base;
	int show_progress = 0;
	struct progress *progress = NULL;
	struct oid_array idiff_prev = OID_ARRAY_INIT;
	struct strbuf idiff_title = STRBUF_INIT;
	const char *rdiff_prev = NULL;
	struct strbuf rdiff1 = STRBUF_INIT;
	struct strbuf rdiff2 = STRBUF_INIT;
	struct strbuf rdiff_title = STRBUF_INIT;
	struct strbuf sprefix = STRBUF_INIT;
	int creation_factor = -1;

	const struct option builtin_format_patch_options[] = {
		OPT_CALLBACK_F('n', "numbered", &numbered, NULL,
			    N_("use [PATCH n/m] even with a single patch"),
			    PARSE_OPT_NOARG, numbered_callback),
		OPT_CALLBACK_F('N', "no-numbered", &numbered, NULL,
			    N_("use [PATCH] even with multiple patches"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, no_numbered_callback),
		OPT_BOOL('s', "signoff", &do_signoff, N_("add a Signed-off-by trailer")),
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
		OPT_STRING('v', "reroll-count", &reroll_count, N_("reroll-count"),
			    N_("mark the series as Nth re-roll")),
		OPT_INTEGER(0, "filename-max-length", &fmt_patch_name_max,
			    N_("max length of output filename")),
		OPT_CALLBACK_F(0, "rfc", &rev, NULL,
			    N_("use [RFC PATCH] instead of [PATCH]"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, rfc_callback),
		OPT_STRING(0, "cover-from-description", &cover_from_description_arg,
			    N_("cover-from-description-mode"),
			    N_("generate parts of a cover letter based on a branch's description")),
		OPT_CALLBACK_F(0, "subject-prefix", &rev, N_("prefix"),
			    N_("use [<prefix>] instead of [PATCH]"),
			    PARSE_OPT_NONEG, subject_prefix_callback),
		OPT_CALLBACK_F('o', "output-directory", &output_directory,
			    N_("dir"), N_("store resulting files in <dir>"),
			    PARSE_OPT_NONEG, output_directory_callback),
		OPT_CALLBACK_F('k', "keep-subject", &rev, NULL,
			    N_("don't strip/add [PATCH]"),
			    PARSE_OPT_NOARG | PARSE_OPT_NONEG, keep_callback),
		OPT_BOOL(0, "no-binary", &no_binary_diff,
			 N_("don't output binary diffs")),
		OPT_BOOL(0, "zero-commit", &zero_commit,
			 N_("output all-zero hash in From header")),
		OPT_BOOL(0, "ignore-if-in-upstream", &ignore_if_in_upstream,
			 N_("don't include a patch matching a commit upstream")),
		OPT_SET_INT_F('p', "no-stat", &use_patch_format,
			      N_("show patch format instead of default (patch + stat)"),
			      1, PARSE_OPT_NONEG),
		OPT_GROUP(N_("Messaging")),
		OPT_CALLBACK(0, "add-header", NULL, N_("header"),
			    N_("add email header"), header_callback),
		OPT_CALLBACK(0, "to", NULL, N_("email"), N_("add To: header"), to_callback),
		OPT_CALLBACK(0, "cc", NULL, N_("email"), N_("add Cc: header"), cc_callback),
		OPT_CALLBACK_F(0, "from", &from, N_("ident"),
			    N_("set From address to <ident> (or committer ident if absent)"),
			    PARSE_OPT_OPTARG, from_callback),
		OPT_STRING(0, "in-reply-to", &in_reply_to, N_("message-id"),
			    N_("make first mail a reply to <message-id>")),
		OPT_CALLBACK_F(0, "attach", &rev, N_("boundary"),
			    N_("attach the patch"), PARSE_OPT_OPTARG,
			    attach_callback),
		OPT_CALLBACK_F(0, "inline", &rev, N_("boundary"),
			    N_("inline the patch"),
			    PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			    inline_callback),
		OPT_CALLBACK_F(0, "thread", &thread, N_("style"),
			    N_("enable message threading, styles: shallow, deep"),
			    PARSE_OPT_OPTARG, thread_callback),
		OPT_STRING(0, "signature", &signature, N_("signature"),
			    N_("add a signature")),
		OPT_CALLBACK_F(0, "base", &base_commit, N_("base-commit"),
			       N_("add prerequisite tree info to the patch series"),
			       0, base_callback),
		OPT_FILENAME(0, "signature-file", &signature_file,
				N_("add a signature from a file")),
		OPT__QUIET(&quiet, N_("don't print the patch filenames")),
		OPT_BOOL(0, "progress", &show_progress,
			 N_("show progress while generating patches")),
		OPT_CALLBACK(0, "interdiff", &idiff_prev, N_("rev"),
			     N_("show changes against <rev> in cover letter or single patch"),
			     parse_opt_object_name),
		OPT_STRING(0, "range-diff", &rdiff_prev, N_("refspec"),
			   N_("show changes against <refspec> in cover letter or single patch")),
		OPT_INTEGER(0, "creation-factor", &creation_factor,
			    N_("percentage by which creation is weighted")),
		OPT_BOOL(0, "force-in-body-from", &force_in_body_from,
			 N_("show in-body From: even if identical to the e-mail header")),
		OPT_END()
	};

	extra_hdr.strdup_strings = 1;
	extra_to.strdup_strings = 1;
	extra_cc.strdup_strings = 1;

	init_log_defaults();
	init_display_notes(&notes_opt);
	git_config(git_format_config, NULL);
	repo_init_revisions(the_repository, &rev, prefix);
	git_config(grep_config, &rev.grep_filter);

	rev.show_notes = show_notes;
	memcpy(&rev.notes_opt, &notes_opt, sizeof(notes_opt));
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.encode_email_headers = default_encode_email_headers;
	rev.expand_tabs_in_log_default = 0;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.max_parents = 1;
	rev.diffopt.flags.recursive = 1;
	rev.diffopt.no_free = 1;
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
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	rev.force_in_body_from = force_in_body_from;

	/* Make sure "0000-$sub.patch" gives non-negative length for $sub */
	if (fmt_patch_name_max <= strlen("0000-") + strlen(fmt_patch_suffix))
		fmt_patch_name_max = strlen("0000-") + strlen(fmt_patch_suffix);

	if (cover_from_description_arg)
		cover_from_description_mode = parse_cover_from_description(cover_from_description_arg);

	if (reroll_count) {
		strbuf_addf(&sprefix, "%s v%s",
			    rev.subject_prefix, reroll_count);
		rev.reroll_count = reroll_count;
		rev.subject_prefix = sprefix.buf;
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

	rev.extra_headers = to_free = strbuf_detach(&buf, NULL);

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
		die(_("options '%s' and '%s' cannot be used together"), "-n", "-k");
	if (keep_subject && subject_prefix)
		die(_("options '%s' and '%s' cannot be used together"), "--subject-prefix/--rfc", "-k");
	rev.preserve_subject = keep_subject;

	argc = setup_revisions(argc, argv, &rev, &s_r_opt);
	if (argc > 1)
		die(_("unrecognized argument: %s"), argv[1]);

	if (rev.diffopt.output_format & DIFF_FORMAT_NAME)
		die(_("--name-only does not make sense"));
	if (rev.diffopt.output_format & DIFF_FORMAT_NAME_STATUS)
		die(_("--name-status does not make sense"));
	if (rev.diffopt.output_format & DIFF_FORMAT_CHECKDIFF)
		die(_("--check does not make sense"));
	if (rev.remerge_diff)
		die(_("--remerge-diff does not make sense"));

	if (!use_patch_format &&
		(!rev.diffopt.output_format ||
		 rev.diffopt.output_format == DIFF_FORMAT_PATCH))
		rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_SUMMARY;
	if (!rev.diffopt.stat_width)
		rev.diffopt.stat_width = MAIL_DEFAULT_WRAP;

	/* Always generate a patch */
	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.always_show_header = 1;

	rev.zero_commit = zero_commit;
	rev.patch_name_max = fmt_patch_name_max;

	if (!rev.diffopt.flags.text && !no_binary_diff)
		rev.diffopt.flags.binary = 1;

	if (rev.show_notes)
		load_display_notes(&rev.notes_opt);

	die_for_incompatible_opt3(use_stdout, "--stdout",
				  rev.diffopt.close_file, "--output",
				  !!output_directory, "--output-directory");

	if (use_stdout && stdout_mboxrd)
		rev.commit_format = CMIT_FMT_MBOXRD;

	if (use_stdout) {
		setup_pager();
	} else if (!rev.diffopt.close_file) {
		int saved;

		if (!output_directory)
			output_directory = config_output_directory;
		output_directory = set_outdir(prefix, output_directory);

		if (rev.diffopt.use_color != GIT_COLOR_ALWAYS)
			rev.diffopt.use_color = GIT_COLOR_NEVER;
		/*
		 * We consider <outdir> as 'outside of gitdir', therefore avoid
		 * applying adjust_shared_perm in s-c-l-d.
		 */
		saved = get_shared_repository();
		set_shared_repository(0);
		switch (safe_create_leading_directories_const(output_directory)) {
		case SCLD_OK:
		case SCLD_EXISTS:
			break;
		default:
			die(_("could not create leading directories "
			      "of '%s'"), output_directory);
		}
		set_shared_repository(saved);
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die_errno(_("could not create directory '%s'"),
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
			const char *ref, *v;
			ref = resolve_ref_unsafe("HEAD", RESOLVE_REF_READING,
						 NULL, NULL);
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
			if (oideq(&o[0].item->oid, &o[1].item->oid))
				goto done;
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
		goto done;
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

	if (idiff_prev.nr) {
		if (!cover_letter && total != 1)
			die(_("--interdiff requires --cover-letter or single patch"));
		rev.idiff_oid1 = &idiff_prev.oid[idiff_prev.nr - 1];
		rev.idiff_oid2 = get_commit_tree_oid(list[0]);
		rev.idiff_title = diff_title(&idiff_title, reroll_count,
					     _("Interdiff:"),
					     _("Interdiff against v%d:"));
	}

	if (creation_factor < 0)
		creation_factor = RANGE_DIFF_CREATION_FACTOR_DEFAULT;
	else if (!rdiff_prev)
		die(_("the option '%s' requires '%s'"), "--creation-factor", "--range-diff");

	if (rdiff_prev) {
		if (!cover_letter && total != 1)
			die(_("--range-diff requires --cover-letter or single patch"));

		infer_range_diff_ranges(&rdiff1, &rdiff2, rdiff_prev,
					origin, list[0]);
		rev.rdiff1 = rdiff1.buf;
		rev.rdiff2 = rdiff2.buf;
		rev.creation_factor = creation_factor;
		rev.rdiff_title = diff_title(&rdiff_title, reroll_count,
					     _("Range-diff:"),
					     _("Range-diff against v%d:"));
	}

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
	base = get_base_commit(base_commit, list, nr);
	if (base) {
		reset_revision_walk();
		clear_object_flags(UNINTERESTING);
		prepare_bases(&bases, base, list, nr);
	}

	if (in_reply_to || thread || cover_letter) {
		rev.ref_message_ids = xmalloc(sizeof(*rev.ref_message_ids));
		string_list_init_nodup(rev.ref_message_ids);
	}
	if (in_reply_to) {
		const char *msgid = clean_message_id(in_reply_to);
		string_list_append(rev.ref_message_ids, msgid);
	}
	rev.numbered_files = just_numbers;
	rev.patch_suffix = fmt_patch_suffix;
	if (cover_letter) {
		if (thread)
			gen_message_id(&rev, "cover");
		make_cover_letter(&rev, !!output_directory,
				  origin, nr, list, branch_name, quiet);
		print_bases(&bases, rev.diffopt.file);
		print_signature(rev.diffopt.file);
		total++;
		start_number--;
		/* interdiff/range-diff in cover-letter; omit from patches */
		rev.idiff_oid1 = NULL;
		rev.rdiff1 = NULL;
	}
	rev.add_signoff = do_signoff;

	if (show_progress)
		progress = start_delayed_progress(_("Generating patches"), total);
	while (0 <= --nr) {
		int shown;
		display_progress(progress, total - nr);
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

		if (output_directory &&
		    open_next_file(rev.numbered_files ? NULL : commit, NULL, &rev, quiet))
			die(_("failed to create output files"));
		shown = log_tree_commit(&rev, commit);
		free_commit_buffer(the_repository->parsed_objects,
				   commit);

		/* We put one extra blank line between formatted
		 * patches and this flag is used by log-tree code
		 * to see if it needs to emit a LF before showing
		 * the log; when using one file per patch, we do
		 * not want the extra blank line.
		 */
		if (output_directory)
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
		if (output_directory)
			fclose(rev.diffopt.file);
	}
	stop_progress(&progress);
	free(list);
	free(branch_name);
	string_list_clear(&extra_to, 0);
	string_list_clear(&extra_cc, 0);
	string_list_clear(&extra_hdr, 0);
	if (ignore_if_in_upstream)
		free_patch_ids(&ids);

done:
	oid_array_clear(&idiff_prev);
	strbuf_release(&idiff_title);
	strbuf_release(&rdiff1);
	strbuf_release(&rdiff2);
	strbuf_release(&rdiff_title);
	strbuf_release(&sprefix);
	free(to_free);
	if (rev.ref_message_ids)
		string_list_clear(rev.ref_message_ids, 0);
	free(rev.ref_message_ids);
	return cmd_log_deinit(0, &rev);
}

static int add_pending_commit(const char *arg, struct rev_info *revs, int flags)
{
	struct object_id oid;
	if (repo_get_oid(the_repository, arg, &oid) == 0) {
		struct commit *commit = lookup_commit_reference(the_repository,
								&oid);
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
		       repo_find_unique_abbrev(the_repository, &commit->object.oid, abbrev));
	} else {
		struct strbuf buf = STRBUF_INIT;
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &buf);
		fprintf(file, "%c %s %s\n", sign,
		       repo_find_unique_abbrev(the_repository, &commit->object.oid, abbrev),
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

	repo_init_revisions(the_repository, &revs, prefix);
	revs.max_parents = 1;

	if (add_pending_commit(head, &revs, 0))
		die(_("unknown commit %s"), head);
	if (add_pending_commit(upstream, &revs, UNINTERESTING))
		die(_("unknown commit %s"), upstream);

	/* Don't say anything if head and upstream are the same. */
	if (revs.pending.nr == 2) {
		struct object_array_entry *o = revs.pending.objects;
		if (oideq(&o[0].item->oid, &o[1].item->oid))
			return 0;
	}

	get_patch_ids(&revs, &ids);

	if (limit && add_pending_commit(limit, &revs, UNINTERESTING))
		die(_("unknown commit %s"), limit);

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
