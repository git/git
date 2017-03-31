#include "cache.h"
#include "submodule-config.h"
#include "submodule.h"
#include "dir.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "run-command.h"
#include "diffcore.h"
#include "refs.h"
#include "string-list.h"
#include "sha1-array.h"
#include "argv-array.h"
#include "blob.h"
#include "thread-utils.h"
#include "quote.h"
#include "worktree.h"

static int config_fetch_recurse_submodules = RECURSE_SUBMODULES_ON_DEMAND;
static int parallel_jobs = 1;
static struct string_list changed_submodule_paths = STRING_LIST_INIT_NODUP;
static int initialized_fetch_ref_tips;
static struct sha1_array ref_tips_before_fetch;
static struct sha1_array ref_tips_after_fetch;

/*
 * The following flag is set if the .gitmodules file is unmerged. We then
 * disable recursion for all submodules where .git/config doesn't have a
 * matching config entry because we can't guess what might be configured in
 * .gitmodules unless the user resolves the conflict. When a command line
 * option is given (which always overrides configuration) this flag will be
 * ignored.
 */
static int gitmodules_is_unmerged;

/*
 * This flag is set if the .gitmodules file had unstaged modifications on
 * startup. This must be checked before allowing modifications to the
 * .gitmodules file with the intention to stage them later, because when
 * continuing we would stage the modifications the user didn't stage herself
 * too. That might change in a future version when we learn to stage the
 * changes we do ourselves without staging any previous modifications.
 */
static int gitmodules_is_modified;

int is_staging_gitmodules_ok(void)
{
	return !gitmodules_is_modified;
}

/*
 * Try to update the "path" entry in the "submodule.<name>" section of the
 * .gitmodules file. Return 0 only if a .gitmodules file was found, a section
 * with the correct path=<oldpath> setting was found and we could update it.
 */
int update_path_in_gitmodules(const char *oldpath, const char *newpath)
{
	struct strbuf entry = STRBUF_INIT;
	const struct submodule *submodule;

	if (!file_exists(".gitmodules")) /* Do nothing without .gitmodules */
		return -1;

	if (gitmodules_is_unmerged)
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(null_sha1, oldpath);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .gitmodules where path=%s"), oldpath);
		return -1;
	}
	strbuf_addstr(&entry, "submodule.");
	strbuf_addstr(&entry, submodule->name);
	strbuf_addstr(&entry, ".path");
	if (git_config_set_in_file_gently(".gitmodules", entry.buf, newpath) < 0) {
		/* Maybe the user already did that, don't error out here */
		warning(_("Could not update .gitmodules entry %s"), entry.buf);
		strbuf_release(&entry);
		return -1;
	}
	strbuf_release(&entry);
	return 0;
}

/*
 * Try to remove the "submodule.<name>" section from .gitmodules where the given
 * path is configured. Return 0 only if a .gitmodules file was found, a section
 * with the correct path=<path> setting was found and we could remove it.
 */
int remove_path_from_gitmodules(const char *path)
{
	struct strbuf sect = STRBUF_INIT;
	const struct submodule *submodule;

	if (!file_exists(".gitmodules")) /* Do nothing without .gitmodules */
		return -1;

	if (gitmodules_is_unmerged)
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(null_sha1, path);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .gitmodules where path=%s"), path);
		return -1;
	}
	strbuf_addstr(&sect, "submodule.");
	strbuf_addstr(&sect, submodule->name);
	if (git_config_rename_section_in_file(".gitmodules", sect.buf, NULL) < 0) {
		/* Maybe the user already did that, don't error out here */
		warning(_("Could not remove .gitmodules entry for %s"), path);
		strbuf_release(&sect);
		return -1;
	}
	strbuf_release(&sect);
	return 0;
}

void stage_updated_gitmodules(void)
{
	if (add_file_to_cache(".gitmodules", 0))
		die(_("staging updated .gitmodules failed"));
}

static int add_submodule_odb(const char *path)
{
	struct strbuf objects_directory = STRBUF_INIT;
	int ret = 0;

	ret = strbuf_git_path_submodule(&objects_directory, path, "objects/");
	if (ret)
		goto done;
	if (!is_directory(objects_directory.buf)) {
		ret = -1;
		goto done;
	}
	add_to_alternates_memory(objects_directory.buf);
done:
	strbuf_release(&objects_directory);
	return ret;
}

void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
					     const char *path)
{
	const struct submodule *submodule = submodule_from_path(null_sha1, path);
	if (submodule) {
		if (submodule->ignore)
			handle_ignore_submodules_arg(diffopt, submodule->ignore);
		else if (gitmodules_is_unmerged)
			DIFF_OPT_SET(diffopt, IGNORE_SUBMODULES);
	}
}

int submodule_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "submodule.fetchjobs")) {
		parallel_jobs = git_config_int(var, value);
		if (parallel_jobs < 0)
			die(_("negative values not allowed for submodule.fetchJobs"));
		return 0;
	} else if (starts_with(var, "submodule."))
		return parse_submodule_config_option(var, value);
	else if (!strcmp(var, "fetch.recursesubmodules")) {
		config_fetch_recurse_submodules = parse_fetch_recurse_submodules_arg(var, value);
		return 0;
	}
	return 0;
}

