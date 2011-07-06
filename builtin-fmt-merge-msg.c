#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "tag.h"

static const char *fmt_merge_msg_usage =
	"git fmt-merge-msg [--log] [--no-log] [--file <file>]";

static int merge_summary;

static int fmt_merge_msg_config(const char *key, const char *value, void *cb)
{
	static int found_merge_log = 0;
	if (!strcmp("merge.log", key)) {
		found_merge_log = 1;
		merge_summary = git_config_bool(key, value);
	}
	if (!found_merge_log && !strcmp("merge.summary", key))
		merge_summary = git_config_bool(key, value);
	return 0;
}

struct list {
	char **list;
	void **payload;
	unsigned nr, alloc;
};

static void append_to_list(struct list *list, char *value, void *payload)
{
	if (list->nr == list->alloc) {
		list->alloc += 32;
		list->list = xrealloc(list->list, sizeof(char *) * list->alloc);
		list->payload = xrealloc(list->payload,
				sizeof(char *) * list->alloc);
	}
	list->payload[list->nr] = payload;
	list->list[list->nr++] = value;
}

static int find_in_list(struct list *list, char *value)
{
	int i;

	for (i = 0; i < list->nr; i++)
		if (!strcmp(list->list[i], value))
			return i;

	return -1;
}

static void free_list(struct list *list)
{
	int i;

	if (list->alloc == 0)
		return;

	for (i = 0; i < list->nr; i++) {
		free(list->list[i]);
		free(list->payload[i]);
	}
	free(list->list);
	free(list->payload);
	list->nr = list->alloc = 0;
}

struct src_data {
	struct list branch, tag, r_branch, generic;
	int head_status;
};

static struct list srcs = { NULL, NULL, 0, 0};
static struct list origins = { NULL, NULL, 0, 0};

static int handle_line(char *line)
{
	int i, len = strlen(line);
	unsigned char *sha1;
	char *src, *origin;
	struct src_data *src_data;
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

	i = find_in_list(&srcs, src);
	if (i < 0) {
		i = srcs.nr;
		append_to_list(&srcs, xstrdup(src),
				xcalloc(1, sizeof(struct src_data)));
	}
	src_data = srcs.payload[i];

	if (pulling_head) {
		origin = xstrdup(src);
		src_data->head_status |= 1;
	} else if (!prefixcmp(line, "branch ")) {
		origin = xstrdup(line + 7);
		append_to_list(&src_data->branch, origin, NULL);
		src_data->head_status |= 2;
	} else if (!prefixcmp(line, "tag ")) {
		origin = line;
		append_to_list(&src_data->tag, xstrdup(origin + 4), NULL);
		src_data->head_status |= 2;
	} else if (!prefixcmp(line, "remote branch ")) {
		origin = xstrdup(line + 14);
		append_to_list(&src_data->r_branch, origin, NULL);
		src_data->head_status |= 2;
	} else {
		origin = xstrdup(src);
		append_to_list(&src_data->generic, xstrdup(line), NULL);
		src_data->head_status |= 2;
	}

	if (!strcmp(".", src) || !strcmp(src, origin)) {
		int len = strlen(origin);
		if (origin[0] == '\'' && origin[len - 1] == '\'') {
			origin = xmemdupz(origin + 1, len - 2);
		} else {
			origin = xstrdup(origin);
		}
	} else {
		char *new_origin = xmalloc(strlen(origin) + strlen(src) + 5);
		sprintf(new_origin, "%s of %s", origin, src);
		origin = new_origin;
	}
	append_to_list(&origins, origin, sha1);
	return 0;
}

static void print_joined(const char *singular, const char *plural,
		struct list *list, struct strbuf *out)
{
	if (list->nr == 0)
		return;
	if (list->nr == 1) {
		strbuf_addf(out, "%s%s", singular, list->list[0]);
	} else {
		int i;
		strbuf_addstr(out, plural);
		for (i = 0; i < list->nr - 1; i++)
			strbuf_addf(out, "%s%s", i > 0 ? ", " : "", list->list[i]);
		strbuf_addf(out, " and %s", list->list[list->nr - 1]);
	}
}

static void shortlog(const char *name, unsigned char *sha1,
		struct commit *head, struct rev_info *rev, int limit,
		struct strbuf *out)
{
	int i, count = 0;
	struct commit *commit;
	struct object *branch;
	struct list subjects = { NULL, NULL, 0, 0 };
	int flags = UNINTERESTING | TREESAME | SEEN | SHOWN | ADDED;

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
		char *oneline, *bol, *eol;

		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		count++;
		if (subjects.nr > limit)
			continue;

