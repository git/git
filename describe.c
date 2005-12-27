#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"

#define SEEN (1u << 0)

static const char describe_usage[] = "git-describe [--all] <committish>*";

static int all = 0;	/* Default to annotated tags only */

static int names = 0, allocs = 0;
static struct commit_name {
	const struct commit *commit;
	char path[];
} **name_array = NULL;

static struct commit_name *match(struct commit *cmit)
{
	int i = names;
	struct commit_name **p = name_array;

	while (i-- > 0) {
		struct commit_name *n = *p++;
		if (n->commit == cmit)
			return n;
	}
	return NULL;
}

static void add_to_known_names(const char *path, const struct commit *commit)
{
	int idx;
	int len = strlen(path)+1;
	struct commit_name *name = xmalloc(sizeof(struct commit_name) + len);

	name->commit = commit;
	memcpy(name->path, path, len);
	idx = names;
	if (idx >= allocs) {
		allocs = (idx + 50) * 3 / 2;
		name_array = xrealloc(name_array, allocs*sizeof(*name_array));
	}
	name_array[idx] = name;
	names = ++idx;
}

static int get_name(const char *path, const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return 0;
	if (!all) {
		struct object *object;
		if (strncmp(path, "refs/tags/", 10))
			return 0;
		object = parse_object(sha1);
		if (object->type != tag_type)
			return 0;
	}
	add_to_known_names(all ? path : path + 10, commit);
	return 0;
}

static int compare_names(const void *_a, const void *_b)
{
	struct commit_name *a = *(struct commit_name **)_a;
	struct commit_name *b = *(struct commit_name **)_b;
	unsigned long a_date = a->commit->date;
	unsigned long b_date = b->commit->date;
	return (a_date > b_date) ? -1 : (a_date == b_date) ? 0 : 1;
}

static void describe(struct commit *cmit)
{
	struct commit_list *list;
	static int initialized = 0;
	struct commit_name *n;

	if (!initialized) {
		initialized = 1;
		for_each_ref(get_name);
		qsort(name_array, names, sizeof(*name_array), compare_names);
	}

	n = match(cmit);
	if (n) {
		printf("%s\n", n->path);
		return;
	}

	list = NULL;
	commit_list_insert(cmit, &list);
	while (list) {
		struct commit *c = pop_most_recent_commit(&list, SEEN);
		n = match(c);
		if (n) {
			printf("%s-g%.8s\n", n->path, sha1_to_hex(cmit->object.sha1));
			return;
		}
	}
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		unsigned char sha1[20];
		struct commit *cmit;

		if (!strcmp(arg, "--all")) {
			all = 1;
			continue;
		}
		if (get_sha1(arg, sha1) < 0)
			usage(describe_usage);
		cmit = lookup_commit_reference(sha1);
		if (!cmit)
			usage(describe_usage);
		describe(cmit);
	}
	return 0;
}