void gitmodules_config(void)
{
	const char *work_tree = get_git_work_tree();
	if (work_tree) {
		struct strbuf gitmodules_path = STRBUF_INIT;
		int pos;
		strbuf_addstr(&gitmodules_path, work_tree);
		strbuf_addstr(&gitmodules_path, "/.gitmodules");
		if (read_cache() < 0)
			die("index file corrupt");
		pos = cache_name_pos(".gitmodules", 11);
		if (pos < 0) { /* .gitmodules not found or isn't merged */
			pos = -1 - pos;
			if (active_nr > pos) {  /* there is a .gitmodules */
				const struct cache_entry *ce = active_cache[pos];
				if (ce_namelen(ce) == 11 &&
				    !memcmp(ce->name, ".gitmodules", 11))
					gitmodules_is_unmerged = 1;
			}
		} else if (pos < active_nr) {
			struct stat st;
			if (lstat(".gitmodules", &st) == 0 &&
			    ce_match_stat(active_cache[pos], &st, 0) & DATA_CHANGED)
				gitmodules_is_modified = 1;
		}

		if (!gitmodules_is_unmerged)
			git_config_from_file(submodule_config, gitmodules_path.buf, NULL);
		strbuf_release(&gitmodules_path);
	}
}

void gitmodules_config_sha1(const unsigned char *commit_sha1)
{
	struct strbuf rev = STRBUF_INIT;
	unsigned char sha1[20];

	if (gitmodule_sha1_from_commit(commit_sha1, sha1, &rev)) {
		git_config_from_blob_sha1(submodule_config, rev.buf,
					  sha1, NULL);
	}
	strbuf_release(&rev);
}

/*
 * Determine if a submodule has been initialized at a given 'path'
 */
int is_submodule_initialized(const char *path)
{
	int ret = 0;
	const struct submodule *module = NULL;

	module = submodule_from_path(null_sha1, path);

	if (module) {
		char *key = xstrfmt("submodule.%s.url", module->name);
		char *value = NULL;

		ret = !git_config_get_string(key, &value);

		free(value);
		free(key);
	}

	return ret;
}

/*
 * Determine if a submodule has been populated at a given 'path'
 */
int is_submodule_populated(const char *path)
{
	int ret = 0;
	char *gitdir = xstrfmt("%s/.git", path);

	if (resolve_gitdir(gitdir))
		ret = 1;

	free(gitdir);
	return ret;
}

int parse_submodule_update_strategy(const char *value,
		struct submodule_update_strategy *dst)
{
	free((void*)dst->command);
	dst->command = NULL;
	if (!strcmp(value, "none"))
		dst->type = SM_UPDATE_NONE;
	else if (!strcmp(value, "checkout"))
		dst->type = SM_UPDATE_CHECKOUT;
	else if (!strcmp(value, "rebase"))
		dst->type = SM_UPDATE_REBASE;
	else if (!strcmp(value, "merge"))
		dst->type = SM_UPDATE_MERGE;
	else if (skip_prefix(value, "!", &value)) {
		dst->type = SM_UPDATE_COMMAND;
		dst->command = xstrdup(value);
	} else
		return -1;
	return 0;
}

const char *submodule_strategy_to_string(const struct submodule_update_strategy *s)
{
	struct strbuf sb = STRBUF_INIT;
	switch (s->type) {
	case SM_UPDATE_CHECKOUT:
		return "checkout";
	case SM_UPDATE_MERGE:
		return "merge";
	case SM_UPDATE_REBASE:
		return "rebase";
	case SM_UPDATE_NONE:
		return "none";
	case SM_UPDATE_UNSPECIFIED:
		return NULL;
	case SM_UPDATE_COMMAND:
		strbuf_addf(&sb, "!%s", s->command);
		return strbuf_detach(&sb, NULL);
	}
	return NULL;
}

void handle_ignore_submodules_arg(struct diff_options *diffopt,
				  const char *arg)
{
	DIFF_OPT_CLR(diffopt, IGNORE_SUBMODULES);
	DIFF_OPT_CLR(diffopt, IGNORE_UNTRACKED_IN_SUBMODULES);
	DIFF_OPT_CLR(diffopt, IGNORE_DIRTY_SUBMODULES);

	if (!strcmp(arg, "all"))
		DIFF_OPT_SET(diffopt, IGNORE_SUBMODULES);
	else if (!strcmp(arg, "untracked"))
		DIFF_OPT_SET(diffopt, IGNORE_UNTRACKED_IN_SUBMODULES);
	else if (!strcmp(arg, "dirty"))
		DIFF_OPT_SET(diffopt, IGNORE_DIRTY_SUBMODULES);
	else if (strcmp(arg, "none"))
		die("bad --ignore-submodules argument: %s", arg);
}

static int prepare_submodule_summary(struct rev_info *rev, const char *path,
		struct commit *left, struct commit *right,
		struct commit_list *merge_bases)
{
	struct commit_list *list;

	init_revisions(rev, NULL);
	setup_revisions(0, NULL, rev, NULL);
	rev->left_right = 1;
	rev->first_parent_only = 1;
	left->object.flags |= SYMMETRIC_LEFT;
	add_pending_object(rev, &left->object, path);
	add_pending_object(rev, &right->object, path);
	for (list = merge_bases; list; list = list->next) {
		list->item->object.flags |= UNINTERESTING;
		add_pending_object(rev, &list->item->object,
			oid_to_hex(&list->item->object.oid));
	}
	return prepare_revision_walk(rev);
}

