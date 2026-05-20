#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "config.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "object-name.h"
#include "pager.h"
#include "parse-options.h"
#include "hash-lookup.h"
#include "commit-slab.h"
#include "commit-graph.h"
#include "wildmatch.h"
#include "mem-pool.h"
#include "pretty.h"
#include "revision.h"
#include "notes.h"
#include "write-or-die.h"

/*
 * One day.  See the 'name a rev shortly after epoch' test in t6120 when
 * changing this value
 */
#define CUTOFF_DATE_SLOP 86400

struct rev_name {
	const char *tip_name;
	timestamp_t taggerdate;
	int generation;
	int distance;
	int from_tag;
};

define_commit_slab(commit_rev_name, struct rev_name);

static timestamp_t generation_cutoff = GENERATION_NUMBER_INFINITY;
static timestamp_t cutoff = TIME_MAX;
static struct commit_rev_name rev_names;

/* Disable the cutoff checks entirely */
static void disable_cutoff(void)
{
	generation_cutoff = 0;
	cutoff = 0;
}

/* Cutoff searching any commits older than this one */
static void set_commit_cutoff(struct commit *commit)
{

	if (cutoff > commit->date)
		cutoff = commit->date;

	if (generation_cutoff) {
		timestamp_t generation = commit_graph_generation(commit);

		if (generation_cutoff > generation)
			generation_cutoff = generation;
	}
}

/* adjust the commit date cutoff with a slop to allow for slightly incorrect
 * commit timestamps in case of clock skew.
 */
static void adjust_cutoff_timestamp_for_slop(void)
{
	if (cutoff) {
		/* check for underflow */
		if (cutoff > TIME_MIN + CUTOFF_DATE_SLOP)
			cutoff = cutoff - CUTOFF_DATE_SLOP;
		else
			cutoff = TIME_MIN;
	}
}

/* Check if a commit is before the cutoff. Prioritize generation numbers
 * first, but use the commit timestamp if we lack generation data.
 */
static int commit_is_before_cutoff(struct commit *commit)
{
	if (generation_cutoff < GENERATION_NUMBER_INFINITY)
		return generation_cutoff &&
			commit_graph_generation(commit) < generation_cutoff;

	return commit->date < cutoff;
}

/* How many generations are maximally preferred over _one_ merge traversal? */
#define MERGE_TRAVERSAL_WEIGHT 65535

static int is_valid_rev_name(const struct rev_name *name)
{
	return name && name->tip_name;
}

static struct rev_name *get_commit_rev_name(const struct commit *commit)
{
	struct rev_name *name = commit_rev_name_peek(&rev_names, commit);

	return is_valid_rev_name(name) ? name : NULL;
}

static int effective_distance(int distance, int generation)
{
	return distance + (generation > 0 ? MERGE_TRAVERSAL_WEIGHT : 0);
}

static int is_better_name(struct rev_name *name,
			  timestamp_t taggerdate,
			  int generation,
			  int distance,
			  int from_tag)
{
	int name_distance = effective_distance(name->distance, name->generation);
	int new_distance = effective_distance(distance, generation);

	/* If both are tags, we prefer the nearer one. */
	if (from_tag && name->from_tag)
		return name_distance > new_distance;

	/* Favor a tag over a non-tag. */
	if (name->from_tag != from_tag)
		return from_tag;

	/*
	 * We are now looking at two non-tags.  Tiebreak to favor
	 * shorter hops.
	 */
	if (name_distance != new_distance)
		return name_distance > new_distance;

	/* ... or tiebreak to favor older date */
	if (name->taggerdate != taggerdate)
		return name->taggerdate > taggerdate;

	/* keep the current one if we cannot decide */
	return 0;
}

static struct rev_name *create_or_update_name(struct commit *commit,
					      timestamp_t taggerdate,
					      int generation, int distance,
					      int from_tag)
{
	struct rev_name *name = commit_rev_name_at(&rev_names, commit);

	if (is_valid_rev_name(name) &&
	    !is_better_name(name, taggerdate, generation, distance, from_tag))
		return NULL;

	name->taggerdate = taggerdate;
	name->generation = generation;
	name->distance = distance;
	name->from_tag = from_tag;

	return name;
}

