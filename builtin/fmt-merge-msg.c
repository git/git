#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "tag.h"
#include "string-list.h"

static const char * const fmt_merge_msg_usage[] = {
	"git fmt-merge-msg [-m <message>] [--log[=<n>]|--no-log] [--file <file>]",
	NULL
};

static int shortlog_len;

static int fmt_merge_msg_config(const char *key, const char *value, void *cb)
{
	if (!strcmp(key, "merge.log") || !strcmp(key, "merge.summary")) {
		int is_bool;
		shortlog_len = git_config_bool_or_int(key, value, &is_bool);
		if (!is_bool && shortlog_len < 0)
			return error("%s: negative length %s", key, value);
		if (is_bool && shortlog_len)
			shortlog_len = DEFAULT_MERGE_LOG_LEN;
	}
	return 0;
}

struct src_data {
	struct string_list branch, tag, r_branch, generic;
	int head_status;
};

static void init_src_data(struct src_data *data)
{
	data->branch.strdup_strings = 1;
	data->tag.strdup_strings = 1;
	data->r_branch.strdup_strings = 1;
	data->generic.strdup_strings = 1;
}

static struct string_list srcs = STRING_LIST_INIT_DUP;
static struct string_list origins = STRING_LIST_INIT_DUP;

static int handle_line(char *line)
{
	int i, len = strlen(line);
	unsigned char *sha1;
	char *src, *origin;
	struct src_data *src_data;
	struct string_list_item *item;
	int pulling_head = 0;

	if (len < 43 || line[40] != '\t')
		return 1;

	if (!prefixcmp(line + 41, "not-for-merge"))
		return 0;

	if (line[41] != '\t')
		return 2;

	line[40] = 0;
	sha1 = xmalloc(20);
	i = get_sha1(line, sha1);
	line[40] = '\t';
	if (i)
		return 3;

	if (line[len - 1] == '\n')
		line[len - 1] = 0;
	line += 42;

	src = strstr(line, " of ");
	if (src) {
		*src = 0;
		src += 4;
		pulling_head = 0;
	} else {
		src = line;
		pulling_head = 1;
	}

	item = unsorted_string_list_lookup(&srcs, src);
	if (!item) {
		item = string_list_append(&srcs, src);
		item->util = xcalloc(1, sizeof(struct src_data));
		init_src_data(item->util);
	}
	src_data = item->util;

	if (pulling_head) {
		origin = src;
		src_data->head_status |= 1;
	} else if (!prefixcmp(line, "branch ")) {
		origin = line + 7;
		string_list_append(&src_data->branch, origin);
		src_data->head_status |= 2;
	} else if (!prefixcmp(line, "tag ")) {
		origin = line;
		string_list_append(&src_data->tag, origin + 4);
		src_data->head_status |= 2;
	} else if (!prefixcmp(line, "remote-tracking branch ")) {
		origin = line + strlen("remote-tracking branch ");
		string_list_append(&src_data->r_branch, origin);
		src_data->head_status |= 2;
	} else {
		origin = src;
		string_list_append(&src_data->generic, line);
		src_data->head_status |= 2;
	}

	if (!strcmp(".", src) || !strcmp(src, origin)) {
		int len = strlen(origin);
		if (origin[0] == '\'' && origin[len - 1] == '\'')
			origin = xmemdupz(origin + 1, len - 2);
	} else {
		char *new_origin = xmalloc(strlen(origin) + strlen(src) + 5);
		sprintf(new_origin, "%s of %s", origin, src);
		origin = new_origin;
	}
	string_list_append(&origins, origin)->util = sha1;
	return 0;
}

static void print_joined(const char *singular, const char *plural,
		struct string_list *list, struct strbuf *out)
{
	if (list->nr == 0)
		return;
	if (list->nr == 1) {
		strbuf_addf(out, "%s%s", singular, list->items[0].string);
	} else {
		int i;
		strbuf_addstr(out, plural);
		for (i = 0; i < list->nr - 1; i++)
			strbuf_addf(out, "%s%s", i > 0 ? ", " : "",
				    list->items[i].string);
		strbuf_addf(out, " and %s", list->items[list->nr - 1].string);
	}
}

