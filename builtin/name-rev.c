#include "builtin.h"
#include "cache.h"
#include "repository.h"
#include "config.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "parse-options.h"
#include "prio-queue.h"
#include "sha1-lookup.h"
#include "commit-slab.h"

/*
 * One day.  See the 'name a rev shortly after epoch' test in t6120 when
 * changing this value
 */
#define CUTOFF_DATE_SLOP 86400

struct rev_name {
	char *tip_name;
	timestamp_t taggerdate;
	int generation;
	int distance;
	int from_tag;
};

define_commit_slab(commit_rev_name, struct rev_name);

static timestamp_t cutoff = TIME_MAX;
static struct commit_rev_name rev_names;

/* How many generations are maximally preferred over _one_ merge traversal? */
#define MERGE_TRAVERSAL_WEIGHT 65535

static int is_valid_rev_name(const struct rev_name *name)
{
	return name && (name->generation || name->tip_name);
}

static struct rev_name *get_commit_rev_name(const struct commit *commit)
{
	struct rev_name *name = commit_rev_name_peek(&rev_names, commit);

	return is_valid_rev_name(name) ? name : NULL;
}

static int is_better_name(struct rev_name *name,
			  timestamp_t taggerdate,
			  int distance,
			  int from_tag)
{
	/*
	 * When comparing names based on tags, prefer names
	 * based on the older tag, even if it is farther away.
	 */
	if (from_tag && name->from_tag)
		return (name->taggerdate > taggerdate ||
			(name->taggerdate == taggerdate &&
			 name->distance > distance));

	/*
	 * We know that at least one of them is a non-tag at this point.
	 * favor a tag over a non-tag.
	 */
	if (name->from_tag != from_tag)
		return from_tag;

	/*
	 * We are now looking at two non-tags.  Tiebreak to favor
	 * shorter hops.
	 */
	if (name->distance != distance)
		return name->distance > distance;

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

	if (is_valid_rev_name(name)) {
		if (!is_better_name(name, taggerdate, distance, from_tag))
			return NULL;

		/*
		 * This string might still be shared with ancestors
		 * (generation > 0).  We can release it here regardless,
		 * because the new name that has just won will be better
		 * for them as well, so name_rev() will replace these
		 * stale pointers when it processes the parents.
		 */
		if (!name->generation)
			free(name->tip_name);
	}

	name->taggerdate = taggerdate;
	name->generation = generation;
	name->distance = distance;
	name->from_tag = from_tag;

	return name;
}

static char *get_parent_name(const struct rev_name *name, int parent_number)
{
	struct strbuf sb = STRBUF_INIT;
	size_t len;

	strip_suffix(name->tip_name, "^0", &len);
	if (name->generation > 0) {
		strbuf_grow(&sb, len +
			    1 + decimal_width(name->generation) +
			    1 + decimal_width(parent_number));
		strbuf_addf(&sb, "%.*s~%d^%d", (int)len, name->tip_name,
			    name->generation, parent_number);
	} else {
		strbuf_grow(&sb, len +
			    1 + decimal_width(parent_number));
		strbuf_addf(&sb, "%.*s^%d", (int)len, name->tip_name,
			    parent_number);
	}
	return strbuf_detach(&sb, NULL);
}