static char *get_parent_name(const struct rev_name *name, int parent_number,
			     struct mem_pool *string_pool)
{
	size_t len;

	strip_suffix(name->tip_name, "^0", &len);
	if (name->generation > 0) {
		return mem_pool_strfmt(string_pool, "%.*s~%d^%d",
				       (int)len, name->tip_name,
				       name->generation, parent_number);
	} else {
		return mem_pool_strfmt(string_pool, "%.*s^%d",
				       (int)len, name->tip_name, parent_number);
	}
}

static void name_rev(struct commit *start_commit,
		const char *tip_name, timestamp_t taggerdate,
		int from_tag, int deref, struct mem_pool *string_pool)
{
	struct commit_stack stack = COMMIT_STACK_INIT;
	struct commit *commit;
	struct commit_stack parents_to_queue = COMMIT_STACK_INIT;
	struct rev_name *start_name;

	repo_parse_commit(the_repository, start_commit);
	if (commit_is_before_cutoff(start_commit))
		return;

	start_name = create_or_update_name(start_commit, taggerdate, 0, 0,
					   from_tag);
	if (!start_name)
		return;
	if (deref)
		start_name->tip_name = mem_pool_strfmt(string_pool, "%s^0",
						       tip_name);
	else
		start_name->tip_name = mem_pool_strdup(string_pool, tip_name);

	commit_stack_push(&stack, start_commit);

	while ((commit = commit_stack_pop(&stack))) {
		struct rev_name *name = get_commit_rev_name(commit);
		struct commit_list *parents;
		int parent_number = 1;

		parents_to_queue.nr = 0;

		for (parents = commit->parents;
				parents;
				parents = parents->next, parent_number++) {
			struct commit *parent = parents->item;
			struct rev_name *parent_name;
			int generation, distance;

			repo_parse_commit(the_repository, parent);
			if (commit_is_before_cutoff(parent))
				continue;

			if (parent_number > 1) {
				generation = 0;
				distance = name->distance + MERGE_TRAVERSAL_WEIGHT;
			} else {
				generation = name->generation + 1;
				distance = name->distance + 1;
			}

			parent_name = create_or_update_name(parent, taggerdate,
							    generation,
							    distance, from_tag);
			if (parent_name) {
				if (parent_number > 1)
					parent_name->tip_name =
						get_parent_name(name,
								parent_number,
								string_pool);
				else
					parent_name->tip_name = name->tip_name;
				commit_stack_push(&parents_to_queue, parent);
			}
		}

		/* The first parent must come out first from the stack */
		while (parents_to_queue.nr)
			commit_stack_push(&stack,
					  commit_stack_pop(&parents_to_queue));
	}

	commit_stack_clear(&stack);
	commit_stack_clear(&parents_to_queue);
}

static int subpath_matches(const char *path, const char *filter)
{
	const char *subpath = path;

	while (subpath) {
		if (!wildmatch(filter, subpath, 0))
			return subpath - path;
		subpath = strchr(subpath, '/');
		if (subpath)
			subpath++;
	}
	return -1;
}

struct name_ref_data {
	int tags_only;
	int name_only;
	struct string_list ref_filters;
	struct string_list exclude_filters;
};

struct pretty_format {
	struct pretty_print_context ctx;
	struct userformat_want want;
};

enum command_type {
	NAME_REV = 1,
	FORMAT_REV = 2,
};

enum stdin_mode {
    TEXT = 1,
    REVS = 2,
};

struct command {
	enum command_type type;
	union {
		int name_only;
		struct pretty_format *pretty_format;
	} u;
};

static void init_name_rev_command(struct command *cmd,
				  int name_only)
{
	cmd->type = NAME_REV;
	cmd->u.name_only = name_only;
}

static void init_format_rev_command(struct command *cmd,
				    struct pretty_format *pretty_format)
{
	cmd->type = FORMAT_REV;
	cmd->u.pretty_format = pretty_format;
}

