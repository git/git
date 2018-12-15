/*
 * Blame
 *
 * Copyright (c) 2006, 2014 by its authors
 * See COPYING for licensing conditions
 */

#include "cache.h"
#include "config.h"
#include "color.h"
#include "builtin.h"
#include "repository.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "quote.h"
#include "string-list.h"
#include "mailmap.h"
#include "parse-options.h"
#include "prio-queue.h"
#include "utf8.h"
#include "userdiff.h"
#include "line-range.h"
#include "line-log.h"
#include "dir.h"
#include "progress.h"
#include "object-store.h"
#include "blame.h"
#include "string-list.h"

static char blame_usage[] = N_("git blame [<options>] [<rev-opts>] [<rev>] [--] <file>");

static const char *blame_opt_usage[] = {
	blame_usage,
	"",
	N_("<rev-opts> are documented in git-rev-list(1)"),
	NULL
};

static int longest_file;
static int longest_author;
static int max_orig_digits;
static int max_digits;
static int max_score_digits;
static int show_root;
static int reverse;
static int blank_boundary;
static int incremental;
static int xdl_opts;
static int abbrev = -1;
static int no_whole_file_rename;
static int show_progress;
static char repeated_meta_color[COLOR_MAXLEN];
static int coloring_mode;

static struct date_mode blame_date_mode = { DATE_ISO8601 };
static size_t blame_date_width;

static struct string_list mailmap = STRING_LIST_INIT_NODUP;

#ifndef DEBUG
#define DEBUG 0
#endif

static unsigned blame_move_score;
static unsigned blame_copy_score;

/* Remember to update object flag allocation in object.h */
#define METAINFO_SHOWN		(1u<<12)
#define MORE_THAN_ONE_PATH	(1u<<13)

struct progress_info {
	struct progress *progress;
	int blamed_lines;
};

static const char *nth_line_cb(void *data, long lno)
{
	return blame_nth_line((struct blame_scoreboard *)data, lno);
}

/*
 * Information on commits, used for output.
 */
struct commit_info {
	struct strbuf author;
	struct strbuf author_mail;
	timestamp_t author_time;
	struct strbuf author_tz;

	/* filled only when asked for details */
	struct strbuf committer;
	struct strbuf committer_mail;
	timestamp_t committer_time;
	struct strbuf committer_tz;

	struct strbuf summary;
};

/*
 * Parse author/committer line in the commit object buffer
 */
static void get_ac_line(const char *inbuf, const char *what,
	struct strbuf *name, struct strbuf *mail,
	timestamp_t *time, struct strbuf *tz)
{
	struct ident_split ident;
	size_t len, maillen, namelen;
	char *tmp, *endp;
	const char *namebuf, *mailbuf;

	tmp = strstr(inbuf, what);
	if (!tmp)
		goto error_out;
	tmp += strlen(what);
	endp = strchr(tmp, '\n');
	if (!endp)
		len = strlen(tmp);
	else
		len = endp - tmp;

	if (split_ident_line(&ident, tmp, len)) {
	error_out:
		/* Ugh */
		tmp = "(unknown)";
		strbuf_addstr(name, tmp);
		strbuf_addstr(mail, tmp);
		strbuf_addstr(tz, tmp);
		*time = 0;
		return;
	}

	namelen = ident.name_end - ident.name_begin;
	namebuf = ident.name_begin;

	maillen = ident.mail_end - ident.mail_begin;
	mailbuf = ident.mail_begin;

	if (ident.date_begin && ident.date_end)
		*time = strtoul(ident.date_begin, NULL, 10);
	else
		*time = 0;

	if (ident.tz_begin && ident.tz_end)
		strbuf_add(tz, ident.tz_begin, ident.tz_end - ident.tz_begin);
	else
		strbuf_addstr(tz, "(unknown)");

	/*
	 * Now, convert both name and e-mail using mailmap
	 */
	map_user(&mailmap, &mailbuf, &maillen,
		 &namebuf, &namelen);

	strbuf_addf(mail, "<%.*s>", (int)maillen, mailbuf);
	strbuf_add(name, namebuf, namelen);
}

static void commit_info_init(struct commit_info *ci)
{

	strbuf_init(&ci->author, 0);
	strbuf_init(&ci->author_mail, 0);
	strbuf_init(&ci->author_tz, 0);
	strbuf_init(&ci->committer, 0);
	strbuf_init(&ci->committer_mail, 0);
	strbuf_init(&ci->committer_tz, 0);
	strbuf_init(&ci->summary, 0);
}

static void commit_info_destroy(struct commit_info *ci)
{

	strbuf_release(&ci->author);
	strbuf_release(&ci->author_mail);
	strbuf_release(&ci->author_tz);
	strbuf_release(&ci->committer);
	strbuf_release(&ci->committer_mail);
	strbuf_release(&ci->committer_tz);
	strbuf_release(&ci->summary);
}

