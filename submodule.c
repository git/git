#include "cache.h"
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

static struct string_list config_name_for_path;
static struct string_list config_fetch_recurse_submodules_for_name;
static struct string_list config_ignore_for_name;
static int config_fetch_recurse_submodules = RECURSE_SUBMODULES_ON_DEMAND;
static struct string_list changed_submodule_paths;
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
	struct string_list_item *path_option;

	if (!file_exists(".gitmodules")) /* Do nothing without .gitmodules */
		return -1;

	if (gitmodules_is_unmerged)
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	path_option = unsorted_string_list_lookup(&config_name_for_path, oldpath);
	if (!path_option) {
		warning(_("Could not find section in .gitmodules where path=%s"), oldpath);
		return -1;
	}
	strbuf_addstr(&entry, "submodule.");
	strbuf_addstr(&entry, path_option->util);
	strbuf_addstr(&entry, ".path");
	if (git_config_set_in_file(".gitmodules", entry.buf, newpath) < 0) {
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
	struct string_list_item *path_option;

	if (!file_exists(".gitmodules")) /* Do nothing without .gitmodules */
		return -1;

	if (gitmodules_is_unmerged)
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	path_option = unsorted_string_list_lookup(&config_name_for_path, path);
	if (!path_option) {
		warning(_("Could not find section in .gitmodules where path=%s"), path);
		return -1;
	}
	strbuf_addstr(&sect, "submodule.");
	strbuf_addstr(&sect, path_option->util);
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
	struct alternate_object_database *alt_odb;
	int ret = 0;
	const char *git_dir;

	strbuf_addf(&objects_directory, "%s/.git", path);
	git_dir = read_gitfile(objects_directory.buf);
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

	/* add possible alternates from the submodule */
	read_info_alternates(objects_directory.buf, 0);
	prepare_alt_odb();
done:
	strbuf_release(&objects_directory);
	return ret;
}

void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
					     const char *path)
{
	struct string_list_item *path_option, *ignore_option;
	path_option = unsorted_string_list_lookup(&config_name_for_path, path);
	if (path_option) {
		ignore_option = unsorted_string_list_lookup(&config_ignore_for_name, path_option->util);
		if (ignore_option)
			handle_ignore_submodules_arg(diffopt, ignore_option->util);
		else if (gitmodules_is_unmerged)
			DIFF_OPT_SET(diffopt, IGNORE_SUBMODULES);
	}
}

