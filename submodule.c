#include "cache.h"
#include "submodule.h"
#include "dir.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "run-command.h"

int add_submodule_odb(const char *path)
{
	struct strbuf objects_directory = STRBUF_INIT;
	struct alternate_object_database *alt_odb;

	strbuf_addf(&objects_directory, "%s/.git/objects/", path);
	if (!is_directory(objects_directory.buf))
		return -1;

	/* avoid adding it twice */
	for (alt_odb = alt_odb_list; alt_odb; alt_odb = alt_odb->next)
		if (alt_odb->name - alt_odb->base == objects_directory.len &&
				!strncmp(alt_odb->base, objects_directory.buf,
					objects_directory.len))
			return 0;

	alt_odb = xmalloc(objects_directory.len + 42 + sizeof(*alt_odb));
	alt_odb->next = alt_odb_list;
	strcpy(alt_odb->base, objects_directory.buf);
	alt_odb->name = alt_odb->base + objects_directory.len;
	alt_odb->name[2] = '/';
	alt_odb->name[40] = '\0';
	alt_odb->name[41] = '\0';
	alt_odb_list = alt_odb;
	prepare_alt_odb();
	return 0;
}

void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		const char *del, const char *add, const char *reset)
{
	struct rev_info rev;
	struct commit *commit, *left = left, *right = right;
	struct commit_list *merge_bases, *list;
	const char *message = NULL;
	struct strbuf sb = STRBUF_INIT;
	static const char *format = "  %m %s";
	int fast_forward = 0, fast_backward = 0;

	if (is_null_sha1(two))
		message = "(submodule deleted)";
	else if (add_submodule_odb(path))
		message = "(not checked out)";
	else if (is_null_sha1(one))
		message = "(new submodule)";
	else if (!(left = lookup_commit_reference(one)) ||
		 !(right = lookup_commit_reference(two)))
		message = "(commits not present)";

	if (!message) {
		init_revisions(&rev, NULL);
		setup_revisions(0, NULL, &rev, NULL);
		rev.left_right = 1;
		rev.first_parent_only = 1;
		left->object.flags |= SYMMETRIC_LEFT;
		add_pending_object(&rev, &left->object, path);
		add_pending_object(&rev, &right->object, path);
		merge_bases = get_merge_bases(left, right, 1);
		if (merge_bases) {
			if (merge_bases->item == left)
				fast_forward = 1;
			else if (merge_bases->item == right)
				fast_backward = 1;
		}
		for (list = merge_bases; list; list = list->next) {
			list->item->object.flags |= UNINTERESTING;
			add_pending_object(&rev, &list->item->object,
				sha1_to_hex(list->item->object.sha1));
		}
		if (prepare_revision_walk(&rev))
			message = "(revision walker failed)";
	}

	strbuf_addf(&sb, "Submodule %s %s..", path,
			find_unique_abbrev(one, DEFAULT_ABBREV));
	if (!fast_backward && !fast_forward)
		strbuf_addch(&sb, '.');
	strbuf_addf(&sb, "%s", find_unique_abbrev(two, DEFAULT_ABBREV));
	if (message)
		strbuf_addf(&sb, " %s\n", message);
	else
		strbuf_addf(&sb, "%s:\n", fast_backward ? " (rewind)" : "");
	fwrite(sb.buf, sb.len, 1, f);

	if (!message) {
		while ((commit = get_revision(&rev))) {
			struct pretty_print_context ctx = {0};
			ctx.date_mode = rev.date_mode;
			strbuf_setlen(&sb, 0);
			if (commit->object.flags & SYMMETRIC_LEFT) {
				if (del)
					strbuf_addstr(&sb, del);
			}
			else if (add)
				strbuf_addstr(&sb, add);
			format_commit_message(commit, format, &sb, &ctx);
			if (reset)
				strbuf_addstr(&sb, reset);
			strbuf_addch(&sb, '\n');
			fprintf(f, "%s", sb.buf);
		}
		clear_commit_marks(left, ~0);
		clear_commit_marks(right, ~0);
	}
	strbuf_release(&sb);
}

int is_submodule_modified(const char *path)
{
	int len;
	struct child_process cp;
	const char *argv[] = {
		"status",
		"--porcelain",
		NULL,
	};
	char *env[3];
	struct strbuf buf = STRBUF_INIT;

	strbuf_addf(&buf, "%s/.git/", path);
	if (!is_directory(buf.buf)) {
		strbuf_release(&buf);
		/* The submodule is not checked out, so it is not modified */
		return 0;

	}
	strbuf_reset(&buf);

	strbuf_addf(&buf, "GIT_WORK_TREE=%s", path);
	env[0] = strbuf_detach(&buf, NULL);
	strbuf_addf(&buf, "GIT_DIR=%s/.git", path);
	env[1] = strbuf_detach(&buf, NULL);
	env[2] = NULL;

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = (const char *const *)env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	if (start_command(&cp))
		die("Could not run 'git status --porcelain' in submodule %s", path);

	len = strbuf_read(&buf, cp.out, 1024);
	close(cp.out);

	if (finish_command(&cp))
		die("'git status --porcelain' failed in submodule %s", path);

	free(env[0]);
	free(env[1]);
	strbuf_release(&buf);
	return len != 0;
}