static void get_commit_info(struct commit *commit,
			    struct commit_info *ret,
			    int detailed)
{
	int len;
	const char *subject, *encoding;
	const char *message;

	commit_info_init(ret);

	encoding = get_log_output_encoding();
	message = logmsg_reencode(commit, NULL, encoding);
	get_ac_line(message, "\nauthor ",
		    &ret->author, &ret->author_mail,
		    &ret->author_time, &ret->author_tz);

	if (!detailed) {
		unuse_commit_buffer(commit, message);
		return;
	}

	get_ac_line(message, "\ncommitter ",
		    &ret->committer, &ret->committer_mail,
		    &ret->committer_time, &ret->committer_tz);

	len = find_commit_subject(message, &subject);
	if (len)
		strbuf_add(&ret->summary, subject, len);
	else
		strbuf_addf(&ret->summary, "(%s)", oid_to_hex(&commit->object.oid));

	unuse_commit_buffer(commit, message);
}

/*
 * Write out any suspect information which depends on the path. This must be
 * handled separately from emit_one_suspect_detail(), because a given commit
 * may have changes in multiple paths. So this needs to appear each time
 * we mention a new group.
 *
 * To allow LF and other nonportable characters in pathnames,
 * they are c-style quoted as needed.
 */
static void write_filename_info(struct blame_origin *suspect)
{
	if (suspect->previous) {
		struct blame_origin *prev = suspect->previous;
		printf("previous %s ", oid_to_hex(&prev->commit->object.oid));
		write_name_quoted(prev->path, stdout, '\n');
	}
	printf("filename ");
	write_name_quoted(suspect->path, stdout, '\n');
}

/*
 * Porcelain/Incremental format wants to show a lot of details per
 * commit.  Instead of repeating this every line, emit it only once,
 * the first time each commit appears in the output (unless the
 * user has specifically asked for us to repeat).
 */
static int emit_one_suspect_detail(struct blame_origin *suspect, int repeat)
{
	struct commit_info ci;

	if (!repeat && (suspect->commit->object.flags & METAINFO_SHOWN))
		return 0;

	suspect->commit->object.flags |= METAINFO_SHOWN;
	get_commit_info(suspect->commit, &ci, 1);
	printf("author %s\n", ci.author.buf);
	printf("author-mail %s\n", ci.author_mail.buf);
	printf("author-time %"PRItime"\n", ci.author_time);
	printf("author-tz %s\n", ci.author_tz.buf);
	printf("committer %s\n", ci.committer.buf);
	printf("committer-mail %s\n", ci.committer_mail.buf);
	printf("committer-time %"PRItime"\n", ci.committer_time);
	printf("committer-tz %s\n", ci.committer_tz.buf);
	printf("summary %s\n", ci.summary.buf);
	if (suspect->commit->object.flags & UNINTERESTING)
		printf("boundary\n");

	commit_info_destroy(&ci);

	return 1;
}

/*
 * The blame_entry is found to be guilty for the range.
 * Show it in incremental output.
 */
static void found_guilty_entry(struct blame_entry *ent, void *data)
{
	struct progress_info *pi = (struct progress_info *)data;

	if (incremental) {
		struct blame_origin *suspect = ent->suspect;

		printf("%s %d %d %d\n",
		       oid_to_hex(&suspect->commit->object.oid),
		       ent->s_lno + 1, ent->lno + 1, ent->num_lines);
		emit_one_suspect_detail(suspect, 0);
		write_filename_info(suspect);
		maybe_flush_or_die(stdout, "stdout");
	}
	pi->blamed_lines += ent->num_lines;
	display_progress(pi->progress, pi->blamed_lines);
}

static const char *format_time(timestamp_t time, const char *tz_str,
			       int show_raw_time)
{
	static struct strbuf time_buf = STRBUF_INIT;

	strbuf_reset(&time_buf);
	if (show_raw_time) {
		strbuf_addf(&time_buf, "%"PRItime" %s", time, tz_str);
	}
	else {
		const char *time_str;
		size_t time_width;
		int tz;
		tz = atoi(tz_str);
		time_str = show_date(time, tz, &blame_date_mode);
		strbuf_addstr(&time_buf, time_str);
		/*
		 * Add space paddings to time_buf to display a fixed width
		 * string, and use time_width for display width calibration.
		 */
		for (time_width = utf8_strwidth(time_str);
		     time_width < blame_date_width;
		     time_width++)
			strbuf_addch(&time_buf, ' ');
	}
	return time_buf.buf;
}