static struct tip_table {
	struct tip_table_entry {
		struct object_id oid;
		const char *refname;
		struct commit *commit;
		timestamp_t taggerdate;
		unsigned int from_tag:1;
		unsigned int deref:1;
	} *table;
	int nr;
	int alloc;
	int sorted;
} tip_table;

static void add_to_tip_table(const struct object_id *oid, const char *refname,
			     int shorten_unambiguous, struct commit *commit,
			     timestamp_t taggerdate, int from_tag, int deref)
{
	char *short_refname = NULL;

	if (shorten_unambiguous)
		short_refname = refs_shorten_unambiguous_ref(get_main_ref_store(the_repository),
							     refname, 0);
	else if (skip_prefix(refname, "refs/heads/", &refname))
		; /* refname already advanced */
	else
		skip_prefix(refname, "refs/", &refname);

	ALLOC_GROW(tip_table.table, tip_table.nr + 1, tip_table.alloc);
	oidcpy(&tip_table.table[tip_table.nr].oid, oid);
	tip_table.table[tip_table.nr].refname = short_refname ?
		short_refname : xstrdup(refname);
	tip_table.table[tip_table.nr].commit = commit;
	tip_table.table[tip_table.nr].taggerdate = taggerdate;
	tip_table.table[tip_table.nr].from_tag = from_tag;
	tip_table.table[tip_table.nr].deref = deref;
	tip_table.nr++;
	tip_table.sorted = 0;
}

static int tipcmp(const void *a_, const void *b_)
{
	const struct tip_table_entry *a = a_, *b = b_;
	return oidcmp(&a->oid, &b->oid);
}

static int cmp_by_tag_and_age(const void *a_, const void *b_)
{
	const struct tip_table_entry *a = a_, *b = b_;
	int cmp;

	/* Prefer tags. */
	cmp = b->from_tag - a->from_tag;
	if (cmp)
		return cmp;

	/* Older is better. */
	if (a->taggerdate < b->taggerdate)
		return -1;
	return a->taggerdate != b->taggerdate;
}

static int name_ref(const struct reference *ref, void *cb_data)
{
	struct object *o = parse_object(the_repository, ref->oid);
	struct name_ref_data *data = cb_data;
	int can_abbreviate_output = data->tags_only && data->name_only;
	int deref = 0;
	int from_tag = 0;
	struct commit *commit = NULL;
	timestamp_t taggerdate = TIME_MAX;

	if (data->tags_only && !starts_with(ref->name, "refs/tags/"))
		return 0;

	if (data->exclude_filters.nr) {
		struct string_list_item *item;

		for_each_string_list_item(item, &data->exclude_filters) {
			if (subpath_matches(ref->name, item->string) >= 0)
				return 0;
		}
	}

	if (data->ref_filters.nr) {
		struct string_list_item *item;
		int matched = 0;

		/* See if any of the patterns match. */
		for_each_string_list_item(item, &data->ref_filters) {
			/*
			 * Check all patterns even after finding a match, so
			 * that we can see if a match with a subpath exists.
			 * When a user asked for 'refs/tags/v*' and 'v1.*',
			 * both of which match, the user is showing her
			 * willingness to accept a shortened output by having
			 * the 'v1.*' in the acceptable refnames, so we
			 * shouldn't stop when seeing 'refs/tags/v1.4' matches
			 * 'refs/tags/v*'.  We should show it as 'v1.4'.
			 */
			switch (subpath_matches(ref->name, item->string)) {
			case -1: /* did not match */
				break;
			case 0: /* matched fully */
				matched = 1;
				break;
			default: /* matched subpath */
				matched = 1;
				can_abbreviate_output = 1;
				break;
			}
		}

		/* If none of the patterns matched, stop now */
		if (!matched)
			return 0;
	}

	while (o && o->type == OBJ_TAG) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o = parse_object(the_repository, &t->tagged->oid);
		deref = 1;
		taggerdate = t->date;
	}
	if (o && o->type == OBJ_COMMIT) {
		commit = (struct commit *)o;
		from_tag = starts_with(ref->name, "refs/tags/");
		if (taggerdate == TIME_MAX)
			taggerdate = commit->date;
	}

	add_to_tip_table(ref->oid, ref->name, can_abbreviate_output,
			 commit, taggerdate, from_tag, deref);
	return 0;
}