static void name_rev(struct commit *start_commit,
		const char *tip_name, timestamp_t taggerdate,
		int from_tag, int deref)
{
	struct prio_queue queue;
	struct commit *commit;
	struct commit **parents_to_queue = NULL;
	size_t parents_to_queue_nr, parents_to_queue_alloc = 0;
	struct rev_name *start_name;

	parse_commit(start_commit);
	if (start_commit->date < cutoff)
		return;

	start_name = create_or_update_name(start_commit, taggerdate, 0, 0,
					   from_tag);
	if (!start_name)
		return;
	if (deref)
		start_name->tip_name = xstrfmt("%s^0", tip_name);
	else
		start_name->tip_name = xstrdup(tip_name);

	memset(&queue, 0, sizeof(queue)); /* Use the prio_queue as LIFO */
	prio_queue_put(&queue, start_commit);

	while ((commit = prio_queue_get(&queue))) {
		struct rev_name *name = get_commit_rev_name(commit);
		struct commit_list *parents;
		int parent_number = 1;

		parents_to_queue_nr = 0;

		for (parents = commit->parents;
				parents;
				parents = parents->next, parent_number++) {
			struct commit *parent = parents->item;
			struct rev_name *parent_name;
			int generation, distance;

			parse_commit(parent);
			if (parent->date < cutoff)
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
								parent_number);
				else
					parent_name->tip_name = name->tip_name;
				ALLOC_GROW(parents_to_queue,
					   parents_to_queue_nr + 1,
					   parents_to_queue_alloc);
				parents_to_queue[parents_to_queue_nr] = parent;
				parents_to_queue_nr++;
			}
		}

		/* The first parent must come out first from the prio_queue */
		while (parents_to_queue_nr)
			prio_queue_put(&queue,
				       parents_to_queue[--parents_to_queue_nr]);
	}

	clear_prio_queue(&queue);
	free(parents_to_queue);
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

static const char *name_ref_abbrev(const char *refname, int shorten_unambiguous)
{
	if (shorten_unambiguous)
		refname = shorten_unambiguous_ref(refname, 0);
	else if (skip_prefix(refname, "refs/heads/", &refname))
		; /* refname already advanced */
	else
		skip_prefix(refname, "refs/", &refname);
	return refname;
}

struct name_ref_data {
	int tags_only;
	int name_only;
	struct string_list ref_filters;
	struct string_list exclude_filters;
};

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
	refname = name_ref_abbrev(refname, shorten_unambiguous);

	ALLOC_GROW(tip_table.table, tip_table.nr + 1, tip_table.alloc);
	oidcpy(&tip_table.table[tip_table.nr].oid, oid);
	tip_table.table[tip_table.nr].refname = xstrdup(refname);
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

static int name_ref(const char *path, const struct object_id *oid, int flags, void *cb_data)
{
	struct object *o = parse_object(the_repository, oid);
	struct name_ref_data *data = cb_data;
	int can_abbreviate_output = data->tags_only && data->name_only;
	int deref = 0;
	int from_tag = 0;
	struct commit *commit = NULL;
	timestamp_t taggerdate = TIME_MAX;

	if (data->tags_only && !starts_with(path, "refs/tags/"))
		return 0;

	if (data->exclude_filters.nr) {
		struct string_list_item *item;

		for_each_string_list_item(item, &data->exclude_filters) {
			if (subpath_matches(path, item->string) >= 0)
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
			switch (subpath_matches(path, item->string)) {
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
		from_tag = starts_with(path, "refs/tags/");
		if (taggerdate == TIME_MAX)
			taggerdate = commit->date;
	}

	add_to_tip_table(oid, path, can_abbreviate_output, commit, taggerdate,
			 from_tag, deref);
	return 0;
}

static void name_tips(void)
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
				 e->from_tag, e->deref);
		}
	}
}