#define OUTPUT_ANNOTATE_COMPAT	001
#define OUTPUT_LONG_OBJECT_NAME	002
#define OUTPUT_RAW_TIMESTAMP	004
#define OUTPUT_PORCELAIN	010
#define OUTPUT_SHOW_NAME	020
#define OUTPUT_SHOW_NUMBER	040
#define OUTPUT_SHOW_SCORE	0100
#define OUTPUT_NO_AUTHOR	0200
#define OUTPUT_SHOW_EMAIL	0400
#define OUTPUT_LINE_PORCELAIN	01000
#define OUTPUT_COLOR_LINE	02000
#define OUTPUT_SHOW_AGE_WITH_COLOR	04000

static void emit_porcelain_details(struct blame_origin *suspect, int repeat)
{
	if (emit_one_suspect_detail(suspect, repeat) ||
	    (suspect->commit->object.flags & MORE_THAN_ONE_PATH))
		write_filename_info(suspect);
}

static void emit_porcelain(struct blame_scoreboard *sb, struct blame_entry *ent,
			   int opt)
{
	int repeat = opt & OUTPUT_LINE_PORCELAIN;
	int cnt;
	const char *cp;
	struct blame_origin *suspect = ent->suspect;
	char hex[GIT_MAX_HEXSZ + 1];

	oid_to_hex_r(hex, &suspect->commit->object.oid);
	printf("%s %d %d %d\n",
	       hex,
	       ent->s_lno + 1,
	       ent->lno + 1,
	       ent->num_lines);
	emit_porcelain_details(suspect, repeat);

	cp = blame_nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		if (cnt) {
			printf("%s %d %d\n", hex,
			       ent->s_lno + 1 + cnt,
			       ent->lno + 1 + cnt);
			if (repeat)
				emit_porcelain_details(suspect, 1);
		}
		putchar('\t');
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}

	if (sb->final_buf_size && cp[-1] != '\n')
		putchar('\n');
}

static struct color_field {
	timestamp_t hop;
	char col[COLOR_MAXLEN];
} *colorfield;
static int colorfield_nr, colorfield_alloc;

static void parse_color_fields(const char *s)
{
	struct string_list l = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	enum { EXPECT_DATE, EXPECT_COLOR } next = EXPECT_COLOR;

	colorfield_nr = 0;

	/* Ideally this would be stripped and split at the same time? */
	string_list_split(&l, s, ',', -1);
	ALLOC_GROW(colorfield, colorfield_nr + 1, colorfield_alloc);

	for_each_string_list_item(item, &l) {
		switch (next) {
		case EXPECT_DATE:
			colorfield[colorfield_nr].hop = approxidate(item->string);
			next = EXPECT_COLOR;
			colorfield_nr++;
			ALLOC_GROW(colorfield, colorfield_nr + 1, colorfield_alloc);
			break;
		case EXPECT_COLOR:
			if (color_parse(item->string, colorfield[colorfield_nr].col))
				die(_("expecting a color: %s"), item->string);
			next = EXPECT_DATE;
			break;
		}
	}

	if (next == EXPECT_COLOR)
		die(_("must end with a color"));

	colorfield[colorfield_nr].hop = TIME_MAX;
	string_list_clear(&l, 0);
}

static void setup_default_color_by_age(void)
{
	parse_color_fields("blue,12 month ago,white,1 month ago,red");
}

static void determine_line_heat(struct blame_entry *ent, const char **dest_color)
{
	int i = 0;
	struct commit_info ci;
	get_commit_info(ent->suspect->commit, &ci, 1);

	while (i < colorfield_nr && ci.author_time > colorfield[i].hop)
		i++;

	*dest_color = colorfield[i].col;
}

