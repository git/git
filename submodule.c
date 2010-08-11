#include "cache.h"
#include "submodule.h"
#include "dir.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "run-command.h"
#include "diffcore.h"
#include "refs.h"

static int add_submodule_odb(const char *path)
{
	struct strbuf objects_directory = STRBUF_INIT;
	struct alternate_object_database *alt_odb;
	int ret = 0;
	const char *git_dir;

	strbuf_addf(&objects_directory, "%s/.git", path);
	git_dir = read_gitfile_gently(objects_directory.buf);
	if (git_dir) {
		strbuf_reset(&objects_directory);
		strbuf_addstr(&objects_directory, git_dir);
	}
	strbuf_addstr(&objects_directory, "/objects/");
	if (!is_directory(objects_directory.buf)) {
		ret = -1;
		goto done;
	}
	/* avoid adding it twice */
	for (alt_odb = alt_odb_list; alt_odb; alt_odb = alt_odb->next)
		if (alt_odb->name - alt_odb->base == objects_directory.len &&
				!strncmp(alt_odb->base, objects_directory.buf,
					objects_directory.len))
			goto done;

	alt_odb = xmalloc(objects_directory.len + 42 + sizeof(*alt_odb));
	alt_odb->next = alt_odb_list;
	strcpy(alt_odb->base, objects_directory.buf);
	alt_odb->name = alt_odb->base + objects_directory.len;
	alt_odb->name[2] = '/';
	alt_odb->name[40] = '\0';
	alt_odb->name[41] = '\0';
	alt_odb_list = alt_odb;
	prepare_alt_odb();
done:
	strbuf_release(&objects_directory);
	return ret;
}

void handle_ignore_submodules_arg(struct diff_options *diffopt,
				  const char *arg)
{
	if (!strcmp(arg, "all"))
		DIFF_OPT_SET(diffopt, IGNORE_SUBMODULES);
	else if (!strcmp(arg, "untracked"))
		DIFF_OPT_SET(diffopt, IGNORE_UNTRACKED_IN_SUBMODULES);
	else if (!strcmp(arg, "dirty"))
		DIFF_OPT_SET(diffopt, IGNORE_DIRTY_SUBMODULES);
	else
		die("bad --ignore-submodules argument: %s", arg);
}

