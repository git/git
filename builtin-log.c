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
#include "refs.h"
#include "run-command.h"
#include "shortlog.h"

/* Set a default date-time format for git log ("log.date" config variable) */
static const char *default_date_mode = NULL;

static int default_show_root = 1;
static const char *fmt_patch_subject_prefix = "PATCH";
static const char *fmt_pretty;

static void add_name_decoration(const char *prefix, const char *name, struct object *obj)
{
	int plen = strlen(prefix);
	int nlen = strlen(name);
	struct name_decoration *res = xmalloc(sizeof(struct name_decoration) + plen + nlen);
	memcpy(res->name, prefix, plen);
	memcpy(res->name + plen, name, nlen + 1);
	res->next = add_decoration(&name_decoration, obj, res);
}

static int add_ref_decoration(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct object *obj = parse_object(sha1);
	if (!obj)
		return 0;
	add_name_decoration("", refname, obj);
	while (obj->type == OBJ_TAG) {
		obj = ((struct tag *)obj)->tagged;
		if (!obj)
			break;
		add_name_decoration("tag: ", refname, obj);
	}
	return 0;
}

static void cmd_log_init(int argc, const char **argv, const char *prefix,
		      struct rev_info *rev)
{
	int i;
	int decorate = 0;

	rev->abbrev = DEFAULT_ABBREV;
	rev->commit_format = CMIT_FMT_DEFAULT;
	if (fmt_pretty)
		get_commit_format(fmt_pretty, rev);
	rev->verbose_header = 1;
	DIFF_OPT_SET(&rev->diffopt, RECURSIVE);
	rev->show_root_diff = default_show_root;
	rev->subject_prefix = fmt_patch_subject_prefix;

	if (default_date_mode)
		rev->date_mode = parse_date_format(default_date_mode);

	argc = setup_revisions(argc, argv, rev, "HEAD");

	if (rev->diffopt.pickaxe || rev->diffopt.filter)
		rev->always_show_header = 0;
	if (DIFF_OPT_TST(&rev->diffopt, FOLLOW_RENAMES)) {
		rev->always_show_header = 0;
		if (rev->diffopt.nr_paths != 1)
			usage("git logs can only follow renames on one pathname at a time");
	}
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--decorate")) {
			if (!decorate)
				for_each_ref(add_ref_decoration, NULL);
			decorate = 1;
		} else
			die("unrecognized argument: %s", arg);
	}
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
	printf("Final output: %d %s\n", nr, stage);
}

struct itimerval early_output_timer;

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

	if (rev->early_output)
		setup_early_output(rev);

	if (prepare_revision_walk(rev))
		die("revision walk setup failed");

	if (rev->early_output)
		finish_early_output(rev);

	while ((commit = get_revision(rev)) != NULL) {
		log_tree_commit(rev, commit);
		if (!rev->reflog_info) {
			/* we allow cycles in reflog ancestry */
			free(commit->buffer);
			commit->buffer = NULL;
		}
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	return 0;
}

static int git_log_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "format.pretty"))
		return git_config_string(&fmt_pretty, var, value);
	if (!strcmp(var, "format.subjectprefix"))
		return git_config_string(&fmt_patch_subject_prefix, var, value);
	if (!strcmp(var, "log.date"))
		return git_config_string(&default_date_mode, var, value);
	if (!strcmp(var, "log.showroot")) {
		default_show_root = git_config_bool(var, value);
		return 0;
	}
	return git_diff_ui_config(var, value, cb);
}

int cmd_whatchanged(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;

	git_config(git_log_config, NULL);

	if (diff_use_color_default == -1)
		diff_use_color_default = git_use_color_default;

	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.simplify_history = 0;
	cmd_log_init(argc, argv, prefix, &rev);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return cmd_log_walk(&rev);
}

static void show_tagger(char *buf, int len, struct rev_info *rev)
{
	char *email_end, *p;
	unsigned long date;
	int tz;

	email_end = memchr(buf, '>', len);
	if (!email_end)
		return;
	p = ++email_end;
	while (isspace(*p))
		p++;
	date = strtoul(p, &p, 10);
	while (isspace(*p))
		p++;
	tz = (int)strtol(p, NULL, 10);
	printf("Tagger: %.*s\nDate:   %s\n", (int)(email_end - buf), buf,
	       show_date(date, tz, rev->date_mode));
}