static void shortlog(const char *name, unsigned char *sha1,
		struct commit *head, struct rev_info *rev, int limit,
		struct strbuf *out)
{
	int i, count = 0;
	struct commit *commit;
	struct object *branch;
	struct string_list subjects = STRING_LIST_INIT_DUP;
	int flags = UNINTERESTING | TREESAME | SEEN | SHOWN | ADDED;
	struct strbuf sb = STRBUF_INIT;

	branch = deref_tag(parse_object(sha1), sha1_to_hex(sha1), 40);
	if (!branch || branch->type != OBJ_COMMIT)
		return;

	setup_revisions(0, NULL, rev, NULL);
	rev->ignore_merges = 1;
	add_pending_object(rev, branch, name);
	add_pending_object(rev, &head->object, "^HEAD");
	head->object.flags |= UNINTERESTING;
	if (prepare_revision_walk(rev))
		die("revision walk setup failed");
	while ((commit = get_revision(rev)) != NULL) {
		struct pretty_print_context ctx = {0};

		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		count++;
		if (subjects.nr > limit)
			continue;

		format_commit_message(commit, "%s", &sb, &ctx);
		strbuf_ltrim(&sb);

		if (!sb.len)
			string_list_append(&subjects,
					   sha1_to_hex(commit->object.sha1));
		else
			string_list_append(&subjects, strbuf_detach(&sb, NULL));
	}

	if (count > limit)
		strbuf_addf(out, "\n* %s: (%d commits)\n", name, count);
	else
		strbuf_addf(out, "\n* %s:\n", name);

	for (i = 0; i < subjects.nr; i++)
		if (i >= limit)
			strbuf_addf(out, "  ...\n");
		else
			strbuf_addf(out, "  %s\n", subjects.items[i].string);

	clear_commit_marks((struct commit *)branch, flags);
	clear_commit_marks(head, flags);
	free_commit_list(rev->commits);
	rev->commits = NULL;
	rev->pending.nr = 0;

	string_list_clear(&subjects, 0);
}

static void do_fmt_merge_msg_title(struct strbuf *out,
	const char *current_branch) {
	int i = 0;
	char *sep = "";

	strbuf_addstr(out, "Merge ");
	for (i = 0; i < srcs.nr; i++) {
		struct src_data *src_data = srcs.items[i].util;
		const char *subsep = "";

		strbuf_addstr(out, sep);
		sep = "; ";

		if (src_data->head_status == 1) {
			strbuf_addstr(out, srcs.items[i].string);
			continue;
		}
		if (src_data->head_status == 3) {
			subsep = ", ";
			strbuf_addstr(out, "HEAD");
		}
		if (src_data->branch.nr) {
			strbuf_addstr(out, subsep);
			subsep = ", ";
			print_joined("branch ", "branches ", &src_data->branch,
					out);
		}
		if (src_data->r_branch.nr) {
			strbuf_addstr(out, subsep);
			subsep = ", ";
			print_joined("remote-tracking branch ", "remote-tracking branches ",
					&src_data->r_branch, out);
		}
		if (src_data->tag.nr) {
			strbuf_addstr(out, subsep);
			subsep = ", ";
			print_joined("tag ", "tags ", &src_data->tag, out);
		}
		if (src_data->generic.nr) {
			strbuf_addstr(out, subsep);
			print_joined("commit ", "commits ", &src_data->generic,
					out);
		}
		if (strcmp(".", srcs.items[i].string))
			strbuf_addf(out, " of %s", srcs.items[i].string);
	}

	if (!strcmp("master", current_branch))
		strbuf_addch(out, '\n');
	else
		strbuf_addf(out, " into %s\n", current_branch);
}