static void name_tips(struct mem_pool *string_pool)
{
	int i;

	/*
	 * Try to set better names first, so that worse ones spread
	 * less.
	 */
	QSORT(tip_table.table, tip_table.nr, cmp_by_tag_and_age);
	for (i = 0; i < tip_table.nr; i++) {
		struct tip_table_entry *e = &tip_table.table[i];
		if (e->commit) {
			name_rev(e->commit, e->refname, e->taggerdate,
				 e->from_tag, e->deref, string_pool);
		}
	}
}

static const struct object_id *nth_tip_table_ent(size_t ix, const void *table_)
{
	const struct tip_table_entry *table = table_;
	return &table[ix].oid;
}

static const char *get_exact_ref_match(const struct object *o)
{
	int found;

	if (!tip_table.table || !tip_table.nr)
		return NULL;

	if (!tip_table.sorted) {
		QSORT(tip_table.table, tip_table.nr, tipcmp);
		tip_table.sorted = 1;
	}

	found = oid_pos(&o->oid, tip_table.table, tip_table.nr,
			nth_tip_table_ent);
	if (0 <= found)
		return tip_table.table[found].refname;
	return NULL;
}

/* may return a constant string or use "buf" as scratch space */
static const char *get_rev_name(const struct object *o, struct strbuf *buf)
{
	struct rev_name *n;
	const struct commit *c;

	if (o->type != OBJ_COMMIT)
		return get_exact_ref_match(o);
	c = (const struct commit *) o;
	n = get_commit_rev_name(c);
	if (!n)
		return NULL;

	if (!n->generation) {
		return n->tip_name;
	} else {
		strbuf_reset(buf);
		strbuf_addstr(buf, n->tip_name);
		strbuf_strip_suffix(buf, "^0");
		strbuf_addf(buf, "~%d", n->generation);
		return buf->buf;
	}
}

static const char *get_format_rev(const struct commit *c,
				  struct pretty_format *format_ctx,
				  struct strbuf *buf)
{
	strbuf_reset(buf);

	if (format_ctx->want.notes) {
		struct strbuf notebuf = STRBUF_INIT;

		format_display_notes(&c->object.oid, &notebuf,
				     get_log_output_encoding(),
				     format_ctx->ctx.fmt == CMIT_FMT_USERFORMAT);
		format_ctx->ctx.notes_message = strbuf_detach(&notebuf, NULL);
	}

	pretty_print_commit(&format_ctx->ctx, c, buf);
	FREE_AND_NULL(format_ctx->ctx.notes_message);

	return buf->buf;
}

static void show_name(const struct object *obj,
		      const char *caller_name,
		      int always, int allow_undefined, int name_only)
{
	const char *name;
	const struct object_id *oid = &obj->oid;
	struct strbuf buf = STRBUF_INIT;

	if (!name_only)
		printf("%s ", caller_name ? caller_name : oid_to_hex(oid));
	name = get_rev_name(obj, &buf);
	if (name)
		printf("%s\n", name);
	else if (allow_undefined)
		printf("undefined\n");
	else if (always)
		printf("%s\n",
		       repo_find_unique_abbrev(the_repository, oid, DEFAULT_ABBREV));
	else
		die("cannot describe '%s'", oid_to_hex(oid));
	strbuf_release(&buf);
}

static char const * const name_rev_usage[] = {
	N_("git name-rev [<options>] <commit>..."),
	N_("git name-rev [<options>] --all"),
	N_("git name-rev [<options>] --annotate-stdin"),
	NULL
};