void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
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

	if (dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
		fprintf(f, "Submodule %s contains untracked content\n", path);
	if (dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
		fprintf(f, "Submodule %s contains modified content\n", path);

	if (!hashcmp(one, two)) {
		strbuf_release(&sb);
		return;
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

unsigned is_submodule_modified(const char *path, int ignore_untracked)
{
	ssize_t len;
	struct child_process cp;
	const char *argv[] = {
		"status",
		"--porcelain",
		NULL,
		NULL,
	};
	struct strbuf buf = STRBUF_INIT;
	unsigned dirty_submodule = 0;
	const char *line, *next_line;
	const char *git_dir;

	strbuf_addf(&buf, "%s/.git", path);
	git_dir = read_gitfile_gently(buf.buf);
	if (!git_dir)
		git_dir = buf.buf;
	if (!is_directory(git_dir)) {
		strbuf_release(&buf);
		/* The submodule is not checked out, so it is not modified */
		return 0;

	}
	strbuf_reset(&buf);

	if (ignore_untracked)
		argv[2] = "-uno";

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = local_repo_env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp))
		die("Could not run git status --porcelain");

	len = strbuf_read(&buf, cp.out, 1024);
	line = buf.buf;
	while (len > 2) {
		if ((line[0] == '?') && (line[1] == '?')) {
			dirty_submodule |= DIRTY_SUBMODULE_UNTRACKED;
			if (dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
				break;
		} else {
			dirty_submodule |= DIRTY_SUBMODULE_MODIFIED;
			if (ignore_untracked ||
			    (dirty_submodule & DIRTY_SUBMODULE_UNTRACKED))
				break;
		}
		next_line = strchr(line, '\n');
		if (!next_line)
			break;
		next_line++;
		len -= (next_line - line);
		line = next_line;
	}
	close(cp.out);

	if (finish_command(&cp))
		die("git status --porcelain failed");

	strbuf_release(&buf);
	return dirty_submodule;
}

static int find_first_merges(struct object_array *result, const char *path,
		struct commit *a, struct commit *b)
{
	int i, j;
	struct object_array merges;
	struct commit *commit;
	int contains_another;

	char merged_revision[42];
	const char *rev_args[] = { "rev-list", "--merges", "--ancestry-path",
				   "--all", merged_revision, NULL };
	struct rev_info revs;
	struct setup_revision_opt rev_opts;

	memset(&merges, 0, sizeof(merges));
	memset(result, 0, sizeof(struct object_array));
	memset(&rev_opts, 0, sizeof(rev_opts));

	/* get all revisions that merge commit a */
	snprintf(merged_revision, sizeof(merged_revision), "^%s",
			sha1_to_hex(a->object.sha1));
	init_revisions(&revs, NULL);
	rev_opts.submodule = path;
	setup_revisions(sizeof(rev_args)/sizeof(char *)-1, rev_args, &revs, &rev_opts);

	/* save all revisions from the above list that contain b */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	while ((commit = get_revision(&revs)) != NULL) {
		struct object *o = &(commit->object);
		if (in_merge_bases(b, &commit, 1))
			add_object_array(o, NULL, &merges);
	}

	/* Now we've got all merges that contain a and b. Prune all
	 * merges that contain another found merge and save them in
	 * result.
	 */
	for (i = 0; i < merges.nr; i++) {
		struct commit *m1 = (struct commit *) merges.objects[i].item;

		contains_another = 0;
		for (j = 0; j < merges.nr; j++) {
			struct commit *m2 = (struct commit *) merges.objects[j].item;
			if (i != j && in_merge_bases(m2, &m1, 1)) {
				contains_another = 1;
				break;
			}
		}

		if (!contains_another)
			add_object_array(merges.objects[i].item,
					 merges.objects[i].name, result);
	}

	free(merges.objects);
	return result->nr;
}

static void print_commit(struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;
	struct pretty_print_context ctx = {0};
	ctx.date_mode = DATE_NORMAL;
	format_commit_message(commit, " %h: %m %s", &sb, &ctx);
	fprintf(stderr, "%s\n", sb.buf);
	strbuf_release(&sb);
}

#define MERGE_WARNING(path, msg) \
	warning("Failed to merge submodule %s (%s)", path, msg);

int merge_submodule(unsigned char result[20], const char *path,
		    const unsigned char base[20], const unsigned char a[20],
		    const unsigned char b[20])
{
	struct commit *commit_base, *commit_a, *commit_b;
	int parent_count;
	struct object_array merges;

	int i;

	/* store a in result in case we fail */
	hashcpy(result, a);

	/* we can not handle deletion conflicts */
	if (is_null_sha1(base))
		return 0;
	if (is_null_sha1(a))
		return 0;
	if (is_null_sha1(b))
		return 0;

	if (add_submodule_odb(path)) {
		MERGE_WARNING(path, "not checked out");
		return 0;
	}

	if (!(commit_base = lookup_commit_reference(base)) ||
	    !(commit_a = lookup_commit_reference(a)) ||
	    !(commit_b = lookup_commit_reference(b))) {
		MERGE_WARNING(path, "commits not present");
		return 0;
	}

	/* check whether both changes are forward */
	if (!in_merge_bases(commit_base, &commit_a, 1) ||
	    !in_merge_bases(commit_base, &commit_b, 1)) {
		MERGE_WARNING(path, "commits don't follow merge-base");
		return 0;
	}

	/* Case #1: a is contained in b or vice versa */
	if (in_merge_bases(commit_a, &commit_b, 1)) {
		hashcpy(result, b);
		return 1;
	}
	if (in_merge_bases(commit_b, &commit_a, 1)) {
		hashcpy(result, a);
		return 1;
	}

	/*
	 * Case #2: There are one or more merges that contain a and b in
	 * the submodule. If there is only one, then present it as a
	 * suggestion to the user, but leave it marked unmerged so the
	 * user needs to confirm the resolution.
	 */

	/* find commit which merges them */
	parent_count = find_first_merges(&merges, path, commit_a, commit_b);
	switch (parent_count) {
	case 0:
		MERGE_WARNING(path, "merge following commits not found");
		break;

	case 1:
		MERGE_WARNING(path, "not fast-forward");
		fprintf(stderr, "Found a possible merge resolution "
				"for the submodule:\n");
		print_commit((struct commit *) merges.objects[0].item);
		fprintf(stderr,
			"If this is correct simply add it to the index "
			"for example\n"
			"by using:\n\n"
			"  git update-index --cacheinfo 160000 %s \"%s\"\n\n"
			"which will accept this suggestion.\n",
			sha1_to_hex(merges.objects[0].item->sha1), path);
		break;

	default:
		MERGE_WARNING(path, "multiple merges found");
		for (i = 0; i < merges.nr; i++)
			print_commit((struct commit *) merges.objects[i].item);
	}

	free(merges.objects);
	return 0;
}