static void emit_other(struct blame_scoreboard *sb, struct blame_entry *ent, int opt)
{
	int cnt;
	const char *cp;
	struct blame_origin *suspect = ent->suspect;
	struct commit_info ci;
	char hex[GIT_MAX_HEXSZ + 1];
	int show_raw_time = !!(opt & OUTPUT_RAW_TIMESTAMP);
	const char *default_color = NULL, *color = NULL, *reset = NULL;

	get_commit_info(suspect->commit, &ci, 1);
	oid_to_hex_r(hex, &suspect->commit->object.oid);

	cp = blame_nth_line(sb, ent->lno);

	if (opt & OUTPUT_SHOW_AGE_WITH_COLOR) {
		determine_line_heat(ent, &default_color);
		color = default_color;
		reset = GIT_COLOR_RESET;
	}

	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		int length = (opt & OUTPUT_LONG_OBJECT_NAME) ? GIT_SHA1_HEXSZ : abbrev;

		if (opt & OUTPUT_COLOR_LINE) {
			if (cnt > 0) {
				color = repeated_meta_color;
				reset = GIT_COLOR_RESET;
			} else  {
				color = default_color ? default_color : NULL;
				reset = default_color ? GIT_COLOR_RESET : NULL;
			}
		}
		if (color)
			fputs(color, stdout);

		if (suspect->commit->object.flags & UNINTERESTING) {
			if (blank_boundary)
				memset(hex, ' ', length);
			else if (!(opt & OUTPUT_ANNOTATE_COMPAT)) {
				length--;
				putchar('^');
			}
		}

		printf("%.*s", length, hex);
		if (opt & OUTPUT_ANNOTATE_COMPAT) {
			const char *name;
			if (opt & OUTPUT_SHOW_EMAIL)
				name = ci.author_mail.buf;
			else
				name = ci.author.buf;
			printf("\t(%10s\t%10s\t%d)", name,
			       format_time(ci.author_time, ci.author_tz.buf,
					   show_raw_time),
			       ent->lno + 1 + cnt);
		} else {
			if (opt & OUTPUT_SHOW_SCORE)
				printf(" %*d %02d",
				       max_score_digits, ent->score,
				       ent->suspect->refcnt);
			if (opt & OUTPUT_SHOW_NAME)
				printf(" %-*.*s", longest_file, longest_file,
				       suspect->path);
			if (opt & OUTPUT_SHOW_NUMBER)
				printf(" %*d", max_orig_digits,
				       ent->s_lno + 1 + cnt);

			if (!(opt & OUTPUT_NO_AUTHOR)) {
				const char *name;
				int pad;
				if (opt & OUTPUT_SHOW_EMAIL)
					name = ci.author_mail.buf;
				else
					name = ci.author.buf;
				pad = longest_author - utf8_strwidth(name);
				printf(" (%s%*s %10s",
				       name, pad, "",
				       format_time(ci.author_time,
						   ci.author_tz.buf,
						   show_raw_time));
			}
			printf(" %*d) ",
			       max_digits, ent->lno + 1 + cnt);
		}
		if (reset)
			fputs(reset, stdout);
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}

	if (sb->final_buf_size && cp[-1] != '\n')
		putchar('\n');

	commit_info_destroy(&ci);
}

static void output(struct blame_scoreboard *sb, int option)
{
	struct blame_entry *ent;

	if (option & OUTPUT_PORCELAIN) {
		for (ent = sb->ent; ent; ent = ent->next) {
			int count = 0;
			struct blame_origin *suspect;
			struct commit *commit = ent->suspect->commit;
			if (commit->object.flags & MORE_THAN_ONE_PATH)
				continue;
			for (suspect = get_blame_suspects(commit); suspect; suspect = suspect->next) {
				if (suspect->guilty && count++) {
					commit->object.flags |= MORE_THAN_ONE_PATH;
					break;
				}
			}
		}
	}

	for (ent = sb->ent; ent; ent = ent->next) {
		if (option & OUTPUT_PORCELAIN)
			emit_porcelain(sb, ent, option);
		else {
			emit_other(sb, ent, option);
		}
	}
}

/*
 * Add phony grafts for use with -S; this is primarily to
 * support git's cvsserver that wants to give a linear history
 * to its clients.
 */
static int read_ancestry(const char *graft_file)
{
	FILE *fp = fopen_or_warn(graft_file, "r");
	struct strbuf buf = STRBUF_INIT;
	if (!fp)
		return -1;
	while (!strbuf_getwholeline(&buf, fp, '\n')) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		struct commit_graft *graft = read_graft_line(&buf);
		if (graft)
			register_commit_graft(the_repository, graft, 0);
	}
	fclose(fp);
	strbuf_release(&buf);
	return 0;
}

static int update_auto_abbrev(int auto_abbrev, struct blame_origin *suspect)
{
	const char *uniq = find_unique_abbrev(&suspect->commit->object.oid,
					      auto_abbrev);
	int len = strlen(uniq);
	if (auto_abbrev < len)
		return len;
	return auto_abbrev;
}

/*
 * How many columns do we need to show line numbers, authors,
 * and filenames?
 */