static void name_rev_line(char *p, struct command *cmd)
{
	struct strbuf buf = STRBUF_INIT;
	int counter = 0;
	char *p_start;
	const unsigned hexsz = the_hash_algo->hexsz;

	for (p_start = p; *p; p++) {
#define ishex(x) (isdigit((x)) || ((x) >= 'a' && (x) <= 'f'))
		if (!ishex(*p)) {
			counter = 0;
		} else if (++counter == hexsz &&
			   !ishex(*(p + 1))) {
			struct object_id oid;
			const char *name = NULL;
			char c = *(p + 1);
			int p_len = p - p_start + 1;
			struct object *o = NULL;
			int oid_ret = 1;

			counter = 0;

			*(p + 1) = 0;
			oid_ret = repo_get_oid(the_repository, p - (hexsz - 1), &oid);
			*(p + 1) = c;

			switch (cmd->type) {
			case NAME_REV:
				if (!oid_ret)
					o = lookup_object(the_repository, &oid);
				if (o)
					name = get_rev_name(o, &buf);
				if (!name)
					continue;
				if (cmd->u.name_only)
					printf("%.*s%s", p_len - hexsz, p_start, name);
				else
					printf("%.*s (%s)", p_len, p_start, name);
				break;
			case FORMAT_REV:
				if (!oid_ret)
					o = parse_object(the_repository, &oid);
				if (o && o->type == OBJ_COMMIT)
					name = get_format_rev((const struct commit *)o,
							      cmd->u.pretty_format,
							      &buf);
				if (name)
					printf("%.*s%s", p_len - hexsz, p_start, name);
				else
					printf("%.*s", p_len, p_start);
				break;
			default:
				BUG("uncovered case: %d", cmd->type);
			}

			p_start = p + 1;
		}
	}

	/* flush */
	if (p_start != p)
		fwrite(p_start, p - p_start, 1, stdout);

	strbuf_release(&buf);
}

