#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "parse-options.h"
#include "sha1-lookup.h"

#define CUTOFF_DATE_SLOP 86400 /* one day */

typedef struct rev_name {
	const char *tip_name;
	int generation;
	int distance;
} rev_name;

static long cutoff = LONG_MAX;

/* How many generations are maximally preferred over _one_ merge traversal? */
#define MERGE_TRAVERSAL_WEIGHT 65535

static void name_rev(struct commit *commit,
		const char *tip_name, int generation, int distance,
		int deref)
{
	struct rev_name *name = (struct rev_name *)commit->util;
	struct commit_list *parents;
	int parent_number = 1;

	parse_commit(commit);

	if (commit->date < cutoff)
		return;

	if (deref) {
		tip_name = xstrfmt("%s^0", tip_name);

		if (generation)
			die("generation: %d, but deref?", generation);
	}

	if (name == NULL) {
		name = xmalloc(sizeof(rev_name));
		commit->util = name;
		goto copy_data;
	} else if (name->distance > distance) {
copy_data:
		name->tip_name = tip_name;
		name->generation = generation;
		name->distance = distance;
	} else
		return;

	for (parents = commit->parents;
			parents;
			parents = parents->next, parent_number++) {
		if (parent_number > 1) {
			int len = strlen(tip_name);
			char *new_name = xmalloc(len +
				1 + decimal_length(generation) +  /* ~<n> */
				1 + 2 +				  /* ^NN */
				1);

			if (len > 2 && !strcmp(tip_name + len - 2, "^0"))
				len -= 2;
			if (generation > 0)
				sprintf(new_name, "%.*s~%d^%d", len, tip_name,
						generation, parent_number);
			else
				sprintf(new_name, "%.*s^%d", len, tip_name,
						parent_number);

			name_rev(parents->item, new_name, 0,
				distance + MERGE_TRAVERSAL_WEIGHT, 0);
		} else {
			name_rev(parents->item, tip_name, generation + 1,
				distance + 1, 0);
		}
	}
}

static int subpath_matches(const char *path, const char *filter)
{
	const char *subpath = path;

	while (subpath) {
		if (!wildmatch(filter, subpath, 0, NULL))
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
	else if (starts_with(refname, "refs/heads/"))
		refname = refname + 11;
	else if (starts_with(refname, "refs/"))
		refname = refname + 5;
	return refname;
}

struct name_ref_data {
	int tags_only;
	int name_only;
	const char *ref_filter;
};

static struct tip_table {
	struct tip_table_entry {
		unsigned char sha1[20];
		const char *refname;
	} *table;
	int nr;
	int alloc;
	int sorted;
} tip_table;

static void add_to_tip_table(const unsigned char *sha1, const char *refname,
			     int shorten_unambiguous)
{
	refname = name_ref_abbrev(refname, shorten_unambiguous);

	ALLOC_GROW(tip_table.table, tip_table.nr + 1, tip_table.alloc);
	hashcpy(tip_table.table[tip_table.nr].sha1, sha1);
	tip_table.table[tip_table.nr].refname = xstrdup(refname);
	tip_table.nr++;
	tip_table.sorted = 0;
}

static int tipcmp(const void *a_, const void *b_)
{
	const struct tip_table_entry *a = a_, *b = b_;
	return hashcmp(a->sha1, b->sha1);
}

static int name_ref(const char *path, const unsigned char *sha1, int flags, void *cb_data)
{
	struct object *o = parse_object(sha1);
	struct name_ref_data *data = cb_data;
	int can_abbreviate_output = data->tags_only && data->name_only;
	int deref = 0;

	if (data->tags_only && !starts_with(path, "refs/tags/"))
		return 0;

	if (data->ref_filter) {
		switch (subpath_matches(path, data->ref_filter)) {
		case -1: /* did not match */
			return 0;
		case 0:  /* matched fully */
			break;
		default: /* matched subpath */
			can_abbreviate_output = 1;
			break;
		}
	}

	add_to_tip_table(sha1, path, can_abbreviate_output);

	while (o && o->type == OBJ_TAG) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o = parse_object(t->tagged->sha1);
		deref = 1;
	}
	if (o && o->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)o;

		path = name_ref_abbrev(path, can_abbreviate_output);
		name_rev(commit, xstrdup(path), 0, 0, deref);
	}
	return 0;
}

static const unsigned char *nth_tip_table_ent(size_t ix, void *table_)
{
	struct tip_table_entry *table = table_;
	return table[ix].sha1;
}