static void find_alignment(struct blame_scoreboard *sb, int *option)
{
	int longest_src_lines = 0;
	int longest_dst_lines = 0;
	unsigned largest_score = 0;
	struct blame_entry *e;
	int compute_auto_abbrev = (abbrev < 0);
	int auto_abbrev = DEFAULT_ABBREV;

	for (e = sb->ent; e; e = e->next) {
		struct blame_origin *suspect = e->suspect;
		int num;

		if (compute_auto_abbrev)
			auto_abbrev = update_auto_abbrev(auto_abbrev, suspect);
		if (strcmp(suspect->path, sb->path))
			*option |= OUTPUT_SHOW_NAME;
		num = strlen(suspect->path);
		if (longest_file < num)
			longest_file = num;
		if (!(suspect->commit->object.flags & METAINFO_SHOWN)) {
			struct commit_info ci;
			suspect->commit->object.flags |= METAINFO_SHOWN;
			get_commit_info(suspect->commit, &ci, 1);
			if (*option & OUTPUT_SHOW_EMAIL)
				num = utf8_strwidth(ci.author_mail.buf);
			else
				num = utf8_strwidth(ci.author.buf);
			if (longest_author < num)
				longest_author = num;
			commit_info_destroy(&ci);
		}
		num = e->s_lno + e->num_lines;
		if (longest_src_lines < num)
			longest_src_lines = num;
		num = e->lno + e->num_lines;
		if (longest_dst_lines < num)
			longest_dst_lines = num;
		if (largest_score < blame_entry_score(sb, e))
			largest_score = blame_entry_score(sb, e);
	}
	max_orig_digits = decimal_width(longest_src_lines);
	max_digits = decimal_width(longest_dst_lines);
	max_score_digits = decimal_width(largest_score);

	if (compute_auto_abbrev)
		/* one more abbrev length is needed for the boundary commit */
		abbrev = auto_abbrev + 1;
}

static void sanity_check_on_fail(struct blame_scoreboard *sb, int baa)
{
	int opt = OUTPUT_SHOW_SCORE | OUTPUT_SHOW_NUMBER | OUTPUT_SHOW_NAME;
	find_alignment(sb, &opt);
	output(sb, opt);
	die("Baa %d!", baa);
}

static unsigned parse_score(const char *arg)
{
	char *end;
	unsigned long score = strtoul(arg, &end, 10);
	if (*end)
		return 0;
	return score;
}

static const char *add_prefix(const char *prefix, const char *path)
{
	return prefix_path(prefix, prefix ? strlen(prefix) : 0, path);
}

static int git_blame_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "blame.showroot")) {
		show_root = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "blame.blankboundary")) {
		blank_boundary = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "blame.showemail")) {
		int *output_option = cb;
		if (git_config_bool(var, value))
			*output_option |= OUTPUT_SHOW_EMAIL;
		else
			*output_option &= ~OUTPUT_SHOW_EMAIL;
		return 0;
	}
	if (!strcmp(var, "blame.date")) {
		if (!value)
			return config_error_nonbool(var);
		parse_date_format(value, &blame_date_mode);
		return 0;
	}
	if (!strcmp(var, "color.blame.repeatedlines")) {
		if (color_parse_mem(value, strlen(value), repeated_meta_color))
			warning(_("invalid color '%s' in color.blame.repeatedLines"),
				value);
		return 0;
	}
	if (!strcmp(var, "color.blame.highlightrecent")) {
		parse_color_fields(value);
		return 0;
	}

	if (!strcmp(var, "blame.coloring")) {
		if (!strcmp(value, "repeatedLines")) {
			coloring_mode |= OUTPUT_COLOR_LINE;
		} else if (!strcmp(value, "highlightRecent")) {
			coloring_mode |= OUTPUT_SHOW_AGE_WITH_COLOR;
		} else if (!strcmp(value, "none")) {
			coloring_mode &= ~(OUTPUT_COLOR_LINE |
					    OUTPUT_SHOW_AGE_WITH_COLOR);
		} else {
			warning(_("invalid value for blame.coloring"));
			return 0;
		}
	}

	if (git_diff_heuristic_config(var, value, cb) < 0)
		return -1;
	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, cb);
}

static int blame_copy_callback(const struct option *option, const char *arg, int unset)
{
	int *opt = option->value;

	BUG_ON_OPT_NEG(unset);

	/*
	 * -C enables copy from removed files;
	 * -C -C enables copy from existing files, but only
	 *       when blaming a new file;
	 * -C -C -C enables copy from existing files for
	 *          everybody
	 */
	if (*opt & PICKAXE_BLAME_COPY_HARDER)
		*opt |= PICKAXE_BLAME_COPY_HARDEST;
	if (*opt & PICKAXE_BLAME_COPY)
		*opt |= PICKAXE_BLAME_COPY_HARDER;
	*opt |= PICKAXE_BLAME_COPY | PICKAXE_BLAME_MOVE;

	if (arg)
		blame_copy_score = parse_score(arg);
	return 0;
}

static int blame_move_callback(const struct option *option, const char *arg, int unset)
{
	int *opt = option->value;

	BUG_ON_OPT_NEG(unset);

	*opt |= PICKAXE_BLAME_MOVE;

	if (arg)
		blame_move_score = parse_score(arg);
	return 0;
}