static const unsigned char *nth_tip_table_ent(size_t ix, void *table_)
{
	struct tip_table_entry *table = table_;
	return table[ix].oid.hash;
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

	found = sha1_pos(o->oid.hash, tip_table.table, tip_table.nr,
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

	if (!n->generation)
		return n->tip_name;
	else {
		strbuf_reset(buf);
		strbuf_addstr(buf, n->tip_name);
		strbuf_strip_suffix(buf, "^0");
		strbuf_addf(buf, "~%d", n->generation);
		return buf->buf;
	}
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
		printf("%s\n", find_unique_abbrev(oid, DEFAULT_ABBREV));
	else
		die("cannot describe '%s'", oid_to_hex(oid));
	strbuf_release(&buf);
}

static char const * const name_rev_usage[] = {
	N_("git name-rev [<options>] <commit>..."),
	N_("git name-rev [<options>] --all"),
	N_("git name-rev [<options>] --stdin"),
	NULL
};

static void name_rev_line(char *p, struct name_ref_data *data)
{
	struct strbuf buf = STRBUF_INIT;
	int counter = 0;
	char *p_start;
	const unsigned hexsz = the_hash_algo->hexsz;

	for (p_start = p; *p; p++) {
#define ishex(x) (isdigit((x)) || ((x) >= 'a' && (x) <= 'f'))
		if (!ishex(*p))
			counter = 0;
		else if (++counter == hexsz &&
			 !ishex(*(p+1))) {
			struct object_id oid;
			const char *name = NULL;
			char c = *(p+1);
			int p_len = p - p_start + 1;

			counter = 0;

			*(p+1) = 0;
			if (!get_oid(p - (hexsz - 1), &oid)) {
				struct object *o =
					lookup_object(the_repository, &oid);
				if (o)
					name = get_rev_name(o, &buf);
			}
			*(p+1) = c;

			if (!name)
				continue;

			if (data->name_only)
				printf("%.*s%s", p_len - hexsz, p_start, name);
			else
				printf("%.*s (%s)", p_len, p_start, name);
			p_start = p + 1;
		}
	}

	/* flush */
	if (p_start != p)
		fwrite(p_start, p - p_start, 1, stdout);

	strbuf_release(&buf);
}

int cmd_name_rev(int argc, const char **argv, const char *prefix)
{
	struct object_array revs = OBJECT_ARRAY_INIT;
	int all = 0, transform_stdin = 0, allow_undefined = 1, always = 0, peel_tag = 0;
	struct name_ref_data data = { 0, 0, STRING_LIST_INIT_NODUP, STRING_LIST_INIT_NODUP };
	struct option opts[] = {
		OPT_BOOL(0, "name-only", &data.name_only, N_("print only names (no SHA-1)")),
		OPT_BOOL(0, "tags", &data.tags_only, N_("only use tags to name the commits")),
		OPT_STRING_LIST(0, "refs", &data.ref_filters, N_("pattern"),
				   N_("only use refs matching <pattern>")),
		OPT_STRING_LIST(0, "exclude", &data.exclude_filters, N_("pattern"),
				   N_("ignore refs matching <pattern>")),
		OPT_GROUP(""),
		OPT_BOOL(0, "all", &all, N_("list all commits reachable from all refs")),
		OPT_BOOL(0, "stdin", &transform_stdin, N_("read from stdin")),
		OPT_BOOL(0, "undefined", &allow_undefined, N_("allow to print `undefined` names (default)")),
		OPT_BOOL(0, "always",     &always,
			   N_("show abbreviated commit object as fallback")),
		{
			/* A Hidden OPT_BOOL */
			OPTION_SET_INT, 0, "peel-tag", &peel_tag, NULL,
			N_("dereference tags in the input (internal use)"),
			PARSE_OPT_NOARG | PARSE_OPT_HIDDEN, NULL, 1,
		},
		OPT_END(),
	};

	init_commit_rev_name(&rev_names);
	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, opts, name_rev_usage, 0);
	if (all + transform_stdin + !!argc > 1) {
		error("Specify either a list, or --all, not both!");
		usage_with_options(name_rev_usage, opts);
	}
	if (all || transform_stdin)
		cutoff = 0;

	for (; argc; argc--, argv++) {
		struct object_id oid;
		struct object *object;
		struct commit *commit;

		if (get_oid(*argv, &oid)) {
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

		if (commit) {
			if (cutoff > commit->date)
				cutoff = commit->date;
		}

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

	if (cutoff) {
		/* check for undeflow */
		if (cutoff > TIME_MIN + CUTOFF_DATE_SLOP)
			cutoff = cutoff - CUTOFF_DATE_SLOP;
		else
			cutoff = TIME_MIN;
	}
	for_each_ref(name_ref, &data);
	name_tips();

	if (transform_stdin) {
		char buffer[2048];

		while (!feof(stdin)) {
			char *p = fgets(buffer, sizeof(buffer), stdin);
			if (!p)
				break;
			name_rev_line(p, &data);
		}
	} else if (all) {
		int i, max;

		max = get_max_object_index();
		for (i = 0; i < max; i++) {
			struct object *obj = get_indexed_object(i);
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

	UNLEAK(revs);
	return 0;
}
