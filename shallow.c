#include "cache.h"
#include "commit.h"
#include "tag.h"

static int is_shallow = -1;
static struct stat shallow_stat;
static char *alternate_shallow_file;

void set_alternate_shallow_file(const char *path)
{
	if (is_shallow != -1)
		die("BUG: is_repository_shallow must not be called before set_alternate_shallow_file");
	free(alternate_shallow_file);
	alternate_shallow_file = path ? xstrdup(path) : NULL;
}

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
	const char *path = alternate_shallow_file;

	if (is_shallow >= 0)
		return is_shallow;

	if (!path)
		path = git_path("shallow");
	/*
	 * fetch-pack sets '--shallow-file ""' as an indicator that no
	 * shallow file should be used. We could just open it and it
	 * will likely fail. But let's do an explicit check instead.
	 */
	if (!*path ||
	    stat(path, &shallow_stat) ||
	    (fp = fopen(path, "r")) == NULL) {
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
	struct object_array stack = OBJECT_ARRAY_INIT;
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
		cur_depth++;
		if (cur_depth >= depth) {
			commit_list_insert(commit, &result);
			commit->object.flags |= shallow_flag;
			commit = NULL;
			continue;
		}
		commit->object.flags |= not_shallow_flag;
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

void check_shallow_file_for_update(void)
{
	struct stat st;

	if (!is_shallow)
		return;
	else if (is_shallow == -1)
		die("BUG: shallow must be initialized by now");

	if (stat(git_path("shallow"), &st))
		die("shallow file was removed during fetch");
	else if (st.st_mtime != shallow_stat.st_mtime
#ifdef USE_NSEC
		 || ST_MTIME_NSEC(st) != ST_MTIME_NSEC(shallow_stat)
#endif
		   )
		die("shallow file was changed during fetch");
}