static int is_a_rev(const char *name)
{
	struct object_id oid;

	if (get_oid(name, &oid))
		return 0;
	return OBJ_NONE < oid_object_info(the_repository, &oid, NULL);
}

int cmd_blame(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	const char *path;
	struct blame_scoreboard sb;
	struct blame_origin *o;
	struct blame_entry *ent = NULL;
	long dashdash_pos, lno;
	struct progress_info pi = { NULL, 0 };

	struct string_list range_list = STRING_LIST_INIT_NODUP;
	int output_option = 0, opt = 0;
	int show_stats = 0;
	const char *revs_file = NULL;
	const char *contents_from = NULL;
	const struct option options[] = {
		OPT_BOOL(0, "incremental", &incremental, N_("Show blame entries as we find them, incrementally")),
		OPT_BOOL('b', NULL, &blank_boundary, N_("Show blank SHA-1 for boundary commits (Default: off)")),
		OPT_BOOL(0, "root", &show_root, N_("Do not treat root commits as boundaries (Default: off)")),
		OPT_BOOL(0, "show-stats", &show_stats, N_("Show work cost statistics")),
		OPT_BOOL(0, "progress", &show_progress, N_("Force progress reporting")),
		OPT_BIT(0, "score-debug", &output_option, N_("Show output score for blame entries"), OUTPUT_SHOW_SCORE),
		OPT_BIT('f', "show-name", &output_option, N_("Show original filename (Default: auto)"), OUTPUT_SHOW_NAME),
		OPT_BIT('n', "show-number", &output_option, N_("Show original linenumber (Default: off)"), OUTPUT_SHOW_NUMBER),
		OPT_BIT('p', "porcelain", &output_option, N_("Show in a format designed for machine consumption"), OUTPUT_PORCELAIN),
		OPT_BIT(0, "line-porcelain", &output_option, N_("Show porcelain format with per-line commit information"), OUTPUT_PORCELAIN|OUTPUT_LINE_PORCELAIN),
		OPT_BIT('c', NULL, &output_option, N_("Use the same output mode as git-annotate (Default: off)"), OUTPUT_ANNOTATE_COMPAT),
		OPT_BIT('t', NULL, &output_option, N_("Show raw timestamp (Default: off)"), OUTPUT_RAW_TIMESTAMP),
		OPT_BIT('l', NULL, &output_option, N_("Show long commit SHA1 (Default: off)"), OUTPUT_LONG_OBJECT_NAME),
		OPT_BIT('s', NULL, &output_option, N_("Suppress author name and timestamp (Default: off)"), OUTPUT_NO_AUTHOR),
		OPT_BIT('e', "show-email", &output_option, N_("Show author email instead of name (Default: off)"), OUTPUT_SHOW_EMAIL),
		OPT_BIT('w', NULL, &xdl_opts, N_("Ignore whitespace differences"), XDF_IGNORE_WHITESPACE),
		OPT_BIT(0, "color-lines", &output_option, N_("color redundant metadata from previous line differently"), OUTPUT_COLOR_LINE),
		OPT_BIT(0, "color-by-age", &output_option, N_("color lines by age"), OUTPUT_SHOW_AGE_WITH_COLOR),

		/*
		 * The following two options are parsed by parse_revision_opt()
		 * and are only included here to get included in the "-h"
		 * output:
		 */
		{ OPTION_LOWLEVEL_CALLBACK, 0, "indent-heuristic", NULL, NULL, N_("Use an experimental heuristic to improve diffs"), PARSE_OPT_NOARG, parse_opt_unknown_cb },

		OPT_BIT(0, "minimal", &xdl_opts, N_("Spend extra cycles to find better match"), XDF_NEED_MINIMAL),
		OPT_STRING('S', NULL, &revs_file, N_("file"), N_("Use revisions from <file> instead of calling git-rev-list")),
		OPT_STRING(0, "contents", &contents_from, N_("file"), N_("Use <file>'s contents as the final image")),
		{ OPTION_CALLBACK, 'C', NULL, &opt, N_("score"), N_("Find line copies within and across files"), PARSE_OPT_OPTARG, blame_copy_callback },
		{ OPTION_CALLBACK, 'M', NULL, &opt, N_("score"), N_("Find line movements within and across files"), PARSE_OPT_OPTARG, blame_move_callback },
		OPT_STRING_LIST('L', NULL, &range_list, N_("n,m"), N_("Process only line range n,m, counting from 1")),
		OPT__ABBREV(&abbrev),
		OPT_END()
	};

	struct parse_opt_ctx_t ctx;
	int cmd_is_annotate = !strcmp(argv[0], "annotate");
	struct range_set ranges;
	unsigned int range_i;
	long anchor;

	setup_default_color_by_age();
	git_config(git_blame_config, &output_option);
	repo_init_revisions(the_repository, &revs, NULL);
	revs.date_mode = blame_date_mode;
	revs.diffopt.flags.allow_textconv = 1;
	revs.diffopt.flags.follow_renames = 1;

	save_commit_buffer = 0;
	dashdash_pos = 0;
	show_progress = -1;

	parse_options_start(&ctx, argc, argv, prefix, options,
			    PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0);
	for (;;) {
		switch (parse_options_step(&ctx, options, blame_opt_usage)) {
		case PARSE_OPT_HELP:
		case PARSE_OPT_ERROR:
			exit(129);
		case PARSE_OPT_COMPLETE:
			exit(0);
		case PARSE_OPT_DONE:
			if (ctx.argv[0])
				dashdash_pos = ctx.cpidx;
			goto parse_done;
		}

		if (!strcmp(ctx.argv[0], "--reverse")) {
			ctx.argv[0] = "--children";
			reverse = 1;
		}
		parse_revision_opt(&revs, &ctx, options, blame_opt_usage);
	}
parse_done:
	no_whole_file_rename = !revs.diffopt.flags.follow_renames;
	xdl_opts |= revs.diffopt.xdl_opts & XDF_INDENT_HEURISTIC;
	revs.diffopt.flags.follow_renames = 0;
	argc = parse_options_end(&ctx);

	if (incremental || (output_option & OUTPUT_PORCELAIN)) {
		if (show_progress > 0)
			die(_("--progress can't be used with --incremental or porcelain formats"));
		show_progress = 0;
	} else if (show_progress < 0)
		show_progress = isatty(2);

	if (0 < abbrev && abbrev < GIT_SHA1_HEXSZ)
		/* one more abbrev length is needed for the boundary commit */
		abbrev++;
	else if (!abbrev)
		abbrev = GIT_SHA1_HEXSZ;

	if (revs_file && read_ancestry(revs_file))
		die_errno("reading graft file '%s' failed", revs_file);

	if (cmd_is_annotate) {
		output_option |= OUTPUT_ANNOTATE_COMPAT;
		blame_date_mode.type = DATE_ISO8601;
	} else {
		blame_date_mode = revs.date_mode;
	}

	/* The maximum width used to show the dates */
	switch (blame_date_mode.type) {
	case DATE_RFC2822:
		blame_date_width = sizeof("Thu, 19 Oct 2006 16:00:04 -0700");
		break;
	case DATE_ISO8601_STRICT:
		blame_date_width = sizeof("2006-10-19T16:00:04-07:00");
		break;
	case DATE_ISO8601:
		blame_date_width = sizeof("2006-10-19 16:00:04 -0700");
		break;
	case DATE_RAW:
		blame_date_width = sizeof("1161298804 -0700");
		break;
	case DATE_UNIX:
		blame_date_width = sizeof("1161298804");
		break;
	case DATE_SHORT:
		blame_date_width = sizeof("2006-10-19");
		break;
	case DATE_RELATIVE:
		/*
		 * TRANSLATORS: This string is used to tell us the
		 * maximum display width for a relative timestamp in
		 * "git blame" output.  For C locale, "4 years, 11
		 * months ago", which takes 22 places, is the longest
		 * among various forms of relative timestamps, but
		 * your language may need more or fewer display
		 * columns.
		 */
		blame_date_width = utf8_strwidth(_("4 years, 11 months ago")) + 1; /* add the null */
		break;
	case DATE_NORMAL:
		blame_date_width = sizeof("Thu Oct 19 16:00:04 2006 -0700");
		break;
	case DATE_STRFTIME:
		blame_date_width = strlen(show_date(0, 0, &blame_date_mode)) + 1; /* add the null */
		break;
	}
	blame_date_width -= 1; /* strip the null */

	if (revs.diffopt.flags.find_copies_harder)
		opt |= (PICKAXE_BLAME_COPY | PICKAXE_BLAME_MOVE |
			PICKAXE_BLAME_COPY_HARDER);

	/*
	 * We have collected options unknown to us in argv[1..unk]
	 * which are to be passed to revision machinery if we are
	 * going to do the "bottom" processing.
	 *
	 * The remaining are:
	 *
	 * (1) if dashdash_pos != 0, it is either
	 *     "blame [revisions] -- <path>" or
	 *     "blame -- <path> <rev>"
	 *
	 * (2) otherwise, it is one of the two:
	 *     "blame [revisions] <path>"
	 *     "blame <path> <rev>"
	 *
	 * Note that we must strip out <path> from the arguments: we do not
	 * want the path pruning but we may want "bottom" processing.
	 */
	if (dashdash_pos) {
		switch (argc - dashdash_pos - 1) {
		case 2: /* (1b) */
			if (argc != 4)
				usage_with_options(blame_opt_usage, options);
			/* reorder for the new way: <rev> -- <path> */
			argv[1] = argv[3];
			argv[3] = argv[2];
			argv[2] = "--";
			/* FALLTHROUGH */
		case 1: /* (1a) */
			path = add_prefix(prefix, argv[--argc]);
			argv[argc] = NULL;
			break;
		default:
			usage_with_options(blame_opt_usage, options);
		}
	} else {
		if (argc < 2)
			usage_with_options(blame_opt_usage, options);
		if (argc == 3 && is_a_rev(argv[argc - 1])) { /* (2b) */
			path = add_prefix(prefix, argv[1]);
			argv[1] = argv[2];
		} else {	/* (2a) */
			if (argc == 2 && is_a_rev(argv[1]) && !get_git_work_tree())
				die("missing <path> to blame");
			path = add_prefix(prefix, argv[argc - 1]);
		}
		argv[argc - 1] = "--";
	}

	revs.disable_stdin = 1;
	setup_revisions(argc, argv, &revs, NULL);

	init_scoreboard(&sb);
	sb.revs = &revs;
	sb.contents_from = contents_from;
	sb.reverse = reverse;
	sb.repo = the_repository;
	setup_scoreboard(&sb, path, &o);
	lno = sb.num_lines;

	if (lno && !range_list.nr)
		string_list_append(&range_list, "1");

	anchor = 1;
	range_set_init(&ranges, range_list.nr);
	for (range_i = 0; range_i < range_list.nr; ++range_i) {
		long bottom, top;
		if (parse_range_arg(range_list.items[range_i].string,
				    nth_line_cb, &sb, lno, anchor,
				    &bottom, &top, sb.path, &the_index))
			usage(blame_usage);
		if ((!lno && (top || bottom)) || lno < bottom)
			die(Q_("file %s has only %lu line",
			       "file %s has only %lu lines",
			       lno), path, lno);
		if (bottom < 1)
			bottom = 1;
		if (top < 1 || lno < top)
			top = lno;
		bottom--;
		range_set_append_unsafe(&ranges, bottom, top);
		anchor = top + 1;
	}
	sort_and_merge_range_set(&ranges);

	for (range_i = ranges.nr; range_i > 0; --range_i) {
		const struct range *r = &ranges.ranges[range_i - 1];
		ent = blame_entry_prepend(ent, r->start, r->end, o);
	}

	o->suspects = ent;
	prio_queue_put(&sb.commits, o->commit);

	blame_origin_decref(o);

	range_set_release(&ranges);
	string_list_clear(&range_list, 0);

	sb.ent = NULL;
	sb.path = path;

	if (blame_move_score)
		sb.move_score = blame_move_score;
	if (blame_copy_score)
		sb.copy_score = blame_copy_score;

	sb.debug = DEBUG;
	sb.on_sanity_fail = &sanity_check_on_fail;

	sb.show_root = show_root;
	sb.xdl_opts = xdl_opts;
	sb.no_whole_file_rename = no_whole_file_rename;

	read_mailmap(&mailmap, NULL);

	sb.found_guilty_entry = &found_guilty_entry;
	sb.found_guilty_entry_data = &pi;
	if (show_progress)
		pi.progress = start_delayed_progress(_("Blaming lines"), sb.num_lines);

	assign_blame(&sb, opt);

	stop_progress(&pi.progress);

	if (!incremental)
		setup_pager();
	else
		return 0;

	blame_sort_final(&sb);

	blame_coalesce(&sb);

	if (!(output_option & (OUTPUT_COLOR_LINE | OUTPUT_SHOW_AGE_WITH_COLOR)))
		output_option |= coloring_mode;

	if (!(output_option & OUTPUT_PORCELAIN)) {
		find_alignment(&sb, &output_option);
		if (!*repeated_meta_color &&
		    (output_option & OUTPUT_COLOR_LINE))
			xsnprintf(repeated_meta_color,
				  sizeof(repeated_meta_color),
				  "%s", GIT_COLOR_CYAN);
	}
	if (output_option & OUTPUT_ANNOTATE_COMPAT)
		output_option &= ~(OUTPUT_COLOR_LINE | OUTPUT_SHOW_AGE_WITH_COLOR);

	output(&sb, output_option);
	free((void *)sb.final_buf);
	for (ent = sb.ent; ent; ) {
		struct blame_entry *e = ent->next;
		free(ent);
		ent = e;
	}

	if (show_stats) {
		printf("num read blob: %d\n", sb.num_read_blob);
		printf("num get patch: %d\n", sb.num_get_patch);
		printf("num commits: %d\n", sb.num_commits);
	}
	return 0;
}