static void print_submodule_summary(struct rev_info *rev, FILE *f,
		const char *line_prefix,
		const char *del, const char *add, const char *reset)
{
	static const char format[] = "  %m %s";
	struct strbuf sb = STRBUF_INIT;
	struct commit *commit;

	while ((commit = get_revision(rev))) {
		struct pretty_print_context ctx = {0};
		ctx.date_mode = rev->date_mode;
		ctx.output_encoding = get_log_output_encoding();
		strbuf_setlen(&sb, 0);
		strbuf_addstr(&sb, line_prefix);
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
	strbuf_release(&sb);
}

/* Helper function to display the submodule header line prior to the full
 * summary output. If it can locate the submodule objects directory it will
 * attempt to lookup both the left and right commits and put them into the
 * left and right pointers.
 */
static void show_submodule_header(FILE *f, const char *path,
		const char *line_prefix,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule, const char *meta,
		const char *reset,
		struct commit **left, struct commit **right,
		struct commit_list **merge_bases)
{
	const char *message = NULL;
	struct strbuf sb = STRBUF_INIT;
	int fast_forward = 0, fast_backward = 0;

	if (dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
		fprintf(f, "%sSubmodule %s contains untracked content\n",
			line_prefix, path);
	if (dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
		fprintf(f, "%sSubmodule %s contains modified content\n",
			line_prefix, path);

	if (is_null_oid(one))
		message = "(new submodule)";
	else if (is_null_oid(two))
		message = "(submodule deleted)";

	if (add_submodule_odb(path)) {
		if (!message)
			message = "(not initialized)";
		goto output_header;
	}

	/*
	 * Attempt to lookup the commit references, and determine if this is
	 * a fast forward or fast backwards update.
	 */
	*left = lookup_commit_reference(one->hash);
	*right = lookup_commit_reference(two->hash);

	/*
	 * Warn about missing commits in the submodule project, but only if
	 * they aren't null.
	 */
	if ((!is_null_oid(one) && !*left) ||
	     (!is_null_oid(two) && !*right))
		message = "(commits not present)";

	*merge_bases = get_merge_bases(*left, *right);
	if (*merge_bases) {
		if ((*merge_bases)->item == *left)
			fast_forward = 1;
		else if ((*merge_bases)->item == *right)
			fast_backward = 1;
	}

	if (!oidcmp(one, two)) {
		strbuf_release(&sb);
		return;
	}

output_header:
	strbuf_addf(&sb, "%s%sSubmodule %s ", line_prefix, meta, path);
	strbuf_add_unique_abbrev(&sb, one->hash, DEFAULT_ABBREV);
	strbuf_addstr(&sb, (fast_backward || fast_forward) ? ".." : "...");
	strbuf_add_unique_abbrev(&sb, two->hash, DEFAULT_ABBREV);
	if (message)
		strbuf_addf(&sb, " %s%s\n", message, reset);
	else
		strbuf_addf(&sb, "%s:%s\n", fast_backward ? " (rewind)" : "", reset);
	fwrite(sb.buf, sb.len, 1, f);

	strbuf_release(&sb);
}

void show_submodule_summary(FILE *f, const char *path,
		const char *line_prefix,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule, const char *meta,
		const char *del, const char *add, const char *reset)
{
	struct rev_info rev;
	struct commit *left = NULL, *right = NULL;
	struct commit_list *merge_bases = NULL;

	show_submodule_header(f, path, line_prefix, one, two, dirty_submodule,
			      meta, reset, &left, &right, &merge_bases);

	/*
	 * If we don't have both a left and a right pointer, there is no
	 * reason to try and display a summary. The header line should contain
	 * all the information the user needs.
	 */
	if (!left || !right)
		goto out;

	/* Treat revision walker failure the same as missing commits */
	if (prepare_submodule_summary(&rev, path, left, right, merge_bases)) {
		fprintf(f, "%s(revision walker failed)\n", line_prefix);
		goto out;
	}

	print_submodule_summary(&rev, f, line_prefix, del, add, reset);

out:
	if (merge_bases)
		free_commit_list(merge_bases);
	clear_commit_marks(left, ~0);
	clear_commit_marks(right, ~0);
}

void show_submodule_inline_diff(FILE *f, const char *path,
		const char *line_prefix,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule, const char *meta,
		const char *del, const char *add, const char *reset,
		const struct diff_options *o)
{
	const struct object_id *old = &empty_tree_oid, *new = &empty_tree_oid;
	struct commit *left = NULL, *right = NULL;
	struct commit_list *merge_bases = NULL;
	struct strbuf submodule_dir = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;

	show_submodule_header(f, path, line_prefix, one, two, dirty_submodule,
			      meta, reset, &left, &right, &merge_bases);

	/* We need a valid left and right commit to display a difference */
	if (!(left || is_null_oid(one)) ||
	    !(right || is_null_oid(two)))
		goto done;

	if (left)
		old = one;
	if (right)
		new = two;

	fflush(f);
	cp.git_cmd = 1;
	cp.dir = path;
	cp.out = dup(fileno(f));
	cp.no_stdin = 1;

	/* TODO: other options may need to be passed here. */
	argv_array_push(&cp.args, "diff");
	argv_array_pushf(&cp.args, "--line-prefix=%s", line_prefix);
	if (DIFF_OPT_TST(o, REVERSE_DIFF)) {
		argv_array_pushf(&cp.args, "--src-prefix=%s%s/",
				 o->b_prefix, path);
		argv_array_pushf(&cp.args, "--dst-prefix=%s%s/",
				 o->a_prefix, path);
	} else {
		argv_array_pushf(&cp.args, "--src-prefix=%s%s/",
				 o->a_prefix, path);
		argv_array_pushf(&cp.args, "--dst-prefix=%s%s/",
				 o->b_prefix, path);
	}
	argv_array_push(&cp.args, oid_to_hex(old));
	/*
	 * If the submodule has modified content, we will diff against the
	 * work tree, under the assumption that the user has asked for the
	 * diff format and wishes to actually see all differences even if they
	 * haven't yet been committed to the submodule yet.
	 */
	if (!(dirty_submodule & DIRTY_SUBMODULE_MODIFIED))
		argv_array_push(&cp.args, oid_to_hex(new));

	if (run_command(&cp))
		fprintf(f, "(diff failed)\n");

done:
	strbuf_release(&submodule_dir);
	if (merge_bases)
		free_commit_list(merge_bases);
	if (left)
		clear_commit_marks(left, ~0);
	if (right)
		clear_commit_marks(right, ~0);
}

void set_config_fetch_recurse_submodules(int value)
{
	config_fetch_recurse_submodules = value;
}

static int has_remote(const char *refname, const struct object_id *oid,
		      int flags, void *cb_data)
{
	return 1;
}

static int append_sha1_to_argv(const unsigned char sha1[20], void *data)
{
	struct argv_array *argv = data;
	argv_array_push(argv, sha1_to_hex(sha1));
	return 0;
}

static int check_has_commit(const unsigned char sha1[20], void *data)
{
	int *has_commit = data;

	if (!lookup_commit_reference(sha1))
		*has_commit = 0;

	return 0;
}

static int submodule_has_commits(const char *path, struct sha1_array *commits)
{
	int has_commit = 1;

	if (add_submodule_odb(path))
		return 0;

	sha1_array_for_each_unique(commits, check_has_commit, &has_commit);
	return has_commit;
}

static int submodule_needs_pushing(const char *path, struct sha1_array *commits)
{
	if (!submodule_has_commits(path, commits))
		/*
		 * NOTE: We do consider it safe to return "no" here. The
		 * correct answer would be "We do not know" instead of
		 * "No push needed", but it is quite hard to change
		 * the submodule pointer without having the submodule
		 * around. If a user did however change the submodules
		 * without having the submodule around, this indicates
		 * an expert who knows what they are doing or a
		 * maintainer integrating work from other people. In
		 * both cases it should be safe to skip this check.
		 */
		return 0;

	if (for_each_remote_ref_submodule(path, has_remote, NULL) > 0) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf buf = STRBUF_INIT;
		int needs_pushing = 0;

		argv_array_push(&cp.args, "rev-list");
		sha1_array_for_each_unique(commits, append_sha1_to_argv, &cp.args);
		argv_array_pushl(&cp.args, "--not", "--remotes", "-n", "1" , NULL);

		prepare_submodule_repo_env(&cp.env_array);
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.out = -1;
		cp.dir = path;
		if (start_command(&cp))
			die("Could not run 'git rev-list <commits> --not --remotes -n 1' command in submodule %s",
					path);
		if (strbuf_read(&buf, cp.out, 41))
			needs_pushing = 1;
		finish_command(&cp);
		close(cp.out);
		strbuf_release(&buf);
		return needs_pushing;
	}

	return 0;
}

static struct sha1_array *submodule_commits(struct string_list *submodules,
					    const char *path)
{
	struct string_list_item *item;

	item = string_list_insert(submodules, path);
	if (item->util)
		return (struct sha1_array *) item->util;

	/* NEEDSWORK: should we have sha1_array_init()? */
	item->util = xcalloc(1, sizeof(struct sha1_array));
	return (struct sha1_array *) item->util;
}

static void collect_submodules_from_diff(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	int i;
	struct string_list *submodules = data;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct sha1_array *commits;
		if (!S_ISGITLINK(p->two->mode))
			continue;
		commits = submodule_commits(submodules, p->two->path);
		sha1_array_append(commits, &p->two->oid);
	}
}

static void find_unpushed_submodule_commits(struct commit *commit,
		struct string_list *needs_pushing)
{
	struct rev_info rev;

	init_revisions(&rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = collect_submodules_from_diff;
	rev.diffopt.format_callback_data = needs_pushing;
	diff_tree_combined_merge(commit, 1, &rev);
}

static void free_submodules_sha1s(struct string_list *submodules)
{
	struct string_list_item *item;
	for_each_string_list_item(item, submodules)
		sha1_array_clear((struct sha1_array *) item->util);
	string_list_clear(submodules, 1);
}

int find_unpushed_submodules(struct sha1_array *commits,
		const char *remotes_name, struct string_list *needs_pushing)
{
	struct rev_info rev;
	struct commit *commit;
	struct string_list submodules = STRING_LIST_INIT_DUP;
	struct string_list_item *submodule;
	struct argv_array argv = ARGV_ARRAY_INIT;

	init_revisions(&rev, NULL);

	/* argv.argv[0] will be ignored by setup_revisions */
	argv_array_push(&argv, "find_unpushed_submodules");
	sha1_array_for_each_unique(commits, append_sha1_to_argv, &argv);
	argv_array_push(&argv, "--not");
	argv_array_pushf(&argv, "--remotes=%s", remotes_name);

	setup_revisions(argv.argc, argv.argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&rev)) != NULL)
		find_unpushed_submodule_commits(commit, &submodules);

	reset_revision_walk();
	argv_array_clear(&argv);

	for_each_string_list_item(submodule, &submodules) {
		struct sha1_array *commits = (struct sha1_array *) submodule->util;

		if (submodule_needs_pushing(submodule->string, commits))
			string_list_insert(needs_pushing, submodule->string);
	}
	free_submodules_sha1s(&submodules);

	return needs_pushing->nr;
}

