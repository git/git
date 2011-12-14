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
	if (!prefixcmp(var, "submodule."))
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
		}

		if (!gitmodules_is_unmerged)
			git_config_from_file(submodule_config, gitmodules_path.buf, NULL);
		strbuf_release(&gitmodules_path);
	}
}

int parse_submodule_config_option(const char *var, const char *value)
{
	int len;
	struct string_list_item *config;
	struct strbuf submodname = STRBUF_INIT;

	var += 10;		/* Skip "submodule." */

	len = strlen(var);
	if ((len > 5) && !strcmp(var + len - 5, ".path")) {
		strbuf_add(&submodname, var, len - 5);
		config = unsorted_string_list_lookup(&config_name_for_path, value);
		if (config)
			free(config->util);
		else
			config = string_list_append(&config_name_for_path, xstrdup(value));
		config->util = strbuf_detach(&submodname, NULL);
		strbuf_release(&submodname);
	} else if ((len > 23) && !strcmp(var + len - 23, ".fetchrecursesubmodules")) {
		strbuf_add(&submodname, var, len - 23);
		config = unsorted_string_list_lookup(&config_fetch_recurse_submodules_for_name, submodname.buf);
		if (!config)
			config = string_list_append(&config_fetch_recurse_submodules_for_name,
						    strbuf_detach(&submodname, NULL));
		config->util = (void *)(intptr_t)parse_fetch_recurse_submodules_arg(var, value);
		strbuf_release(&submodname);
	} else if ((len > 7) && !strcmp(var + len - 7, ".ignore")) {
		if (strcmp(value, "untracked") && strcmp(value, "dirty") &&
		    strcmp(value, "all") && strcmp(value, "none")) {
			warning("Invalid parameter \"%s\" for config option \"submodule.%s.ignore\"", value, var);
			return 0;
		}

		strbuf_add(&submodname, var, len - 7);
		config = unsorted_string_list_lookup(&config_ignore_for_name, submodname.buf);
		if (config)
			free(config->util);
		else
			config = string_list_append(&config_ignore_for_name,
						    strbuf_detach(&submodname, NULL));
		strbuf_release(&submodname);
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
		const char *del, const char *add, const char *reset)
{
	static const char format[] = "  %m %s";
	struct strbuf sb = STRBUF_INIT;
	struct commit *commit;

	while ((commit = get_revision(rev))) {
		struct pretty_print_context ctx = {0};
		ctx.date_mode = rev->date_mode;
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
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
		const char *del, const char *add, const char *reset)
{
	struct rev_info rev;
	struct commit *left = left, *right = right;
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

	if (!message &&
	    prepare_submodule_summary(&rev, path, left, right,
					&fast_forward, &fast_backward))
		message = "(revision walker failed)";

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
		print_submodule_summary(&rev, f, del, add, reset);
		clear_commit_marks(left, ~0);
		clear_commit_marks(right, ~0);
	}

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
	int *needs_pushing = data;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!S_ISGITLINK(p->two->mode))
			continue;
		if (submodule_needs_pushing(p->two->path, p->two->sha1)) {
			*needs_pushing = 1;
			break;
		}
	}
}


static void commit_need_pushing(struct commit *commit, struct commit_list *parent, int *needs_pushing)
{
	const unsigned char (*parents)[20];
	unsigned int i, n;
	struct rev_info rev;

	n = commit_list_count(parent);
	parents = xmalloc(n * sizeof(*parents));

	for (i = 0; i < n; i++) {
		hashcpy((unsigned char *)(parents + i), parent->item->object.sha1);
		parent = parent->next;
	}

	init_revisions(&rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = collect_submodules_from_diff;
	rev.diffopt.format_callback_data = needs_pushing;
	diff_tree_combined(commit->object.sha1, parents, n, 1, &rev);

	free((void *)parents);
}

int check_submodule_needs_pushing(unsigned char new_sha1[20], const char *remotes_name)
{
	struct rev_info rev;
	struct commit *commit;
	const char *argv[] = {NULL, NULL, "--not", "NULL", NULL};
	int argc = ARRAY_SIZE(argv) - 1;
	char *sha1_copy;
	int needs_pushing = 0;
	struct strbuf remotes_arg = STRBUF_INIT;

	strbuf_addf(&remotes_arg, "--remotes=%s", remotes_name);
	init_revisions(&rev, NULL);
	sha1_copy = xstrdup(sha1_to_hex(new_sha1));
	argv[1] = sha1_copy;
	argv[3] = remotes_arg.buf;
	setup_revisions(argc, argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&rev)) && !needs_pushing)
		commit_need_pushing(commit, commit->parents, &needs_pushing);

	free(sha1_copy);
	strbuf_release(&remotes_arg);

	return needs_pushing;
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
			if (diff_setup_done(&diff_opts) < 0)
				die("diff_setup_done failed");
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

int fetch_populated_submodules(int num_options, const char **options,
			       const char *prefix, int command_line_option,
			       int quiet)
{
	int i, result = 0, argc = 0, default_argc;
	struct child_process cp;
	const char **argv;
	struct string_list_item *name_for_path;
	const char *work_tree = get_git_work_tree();
	if (!work_tree)
		goto out;

	if (!the_index.initialized)
		if (read_cache() < 0)
			die("index file corrupt");

	/* 6: "fetch" (options) --recurse-submodules-default default "--submodule-prefix" prefix NULL */
	argv = xcalloc(num_options + 6, sizeof(const char *));
	argv[argc++] = "fetch";
	for (i = 0; i < num_options; i++)
		argv[argc++] = options[i];
	argv[argc++] = "--recurse-submodules-default";
	default_argc = argc++;
	argv[argc++] = "--submodule-prefix";

	memset(&cp, 0, sizeof(cp));
	cp.argv = argv;
	cp.env = local_repo_env;
	cp.git_cmd = 1;
	cp.no_stdin = 1;

	calculate_changed_submodule_paths();

	for (i = 0; i < active_nr; i++) {
		struct strbuf submodule_path = STRBUF_INIT;
		struct strbuf submodule_git_dir = STRBUF_INIT;
		struct strbuf submodule_prefix = STRBUF_INIT;
		struct cache_entry *ce = active_cache[i];
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
			argv[default_argc] = default_argv;
			argv[argc] = submodule_prefix.buf;
			if (run_command(&cp))
				result = 1;
		}
		strbuf_release(&submodule_path);
		strbuf_release(&submodule_git_dir);
		strbuf_release(&submodule_prefix);
	}
	free(argv);
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
