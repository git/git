#include "stash.h"
#include "strbuf.h"

static int prepare_update_index_argv(struct argv_array *args,
	struct strbuf *buf)
{
	struct strbuf **bufs, **b;

	bufs = strbuf_split(buf, '\0');
	for (b = bufs; *b; b++)
		argv_array_pushf(args, "%s", (*b)->buf);
	argv_array_push(args, "--");
	strbuf_list_free(bufs);

	return 0;
}

int stash_non_patch(const char *tmp_indexfile, const char *i_tree,
	const char *prefix)
{
	int result;
	struct child_process read_tree = CHILD_PROCESS_INIT;
	struct child_process diff = CHILD_PROCESS_INIT;
	struct child_process update_index = CHILD_PROCESS_INIT;
	struct child_process write_tree = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;

	argv_array_push(&read_tree.args, "read-tree");
	argv_array_pushf(&read_tree.args, "--index-output=%s", tmp_indexfile);
	argv_array_pushl(&read_tree.args, "-m", i_tree, NULL);

	argv_array_pushl(&diff.args, "diff", "--name-only", "-z", "HEAD", "--",
		NULL);

	argv_array_pushl(&update_index.args, "update-index", "--add",
		"--remove", NULL);

	argv_array_push(&write_tree.args, "write-tree");

	read_tree.env =
		diff.env =
		update_index.env =
		write_tree.env = prefix;

	read_tree.use_shell =
		diff.use_shell =
		update_index.use_shell =
		write_tree.use_shell = 1;

	read_tree.git_cmd =
		diff.git_cmd =
		update_index.git_cmd =
		write_tree.git_cmd = 1;

	result = run_command(&read_tree) ||
		setenv("GIT_INDEX_FILE", tmp_indexfile, 1) ||
		capture_command(&diff, &buf, 0) ||
		prepare_update_index_argv(&update_index.args, &buf) ||
		run_command(&update_index) ||
		run_command(&write_tree) ||
		remove(tmp_indexfile);

	strbuf_release(&buf);
	return result;
}