static int push_submodule(const char *path, int dry_run)
{
	if (add_submodule_odb(path))
		return 1;

	if (for_each_remote_ref_submodule(path, has_remote, NULL) > 0) {
		struct child_process cp = CHILD_PROCESS_INIT;
		argv_array_push(&cp.args, "push");
		if (dry_run)
			argv_array_push(&cp.args, "--dry-run");

		prepare_submodule_repo_env(&cp.env_array);
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.dir = path;
		if (run_command(&cp))
			return 0;
		close(cp.out);
	}

	return 1;
}

int push_unpushed_submodules(struct sha1_array *commits,
			     const char *remotes_name,
			     int dry_run)
{
	int i, ret = 1;
	struct string_list needs_pushing = STRING_LIST_INIT_DUP;

	if (!find_unpushed_submodules(commits, remotes_name, &needs_pushing))
		return 1;

	for (i = 0; i < needs_pushing.nr; i++) {
		const char *path = needs_pushing.items[i].string;
		fprintf(stderr, "Pushing submodule '%s'\n", path);
		if (!push_submodule(path, dry_run)) {
			fprintf(stderr, "Unable to push submodule '%s'\n", path);
			ret = 0;
		}
	}

	string_list_clear(&needs_pushing, 0);

	return ret;
}

static int is_submodule_commit_present(const char *path, unsigned char sha1[20])
{
	int is_present = 0;
	if (!add_submodule_odb(path) && lookup_commit_reference(sha1)) {
		/* Even if the submodule is checked out and the commit is
		 * present, make sure it is reachable from a ref. */
		struct child_process cp = CHILD_PROCESS_INIT;
		const char *argv[] = {"rev-list", "-n", "1", NULL, "--not", "--all", NULL};
		struct strbuf buf = STRBUF_INIT;

		argv[3] = sha1_to_hex(sha1);
		cp.argv = argv;
		prepare_submodule_repo_env(&cp.env_array);
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.dir = path;
		if (!capture_command(&cp, &buf, 1024) && !buf.len)
			is_present = 1;

		strbuf_release(&buf);
	}
	return is_present;
}