int cmd_name_rev(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo UNUSED)
{
	struct mem_pool string_pool;
	struct object_array revs = OBJECT_ARRAY_INIT;

#ifndef WITH_BREAKING_CHANGES
	int transform_stdin = 0;
#endif
	int all = 0, annotate_stdin = 0, allow_undefined = 1, always = 0, peel_tag = 0;
	struct name_ref_data data = { 0, 0, STRING_LIST_INIT_NODUP, STRING_LIST_INIT_NODUP };
	struct command cmd;
	struct option opts[] = {
		OPT_BOOL(0, "name-only", &data.name_only, N_("print only ref-based names (no object names)")),
		OPT_BOOL(0, "tags", &data.tags_only, N_("only use tags to name the commits")),
		OPT_STRING_LIST(0, "refs", &data.ref_filters, N_("pattern"),
				N_("only use refs matching <pattern>")),
		OPT_STRING_LIST(0, "exclude", &data.exclude_filters, N_("pattern"),
				N_("ignore refs matching <pattern>")),
		OPT_GROUP(""),
		OPT_BOOL(0, "all", &all, N_("list all commits reachable from all refs")),
#ifndef WITH_BREAKING_CHANGES
		OPT_BOOL_F(0,
			   "stdin",
			   &transform_stdin,
			   N_("deprecated: use --annotate-stdin instead"),
			   PARSE_OPT_HIDDEN),
#endif /* WITH_BREAKING_CHANGES */
		OPT_BOOL(0, "annotate-stdin", &annotate_stdin, N_("annotate text from stdin")),
		OPT_BOOL(0, "undefined", &allow_undefined, N_("allow to print `undefined` names (default)")),
		OPT_BOOL(0, "always", &always,
			 N_("show abbreviated commit object as fallback")),
		OPT_HIDDEN_BOOL(0, "peel-tag", &peel_tag,
				N_("dereference tags in the input (internal use)")),
		OPT_END(),
	};

	mem_pool_init(&string_pool, 0);
	init_commit_rev_name(&rev_names);
	repo_config(the_repository, git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, opts, name_rev_usage, 0);
	init_name_rev_command(&cmd, data.name_only);

#ifndef WITH_BREAKING_CHANGES
	if (transform_stdin) {
		warning("--stdin is deprecated. Please use --annotate-stdin instead, "
					"which is functionally equivalent.\n"
					"This option will be removed in a future release.");
		annotate_stdin = 1;
	}
#endif

	if (all + annotate_stdin + !!argc > 1) {
		error("Specify either a list, or --all, not both!");
		usage_with_options(name_rev_usage, opts);
	}
	if (all || annotate_stdin)
		disable_cutoff();

	for (; argc; argc--, argv++) {
		struct object_id oid;
		struct object *object;
		struct commit *commit;

		if (repo_get_oid(the_repository, *argv, &oid)) {
			fprintf(stderr, "Could not get sha1 for %s. Skipping.\n",
					*argv);
			continue;
		}

		commit = NULL;
		object = parse_object(the_repository, &oid);
		if (object) {
			struct object *peeled = deref_tag(the_repository,
							  object, *argv, 0);
			if (peeled && peeled->type == OBJ_COMMIT)
				commit = (struct commit *)peeled;
		}

		if (!object) {
			fprintf(stderr, "Could not get object for %s. Skipping.\n",
					*argv);
			continue;
		}

		if (commit)
			set_commit_cutoff(commit);

		if (peel_tag) {
			if (!commit) {
				fprintf(stderr, "Could not get commit for %s. Skipping.\n",
					*argv);
				continue;
			}
			object = (struct object *)commit;
		}
		add_object_array(object, *argv, &revs);
	}

	adjust_cutoff_timestamp_for_slop();

	refs_for_each_ref(get_main_ref_store(the_repository), name_ref, &data);
	name_tips(&string_pool);

	if (annotate_stdin) {
		struct strbuf sb = STRBUF_INIT;

		while (strbuf_getline(&sb, stdin) != EOF) {
			strbuf_addch(&sb, '\n');
			name_rev_line(sb.buf, &cmd);
		}
		strbuf_release(&sb);
	} else if (all) {
		int i, max;

		max = get_max_object_index(the_repository);
		for (i = 0; i < max; i++) {
			struct object *obj = get_indexed_object(the_repository, i);
			if (!obj || obj->type != OBJ_COMMIT)
				continue;
			show_name(obj, NULL,
				  always, allow_undefined, data.name_only);
		}
	} else {
		int i;
		for (i = 0; i < revs.nr; i++)
			show_name(revs.objects[i].item, revs.objects[i].name,
				  always, allow_undefined, data.name_only);
	}

	string_list_clear(&data.ref_filters, 0);
	string_list_clear(&data.exclude_filters, 0);
	mem_pool_discard(&string_pool, 0);
	object_array_clear(&revs);
	return 0;
}

struct format_nul_data {
	bool nul_input;
	bool nul_output;
};

static int format_nul_cb(const struct option *option,
			 const char *arg,
			 int unset)
{
	struct format_nul_data *data = option->value;
	data->nul_input = 1;
	data->nul_output = 1;
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return 0;
}

static enum stdin_mode parse_stdin_mode(const char *stdin_mode)
{
	if (!strcmp(stdin_mode, "text"))
		return TEXT;
	else if (!strcmp(stdin_mode, "revs") ||
		 !strcmp(stdin_mode, "rev"))
		return REVS;
	else
		die(_("'%s' needs to be either text, revs, or rev"),
		    "--stdin-mode");
}

static char const *const format_rev_usage[] = {
	N_("(EXPERIMENTAL!) git format-rev --stdin-mode=<mode> "
	   "--format=<pretty> [--[no-]notes=<ref>] "
	   "[-z] [--[no-]null-output] [--[no-]null-input]"),
	NULL
};