static const char *get_exact_ref_match(const struct object *o)
{
	int found;

	if (!tip_table.table || !tip_table.nr)
		return NULL;

	if (!tip_table.sorted) {
		qsort(tip_table.table, tip_table.nr, sizeof(*tip_table.table),
		      tipcmp);
		tip_table.sorted = 1;
	}

	found = sha1_pos(o->sha1, tip_table.table, tip_table.nr,
			 nth_tip_table_ent);
	if (0 <= found)
		return tip_table.table[found].refname;
	return NULL;
}

/* returns a static buffer */
static const char *get_rev_name(const struct object *o)
{
	static char buffer[1024];
	struct rev_name *n;
	struct commit *c;

	if (o->type != OBJ_COMMIT)
		return get_exact_ref_match(o);
	c = (struct commit *) o;
	n = c->util;
	if (!n)
		return NULL;

	if (!n->generation)
		return n->tip_name;
	else {
		int len = strlen(n->tip_name);
		if (len > 2 && !strcmp(n->tip_name + len - 2, "^0"))
			len -= 2;
		snprintf(buffer, sizeof(buffer), "%.*s~%d", len, n->tip_name,
				n->generation);

		return buffer;
	}
}

static void show_name(const struct object *obj,
		      const char *caller_name,
		      int always, int allow_undefined, int name_only)
{
	const char *name;
	const unsigned char *sha1 = obj->sha1;

	if (!name_only)
		printf("%s ", caller_name ? caller_name : sha1_to_hex(sha1));
	name = get_rev_name(obj);
	if (name)
		printf("%s\n", name);
	else if (allow_undefined)
		printf("undefined\n");
	else if (always)
		printf("%s\n", find_unique_abbrev(sha1, DEFAULT_ABBREV));
	else
		die("cannot describe '%s'", sha1_to_hex(sha1));
}

static char const * const name_rev_usage[] = {
	N_("git name-rev [options] <commit>..."),
	N_("git name-rev [options] --all"),
	N_("git name-rev [options] --stdin"),
	NULL
};

static void name_rev_line(char *p, struct name_ref_data *data)
{
	int forty = 0;
	char *p_start;
	for (p_start = p; *p; p++) {
#define ishex(x) (isdigit((x)) || ((x) >= 'a' && (x) <= 'f'))
		if (!ishex(*p))
			forty = 0;
		else if (++forty == 40 &&
			 !ishex(*(p+1))) {
			unsigned char sha1[40];
			const char *name = NULL;
			char c = *(p+1);
			int p_len = p - p_start + 1;

			forty = 0;

			*(p+1) = 0;
			if (!get_sha1(p - 39, sha1)) {
				struct object *o =
					lookup_object(sha1);
				if (o)
					name = get_rev_name(o);
			}
			*(p+1) = c;

			if (!name)
				continue;

			if (data->name_only)
				printf("%.*s%s", p_len - 40, p_start, name);
			else
				printf("%.*s (%s)", p_len, p_start, name);
			p_start = p + 1;
		}
	}

	/* flush */
	if (p_start != p)
		fwrite(p_start, p - p_start, 1, stdout);
}

int cmd_name_rev(int argc, const char **argv, const char *prefix)
{
	struct object_array revs = OBJECT_ARRAY_INIT;
	int all = 0, transform_stdin = 0, allow_undefined = 1, always = 0, peel_tag = 0;
	struct name_ref_data data = { 0, 0, NULL };
	struct option opts[] = {
		OPT_BOOL(0, "name-only", &data.name_only, N_("print only names (no SHA-1)")),
		OPT_BOOL(0, "tags", &data.tags_only, N_("only use tags to name the commits")),
		OPT_STRING(0, "refs", &data.ref_filter, N_("pattern"),
				   N_("only use refs matching <pattern>")),
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

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, opts, name_rev_usage, 0);
	if (all + transform_stdin + !!argc > 1) {
		error("Specify either a list, or --all, not both!");
		usage_with_options(name_rev_usage, opts);
	}
	if (all || transform_stdin)
		cutoff = 0;

	for (; argc; argc--, argv++) {
		unsigned char sha1[20];
		struct object *object;
		struct commit *commit;

		if (get_sha1(*argv, sha1)) {
			fprintf(stderr, "Could not get sha1 for %s. Skipping.\n",
					*argv);
			continue;
		}

		commit = NULL;
		object = parse_object(sha1);
		if (object) {
			struct object *peeled = deref_tag(object, *argv, 0);
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

	if (cutoff)
		cutoff = cutoff - CUTOFF_DATE_SLOP;
	for_each_ref(name_ref, &data);

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

	return 0;
}