static void submodule_collect_changed_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	int i;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!S_ISGITLINK(p->two->mode))
			continue;

		if (S_ISGITLINK(p->one->mode)) {
			/* NEEDSWORK: We should honor the name configured in
			 * the .gitmodules file of the commit we are examining
			 * here to be able to correctly follow submodules
			 * being moved around. */
			struct string_list_item *path;
			path = unsorted_string_list_lookup(&changed_submodule_paths, p->two->path);
			if (!path && !is_submodule_commit_present(p->two->path, p->two->oid.hash))
				string_list_append(&changed_submodule_paths, xstrdup(p->two->path));
		} else {
			/* Submodule is new or was moved here */
			/* NEEDSWORK: When the .git directories of submodules
			 * live inside the superprojects .git directory some
			 * day we should fetch new submodules directly into
			 * that location too when config or options request
			 * that so they can be checked out from there. */
			continue;
		}
	}
}

static int add_sha1_to_array(const char *ref, const struct object_id *oid,
			     int flags, void *data)
{
	sha1_array_append(data, oid);
	return 0;
}

void check_for_new_submodule_commits(struct object_id *oid)
{
	if (!initialized_fetch_ref_tips) {
		for_each_ref(add_sha1_to_array, &ref_tips_before_fetch);
		initialized_fetch_ref_tips = 1;
	}

	sha1_array_append(&ref_tips_after_fetch, oid);
}

static int add_sha1_to_argv(const unsigned char sha1[20], void *data)
{
	argv_array_push(data, sha1_to_hex(sha1));
	return 0;
}