int cmd_format_rev(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo UNUSED)
{
	const char *format = NULL;
	enum stdin_mode stdin_mode;
	const char *stdin_mode_arg = NULL;
	struct format_nul_data nul_data = { 0, 0 };
	char output_terminator;
	strbuf_getline_fn getline_fn;
	struct display_notes_opt format_notes_opt;
	struct rev_info format_rev = REV_INFO_INIT;
	struct pretty_format format_pp = { 0 };
	struct string_list notes = STRING_LIST_INIT_NODUP;
	struct strbuf scratch_buf = STRBUF_INIT;
	struct command cmd;
	struct option opts[] = {
		OPT_STRING(0, "format", &format, N_("format"),
			   N_("pretty format to use")),
		OPT_STRING(0, "stdin-mode", &stdin_mode_arg, N_("stdin-mode"),
			   N_("how revs are processed")),
		OPT_STRING_LIST(0, "notes", &notes, N_("notes"),
				N_("display notes for pretty format")),
		OPT_CALLBACK_F('z', "null", &nul_data, N_("z"),
			       N_("Use NUL for input and output termination"),
			       PARSE_OPT_NOARG | PARSE_OPT_NONEG, format_nul_cb),
		OPT_BOOL(0, "null-input", &nul_data.nul_input,
			 N_("Use NUL for input termination")),
		OPT_BOOL(0, "null-output", &nul_data.nul_output,
			 N_("Use NUL for output termination")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, opts, format_rev_usage, 0);

	if (argc > 0) {
		error(_("too many arguments"));
		usage_with_options(format_rev_usage, opts);
	}

	if (!format)
		die(_("'%s' is required"), "--format");
	if (!stdin_mode_arg)
		die(_("'%s' is required"), "--stdin-mode");

	getline_fn = nul_data.nul_input ? strbuf_getline_nul : strbuf_getline_lf;
	output_terminator = nul_data.nul_output ? '\0' : '\n';

	init_display_notes(&format_notes_opt);
	stdin_mode = parse_stdin_mode(stdin_mode_arg);

	get_commit_format(format, &format_rev);
	format_pp.ctx.rev = &format_rev;
	format_pp.ctx.fmt = format_rev.commit_format;
	format_pp.ctx.abbrev = format_rev.abbrev;
	format_pp.ctx.date_mode_explicit = format_rev.date_mode_explicit;
	format_pp.ctx.date_mode = format_rev.date_mode;
	format_pp.ctx.color = GIT_COLOR_AUTO;

	userformat_find_requirements(format,
				     &format_pp.want);
	if (format_pp.want.notes) {
		int ignore_show_notes = 0;
		struct string_list_item *n;

		for_each_string_list_item(n, &notes)
			enable_ref_display_notes(&format_notes_opt,
						 &ignore_show_notes,
						 n->string);
		load_display_notes(&format_notes_opt);
	}

	init_format_rev_command(&cmd, &format_pp);

	switch (stdin_mode) {
	case TEXT:
		while (getline_fn(&scratch_buf, stdin) != EOF) {
			name_rev_line(scratch_buf.buf, &cmd);
			/*
			 * We do not pass on the terminator to name_rev_line,
			 * unlike name-rev.
			 */
			printf("%c", output_terminator);
			maybe_flush_or_die(stdout, "stdout");
		}
		break;
	case REVS:
		while (getline_fn(&scratch_buf, stdin) != EOF) {
			struct object_id oid;
			struct object *object;
			struct object *peeled;

			if (repo_get_oid(the_repository, scratch_buf.buf, &oid)) {
				fprintf(stderr, "Could not get object name for %s. Skipping.\n",
					scratch_buf.buf);
				continue;
			}

			object = parse_object(the_repository, &oid);
			if (!object) {
				fprintf(stderr, "Could not get object for %s. Skipping.\n",
					scratch_buf.buf);
				continue;
			}

			peeled = deref_tag(the_repository, object, scratch_buf.buf, 0);
			if (!peeled || peeled->type != OBJ_COMMIT) {
				fprintf(stderr,
					"Could not get commit for %s. Skipping.\n",
					scratch_buf.buf);
				continue;
			}

			get_format_rev((struct commit *)peeled,
				       &format_pp, &scratch_buf);
			printf("%s%c", scratch_buf.buf, output_terminator);
			maybe_flush_or_die(stdout, "stdout");
			strbuf_release(&scratch_buf);
		}
		break;
	default:
		BUG("uncovered case: %d", stdin_mode);
	}

	strbuf_release(&scratch_buf);
	string_list_clear(&notes, 0);
	release_display_notes(&format_notes_opt);
	return 0;
}