static int show_object(const unsigned char *sha1, int show_tag_object,
	struct rev_info *rev)
{
	unsigned long size;
	enum object_type type;
	char *buf = read_sha1_file(sha1, &type, &size);
	int offset = 0;

	if (!buf)
		return error("Could not read object %s", sha1_to_hex(sha1));

	if (show_tag_object)
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

int cmd_show(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct object_array_entry *objects;
	int i, count, ret = 0;

	git_config(git_log_config, NULL);

	if (diff_use_color_default == -1)
		diff_use_color_default = git_use_color_default;

	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.combine_merges = 1;
	rev.dense_combined_merges = 1;
	rev.always_show_header = 1;
	rev.ignore_merges = 0;
	rev.no_walk = 1;
	cmd_log_init(argc, argv, prefix, &rev);

	count = rev.pending.nr;
	objects = rev.pending.objects;
	for (i = 0; i < count && !ret; i++) {
		struct object *o = objects[i].item;
		const char *name = objects[i].name;
		switch (o->type) {
		case OBJ_BLOB:
			ret = show_object(o->sha1, 0, NULL);
			break;
		case OBJ_TAG: {
			struct tag *t = (struct tag *)o;

			printf("%stag %s%s\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					t->tag,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			ret = show_object(o->sha1, 1, &rev);
			objects[i].item = parse_object(t->tagged->sha1);
			i--;
			break;
		}
		case OBJ_TREE:
			printf("%stree %s%s\n\n",
					diff_get_color_opt(&rev.diffopt, DIFF_COMMIT),
					name,
					diff_get_color_opt(&rev.diffopt, DIFF_RESET));
			read_tree_recursive((struct tree *)o, "", 0, 0, NULL,
					show_tree_object, NULL);
			break;
		case OBJ_COMMIT:
			rev.pending.nr = rev.pending.alloc = 0;
			rev.pending.objects = NULL;
			add_object_array(o, name, &rev.pending);
			ret = cmd_log_walk(&rev);
			break;
		default:
			ret = error("Unknown type: %d", o->type);
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

	git_config(git_log_config, NULL);

	if (diff_use_color_default == -1)
		diff_use_color_default = git_use_color_default;

	init_revisions(&rev, prefix);
	init_reflog_walk(&rev.reflog_info);
	rev.abbrev_commit = 1;
	rev.verbose_header = 1;
	cmd_log_init(argc, argv, prefix, &rev);

	/*
	 * This means that we override whatever commit format the user gave
	 * on the cmd line.  Sad, but cmd_log_init() currently doesn't
	 * allow us to set a different default.
	 */
	rev.commit_format = CMIT_FMT_ONELINE;
	rev.use_terminator = 1;
	rev.always_show_header = 1;

	/*
	 * We get called through "git reflog", so unlike the other log
	 * routines, we need to set up our pager manually..
	 */
	setup_pager();

	return cmd_log_walk(&rev);
}

int cmd_log(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;

	git_config(git_log_config, NULL);

	if (diff_use_color_default == -1)
		diff_use_color_default = git_use_color_default;

	init_revisions(&rev, prefix);
	rev.always_show_header = 1;
	cmd_log_init(argc, argv, prefix, &rev);
	return cmd_log_walk(&rev);
}

/* format-patch */
#define FORMAT_PATCH_NAME_MAX 64

static int istitlechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '.' || c == '_';
}

static const char *fmt_patch_suffix = ".patch";
static int numbered = 0;
static int auto_number = 0;

static char **extra_hdr;
static int extra_hdr_nr;
static int extra_hdr_alloc;

static char **extra_to;
static int extra_to_nr;
static int extra_to_alloc;

static char **extra_cc;
static int extra_cc_nr;
static int extra_cc_alloc;

static void add_header(const char *value)
{
	int len = strlen(value);
	while (value[len - 1] == '\n')
		len--;
	if (!strncasecmp(value, "to: ", 4)) {
		ALLOC_GROW(extra_to, extra_to_nr + 1, extra_to_alloc);
		extra_to[extra_to_nr++] = xstrndup(value + 4, len - 4);
		return;
	}
	if (!strncasecmp(value, "cc: ", 4)) {
		ALLOC_GROW(extra_cc, extra_cc_nr + 1, extra_cc_alloc);
		extra_cc[extra_cc_nr++] = xstrndup(value + 4, len - 4);
		return;
	}
	ALLOC_GROW(extra_hdr, extra_hdr_nr + 1, extra_hdr_alloc);
	extra_hdr[extra_hdr_nr++] = xstrndup(value, len);
}

static int git_format_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "format.headers")) {
		if (!value)
			die("format.headers without value");
		add_header(value);
		return 0;
	}
	if (!strcmp(var, "format.suffix"))
		return git_config_string(&fmt_patch_suffix, var, value);
	if (!strcmp(var, "format.cc")) {
		if (!value)
			return config_error_nonbool(var);
		ALLOC_GROW(extra_cc, extra_cc_nr + 1, extra_cc_alloc);
		extra_cc[extra_cc_nr++] = xstrdup(value);
		return 0;
	}
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff")) {
		return 0;
	}
	if (!strcmp(var, "format.numbered")) {
		if (value && !strcasecmp(value, "auto")) {
			auto_number = 1;
			return 0;
		}
		numbered = git_config_bool(var, value);
		return 0;
	}

	return git_log_config(var, value, cb);
}


