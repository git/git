#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "pkt-line.h"

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
			if (p->next)
				add_object_array(&p->item->object,
						NULL, &stack);
			else {
				commit = p->item;
				cur_depth = *(int *)commit->util;
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

struct write_shallow_data {
	struct strbuf *out;
	int use_pack_protocol;
	int count;
};

static int write_one_shallow(const struct commit_graft *graft, void *cb_data)
{
	struct write_shallow_data *data = cb_data;
	const char *hex = sha1_to_hex(graft->sha1);
	if (graft->nr_parent != -1)
		return 0;
	data->count++;
	if (data->use_pack_protocol)
		packet_buf_write(data->out, "shallow %s", hex);
	else {
		strbuf_addstr(data->out, hex);
		strbuf_addch(data->out, '\n');
	}
	return 0;
}

int write_shallow_commits(struct strbuf *out, int use_pack_protocol)
{
	struct write_shallow_data data;
	data.out = out;
	data.use_pack_protocol = use_pack_protocol;
	data.count = 0;
	for_each_commit_graft(write_one_shallow, &data);
	return data.count;
}

char *setup_temporary_shallow(void)
{
	struct strbuf sb = STRBUF_INIT;
	int fd;

	if (write_shallow_commits(&sb, 0)) {
		struct strbuf path = STRBUF_INIT;
		strbuf_addstr(&path, git_path("shallow_XXXXXX"));
		fd = xmkstemp(path.buf);
		if (write_in_full(fd, sb.buf, sb.len) != sb.len)
			die_errno("failed to write to %s",
				  path.buf);
		close(fd);
		strbuf_release(&sb);
		return strbuf_detach(&path, NULL);
	}
	/*
	 * is_repository_shallow() sees empty string as "no shallow
	 * file".
	 */
	return xstrdup("");
}

void setup_alternate_shallow(struct lock_file *shallow_lock,
			     const char **alternate_shallow_file)
{
	struct strbuf sb = STRBUF_INIT;
	int fd;

	check_shallow_file_for_update();
	fd = hold_lock_file_for_update(shallow_lock, git_path("shallow"),
				       LOCK_DIE_ON_ERROR);
	if (write_shallow_commits(&sb, 0)) {
		if (write_in_full(fd, sb.buf, sb.len) != sb.len)
			die_errno("failed to write to %s",
				  shallow_lock->filename);
		*alternate_shallow_file = shallow_lock->filename;
	} else
		/*
		 * is_repository_shallow() sees empty string as "no
		 * shallow file".
		 */
		*alternate_shallow_file = "";
	strbuf_release(&sb);
}