static void calculate_changed_submodule_paths(void)
{
	struct rev_info rev;
	struct commit *commit;
	struct argv_array argv = ARGV_ARRAY_INIT;

	/* No need to check if there are no submodules configured */
	if (!submodule_from_path(NULL, NULL))
		return;

	init_revisions(&rev, NULL);
	argv_array_push(&argv, "--"); /* argv[0] program name */
	sha1_array_for_each_unique(&ref_tips_after_fetch,
				   add_sha1_to_argv, &argv);
	argv_array_push(&argv, "--not");
	sha1_array_for_each_unique(&ref_tips_before_fetch,
				   add_sha1_to_argv, &argv);
	setup_revisions(argv.argc, argv.argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	/*
	 * Collect all submodules (whether checked out or not) for which new
	 * commits have been recorded upstream in "changed_submodule_paths".
	 */
	while ((commit = get_revision(&rev))) {
		struct commit_list *parent = commit->parents;
		while (parent) {
			struct diff_options diff_opts;
			diff_setup(&diff_opts);
			DIFF_OPT_SET(&diff_opts, RECURSIVE);
			diff_opts.output_format |= DIFF_FORMAT_CALLBACK;
			diff_opts.format_callback = submodule_collect_changed_cb;
			diff_setup_done(&diff_opts);
			diff_tree_sha1(parent->item->object.oid.hash, commit->object.oid.hash, "", &diff_opts);
			diffcore_std(&diff_opts);
			diff_flush(&diff_opts);
			parent = parent->next;
		}
	}

	argv_array_clear(&argv);
	sha1_array_clear(&ref_tips_before_fetch);
	sha1_array_clear(&ref_tips_after_fetch);
	initialized_fetch_ref_tips = 0;
}

struct submodule_parallel_fetch {
	int count;
	struct argv_array args;
	const char *work_tree;
	const char *prefix;
	int command_line_option;
	int quiet;
	int result;
};
#define SPF_INIT {0, ARGV_ARRAY_INIT, NULL, NULL, 0, 0, 0}

static int get_next_submodule(struct child_process *cp,
			      struct strbuf *err, void *data, void **task_cb)
{
	int ret = 0;
	struct submodule_parallel_fetch *spf = data;

	for (; spf->count < active_nr; spf->count++) {
		struct strbuf submodule_path = STRBUF_INIT;
		struct strbuf submodule_git_dir = STRBUF_INIT;
		struct strbuf submodule_prefix = STRBUF_INIT;
		const struct cache_entry *ce = active_cache[spf->count];
		const char *git_dir, *default_argv;
		const struct submodule *submodule;

		if (!S_ISGITLINK(ce->ce_mode))
			continue;

		submodule = submodule_from_path(null_sha1, ce->name);
		if (!submodule)
			submodule = submodule_from_name(null_sha1, ce->name);

		default_argv = "yes";
		if (spf->command_line_option == RECURSE_SUBMODULES_DEFAULT) {
			if (submodule &&
			    submodule->fetch_recurse !=
						RECURSE_SUBMODULES_NONE) {
				if (submodule->fetch_recurse ==
						RECURSE_SUBMODULES_OFF)
					continue;
				if (submodule->fetch_recurse ==
						RECURSE_SUBMODULES_ON_DEMAND) {
					if (!unsorted_string_list_lookup(&changed_submodule_paths, ce->name))
						continue;
					default_argv = "on-demand";
				}
			} else {
				if ((config_fetch_recurse_submodules == RECURSE_SUBMODULES_OFF) ||
				    gitmodules_is_unmerged)
					continue;
				if (config_fetch_recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND) {
					if (!unsorted_string_list_lookup(&changed_submodule_paths, ce->name))
						continue;
					default_argv = "on-demand";
				}
			}
		} else if (spf->command_line_option == RECURSE_SUBMODULES_ON_DEMAND) {
			if (!unsorted_string_list_lookup(&changed_submodule_paths, ce->name))
				continue;
			default_argv = "on-demand";
		}

		strbuf_addf(&submodule_path, "%s/%s", spf->work_tree, ce->name);
		strbuf_addf(&submodule_git_dir, "%s/.git", submodule_path.buf);
		strbuf_addf(&submodule_prefix, "%s%s/", spf->prefix, ce->name);
		git_dir = read_gitfile(submodule_git_dir.buf);
		if (!git_dir)
			git_dir = submodule_git_dir.buf;
		if (is_directory(git_dir)) {
			child_process_init(cp);
			cp->dir = strbuf_detach(&submodule_path, NULL);
			prepare_submodule_repo_env(&cp->env_array);
			cp->git_cmd = 1;
			if (!spf->quiet)
				strbuf_addf(err, "Fetching submodule %s%s\n",
					    spf->prefix, ce->name);
			argv_array_init(&cp->args);
			argv_array_pushv(&cp->args, spf->args.argv);
			argv_array_push(&cp->args, default_argv);
			argv_array_push(&cp->args, "--submodule-prefix");
			argv_array_push(&cp->args, submodule_prefix.buf);
			ret = 1;
		}
		strbuf_release(&submodule_path);
		strbuf_release(&submodule_git_dir);
		strbuf_release(&submodule_prefix);
		if (ret) {
			spf->count++;
			return 1;
		}
	}
	return 0;
}

static int fetch_start_failure(struct strbuf *err,
			       void *cb, void *task_cb)
{
	struct submodule_parallel_fetch *spf = cb;

	spf->result = 1;

	return 0;
}

static int fetch_finish(int retvalue, struct strbuf *err,
			void *cb, void *task_cb)
{
	struct submodule_parallel_fetch *spf = cb;

	if (retvalue)
		spf->result = 1;

	return 0;
}

int fetch_populated_submodules(const struct argv_array *options,
			       const char *prefix, int command_line_option,
			       int quiet, int max_parallel_jobs)
{
	int i;
	struct submodule_parallel_fetch spf = SPF_INIT;

	spf.work_tree = get_git_work_tree();
	spf.command_line_option = command_line_option;
	spf.quiet = quiet;
	spf.prefix = prefix;

	if (!spf.work_tree)
		goto out;

	if (read_cache() < 0)
		die("index file corrupt");

	argv_array_push(&spf.args, "fetch");
	for (i = 0; i < options->argc; i++)
		argv_array_push(&spf.args, options->argv[i]);
	argv_array_push(&spf.args, "--recurse-submodules-default");
	/* default value, "--submodule-prefix" and its value are added later */

	if (max_parallel_jobs < 0)
		max_parallel_jobs = parallel_jobs;

	calculate_changed_submodule_paths();
	run_processes_parallel(max_parallel_jobs,
			       get_next_submodule,
			       fetch_start_failure,
			       fetch_finish,
			       &spf);

	argv_array_clear(&spf.args);
out:
	string_list_clear(&changed_submodule_paths, 1);
	return spf.result;
}

unsigned is_submodule_modified(const char *path, int ignore_untracked)
{
	ssize_t len;
	struct child_process cp = CHILD_PROCESS_INIT;
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
	git_dir = read_gitfile(buf.buf);
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

	cp.argv = argv;
	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp))
		die("Could not run 'git status --porcelain' in submodule %s", path);

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
		die("'git status --porcelain' failed in submodule %s", path);

	strbuf_release(&buf);
	return dirty_submodule;
}

int submodule_uses_gitfile(const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *argv[] = {
		"submodule",
		"foreach",
		"--quiet",
		"--recursive",
		"test -f .git",
		NULL,
	};
	struct strbuf buf = STRBUF_INIT;
	const char *git_dir;

	strbuf_addf(&buf, "%s/.git", path);
	git_dir = read_gitfile(buf.buf);
	if (!git_dir) {
		strbuf_release(&buf);
		return 0;
	}
	strbuf_release(&buf);

	/* Now test that all nested submodules use a gitfile too */
	cp.argv = argv;
	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.no_stderr = 1;
	cp.no_stdout = 1;
	cp.dir = path;
	if (run_command(&cp))
		return 0;

	return 1;
}

/*
 * Check if it is a bad idea to remove a submodule, i.e. if we'd lose data
 * when doing so.
 *
 * Return 1 if we'd lose data, return 0 if the removal is fine,
 * and negative values for errors.
 */
int bad_to_remove_submodule(const char *path, unsigned flags)
{
	ssize_t len;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;

	if (!file_exists(path) || is_empty_dir(path))
		return 0;

	if (!submodule_uses_gitfile(path))
		return 1;

	argv_array_pushl(&cp.args, "status", "--porcelain",
				   "--ignore-submodules=none", NULL);

	if (flags & SUBMODULE_REMOVAL_IGNORE_UNTRACKED)
		argv_array_push(&cp.args, "-uno");
	else
		argv_array_push(&cp.args, "-uall");

	if (!(flags & SUBMODULE_REMOVAL_IGNORE_IGNORED_UNTRACKED))
		argv_array_push(&cp.args, "--ignored");

	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp)) {
		if (flags & SUBMODULE_REMOVAL_DIE_ON_ERROR)
			die(_("could not start 'git status in submodule '%s'"),
				path);
		ret = -1;
		goto out;
	}

	len = strbuf_read(&buf, cp.out, 1024);
	if (len > 2)
		ret = 1;
	close(cp.out);

	if (finish_command(&cp)) {
		if (flags & SUBMODULE_REMOVAL_DIE_ON_ERROR)
			die(_("could not run 'git status in submodule '%s'"),
				path);
		ret = -1;
	}