static const char *get_oneline_for_filename(struct commit *commit,
					    int keep_subject)
{
	static char filename[PATH_MAX];
	char *sol;
	int len = 0;
	int suffix_len = strlen(fmt_patch_suffix) + 1;

	sol = strstr(commit->buffer, "\n\n");
	if (!sol)
		filename[0] = '\0';
	else {
		int j, space = 0;

		sol += 2;
		/* strip [PATCH] or [PATCH blabla] */
		if (!keep_subject && !prefixcmp(sol, "[PATCH")) {
			char *eos = strchr(sol + 6, ']');
			if (eos) {
				while (isspace(*eos))
					eos++;
				sol = eos;
			}
		}

		for (j = 0;
		     j < FORMAT_PATCH_NAME_MAX - suffix_len - 5 &&
			     len < sizeof(filename) - suffix_len &&
			     sol[j] && sol[j] != '\n';
		     j++) {
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
		while (filename[len - 1] == '.'
		       || filename[len - 1] == '-')
			len--;
		filename[len] = '\0';
	}
	return filename;
}

static FILE *realstdout = NULL;
static const char *output_directory = NULL;

static int reopen_stdout(const char *oneline, int nr, int total)
{
	char filename[PATH_MAX];
	int len = 0;
	int suffix_len = strlen(fmt_patch_suffix) + 1;

	if (output_directory) {
		len = snprintf(filename, sizeof(filename), "%s",
				output_directory);
		if (len >=
		    sizeof(filename) - FORMAT_PATCH_NAME_MAX - suffix_len)
			return error("name of output directory is too long");
		if (filename[len - 1] != '/')
			filename[len++] = '/';
	}

	if (!oneline)
		len += sprintf(filename + len, "%d", nr);
	else {
		len += sprintf(filename + len, "%04d-", nr);
		len += snprintf(filename + len, sizeof(filename) - len - 1
				- suffix_len, "%s", oneline);
		strcpy(filename + len, fmt_patch_suffix);
	}

	fprintf(realstdout, "%s\n", filename);
	if (freopen(filename, "w", stdout) == NULL)
		return error("Cannot open patch file %s",filename);

	return 0;
}

static void get_patch_ids(struct rev_info *rev, struct patch_ids *ids, const char *prefix)
{
	struct rev_info check_rev;
	struct commit *commit;
	struct object *o1, *o2;
	unsigned flags1, flags2;

	if (rev->pending.nr != 2)
		die("Need exactly one range.");

	o1 = rev->pending.objects[0].item;
	flags1 = o1->flags;
	o2 = rev->pending.objects[1].item;
	flags2 = o2->flags;

	if ((flags1 & UNINTERESTING) == (flags2 & UNINTERESTING))
		die("Not a range.");

	init_patch_ids(ids);

	/* given a range a..b get all patch ids for b..a */
	init_revisions(&check_rev, prefix);
	o1->flags ^= UNINTERESTING;
	o2->flags ^= UNINTERESTING;
	add_pending_object(&check_rev, o1, "o1");
	add_pending_object(&check_rev, o2, "o2");
	if (prepare_revision_walk(&check_rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&check_rev)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

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
	const char *committer = git_committer_info(IDENT_WARN_ON_NO_NAME);
	const char *email_start = strrchr(committer, '<');
	const char *email_end = strrchr(committer, '>');
	struct strbuf buf;
	if (!email_start || !email_end || email_start > email_end - 1)
		die("Could not extract email from committer identity.");
	strbuf_init(&buf, 0);
	strbuf_addf(&buf, "%s.%lu.git.%.*s", base,
		    (unsigned long) time(NULL),
		    (int)(email_end - email_start - 1), email_start + 1);
	info->message_id = strbuf_detach(&buf, NULL);
}

static void make_cover_letter(struct rev_info *rev, int use_stdout,
			      int numbered, int numbered_files,
			      struct commit *origin,
			      int nr, struct commit **list, struct commit *head)
{
	const char *committer;
	char *head_sha1;
	const char *subject_start = NULL;
	const char *body = "*** SUBJECT HERE ***\n\n*** BLURB HERE ***\n";
	const char *msg;
	const char *extra_headers = rev->extra_headers;
	struct shortlog log;
	struct strbuf sb;
	int i;
	const char *encoding = "utf-8";
	struct diff_options opts;
	int need_8bit_cte = 0;

	if (rev->commit_format != CMIT_FMT_EMAIL)
		die("Cover letter needs email format");

	if (!use_stdout && reopen_stdout(numbered_files ?
				NULL : "cover-letter", 0, rev->total))
		return;

	head_sha1 = sha1_to_hex(head->object.sha1);

	log_write_email_headers(rev, head_sha1, &subject_start, &extra_headers,
				&need_8bit_cte);

	committer = git_committer_info(0);

	msg = body;
	strbuf_init(&sb, 0);
	pp_user_info(NULL, CMIT_FMT_EMAIL, &sb, committer, DATE_RFC2822,
		     encoding);
	pp_title_line(CMIT_FMT_EMAIL, &msg, &sb, subject_start, extra_headers,
		      encoding, need_8bit_cte);
	pp_remainder(CMIT_FMT_EMAIL, &msg, &sb, 0);
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
		die("insane in-reply-to: %s", msg_id);
	if (++z == m)
		return a;
	return xmemdupz(a, z - a);
}

int cmd_format_patch(int argc, const char **argv, const char *prefix)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	int nr = 0, total, i, j;
	int use_stdout = 0;
	int start_number = -1;
	int keep_subject = 0;
	int numbered_files = 0;		/* _just_ numbers */
	int subject_prefix = 0;
	int ignore_if_in_upstream = 0;
	int thread = 0;
	int cover_letter = 0;
	int boundary_count = 0;
	int no_binary_diff = 0;
	struct commit *origin = NULL, *head = NULL;
	const char *in_reply_to = NULL;
	struct patch_ids ids;
	char *add_signoff = NULL;
	struct strbuf buf;

	git_config(git_format_config, NULL);
	init_revisions(&rev, prefix);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.combine_merges = 0;
	rev.ignore_merges = 1;
	DIFF_OPT_SET(&rev.diffopt, RECURSIVE);

	rev.subject_prefix = fmt_patch_subject_prefix;

	/*
	 * Parse the arguments before setup_revisions(), or something
	 * like "git format-patch -o a123 HEAD^.." may fail; a123 is
	 * possibly a valid SHA1.
	 */
	for (i = 1, j = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--stdout"))
			use_stdout = 1;
		else if (!strcmp(argv[i], "-n") ||
				!strcmp(argv[i], "--numbered"))
			numbered = 1;
		else if (!strcmp(argv[i], "-N") ||
				!strcmp(argv[i], "--no-numbered")) {
			numbered = 0;
			auto_number = 0;
		}
		else if (!prefixcmp(argv[i], "--start-number="))
			start_number = strtol(argv[i] + 15, NULL, 10);
		else if (!strcmp(argv[i], "--numbered-files"))
			numbered_files = 1;
		else if (!strcmp(argv[i], "--start-number")) {
			i++;
			if (i == argc)
				die("Need a number for --start-number");
			start_number = strtol(argv[i], NULL, 10);
		}
		else if (!prefixcmp(argv[i], "--cc=")) {
			ALLOC_GROW(extra_cc, extra_cc_nr + 1, extra_cc_alloc);
			extra_cc[extra_cc_nr++] = xstrdup(argv[i] + 5);
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
			committer = git_committer_info(IDENT_ERROR_ON_NO_NAME);
			endpos = strchr(committer, '>');
			if (!endpos)
				die("bogos committer info %s\n", committer);
			add_signoff = xmemdupz(committer, endpos - committer + 1);
		}
		else if (!strcmp(argv[i], "--attach")) {
			rev.mime_boundary = git_version_string;
			rev.no_inline = 1;
		}
		else if (!prefixcmp(argv[i], "--attach=")) {
			rev.mime_boundary = argv[i] + 9;
			rev.no_inline = 1;
		}
		else if (!strcmp(argv[i], "--inline")) {
			rev.mime_boundary = git_version_string;
			rev.no_inline = 0;
		}
		else if (!prefixcmp(argv[i], "--inline=")) {
			rev.mime_boundary = argv[i] + 9;
			rev.no_inline = 0;
		}
		else if (!strcmp(argv[i], "--ignore-if-in-upstream"))
			ignore_if_in_upstream = 1;
		else if (!strcmp(argv[i], "--thread"))
			thread = 1;
		else if (!prefixcmp(argv[i], "--in-reply-to="))
			in_reply_to = argv[i] + 14;
		else if (!strcmp(argv[i], "--in-reply-to")) {
			i++;
			if (i == argc)
				die("Need a Message-Id for --in-reply-to");
			in_reply_to = argv[i];
		} else if (!prefixcmp(argv[i], "--subject-prefix=")) {
			subject_prefix = 1;
			rev.subject_prefix = argv[i] + 17;
		} else if (!prefixcmp(argv[i], "--suffix="))
			fmt_patch_suffix = argv[i] + 9;
		else if (!strcmp(argv[i], "--cover-letter"))
			cover_letter = 1;
		else if (!strcmp(argv[i], "--no-binary"))
			no_binary_diff = 1;
		else
			argv[j++] = argv[i];
	}
	argc = j;

	strbuf_init(&buf, 0);

	for (i = 0; i < extra_hdr_nr; i++) {
		strbuf_addstr(&buf, extra_hdr[i]);
		strbuf_addch(&buf, '\n');
	}

	if (extra_to_nr)
		strbuf_addstr(&buf, "To: ");
	for (i = 0; i < extra_to_nr; i++) {
		if (i)
			strbuf_addstr(&buf, "    ");
		strbuf_addstr(&buf, extra_to[i]);
		if (i + 1 < extra_to_nr)
			strbuf_addch(&buf, ',');
		strbuf_addch(&buf, '\n');
	}

	if (extra_cc_nr)
		strbuf_addstr(&buf, "Cc: ");
	for (i = 0; i < extra_cc_nr; i++) {
		if (i)
			strbuf_addstr(&buf, "    ");
		strbuf_addstr(&buf, extra_cc[i]);
		if (i + 1 < extra_cc_nr)
			strbuf_addch(&buf, ',');
		strbuf_addch(&buf, '\n');
	}

	rev.extra_headers = strbuf_detach(&buf, 0);

	if (start_number < 0)
		start_number = 1;
	if (numbered && keep_subject)
		die ("-n and -k are mutually exclusive.");
	if (keep_subject && subject_prefix)
		die ("--subject-prefix and -k are mutually exclusive.");
	if (numbered_files && use_stdout)
		die ("--numbered-files and --stdout are mutually exclusive.");

	argc = setup_revisions(argc, argv, &rev, "HEAD");
	if (argc > 1)
		die ("unrecognized argument: %s", argv[1]);

	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_SUMMARY | DIFF_FORMAT_PATCH;

	if (!DIFF_OPT_TST(&rev.diffopt, TEXT) && !no_binary_diff)
		DIFF_OPT_SET(&rev.diffopt, BINARY);

	if (!output_directory && !use_stdout)
		output_directory = prefix;

	if (output_directory) {
		if (use_stdout)
			die("standard output, or directory, which one?");
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die("Could not create directory %s",
			    output_directory);
	}

	if (rev.pending.nr == 1) {
		if (rev.max_count < 0 && !rev.show_root_diff) {
			/*
			 * This is traditional behaviour of "git format-patch
			 * origin" that prepares what the origin side still
			 * does not have.
			 */
			rev.pending.objects[0].item->flags |= UNINTERESTING;
			add_head_to_pending(&rev);
		}
		/*
		 * Otherwise, it is "format-patch -22 HEAD", and/or
		 * "format-patch --root HEAD".  The user wants
		 * get_revision() to do the usual traversal.
		 */
	}
	if (cover_letter) {
		/* remember the range */
		int i;
		for (i = 0; i < rev.pending.nr; i++) {
			struct object *o = rev.pending.objects[i].item;
			if (!(o->flags & UNINTERESTING))
				head = (struct commit *)o;
		}
		/* We can't generate a cover letter without any patches */
		if (!head)
			return 0;
	}

	if (ignore_if_in_upstream)
		get_patch_ids(&rev, &ids, prefix);

	if (!use_stdout)
		realstdout = xfdopen(xdup(1), "w");

	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");
	rev.boundary = 1;
	while ((commit = get_revision(&rev)) != NULL) {
		if (commit->object.flags & BOUNDARY) {
			boundary_count++;
			origin = (boundary_count == 1) ? commit : NULL;
			continue;
		}

		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

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
	if (in_reply_to)
		rev.ref_message_id = clean_message_id(in_reply_to);
	if (cover_letter) {
		if (thread)
			gen_message_id(&rev, "cover");
		make_cover_letter(&rev, use_stdout, numbered, numbered_files,
				  origin, nr, list, head);
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
				 * If we've got the ID to be a reply
				 * to, discard the current ID;
				 * otherwise, make everything a reply
				 * to that.
				 */
				if (rev.ref_message_id)
					free(rev.message_id);
				else
					rev.ref_message_id = rev.message_id;
			}
			gen_message_id(&rev, sha1_to_hex(commit->object.sha1));
		}
		if (!use_stdout && reopen_stdout(numbered_files ? NULL :
				get_oneline_for_filename(commit, keep_subject),
				rev.nr, rev.total))
			die("Failed to create output files");
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

static const char cherry_usage[] =
"git cherry [-v] <upstream> [<head>] [<limit>]";
int cmd_cherry(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct patch_ids ids;
	struct commit *commit;
	struct commit_list *list = NULL;
	const char *upstream;
	const char *head = "HEAD";
	const char *limit = NULL;
	int verbose = 0;

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		verbose = 1;
		argc--;
		argv++;
	}

	switch (argc) {
	case 4:
		limit = argv[3];
		/* FALLTHROUGH */
	case 3:
		head = argv[2];
		/* FALLTHROUGH */
	case 2:
		upstream = argv[1];
		break;
	default:
		usage(cherry_usage);
	}

	init_revisions(&revs, prefix);
	revs.diff = 1;
	revs.combine_merges = 0;
	revs.ignore_merges = 1;
	DIFF_OPT_SET(&revs.diffopt, RECURSIVE);

	if (add_pending_commit(head, &revs, 0))
		die("Unknown commit %s", head);
	if (add_pending_commit(upstream, &revs, UNINTERESTING))
		die("Unknown commit %s", upstream);

	/* Don't say anything if head and upstream are the same. */
	if (revs.pending.nr == 2) {
		struct object_array_entry *o = revs.pending.objects;
		if (hashcmp(o[0].item->sha1, o[1].item->sha1) == 0)
			return 0;
	}

	get_patch_ids(&revs, &ids, prefix);

	if (limit && add_pending_commit(limit, &revs, UNINTERESTING))
		die("Unknown commit %s", limit);

	/* reverse the list of commits */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	while ((commit = get_revision(&revs)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		commit_list_insert(commit, &list);
	}

	while (list) {
		char sign = '+';

		commit = list->item;
		if (has_commit_patch_id(commit, &ids))
			sign = '-';

		if (verbose) {
			struct strbuf buf;
			strbuf_init(&buf, 0);
			pretty_print_commit(CMIT_FMT_ONELINE, commit,
			                    &buf, 0, NULL, NULL, 0, 0);
			printf("%c %s %s\n", sign,
			       sha1_to_hex(commit->object.sha1), buf.buf);
			strbuf_release(&buf);
		}
		else {
			printf("%c %s\n", sign,
			       sha1_to_hex(commit->object.sha1));
		}

		list = list->next;
	}

	free_patch_ids(&ids);
	return 0;
}
