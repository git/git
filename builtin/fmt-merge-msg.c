#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "tag.h"
#include "string-list.h"
#include "branch.h"
#include "fmt-merge-msg.h"
#include "gpg-interface.h"

static const char * const fmt_merge_msg_usage[] = {
	N_("git fmt-merge-msg [-m <message>] [--log[=<n>]|--no-log] [--file <file>]"),
	NULL
};

static int use_branch_desc;

int fmt_merge_msg_config(const char *key, const char *value, void *cb)
{
	if (!strcmp(key, "merge.log") || !strcmp(key, "merge.summary")) {
		int is_bool;
		merge_log_config = git_config_bool_or_int(key, value, &is_bool);
		if (!is_bool && merge_log_config < 0)
			return error("%s: negative length %s", key, value);
		if (is_bool && merge_log_config)
			merge_log_config = DEFAULT_MERGE_LOG_LEN;
	} else if (!strcmp(key, "merge.branchdesc")) {
		use_branch_desc = git_config_bool(key, value);
	} else {
		return git_default_config(key, value, cb);
	}
	return 0;
}

/* merge data per repository where the merged tips came from */
struct src_data {
	struct string_list branch, tag, r_branch, generic;
	int head_status;
};

struct origin_data {
	unsigned char sha1[20];
	unsigned is_local_branch:1;
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

struct merge_parents {
	int alloc, nr;
	struct merge_parent {
		unsigned char given[20];
		unsigned char commit[20];
		unsigned char used;
	} *item;
};

/*
 * I know, I know, this is inefficient, but you won't be pulling and merging
 * hundreds of heads at a time anyway.
 */
static struct merge_parent *find_merge_parent(struct merge_parents *table,
					      unsigned char *given,
					      unsigned char *commit)
{
	int i;
	for (i = 0; i < table->nr; i++) {
		if (given && hashcmp(table->item[i].given, given))
			continue;
		if (commit && hashcmp(table->item[i].commit, commit))
			continue;
		return &table->item[i];
	}
	return NULL;
}

static void add_merge_parent(struct merge_parents *table,
			     unsigned char *given,
			     unsigned char *commit)
{
	if (table->nr && find_merge_parent(table, given, commit))
		return;
	ALLOC_GROW(table->item, table->nr + 1, table->alloc);
	hashcpy(table->item[table->nr].given, given);
	hashcpy(table->item[table->nr].commit, commit);
	table->item[table->nr].used = 0;
	table->nr++;
}

static int handle_line(char *line, struct merge_parents *merge_parents)
{
	int i, len = strlen(line);
	struct origin_data *origin_data;
	char *src, *origin;
	struct src_data *src_data;
	struct string_list_item *item;
	int pulling_head = 0;
	unsigned char sha1[20];

	if (len < 43 || line[40] != '\t')
		return 1;

	if (!prefixcmp(line + 41, "not-for-merge"))
		return 0;

	if (line[41] != '\t')
		return 2;

	i = get_sha1_hex(line, sha1);
	if (i)
		return 3;

	if (!find_merge_parent(merge_parents, sha1, NULL))
		return 0; /* subsumed by other parents */

	origin_data = xcalloc(1, sizeof(struct origin_data));
	hashcpy(origin_data->sha1, sha1);

	if (line[len - 1] == '\n')
		line[len - 1] = 0;
	line += 42;

	/*
	 * At this point, line points at the beginning of comment e.g.
	 * "branch 'frotz' of git://that/repository.git".
	 * Find the repository name and point it with src.
	 */
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
		origin_data->is_local_branch = 1;
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
	if (strcmp(".", src))
		origin_data->is_local_branch = 0;
	string_list_append(&origins, origin)->util = origin_data;
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

static void add_branch_desc(struct strbuf *out, const char *name)
{
	struct strbuf desc = STRBUF_INIT;

	if (!read_branch_desc(&desc, name)) {
		const char *bp = desc.buf;
		while (*bp) {
			const char *ep = strchrnul(bp, '\n');
			if (*ep)
				ep++;
			strbuf_addf(out, "  : %.*s", (int)(ep - bp), bp);
			bp = ep;
		}
		if (out->buf[out->len - 1] != '\n')
			strbuf_addch(out, '\n');
	}
	strbuf_release(&desc);
}

#define util_as_integral(elem) ((intptr_t)((elem)->util))

static void record_person(int which, struct string_list *people,
			  struct commit *commit)
{
	char *name_buf, *name, *name_end;
	struct string_list_item *elem;
	const char *field;

	field = (which == 'a') ? "\nauthor " : "\ncommitter ";
	name = strstr(commit->buffer, field);
	if (!name)
		return;
	name += strlen(field);
	name_end = strchrnul(name, '<');
	if (*name_end)
		name_end--;
	while (isspace(*name_end) && name <= name_end)
		name_end--;
	if (name_end < name)
		return;
	name_buf = xmemdupz(name, name_end - name + 1);

	elem = string_list_lookup(people, name_buf);
	if (!elem) {
		elem = string_list_insert(people, name_buf);
		elem->util = (void *)0;
	}
	elem->util = (void*)(util_as_integral(elem) + 1);
	free(name_buf);
}

static int cmp_string_list_util_as_integral(const void *a_, const void *b_)
{
	const struct string_list_item *a = a_, *b = b_;
	return util_as_integral(b) - util_as_integral(a);
}

static void add_people_count(struct strbuf *out, struct string_list *people)
{
	if (people->nr == 1)
		strbuf_addf(out, "%s", people->items[0].string);
	else if (people->nr == 2)
		strbuf_addf(out, "%s (%d) and %s (%d)",
			    people->items[0].string,
			    (int)util_as_integral(&people->items[0]),
			    people->items[1].string,
			    (int)util_as_integral(&people->items[1]));
	else if (people->nr)
		strbuf_addf(out, "%s (%d) and others",
			    people->items[0].string,
			    (int)util_as_integral(&people->items[0]));
}

static void credit_people(struct strbuf *out,
			  struct string_list *them,
			  int kind)
{
	const char *label;
	const char *me;

	if (kind == 'a') {
		label = "\n# By ";
		me = git_author_info(IDENT_NO_DATE);
	} else {
		label = "\n# Via ";
		me = git_committer_info(IDENT_NO_DATE);
	}

	if (!them->nr ||
	    (them->nr == 1 &&
	     me &&
	     (me = skip_prefix(me, them->items->string)) != NULL &&
	     skip_prefix(me, " <")))
		return;
	strbuf_addstr(out, label);
	add_people_count(out, them);
}

static void add_people_info(struct strbuf *out,
			    struct string_list *authors,
			    struct string_list *committers)
{
	if (authors->nr)
		qsort(authors->items,
		      authors->nr, sizeof(authors->items[0]),
		      cmp_string_list_util_as_integral);
	if (committers->nr)
		qsort(committers->items,
		      committers->nr, sizeof(committers->items[0]),
		      cmp_string_list_util_as_integral);

	credit_people(out, authors, 'a');
	credit_people(out, committers, 'c');
}

static void shortlog(const char *name,
		     struct origin_data *origin_data,
		     struct commit *head,
		     struct rev_info *rev,
		     struct fmt_merge_msg_opts *opts,
		     struct strbuf *out)
{
	int i, count = 0;
	struct commit *commit;
	struct object *branch;
	struct string_list subjects = STRING_LIST_INIT_DUP;
	struct string_list authors = STRING_LIST_INIT_DUP;
	struct string_list committers = STRING_LIST_INIT_DUP;
	int flags = UNINTERESTING | TREESAME | SEEN | SHOWN | ADDED;
	struct strbuf sb = STRBUF_INIT;
	const unsigned char *sha1 = origin_data->sha1;
	int limit = opts->shortlog_len;

	branch = deref_tag(parse_object(sha1), sha1_to_hex(sha1), 40);
	if (!branch || branch->type != OBJ_COMMIT)
		return;

	setup_revisions(0, NULL, rev, NULL);
	add_pending_object(rev, branch, name);
	add_pending_object(rev, &head->object, "^HEAD");
	head->object.flags |= UNINTERESTING;
	if (prepare_revision_walk(rev))
		die("revision walk setup failed");
	while ((commit = get_revision(rev)) != NULL) {
		struct pretty_print_context ctx = {0};

		if (commit->parents && commit->parents->next) {
			/* do not list a merge but count committer */
			if (opts->credit_people)
				record_person('c', &committers, commit);
			continue;
		}
		if (!count && opts->credit_people)
			/* the 'tip' committer */
			record_person('c', &committers, commit);
		if (opts->credit_people)
			record_person('a', &authors, commit);
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

	if (opts->credit_people)
		add_people_info(out, &authors, &committers);
	if (count > limit)
		strbuf_addf(out, "\n* %s: (%d commits)\n", name, count);
	else
		strbuf_addf(out, "\n* %s:\n", name);

	if (origin_data->is_local_branch && use_branch_desc)
		add_branch_desc(out, name);

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

	string_list_clear(&authors, 0);
	string_list_clear(&committers, 0);
	string_list_clear(&subjects, 0);
}

static void fmt_merge_msg_title(struct strbuf *out,
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

static void fmt_tag_signature(struct strbuf *tagbuf,
			      struct strbuf *sig,
			      const char *buf,
			      unsigned long len)
{
	const char *tag_body = strstr(buf, "\n\n");
	if (tag_body) {
		tag_body += 2;
		strbuf_add(tagbuf, tag_body, buf + len - tag_body);
	}
	strbuf_complete_line(tagbuf);
	if (sig->len) {
		strbuf_addch(tagbuf, '\n');
		strbuf_add_lines(tagbuf, "# ", sig->buf, sig->len);
	}
}

static void fmt_merge_msg_sigs(struct strbuf *out)
{
	int i, tag_number = 0, first_tag = 0;
	struct strbuf tagbuf = STRBUF_INIT;

	for (i = 0; i < origins.nr; i++) {
		unsigned char *sha1 = origins.items[i].util;
		enum object_type type;
		unsigned long size, len;
		char *buf = read_sha1_file(sha1, &type, &size);
		struct strbuf sig = STRBUF_INIT;

		if (!buf || type != OBJ_TAG)
			goto next;
		len = parse_signature(buf, size);

		if (size == len)
			; /* merely annotated */
		else if (verify_signed_buffer(buf, len, buf + len, size - len, &sig)) {
			if (!sig.len)
				strbuf_addstr(&sig, "gpg verification failed.\n");
		}

		if (!tag_number++) {
			fmt_tag_signature(&tagbuf, &sig, buf, len);
			first_tag = i;
		} else {
			if (tag_number == 2) {
				struct strbuf tagline = STRBUF_INIT;
				strbuf_addf(&tagline, "\n# %s\n",
					    origins.items[first_tag].string);
				strbuf_insert(&tagbuf, 0, tagline.buf,
					      tagline.len);
				strbuf_release(&tagline);
			}
			strbuf_addf(&tagbuf, "\n# %s\n",
				    origins.items[i].string);
			fmt_tag_signature(&tagbuf, &sig, buf, len);
		}
		strbuf_release(&sig);
	next:
		free(buf);
	}
	if (tagbuf.len) {
		strbuf_addch(out, '\n');
		strbuf_addbuf(out, &tagbuf);
	}
	strbuf_release(&tagbuf);
}

static void find_merge_parents(struct merge_parents *result,
			       struct strbuf *in, unsigned char *head)
{
	struct commit_list *parents, *next;
	struct commit *head_commit;
	int pos = 0, i, j;

	parents = NULL;
	while (pos < in->len) {
		int len;
		char *p = in->buf + pos;
		char *newline = strchr(p, '\n');
		unsigned char sha1[20];
		struct commit *parent;
		struct object *obj;

		len = newline ? newline - p : strlen(p);
		pos += len + !!newline;

		if (len < 43 ||
		    get_sha1_hex(p, sha1) ||
		    p[40] != '\t' ||
		    p[41] != '\t')
			continue; /* skip not-for-merge */
		/*
		 * Do not use get_merge_parent() here; we do not have
		 * "name" here and we do not want to contaminate its
		 * util field yet.
		 */
		obj = parse_object(sha1);
		parent = (struct commit *)peel_to_type(NULL, 0, obj, OBJ_COMMIT);
		if (!parent)
			continue;
		commit_list_insert(parent, &parents);
		add_merge_parent(result, obj->sha1, parent->object.sha1);
	}
	head_commit = lookup_commit(head);
	if (head_commit)
		commit_list_insert(head_commit, &parents);
	parents = reduce_heads(parents);

	while (parents) {
		for (i = 0; i < result->nr; i++)
			if (!hashcmp(result->item[i].commit,
				     parents->item->object.sha1))
				result->item[i].used = 1;
		next = parents->next;
		free(parents);
		parents = next;
	}

	for (i = j = 0; i < result->nr; i++) {
		if (result->item[i].used) {
			if (i != j)
				result->item[j] = result->item[i];
			j++;
		}
	}
	result->nr = j;
}

int fmt_merge_msg(struct strbuf *in, struct strbuf *out,
		  struct fmt_merge_msg_opts *opts)
{
	int i = 0, pos = 0;
	unsigned char head_sha1[20];
	const char *current_branch;
	void *current_branch_to_free;
	struct merge_parents merge_parents;

	memset(&merge_parents, 0, sizeof(merge_parents));

	/* get current branch */
	current_branch = current_branch_to_free =
		resolve_refdup("HEAD", head_sha1, 1, NULL);
	if (!current_branch)
		die("No current branch");
	if (!prefixcmp(current_branch, "refs/heads/"))
		current_branch += 11;

	find_merge_parents(&merge_parents, in, head_sha1);

	/* get a line */
	while (pos < in->len) {
		int len;
		char *newline, *p = in->buf + pos;

		newline = strchr(p, '\n');
		len = newline ? newline - p : strlen(p);
		pos += len + !!newline;
		i++;
		p[len] = 0;
		if (handle_line(p, &merge_parents))
			die ("Error in line %d: %.*s", i, len, p);
	}

	if (opts->add_title && srcs.nr)
		fmt_merge_msg_title(out, current_branch);

	if (origins.nr)
		fmt_merge_msg_sigs(out);

	if (opts->shortlog_len) {
		struct commit *head;
		struct rev_info rev;

		head = lookup_commit_or_die(head_sha1, "HEAD");
		init_revisions(&rev, NULL);
		rev.commit_format = CMIT_FMT_ONELINE;
		rev.ignore_merges = 1;
		rev.limited = 1;

		strbuf_complete_line(out);

		for (i = 0; i < origins.nr; i++)
			shortlog(origins.items[i].string,
				 origins.items[i].util,
				 head, &rev, opts, out);
	}

	strbuf_complete_line(out);
	free(current_branch_to_free);
	free(merge_parents.item);
	return 0;
}

int cmd_fmt_merge_msg(int argc, const char **argv, const char *prefix)
{
	const char *inpath = NULL;
	const char *message = NULL;
	int shortlog_len = -1;
	struct option options[] = {
		{ OPTION_INTEGER, 0, "log", &shortlog_len, N_("n"),
		  N_("populate log with at most <n> entries from shortlog"),
		  PARSE_OPT_OPTARG, NULL, DEFAULT_MERGE_LOG_LEN },
		{ OPTION_INTEGER, 0, "summary", &shortlog_len, N_("n"),
		  N_("alias for --log (deprecated)"),
		  PARSE_OPT_OPTARG | PARSE_OPT_HIDDEN, NULL,
		  DEFAULT_MERGE_LOG_LEN },
		OPT_STRING('m', "message", &message, N_("text"),
			N_("use <text> as start of message")),
		OPT_FILENAME('F', "file", &inpath, N_("file to read from")),
		OPT_END()
	};

	FILE *in = stdin;
	struct strbuf input = STRBUF_INIT, output = STRBUF_INIT;
	int ret;
	struct fmt_merge_msg_opts opts;

	git_config(fmt_merge_msg_config, NULL);
	argc = parse_options(argc, argv, prefix, options, fmt_merge_msg_usage,
			     0);
	if (argc > 0)
		usage_with_options(fmt_merge_msg_usage, options);
	if (shortlog_len < 0)
		shortlog_len = (merge_log_config > 0) ? merge_log_config : 0;

	if (inpath && strcmp(inpath, "-")) {
		in = fopen(inpath, "r");
		if (!in)
			die_errno("cannot open '%s'", inpath);
	}

	if (strbuf_read(&input, fileno(in), 0) < 0)
		die_errno("could not read input file");

	if (message)
		strbuf_addstr(&output, message);

	memset(&opts, 0, sizeof(opts));
	opts.add_title = !message;
	opts.credit_people = 1;
	opts.shortlog_len = shortlog_len;

	ret = fmt_merge_msg(&input, &output, &opts);
	if (ret)
		return ret;
	write_in_full(STDOUT_FILENO, output.buf, output.len);
	return 0;
}