out:
	strbuf_release(&buf);
	return ret;
}

static int find_first_merges(struct object_array *result, const char *path,
		struct commit *a, struct commit *b)
{
	int i, j;
	struct object_array merges = OBJECT_ARRAY_INIT;
	struct commit *commit;
	int contains_another;

	char merged_revision[42];
	const char *rev_args[] = { "rev-list", "--merges", "--ancestry-path",
				   "--all", merged_revision, NULL };
	struct rev_info revs;
	struct setup_revision_opt rev_opts;

	memset(result, 0, sizeof(struct object_array));
	memset(&rev_opts, 0, sizeof(rev_opts));

	/* get all revisions that merge commit a */
	snprintf(merged_revision, sizeof(merged_revision), "^%s",
			oid_to_hex(&a->object.oid));
	init_revisions(&revs, NULL);
	rev_opts.submodule = path;
	setup_revisions(ARRAY_SIZE(rev_args)-1, rev_args, &revs, &rev_opts);

	/* save all revisions from the above list that contain b */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	while ((commit = get_revision(&revs)) != NULL) {
		struct object *o = &(commit->object);
		if (in_merge_bases(b, commit))
			add_object_array(o, NULL, &merges);
	}
	reset_revision_walk();

	/* Now we've got all merges that contain a and b. Prune all
	 * merges that contain another found merge and save them in
	 * result.
	 */
	for (i = 0; i < merges.nr; i++) {
		struct commit *m1 = (struct commit *) merges.objects[i].item;

		contains_another = 0;
		for (j = 0; j < merges.nr; j++) {
			struct commit *m2 = (struct commit *) merges.objects[j].item;
			if (i != j && in_merge_bases(m2, m1)) {
				contains_another = 1;
				break;
			}
		}

		if (!contains_another)
			add_object_array(merges.objects[i].item, NULL, result);
	}

	free(merges.objects);
	return result->nr;
}

static void print_commit(struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;
	struct pretty_print_context ctx = {0};
	ctx.date_mode.type = DATE_NORMAL;
	format_commit_message(commit, " %h: %m %s", &sb, &ctx);
	fprintf(stderr, "%s\n", sb.buf);
	strbuf_release(&sb);
}

#define MERGE_WARNING(path, msg) \
	warning("Failed to merge submodule %s (%s)", path, msg);

int merge_submodule(unsigned char result[20], const char *path,
		    const unsigned char base[20], const unsigned char a[20],
		    const unsigned char b[20], int search)
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
	if (!in_merge_bases(commit_base, commit_a) ||
	    !in_merge_bases(commit_base, commit_b)) {
		MERGE_WARNING(path, "commits don't follow merge-base");
		return 0;
	}

	/* Case #1: a is contained in b or vice versa */
	if (in_merge_bases(commit_a, commit_b)) {
		hashcpy(result, b);
		return 1;
	}
	if (in_merge_bases(commit_b, commit_a)) {
		hashcpy(result, a);
		return 1;
	}

	/*
	 * Case #2: There are one or more merges that contain a and b in
	 * the submodule. If there is only one, then present it as a
	 * suggestion to the user, but leave it marked unmerged so the
	 * user needs to confirm the resolution.
	 */

	/* Skip the search if makes no sense to the calling context.  */
	if (!search)
		return 0;

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
			oid_to_hex(&merges.objects[0].item->oid), path);
		break;

	default:
		MERGE_WARNING(path, "multiple merges found");
		for (i = 0; i < merges.nr; i++)
			print_commit((struct commit *) merges.objects[i].item);
	}

	free(merges.objects);
	return 0;
}

int parallel_submodules(void)
{
	return parallel_jobs;
}

void prepare_submodule_repo_env(struct argv_array *out)
{
	const char * const *var;

	for (var = local_repo_env; *var; var++) {
		if (strcmp(*var, CONFIG_DATA_ENVIRONMENT))
			argv_array_push(out, *var);
	}
	argv_array_pushf(out, "%s=%s", GIT_DIR_ENVIRONMENT,
			 DEFAULT_GIT_DIR_ENVIRONMENT);
}

/*
 * Embeds a single submodules git directory into the superprojects git dir,
 * non recursively.
 */
static void relocate_single_git_dir_into_superproject(const char *prefix,
						      const char *path)
{
	char *old_git_dir = NULL, *real_old_git_dir = NULL, *real_new_git_dir = NULL;
	const char *new_git_dir;
	const struct submodule *sub;

	if (submodule_uses_worktrees(path))
		die(_("relocate_gitdir for submodule '%s' with "
		      "more than one worktree not supported"), path);

	old_git_dir = xstrfmt("%s/.git", path);
	if (read_gitfile(old_git_dir))
		/* If it is an actual gitfile, it doesn't need migration. */
		return;

	real_old_git_dir = real_pathdup(old_git_dir, 1);

	sub = submodule_from_path(null_sha1, path);
	if (!sub)
		die(_("could not lookup name for submodule '%s'"), path);

	new_git_dir = git_path("modules/%s", sub->name);
	if (safe_create_leading_directories_const(new_git_dir) < 0)
		die(_("could not create directory '%s'"), new_git_dir);
	real_new_git_dir = real_pathdup(new_git_dir, 1);

	if (!prefix)
		prefix = get_super_prefix();

	fprintf(stderr, _("Migrating git directory of '%s%s' from\n'%s' to\n'%s'\n"),
		prefix ? prefix : "", path,
		real_old_git_dir, real_new_git_dir);

	relocate_gitdir(path, real_old_git_dir, real_new_git_dir);

	free(old_git_dir);
	free(real_old_git_dir);
	free(real_new_git_dir);
}