int submodule_config(const char *var, const char *value, void *cb)
{
	if (starts_with(var, "submodule."))
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

int parse_submodule_config_option(const char *var, const char *value)
{
	struct string_list_item *config;
	const char *name, *key;
	int namelen;

	if (parse_config_key(var, "submodule", &name, &namelen, &key) < 0 || !name)
		return 0;

	if (!strcmp(key, "path")) {
		if (!value)
			return config_error_nonbool(var);

		config = unsorted_string_list_lookup(&config_name_for_path, value);
		if (config)
			free(config->util);
		else
			config = string_list_append(&config_name_for_path, xstrdup(value));
		config->util = xmemdupz(name, namelen);
	} else if (!strcmp(key, "fetchrecursesubmodules")) {
		char *name_cstr = xmemdupz(name, namelen);
		config = unsorted_string_list_lookup(&config_fetch_recurse_submodules_for_name, name_cstr);
		if (!config)
			config = string_list_append(&config_fetch_recurse_submodules_for_name, name_cstr);
		else
			free(name_cstr);
		config->util = (void *)(intptr_t)parse_fetch_recurse_submodules_arg(var, value);
	} else if (!strcmp(key, "ignore")) {
		char *name_cstr;

		if (!value)
			return config_error_nonbool(var);

		if (strcmp(value, "untracked") && strcmp(value, "dirty") &&
		    strcmp(value, "all") && strcmp(value, "none")) {
			warning("Invalid parameter \"%s\" for config option \"submodule.%s.ignore\"", value, var);
			return 0;
		}

		name_cstr = xmemdupz(name, namelen);
		config = unsorted_string_list_lookup(&config_ignore_for_name, name_cstr);
		if (config) {
			free(config->util);
			free(name_cstr);
		} else
			config = string_list_append(&config_ignore_for_name, name_cstr);
		config->util = xstrdup(value);
		return 0;
	}
	return 0;
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
		int *fast_forward, int *fast_backward)
{
	struct commit_list *merge_bases, *list;

	init_revisions(rev, NULL);
	setup_revisions(0, NULL, rev, NULL);
	rev->left_right = 1;
	rev->first_parent_only = 1;
	left->object.flags |= SYMMETRIC_LEFT;
	add_pending_object(rev, &left->object, path);
	add_pending_object(rev, &right->object, path);
	merge_bases = get_merge_bases(left, right, 1);
	if (merge_bases) {
		if (merge_bases->item == left)
			*fast_forward = 1;
		else if (merge_bases->item == right)
			*fast_backward = 1;
	}
	for (list = merge_bases; list; list = list->next) {
		list->item->object.flags |= UNINTERESTING;
		add_pending_object(rev, &list->item->object,
			sha1_to_hex(list->item->object.sha1));
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

int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg)
{
	switch (git_config_maybe_bool(opt, arg)) {
	case 1:
		return RECURSE_SUBMODULES_ON;
	case 0:
		return RECURSE_SUBMODULES_OFF;
	default:
		if (!strcmp(arg, "on-demand"))
			return RECURSE_SUBMODULES_ON_DEMAND;
		die("bad %s argument: %s", opt, arg);
	}
}

void show_submodule_summary(FILE *f, const char *path,
		const char *line_prefix,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule, const char *meta,
		const char *del, const char *add, const char *reset)
{
	struct rev_info rev;
	struct commit *left = NULL, *right = NULL;
	const char *message = NULL;
	struct strbuf sb = STRBUF_INIT;
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
	else if (prepare_submodule_summary(&rev, path, left, right,
					   &fast_forward, &fast_backward))
		message = "(revision walker failed)";

	if (dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
		fprintf(f, "%sSubmodule %s contains untracked content\n",
			line_prefix, path);
	if (dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
		fprintf(f, "%sSubmodule %s contains modified content\n",
			line_prefix, path);

	if (!hashcmp(one, two)) {
		strbuf_release(&sb);
		return;
	}

	strbuf_addf(&sb, "%s%sSubmodule %s %s..", line_prefix, meta, path,
			find_unique_abbrev(one, DEFAULT_ABBREV));
	if (!fast_backward && !fast_forward)
		strbuf_addch(&sb, '.');
	strbuf_addf(&sb, "%s", find_unique_abbrev(two, DEFAULT_ABBREV));
	if (message)
		strbuf_addf(&sb, " %s%s\n", message, reset);
	else
		strbuf_addf(&sb, "%s:%s\n", fast_backward ? " (rewind)" : "", reset);
	fwrite(sb.buf, sb.len, 1, f);

	if (!message) /* only NULL if we succeeded in setting up the walk */
		print_submodule_summary(&rev, f, line_prefix, del, add, reset);
	if (left)
		clear_commit_marks(left, ~0);
	if (right)
		clear_commit_marks(right, ~0);

	strbuf_release(&sb);
}

void set_config_fetch_recurse_submodules(int value)
{
	config_fetch_recurse_submodules = value;
}

static int has_remote(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	return 1;
}

static int submodule_needs_pushing(const char *path, const unsigned char sha1[20])
{
	if (add_submodule_odb(path) || !lookup_commit_reference(sha1))
		return 0;

	if (for_each_remote_ref_submodule(path, has_remote, NULL) > 0) {
		struct child_process cp;
		const char *argv[] = {"rev-list", NULL, "--not", "--remotes", "-n", "1" , NULL};
		struct strbuf buf = STRBUF_INIT;
		int needs_pushing = 0;

		argv[1] = sha1_to_hex(sha1);
		memset(&cp, 0, sizeof(cp));
		cp.argv = argv;
		cp.env = local_repo_env;
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.out = -1;
		cp.dir = path;
		if (start_command(&cp))
			die("Could not run 'git rev-list %s --not --remotes -n 1' command in submodule %s",
				sha1_to_hex(sha1), path);
		if (strbuf_read(&buf, cp.out, 41))
			needs_pushing = 1;
		finish_command(&cp);
		close(cp.out);
		strbuf_release(&buf);
		return needs_pushing;
	}

	return 0;
}

static void collect_submodules_from_diff(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	int i;
	struct string_list *needs_pushing = data;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!S_ISGITLINK(p->two->mode))
			continue;
		if (submodule_needs_pushing(p->two->path, p->two->sha1))
			string_list_insert(needs_pushing, p->two->path);
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

int find_unpushed_submodules(unsigned char new_sha1[20],
		const char *remotes_name, struct string_list *needs_pushing)
{
	struct rev_info rev;
	struct commit *commit;
	const char *argv[] = {NULL, NULL, "--not", "NULL", NULL};
	int argc = ARRAY_SIZE(argv) - 1;
	char *sha1_copy;

	struct strbuf remotes_arg = STRBUF_INIT;

	strbuf_addf(&remotes_arg, "--remotes=%s", remotes_name);
	init_revisions(&rev, NULL);
	sha1_copy = xstrdup(sha1_to_hex(new_sha1));
	argv[1] = sha1_copy;
	argv[3] = remotes_arg.buf;
	setup_revisions(argc, argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&rev)) != NULL)
		find_unpushed_submodule_commits(commit, needs_pushing);

	reset_revision_walk();
	free(sha1_copy);
	strbuf_release(&remotes_arg);

	return needs_pushing->nr;
}

static int push_submodule(const char *path)
{
	if (add_submodule_odb(path))
		return 1;

	if (for_each_remote_ref_submodule(path, has_remote, NULL) > 0) {
		struct child_process cp;
		const char *argv[] = {"push", NULL};

		memset(&cp, 0, sizeof(cp));
		cp.argv = argv;
		cp.env = local_repo_env;
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.dir = path;
		if (run_command(&cp))
			return 0;
		close(cp.out);
	}

	return 1;
}

int push_unpushed_submodules(unsigned char new_sha1[20], const char *remotes_name)
{
	int i, ret = 1;
	struct string_list needs_pushing = STRING_LIST_INIT_DUP;

	if (!find_unpushed_submodules(new_sha1, remotes_name, &needs_pushing))
		return 1;

	for (i = 0; i < needs_pushing.nr; i++) {
		const char *path = needs_pushing.items[i].string;
		fprintf(stderr, "Pushing submodule '%s'\n", path);
		if (!push_submodule(path)) {
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
		struct child_process cp;
		const char *argv[] = {"rev-list", "-n", "1", NULL, "--not", "--all", NULL};
		struct strbuf buf = STRBUF_INIT;

		argv[3] = sha1_to_hex(sha1);
		memset(&cp, 0, sizeof(cp));
		cp.argv = argv;
		cp.env = local_repo_env;
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.out = -1;
		cp.dir = path;
		if (!run_command(&cp) && !strbuf_read(&buf, cp.out, 1024))
			is_present = 1;

		close(cp.out);
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
			if (!path && !is_submodule_commit_present(p->two->path, p->two->sha1))
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

static int add_sha1_to_array(const char *ref, const unsigned char *sha1,
			     int flags, void *data)
{
	sha1_array_append(data, sha1);
	return 0;
}

void check_for_new_submodule_commits(unsigned char new_sha1[20])
{
	if (!initialized_fetch_ref_tips) {
		for_each_ref(add_sha1_to_array, &ref_tips_before_fetch);
		initialized_fetch_ref_tips = 1;
	}

	sha1_array_append(&ref_tips_after_fetch, new_sha1);
}

static void add_sha1_to_argv(const unsigned char sha1[20], void *data)
{
	argv_array_push(data, sha1_to_hex(sha1));
}

static void calculate_changed_submodule_paths(void)
{
	struct rev_info rev;
	struct commit *commit;
	struct argv_array argv = ARGV_ARRAY_INIT;

	/* No need to check if there are no submodules configured */
	if (!config_name_for_path.nr)
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
			diff_tree_sha1(parent->item->object.sha1, commit->object.sha1, "", &diff_opts);
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

int fetch_populated_submodules(const struct argv_array *options,
			       const char *prefix, int command_line_option,
			       int quiet)
{
	int i, result = 0;
	struct child_process cp;
	struct argv_array argv = ARGV_ARRAY_INIT;
	struct string_list_item *name_for_path;
	const char *work_tree = get_git_work_tree();
	if (!work_tree)
		goto out;

	if (read_cache() < 0)
		die("index file corrupt");

	argv_array_push(&argv, "fetch");
	for (i = 0; i < options->argc; i++)
		argv_array_push(&argv, options->argv[i]);
	argv_array_push(&argv, "--recurse-submodules-default");
	/* default value, "--submodule-prefix" and its value are added later */

	memset(&cp, 0, sizeof(cp));
	cp.env = local_repo_env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	calculate_changed_submodule_paths();

	for (i = 0; i < active_nr; i++) {
		struct strbuf submodule_path = STRBUF_INIT;
		struct strbuf submodule_git_dir = STRBUF_INIT;
		struct strbuf submodule_prefix = STRBUF_INIT;
		const struct cache_entry *ce = active_cache[i];
		const char *git_dir, *name, *default_argv;

		if (!S_ISGITLINK(ce->ce_mode))
			continue;

		name = ce->name;
		name_for_path = unsorted_string_list_lookup(&config_name_for_path, ce->name);
		if (name_for_path)
			name = name_for_path->util;

		default_argv = "yes";
		if (command_line_option == RECURSE_SUBMODULES_DEFAULT) {
			struct string_list_item *fetch_recurse_submodules_option;
			fetch_recurse_submodules_option = unsorted_string_list_lookup(&config_fetch_recurse_submodules_for_name, name);
			if (fetch_recurse_submodules_option) {
				if ((intptr_t)fetch_recurse_submodules_option->util == RECURSE_SUBMODULES_OFF)
					continue;
				if ((intptr_t)fetch_recurse_submodules_option->util == RECURSE_SUBMODULES_ON_DEMAND) {
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
		} else if (command_line_option == RECURSE_SUBMODULES_ON_DEMAND) {
			if (!unsorted_string_list_lookup(&changed_submodule_paths, ce->name))
				continue;
			default_argv = "on-demand";
		}

		strbuf_addf(&submodule_path, "%s/%s", work_tree, ce->name);
		strbuf_addf(&submodule_git_dir, "%s/.git", submodule_path.buf);
		strbuf_addf(&submodule_prefix, "%s%s/", prefix, ce->name);
		git_dir = read_gitfile(submodule_git_dir.buf);
		if (!git_dir)
			git_dir = submodule_git_dir.buf;
		if (is_directory(git_dir)) {
			if (!quiet)
				printf("Fetching submodule %s%s\n", prefix, ce->name);
			cp.dir = submodule_path.buf;
			argv_array_push(&argv, default_argv);
			argv_array_push(&argv, "--submodule-prefix");
			argv_array_push(&argv, submodule_prefix.buf);
			cp.argv = argv.argv;
			if (run_command(&cp))
				result = 1;
			argv_array_pop(&argv);
			argv_array_pop(&argv);
			argv_array_pop(&argv);
		}
		strbuf_release(&submodule_path);
		strbuf_release(&submodule_git_dir);
		strbuf_release(&submodule_prefix);
	}
	argv_array_clear(&argv);
out:
	string_list_clear(&changed_submodule_paths, 1);
	return result;
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

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = local_repo_env;
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
	struct child_process cp;
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
	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = local_repo_env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.no_stderr = 1;
	cp.no_stdout = 1;
	cp.dir = path;
	if (run_command(&cp))
		return 0;

	return 1;
}

int ok_to_remove_submodule(const char *path)
{
	struct stat st;
	ssize_t len;
	struct child_process cp;
	const char *argv[] = {
		"status",
		"--porcelain",
		"-u",
		"--ignore-submodules=none",
		NULL,
	};
	struct strbuf buf = STRBUF_INIT;
	int ok_to_remove = 1;

	if ((lstat(path, &st) < 0) || is_empty_dir(path))
		return 1;

	if (!submodule_uses_gitfile(path))
		return 0;

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = local_repo_env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp))
		die("Could not run 'git status --porcelain -uall --ignore-submodules=none' in submodule %s", path);

	len = strbuf_read(&buf, cp.out, 1024);
	if (len > 2)
		ok_to_remove = 0;
	close(cp.out);

	if (finish_command(&cp))
		die("'git status --porcelain -uall --ignore-submodules=none' failed in submodule %s", path);

	strbuf_release(&buf);
	return ok_to_remove;
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
			sha1_to_hex(a->object.sha1));
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
	ctx.date_mode = DATE_NORMAL;
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

/* Update gitfile and core.worktree setting to connect work tree and git dir */
void connect_work_tree_and_git_dir(const char *work_tree, const char *git_dir)
{
	struct strbuf file_name = STRBUF_INIT;
	struct strbuf rel_path = STRBUF_INIT;
	const char *real_work_tree = xstrdup(real_path(work_tree));
	FILE *fp;

	/* Update gitfile */
	strbuf_addf(&file_name, "%s/.git", work_tree);
	fp = fopen(file_name.buf, "w");
	if (!fp)
		die(_("Could not create git link %s"), file_name.buf);
	fprintf(fp, "gitdir: %s\n", relative_path(git_dir, real_work_tree,
						  &rel_path));
	fclose(fp);

	/* Update core.worktree setting */
	strbuf_reset(&file_name);
	strbuf_addf(&file_name, "%s/config", git_dir);
	if (git_config_set_in_file(file_name.buf, "core.worktree",
				   relative_path(real_work_tree, git_dir,
						 &rel_path)))
		die(_("Could not set core.worktree in %s"),
		    file_name.buf);

	strbuf_release(&file_name);
	strbuf_release(&rel_path);
	free((void *)real_work_tree);
}
