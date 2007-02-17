#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"

static const char name_rev_usage[] =
	"git-name-rev [--tags | --refs=<pattern>] ( --all | --stdin | committish [committish...] )\n";

typedef struct rev_name {
	const char *tip_name;
	int merge_traversals;
	int generation;
} rev_name;

static long cutoff = LONG_MAX;

static void name_rev(struct commit *commit,
		const char *tip_name, int merge_traversals, int generation,
		int deref)
{
	struct rev_name *name = (struct rev_name *)commit->util;
	struct commit_list *parents;
	int parent_number = 1;

	if (!commit->object.parsed)
		parse_commit(commit);

	if (commit->date < cutoff)
		return;

	if (deref) {
		char *new_name = xmalloc(strlen(tip_name)+3);
		strcpy(new_name, tip_name);
		strcat(new_name, "^0");
		tip_name = new_name;

		if (generation)
			die("generation: %d, but deref?", generation);
	}

	if (name == NULL) {
		name = xmalloc(sizeof(rev_name));
		commit->util = name;
		goto copy_data;
	} else if (name->merge_traversals > merge_traversals ||
			(name->merge_traversals == merge_traversals &&
			 name->generation > generation)) {
copy_data:
		name->tip_name = tip_name;
		name->merge_traversals = merge_traversals;
		name->generation = generation;
	} else
		return;

	for (parents = commit->parents;
			parents;
			parents = parents->next, parent_number++) {
		if (parent_number > 1) {
			char *new_name = xmalloc(strlen(tip_name)+8);

			if (generation > 0)
				sprintf(new_name, "%s~%d^%d", tip_name,
						generation, parent_number);
			else
				sprintf(new_name, "%s^%d", tip_name, parent_number);

			name_rev(parents->item, new_name,
				merge_traversals + 1 , 0, 0);
		} else {
			name_rev(parents->item, tip_name, merge_traversals,
				generation + 1, 0);
		}
	}
}

struct name_ref_data {
	int tags_only;
	const char *ref_filter;
};

static int name_ref(const char *path, const unsigned char *sha1, int flags, void *cb_data)
{
	struct object *o = parse_object(sha1);
	struct name_ref_data *data = cb_data;
	int deref = 0;

	if (data->tags_only && strncmp(path, "refs/tags/", 10))
		return 0;

	if (data->ref_filter && fnmatch(data->ref_filter, path, 0))
		return 0;

	while (o && o->type == OBJ_TAG) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o = parse_object(t->tagged->sha1);
		deref = 1;
	}
	if (o && o->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)o;

		if (!strncmp(path, "refs/heads/", 11))
			path = path + 11;
		else if (!strncmp(path, "refs/", 5))
			path = path + 5;

		name_rev(commit, xstrdup(path), 0, 0, deref);
	}
	return 0;
}

/* returns a static buffer */
static const char* get_rev_name(struct object *o)
{
	static char buffer[1024];
	struct rev_name *n;
	struct commit *c;

	if (o->type != OBJ_COMMIT)
		return "undefined";
	c = (struct commit *) o;
	n = c->util;
	if (!n)
		return "undefined";

	if (!n->generation)
		return n->tip_name;

	snprintf(buffer, sizeof(buffer), "%s~%d", n->tip_name, n->generation);

	return buffer;
}

int cmd_name_rev(int argc, const char **argv, const char *prefix)
{
	struct object_array revs = { 0, 0, NULL };
	int as_is = 0, all = 0, transform_stdin = 0;
	struct name_ref_data data = { 0, NULL };

	git_config(git_default_config);

	if (argc < 2)
		usage(name_rev_usage);

	for (--argc, ++argv; argc; --argc, ++argv) {
		unsigned char sha1[20];
		struct object *o;
		struct commit *commit;

		if (!as_is && (*argv)[0] == '-') {
			if (!strcmp(*argv, "--")) {
				as_is = 1;
				continue;
			} else if (!strcmp(*argv, "--tags")) {
				data.tags_only = 1;
				continue;
			} else  if (!strncmp(*argv, "--refs=", 7)) {
				data.ref_filter = *argv + 7;
				continue;
			} else if (!strcmp(*argv, "--all")) {
				if (argc > 1)
					die("Specify either a list, or --all, not both!");
				all = 1;
				cutoff = 0;
				continue;
			} else if (!strcmp(*argv, "--stdin")) {
				if (argc > 1)
					die("Specify either a list, or --stdin, not both!");
				transform_stdin = 1;
				cutoff = 0;
				continue;
			}
			usage(name_rev_usage);
		}

		if (get_sha1(*argv, sha1)) {
			fprintf(stderr, "Could not get sha1 for %s. Skipping.\n",
					*argv);
			continue;
		}

		o = deref_tag(parse_object(sha1), *argv, 0);
		if (!o || o->type != OBJ_COMMIT) {
			fprintf(stderr, "Could not get commit for %s. Skipping.\n",
					*argv);
			continue;
		}

		commit = (struct commit *)o;

		if (cutoff > commit->date)
			cutoff = commit->date;

		add_object_array((struct object *)commit, *argv, &revs);
	}

	for_each_ref(name_ref, &data);

	if (transform_stdin) {
		char buffer[2048];
		char *p, *p_start;

		while (!feof(stdin)) {
			int forty = 0;
			p = fgets(buffer, sizeof(buffer), stdin);
			if (!p)
				break;

			for (p_start = p; *p; p++) {
#define ishex(x) (isdigit((x)) || ((x) >= 'a' && (x) <= 'f'))
				if (!ishex(*p))
					forty = 0;
				else if (++forty == 40 &&
						!ishex(*(p+1))) {
					unsigned char sha1[40];
					const char *name = "undefined";
					char c = *(p+1);

					forty = 0;

					*(p+1) = 0;
					if (!get_sha1(p - 39, sha1)) {
						struct object *o =
							lookup_object(sha1);
						if (o)
							name = get_rev_name(o);
					}
					*(p+1) = c;

					if (!strcmp(name, "undefined"))
						continue;

					fwrite(p_start, p - p_start + 1, 1,
					       stdout);
					printf(" (%s)", name);
					p_start = p + 1;
				}
			}

			/* flush */
			if (p_start != p)
				fwrite(p_start, p - p_start, 1, stdout);
		}
	} else if (all) {
		int i, max;

		max = get_max_object_index();
		for (i = 0; i < max; i++) {
			struct object * obj = get_indexed_object(i);
			if (!obj)
				continue;
			printf("%s %s\n", sha1_to_hex(obj->sha1), get_rev_name(obj));
		}
	} else {
		int i;
		for (i = 0; i < revs.nr; i++)
			printf("%s %s\n",
				revs.objects[i].name,
				get_rev_name(revs.objects[i].item));
	}

	return 0;
}