/*
 * Migrate the git directory of the submodule given by path from
 * having its git directory within the working tree to the git dir nested
 * in its superprojects git dir under modules/.
 */
void absorb_git_dir_into_superproject(const char *prefix,
				      const char *path,
				      unsigned flags)
{
	int err_code;
	const char *sub_git_dir;
	struct strbuf gitdir = STRBUF_INIT;
	strbuf_addf(&gitdir, "%s/.git", path);
	sub_git_dir = resolve_gitdir_gently(gitdir.buf, &err_code);

	/* Not populated? */
	if (!sub_git_dir) {
		char *real_new_git_dir;
		const char *new_git_dir;
		const struct submodule *sub;

		if (err_code == READ_GITFILE_ERR_STAT_FAILED) {
			/* unpopulated as expected */
			strbuf_release(&gitdir);
			return;
		}

		if (err_code != READ_GITFILE_ERR_NOT_A_REPO)
			/* We don't know what broke here. */
			read_gitfile_error_die(err_code, path, NULL);

		/*
		* Maybe populated, but no git directory was found?
		* This can happen if the superproject is a submodule
		* itself and was just absorbed. The absorption of the
		* superproject did not rewrite the git file links yet,
		* fix it now.
		*/
		sub = submodule_from_path(null_sha1, path);
		if (!sub)
			die(_("could not lookup name for submodule '%s'"), path);
		new_git_dir = git_path("modules/%s", sub->name);
		if (safe_create_leading_directories_const(new_git_dir) < 0)
			die(_("could not create directory '%s'"), new_git_dir);
		real_new_git_dir = real_pathdup(new_git_dir, 1);
		connect_work_tree_and_git_dir(path, real_new_git_dir);

		free(real_new_git_dir);
	} else {
		/* Is it already absorbed into the superprojects git dir? */
		char *real_sub_git_dir = real_pathdup(sub_git_dir, 1);
		char *real_common_git_dir = real_pathdup(get_git_common_dir(), 1);

		if (!starts_with(real_sub_git_dir, real_common_git_dir))
			relocate_single_git_dir_into_superproject(prefix, path);

		free(real_sub_git_dir);
		free(real_common_git_dir);
	}
	strbuf_release(&gitdir);

	if (flags & ABSORB_GITDIR_RECURSE_SUBMODULES) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf sb = STRBUF_INIT;

		if (flags & ~ABSORB_GITDIR_RECURSE_SUBMODULES)
			die("BUG: we don't know how to pass the flags down?");

		if (get_super_prefix())
			strbuf_addstr(&sb, get_super_prefix());
		strbuf_addstr(&sb, path);
		strbuf_addch(&sb, '/');

		cp.dir = path;
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		argv_array_pushl(&cp.args, "--super-prefix", sb.buf,
					   "submodule--helper",
					   "absorb-git-dirs", NULL);
		prepare_submodule_repo_env(&cp.env_array);
		if (run_command(&cp))
			die(_("could not recurse into submodule '%s'"), path);

		strbuf_release(&sb);
	}
}

const char *get_superproject_working_tree(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;
	const char *one_up = real_path_if_valid("../");
	const char *cwd = xgetcwd();
	const char *ret = NULL;
	const char *subpath;
	int code;
	ssize_t len;

	if (!is_inside_work_tree())
		/*
		 * FIXME:
		 * We might have a superproject, but it is harder
		 * to determine.
		 */
		return NULL;

	if (!one_up)
		return NULL;

	subpath = relative_path(cwd, one_up, &sb);

	prepare_submodule_repo_env(&cp.env_array);
	argv_array_pop(&cp.env_array);

	argv_array_pushl(&cp.args, "--literal-pathspecs", "-C", "..",
			"ls-files", "-z", "--stage", "--full-name", "--",
			subpath, NULL);
	strbuf_reset(&sb);

	cp.no_stdin = 1;
	cp.no_stderr = 1;
	cp.out = -1;
	cp.git_cmd = 1;

	if (start_command(&cp))
		die(_("could not start ls-files in .."));

	len = strbuf_read(&sb, cp.out, PATH_MAX);
	close(cp.out);

	if (starts_with(sb.buf, "160000")) {
		int super_sub_len;
		int cwd_len = strlen(cwd);
		char *super_sub, *super_wt;

		/*
		 * There is a superproject having this repo as a submodule.
		 * The format is <mode> SP <hash> SP <stage> TAB <full name> \0,
		 * We're only interested in the name after the tab.
		 */
		super_sub = strchr(sb.buf, '\t') + 1;
		super_sub_len = sb.buf + sb.len - super_sub - 1;

		if (super_sub_len > cwd_len ||
		    strcmp(&cwd[cwd_len - super_sub_len], super_sub))
			die (_("BUG: returned path string doesn't match cwd?"));

		super_wt = xstrdup(cwd);
		super_wt[cwd_len - super_sub_len] = '\0';

		ret = real_path(super_wt);
		free(super_wt);
	}
	strbuf_release(&sb);

	code = finish_command(&cp);

	if (code == 128)
		/* '../' is not a git repository */
		return NULL;
	if (code == 0 && len == 0)
		/* There is an unrelated git repository at '../' */
		return NULL;
	if (code)
		die(_("ls-tree returned unexpected return code %d"), code);

	return ret;
}