static int do_fmt_merge_msg(int merge_title, struct strbuf *in,
	struct strbuf *out, int shortlog_len) {
	int i = 0, pos = 0;
	unsigned char head_sha1[20];
	const char *current_branch;

	/* get current branch */
	current_branch = resolve_ref("HEAD", head_sha1, 1, NULL);
	if (!current_branch)
		die("No current branch");
	if (!prefixcmp(current_branch, "refs/heads/"))
		current_branch += 11;

	/* get a line */
	while (pos < in->len) {
		int len;
		char *newline, *p = in->buf + pos;

		newline = strchr(p, '\n');
		len = newline ? newline - p : strlen(p);
		pos += len + !!newline;
		i++;
		p[len] = 0;
		if (handle_line(p))
			die ("Error in line %d: %.*s", i, len, p);
	}

	if (!srcs.nr)
		return 0;

	if (merge_title)
		do_fmt_merge_msg_title(out, current_branch);

	if (shortlog_len) {
		struct commit *head;
		struct rev_info rev;

		head = lookup_commit_or_die(head_sha1, "HEAD");
		init_revisions(&rev, NULL);
		rev.commit_format = CMIT_FMT_ONELINE;
		rev.ignore_merges = 1;
		rev.limited = 1;

		if (suffixcmp(out->buf, "\n"))
			strbuf_addch(out, '\n');

		for (i = 0; i < origins.nr; i++)
			shortlog(origins.items[i].string, origins.items[i].util,
					head, &rev, shortlog_len, out);
	}
	return 0;
}

int fmt_merge_msg(struct strbuf *in, struct strbuf *out,
		  int merge_title, int shortlog_len) {
	return do_fmt_merge_msg(merge_title, in, out, shortlog_len);
}

int cmd_fmt_merge_msg(int argc, const char **argv, const char *prefix)
{
	const char *inpath = NULL;
	const char *message = NULL;
	struct option options[] = {
		{ OPTION_INTEGER, 0, "log", &shortlog_len, "n",
		  "populate log with at most <n> entries from shortlog",
		  PARSE_OPT_OPTARG, NULL, DEFAULT_MERGE_LOG_LEN },
		{ OPTION_INTEGER, 0, "summary", &shortlog_len, "n",
		  "alias for --log (deprecated)",
		  PARSE_OPT_OPTARG | PARSE_OPT_HIDDEN, NULL,
		  DEFAULT_MERGE_LOG_LEN },
		OPT_STRING('m', "message", &message, "text",
			"use <text> as start of message"),
		OPT_FILENAME('F', "file", &inpath, "file to read from"),
		OPT_END()
	};

	FILE *in = stdin;
	struct strbuf input = STRBUF_INIT, output = STRBUF_INIT;
	int ret;

	git_config(fmt_merge_msg_config, NULL);
	argc = parse_options(argc, argv, prefix, options, fmt_merge_msg_usage,
			     0);
	if (argc > 0)
		usage_with_options(fmt_merge_msg_usage, options);
	if (message && !shortlog_len) {
		char nl = '\n';
		write_in_full(STDOUT_FILENO, message, strlen(message));
		write_in_full(STDOUT_FILENO, &nl, 1);
		return 0;
	}
	if (shortlog_len < 0)
		die("Negative --log=%d", shortlog_len);

	if (inpath && strcmp(inpath, "-")) {
		in = fopen(inpath, "r");
		if (!in)
			die_errno("cannot open '%s'", inpath);
	}

	if (strbuf_read(&input, fileno(in), 0) < 0)
		die_errno("could not read input file");

	if (message)
		strbuf_addstr(&output, message);
	ret = fmt_merge_msg(&input, &output,
			    message ? 0 : 1,
			    shortlog_len);

	if (ret)
		return ret;
	write_in_full(STDOUT_FILENO, output.buf, output.len);
	return 0;
}