		bol = strstr(commit->buffer, "\n\n");
		if (bol) {
			unsigned char c;
			do {
				c = *++bol;
			} while (isspace(c));
			if (!c)
				bol = NULL;
		}

		if (!bol) {
			append_to_list(&subjects, xstrdup(sha1_to_hex(
							commit->object.sha1)),
					NULL);
			continue;
		}

		eol = strchr(bol, '\n');
		if (eol) {
			oneline = xmemdupz(bol, eol - bol);
		} else {
			oneline = xstrdup(bol);
		}
		append_to_list(&subjects, oneline, NULL);
	}

	if (count > limit)
		strbuf_addf(out, "\n* %s: (%d commits)\n", name, count);
	else
		strbuf_addf(out, "\n* %s:\n", name);

	for (i = 0; i < subjects.nr; i++)
		if (i >= limit)
			strbuf_addf(out, "  ...\n");
		else
			strbuf_addf(out, "  %s\n", subjects.list[i]);

	clear_commit_marks((struct commit *)branch, flags);
	clear_commit_marks(head, flags);
	free_commit_list(rev->commits);
	rev->commits = NULL;
	rev->pending.nr = 0;

	free_list(&subjects);
}

int fmt_merge_msg(int merge_summary, struct strbuf *in, struct strbuf *out) {
	int limit = 20, i = 0, pos = 0;
	char line[1024];
	char *p = line, *sep = "";
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
		char *newline;

		p = in->buf + pos;
		newline = strchr(p, '\n');
		len = newline ? newline - p : strlen(p);
		pos += len + !!newline;
		i++;
		p[len] = 0;
		if (handle_line(p))
			die ("Error in line %d: %.*s", i, len, p);
	}

	strbuf_addstr(out, "Merge ");
	for (i = 0; i < srcs.nr; i++) {
		struct src_data *src_data = srcs.payload[i];
		const char *subsep = "";

		strbuf_addstr(out, sep);
		sep = "; ";

		if (src_data->head_status == 1) {
			strbuf_addstr(out, srcs.list[i]);
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
			print_joined("remote branch ", "remote branches ",
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
		if (strcmp(".", srcs.list[i]))
			strbuf_addf(out, " of %s", srcs.list[i]);
	}

	if (!strcmp("master", current_branch))
		strbuf_addch(out, '\n');
	else
		strbuf_addf(out, " into %s\n", current_branch);

	if (merge_summary) {
		struct commit *head;
		struct rev_info rev;

		head = lookup_commit(head_sha1);
		init_revisions(&rev, NULL);
		rev.commit_format = CMIT_FMT_ONELINE;
		rev.ignore_merges = 1;
		rev.limited = 1;

		for (i = 0; i < origins.nr; i++)
			shortlog(origins.list[i], origins.payload[i],
					head, &rev, limit, out);
	}
	return 0;
}

int cmd_fmt_merge_msg(int argc, const char **argv, const char *prefix)
{
	FILE *in = stdin;
	struct strbuf input, output;
	int ret;

	git_config(fmt_merge_msg_config, NULL);

	while (argc > 1) {
		if (!strcmp(argv[1], "--log") || !strcmp(argv[1], "--summary"))
			merge_summary = 1;
		else if (!strcmp(argv[1], "--no-log")
				|| !strcmp(argv[1], "--no-summary"))
			merge_summary = 0;
		else if (!strcmp(argv[1], "-F") || !strcmp(argv[1], "--file")) {
			if (argc < 3)
				die ("Which file?");
			if (!strcmp(argv[2], "-"))
				in = stdin;
			else {
				fclose(in);
				in = fopen(argv[2], "r");
				if (!in)
					die("cannot open %s", argv[2]);
			}
			argc--; argv++;
		} else
			break;
		argc--; argv++;
	}

	if (argc > 1)
		usage(fmt_merge_msg_usage);

	strbuf_init(&input, 0);
	if (strbuf_read(&input, fileno(in), 0) < 0)
		die("could not read input file %s", strerror(errno));
	strbuf_init(&output, 0);

	ret = fmt_merge_msg(merge_summary, &input, &output);
	if (ret)
		return ret;
	printf("%s", output.buf);
	return 0;
}
