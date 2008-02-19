#include "cache.h"
#include "commit.h"
#include "tag.h"

static int is_shallow = -1;

int register_shallow(const unsigned char *sha1)
{
	struct commit_graft *graft =
		xmalloc(sizeof(struct commit_graft));
	struct commit *commit = lookup_commit(sha1);

	hashcpy(graft->sha1, sha1);
	graft->nr_parent = -1;
	if (commit && commit->object.parsed)
		commit->parents = NULL;
	return register_commit_graft(graft, 0);
}

int is_repository_shallow(void)
{
	FILE *fp;
	char buf[1024];

	if (is_shallow >= 0)
		return is_shallow;

	fp = fopen(git_path("shallow"), "r");
	if (!fp) {
		is_shallow = 0;
		return is_shallow;
	}
	is_shallow = 1;

	while (fgets(buf, sizeof(buf), fp)) {
		unsigned char sha1[20];
		if (get_sha1_hex(buf, sha1))
			die("bad shallow line: %s", buf);
		register_shallow(sha1);
	}
	fclose(fp);
	return is_shallow;
}

struct commit_list *get_shallow_commits(struct object_array *heads, int depth,
		int shallow_flag, int not_shallow_flag)
{
	int i = 0, cur_depth = 0;
	struct commit_list *result = NULL;
	struct object_array stack = {0, 0, NULL};
	struct commit *commit = NULL;

	while (commit || i < heads->nr || stack.nr) {
		struct commit_list *p;
		if (!commit) {
			if (i < heads->nr) {
				commit = (struct commit *)
					deref_tag(heads->objects[i++].item, NULL, 0);
				if (!commit || commit->object.type != OBJ_COMMIT) {
					commit = NULL;
					continue;
				}
				if (!commit->util)
					commit->util = xmalloc(sizeof(int));
				*(int *)commit->util = 0;
				cur_depth = 0;
			} else {
				commit = (struct commit *)
					stack.objects[--stack.nr].item;
				cur_depth = *(int *)commit->util;
			}
		}
		if (parse_commit(commit))
			die("invalid commit");
		commit->object.flags |= not_shallow_flag;
		cur_depth++;
		for (p = commit->parents, commit = NULL; p; p = p->next) {
			if (!p->item->util) {
				int *pointer = xmalloc(sizeof(int));
				p->item->util = pointer;
				*pointer =  cur_depth;
			} else {
				int *pointer = p->item->util;
				if (cur_depth >= *pointer)
					continue;
				*pointer = cur_depth;
			}
			if (cur_depth < depth) {
				if (p->next)
					add_object_array(&p->item->object,
							NULL, &stack);
				else {
					commit = p->item;
					cur_depth = *(int *)commit->util;
				}
			} else {
				commit_list_insert(p->item, &result);
				p->item->object.flags |= shallow_flag;
			}
		}
	}

	return result;
}
