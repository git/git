
#include "cache.h"
#include "repository.h"
#include "config.h"
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
#include "remote.h"
#include "worktree.h"
#include "parse-options.h"
#include "object-store.h"
#include "commit-reach.h"

static int config_update_recurse_submodules = RECURSE_SUBMODULES_OFF;
static int initialized_fetch_ref_tips;
static struct oid_array ref_tips_before_fetch;
static struct oid_array ref_tips_after_fetch;

/*
 * Check if the .gitmodules file is unmerged. Parsing of the .gitmodules file
 * will be disabled because we can't guess what might be configured in
 * .gitmodules unless the user resolves the conflict.
 */
int is_gitmodules_unmerged(const struct index_state *istate)
{
	int pos = index_name_pos(istate, GITMODULES_FILE, strlen(GITMODULES_FILE));
	if (pos < 0) { /* .gitmodules not found or isn't merged */
		pos = -1 - pos;
		if (istate->cache_nr > pos) {  /* there is a .gitmodules */
			const struct cache_entry *ce = istate->cache[pos];
			if (ce_namelen(ce) == strlen(GITMODULES_FILE) &&
			    !strcmp(ce->name, GITMODULES_FILE))
				return 1;
		}
	}

	return 0;
}

/*
 * Check if the .gitmodules file is safe to write.
 *
 * Writing to the .gitmodules file requires that the file exists in the
 * working tree or, if it doesn't, that a brand new .gitmodules file is going
 * to be created (i.e. it's neither in the index nor in the current branch).
 *
 * It is not safe to write to .gitmodules if it's not in the working tree but
 * it is in the index or in the current branch, because writing new values
 * (and staging them) would blindly overwrite ALL the old content.
 */
int is_writing_gitmodules_ok(void)
{
	struct object_id oid;
	return file_exists(GITMODULES_FILE) ||
		(get_oid(GITMODULES_INDEX, &oid) < 0 && get_oid(GITMODULES_HEAD, &oid) < 0);
}

/*
 * Check if the .gitmodules file has unstaged modifications.  This must be
 * checked before allowing modifications to the .gitmodules file with the
 * intention to stage them later, because when continuing we would stage the
 * modifications the user didn't stage herself too. That might change in a
 * future version when we learn to stage the changes we do ourselves without
 * staging any previous modifications.
 */
int is_staging_gitmodules_ok(struct index_state *istate)
{
	int pos = index_name_pos(istate, GITMODULES_FILE, strlen(GITMODULES_FILE));

	if ((pos >= 0) && (pos < istate->cache_nr)) {
		struct stat st;
		if (lstat(GITMODULES_FILE, &st) == 0 &&
		    ie_match_stat(istate, istate->cache[pos], &st, 0) & DATA_CHANGED)
			return 0;
	}

	return 1;
}

static int for_each_remote_ref_submodule(const char *submodule,
					 each_ref_fn fn, void *cb_data)
{
	return refs_for_each_remote_ref(get_submodule_ref_store(submodule),
					fn, cb_data);
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
	int ret;

	if (!file_exists(GITMODULES_FILE)) /* Do nothing without .gitmodules */
		return -1;

	if (is_gitmodules_unmerged(the_repository->index))
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(the_repository, &null_oid, oldpath);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .gitmodules where path=%s"), oldpath);
		return -1;
	}
	strbuf_addstr(&entry, "submodule.");
	strbuf_addstr(&entry, submodule->name);
	strbuf_addstr(&entry, ".path");
	ret = config_set_in_gitmodules_file_gently(entry.buf, newpath);
	strbuf_release(&entry);
	return ret;
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

	if (!file_exists(GITMODULES_FILE)) /* Do nothing without .gitmodules */
		return -1;

	if (is_gitmodules_unmerged(the_repository->index))
		die(_("Cannot change unmerged .gitmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(the_repository, &null_oid, path);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .gitmodules where path=%s"), path);
		return -1;
	}
	strbuf_addstr(&sect, "submodule.");
	strbuf_addstr(&sect, submodule->name);
	if (git_config_rename_section_in_file(GITMODULES_FILE, sect.buf, NULL) < 0) {
		/* Maybe the user already did that, don't error out here */
		warning(_("Could not remove .gitmodules entry for %s"), path);
		strbuf_release(&sect);
		return -1;
	}
	strbuf_release(&sect);
	return 0;
}

void stage_updated_gitmodules(struct index_state *istate)
{
	if (add_file_to_index(istate, GITMODULES_FILE, 0))
		die(_("staging updated .gitmodules failed"));
}

/* TODO: remove this function, use repo_submodule_init instead. */
int add_submodule_odb(const char *path)
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
	const struct submodule *submodule = submodule_from_path(the_repository,
								&null_oid, path);
	if (submodule) {
		const char *ignore;
		char *key;

		key = xstrfmt("submodule.%s.ignore", submodule->name);
		if (repo_config_get_string_const(the_repository, key, &ignore))
			ignore = submodule->ignore;
		free(key);

		if (ignore)
			handle_ignore_submodules_arg(diffopt, ignore);
		else if (is_gitmodules_unmerged(the_repository->index))
			diffopt->flags.ignore_submodules = 1;
	}
}

/* Cheap function that only determines if we're interested in submodules at all */
int git_default_submodule_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "submodule.recurse")) {
		int v = git_config_bool(var, value) ?
			RECURSE_SUBMODULES_ON : RECURSE_SUBMODULES_OFF;
		config_update_recurse_submodules = v;
	}
	return 0;
}

int option_parse_recurse_submodules_worktree_updater(const struct option *opt,
						     const char *arg, int unset)
{
	if (unset) {
		config_update_recurse_submodules = RECURSE_SUBMODULES_OFF;
		return 0;
	}
	if (arg)
		config_update_recurse_submodules =
			parse_update_recurse_submodules_arg(opt->long_name,
							    arg);
	else
		config_update_recurse_submodules = RECURSE_SUBMODULES_ON;

	return 0;
}

/*
 * Determine if a submodule has been initialized at a given 'path'
 */
int is_submodule_active(struct repository *repo, const char *path)
{
	int ret = 0;
	char *key = NULL;
	char *value = NULL;
	const struct string_list *sl;
	const struct submodule *module;

	module = submodule_from_path(repo, &null_oid, path);

	/* early return if there isn't a path->module mapping */
	if (!module)
		return 0;

	/* submodule.<name>.active is set */
	key = xstrfmt("submodule.%s.active", module->name);
	if (!repo_config_get_bool(repo, key, &ret)) {
		free(key);
		return ret;
	}
	free(key);

	/* submodule.active is set */
	sl = repo_config_get_value_multi(repo, "submodule.active");
	if (sl) {
		struct pathspec ps;
		struct argv_array args = ARGV_ARRAY_INIT;
		const struct string_list_item *item;

		for_each_string_list_item(item, sl) {
			argv_array_push(&args, item->string);
		}

		parse_pathspec(&ps, 0, 0, NULL, args.argv);
		ret = match_pathspec(repo->index, &ps, path, strlen(path), 0, NULL, 1);

		argv_array_clear(&args);
		clear_pathspec(&ps);
		return ret;
	}

	/* fallback to checking if the URL is set */
	key = xstrfmt("submodule.%s.url", module->name);
	ret = !repo_config_get_string(repo, key, &value);

	free(value);
	free(key);
	return ret;
}

int is_submodule_populated_gently(const char *path, int *return_error_code)
{
	int ret = 0;
	char *gitdir = xstrfmt("%s/.git", path);

	if (resolve_gitdir_gently(gitdir, return_error_code))
		ret = 1;

	free(gitdir);
	return ret;
}

/*
 * Dies if the provided 'prefix' corresponds to an unpopulated submodule
 */
void die_in_unpopulated_submodule(const struct index_state *istate,
				  const char *prefix)
{
	int i, prefixlen;

	if (!prefix)
		return;

	prefixlen = strlen(prefix);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		int ce_len = ce_namelen(ce);

		if (!S_ISGITLINK(ce->ce_mode))
			continue;
		if (prefixlen <= ce_len)
			continue;
		if (strncmp(ce->name, prefix, ce_len))
			continue;
		if (prefix[ce_len] != '/')
			continue;

		die(_("in unpopulated submodule '%s'"), ce->name);
	}
}

/*
 * Dies if any paths in the provided pathspec descends into a submodule
 */
void die_path_inside_submodule(const struct index_state *istate,
			       const struct pathspec *ps)
{
	int i, j;

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		int ce_len = ce_namelen(ce);

		if (!S_ISGITLINK(ce->ce_mode))
			continue;

		for (j = 0; j < ps->nr ; j++) {
			const struct pathspec_item *item = &ps->items[j];

			if (item->len <= ce_len)
				continue;
			if (item->match[ce_len] != '/')
				continue;
			if (strncmp(ce->name, item->match, ce_len))
				continue;
			if (item->len == ce_len + 1)
				continue;

			die(_("Pathspec '%s' is in submodule '%.*s'"),
			    item->original, ce_len, ce->name);
		}
	}
}

enum submodule_update_type parse_submodule_update_type(const char *value)
{
	if (!strcmp(value, "none"))
		return SM_UPDATE_NONE;
	else if (!strcmp(value, "checkout"))
		return SM_UPDATE_CHECKOUT;
	else if (!strcmp(value, "rebase"))
		return SM_UPDATE_REBASE;
	else if (!strcmp(value, "merge"))
		return SM_UPDATE_MERGE;
	else if (*value == '!')
		return SM_UPDATE_COMMAND;
	else
		return SM_UPDATE_UNSPECIFIED;
}

int parse_submodule_update_strategy(const char *value,
		struct submodule_update_strategy *dst)
{
	enum submodule_update_type type;

	free((void*)dst->command);
	dst->command = NULL;

	type = parse_submodule_update_type(value);
	if (type == SM_UPDATE_UNSPECIFIED)
		return -1;

	dst->type = type;
	if (type == SM_UPDATE_COMMAND)
		dst->command = xstrdup(value + 1);

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
	diffopt->flags.ignore_submodules = 0;
	diffopt->flags.ignore_untracked_in_submodules = 0;
	diffopt->flags.ignore_dirty_submodules = 0;

	if (!strcmp(arg, "all"))
		diffopt->flags.ignore_submodules = 1;
	else if (!strcmp(arg, "untracked"))
		diffopt->flags.ignore_untracked_in_submodules = 1;
	else if (!strcmp(arg, "dirty"))
		diffopt->flags.ignore_dirty_submodules = 1;
	else if (strcmp(arg, "none"))
		die("bad --ignore-submodules argument: %s", arg);
	/*
	 * Please update _git_status() in git-completion.bash when you
	 * add new options
	 */
}

static int prepare_submodule_summary(struct rev_info *rev, const char *path,
		struct commit *left, struct commit *right,
		struct commit_list *merge_bases)
{
	struct commit_list *list;

	repo_init_revisions(the_repository, rev, NULL);
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

static void print_submodule_summary(struct repository *r, struct rev_info *rev, struct diff_options *o)
{
	static const char format[] = "  %m %s";
	struct strbuf sb = STRBUF_INIT;
	struct commit *commit;

	while ((commit = get_revision(rev))) {
		struct pretty_print_context ctx = {0};
		ctx.date_mode = rev->date_mode;
		ctx.output_encoding = get_log_output_encoding();
		strbuf_setlen(&sb, 0);
		repo_format_commit_message(r, commit, format, &sb,
				      &ctx);
		strbuf_addch(&sb, '\n');
		if (commit->object.flags & SYMMETRIC_LEFT)
			diff_emit_submodule_del(o, sb.buf);
		else
			diff_emit_submodule_add(o, sb.buf);
	}
	strbuf_release(&sb);
}

static void prepare_submodule_repo_env_no_git_dir(struct argv_array *out)
{
	const char * const *var;

	for (var = local_repo_env; *var; var++) {
		if (strcmp(*var, CONFIG_DATA_ENVIRONMENT))
			argv_array_push(out, *var);
	}
}

void prepare_submodule_repo_env(struct argv_array *out)
{
	prepare_submodule_repo_env_no_git_dir(out);
	argv_array_pushf(out, "%s=%s", GIT_DIR_ENVIRONMENT,
			 DEFAULT_GIT_DIR_ENVIRONMENT);
}

static void prepare_submodule_repo_env_in_gitdir(struct argv_array *out)
{
	prepare_submodule_repo_env_no_git_dir(out);
	argv_array_pushf(out, "%s=.", GIT_DIR_ENVIRONMENT);
}

/*
 * Initialize a repository struct for a submodule based on the provided 'path'.
 *
 * Unlike repo_submodule_init, this tolerates submodules not present
 * in .gitmodules. This function exists only to preserve historical behavior,
 *
 * Returns the repository struct on success,
 * NULL when the submodule is not present.
 */
static struct repository *open_submodule(const char *path)
{
	struct strbuf sb = STRBUF_INIT;
	struct repository *out = xmalloc(sizeof(*out));

	if (submodule_to_gitdir(&sb, path) || repo_init(out, sb.buf, NULL)) {
		strbuf_release(&sb);
		free(out);
		return NULL;
	}

	/* Mark it as a submodule */
	out->submodule_prefix = xstrdup(path);

	strbuf_release(&sb);
	return out;
}

/*
 * Helper function to display the submodule header line prior to the full
 * summary output.
 *
 * If it can locate the submodule git directory it will create a repository
 * handle for the submodule and lookup both the left and right commits and
 * put them into the left and right pointers.
 */
static void show_submodule_header(struct diff_options *o,
		const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule,
		struct repository *sub,
		struct commit **left, struct commit **right,
		struct commit_list **merge_bases)
{
	const char *message = NULL;
	struct strbuf sb = STRBUF_INIT;
	int fast_forward = 0, fast_backward = 0;

	if (dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
		diff_emit_submodule_untracked(o, path);

	if (dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
		diff_emit_submodule_modified(o, path);

	if (is_null_oid(one))
		message = "(new submodule)";
	else if (is_null_oid(two))
		message = "(submodule deleted)";

	if (!sub) {
		if (!message)
			message = "(commits not present)";
		goto output_header;
	}

	/*
	 * Attempt to lookup the commit references, and determine if this is
	 * a fast forward or fast backwards update.
	 */
	*left = lookup_commit_reference(sub, one);
	*right = lookup_commit_reference(sub, two);

	/*
	 * Warn about missing commits in the submodule project, but only if
	 * they aren't null.
	 */
	if ((!is_null_oid(one) && !*left) ||
	     (!is_null_oid(two) && !*right))
		message = "(commits not present)";

	*merge_bases = repo_get_merge_bases(sub, *left, *right);
	if (*merge_bases) {
		if ((*merge_bases)->item == *left)
			fast_forward = 1;
		else if ((*merge_bases)->item == *right)
			fast_backward = 1;
	}

	if (oideq(one, two)) {
		strbuf_release(&sb);
		return;
	}

output_header:
	strbuf_addf(&sb, "Submodule %s ", path);
	strbuf_add_unique_abbrev(&sb, one, DEFAULT_ABBREV);
	strbuf_addstr(&sb, (fast_backward || fast_forward) ? ".." : "...");
	strbuf_add_unique_abbrev(&sb, two, DEFAULT_ABBREV);
	if (message)
		strbuf_addf(&sb, " %s\n", message);
	else
		strbuf_addf(&sb, "%s:\n", fast_backward ? " (rewind)" : "");
	diff_emit_submodule_header(o, sb.buf);

	strbuf_release(&sb);
}

void show_submodule_summary(struct diff_options *o, const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule)
{
	struct rev_info rev;
	struct commit *left = NULL, *right = NULL;
	struct commit_list *merge_bases = NULL;
	struct repository *sub;

	sub = open_submodule(path);
	show_submodule_header(o, path, one, two, dirty_submodule,
			      sub, &left, &right, &merge_bases);

	/*
	 * If we don't have both a left and a right pointer, there is no
	 * reason to try and display a summary. The header line should contain
	 * all the information the user needs.
	 */
	if (!left || !right || !sub)
		goto out;

	/* Treat revision walker failure the same as missing commits */
	if (prepare_submodule_summary(&rev, path, left, right, merge_bases)) {
		diff_emit_submodule_error(o, "(revision walker failed)\n");
		goto out;
	}

	print_submodule_summary(sub, &rev, o);

out:
	if (merge_bases)
		free_commit_list(merge_bases);
	clear_commit_marks(left, ~0);
	clear_commit_marks(right, ~0);
	if (sub) {
		repo_clear(sub);
		free(sub);
	}
}

void show_submodule_inline_diff(struct diff_options *o, const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule)
{
	const struct object_id *old_oid = the_hash_algo->empty_tree, *new_oid = the_hash_algo->empty_tree;
	struct commit *left = NULL, *right = NULL;
	struct commit_list *merge_bases = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;
	struct repository *sub;

	sub = open_submodule(path);
	show_submodule_header(o, path, one, two, dirty_submodule,
			      sub, &left, &right, &merge_bases);

	/* We need a valid left and right commit to display a difference */
	if (!(left || is_null_oid(one)) ||
	    !(right || is_null_oid(two)))
		goto done;

	if (left)
		old_oid = one;
	if (right)
		new_oid = two;

	cp.git_cmd = 1;
	cp.dir = path;
	cp.out = -1;
	cp.no_stdin = 1;

	/* TODO: other options may need to be passed here. */
	argv_array_pushl(&cp.args, "diff", "--submodule=diff", NULL);
	argv_array_pushf(&cp.args, "--color=%s", want_color(o->use_color) ?
			 "always" : "never");

	if (o->flags.reverse_diff) {
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
	argv_array_push(&cp.args, oid_to_hex(old_oid));
	/*
	 * If the submodule has modified content, we will diff against the
	 * work tree, under the assumption that the user has asked for the
	 * diff format and wishes to actually see all differences even if they
	 * haven't yet been committed to the submodule yet.
	 */
	if (!(dirty_submodule & DIRTY_SUBMODULE_MODIFIED))
		argv_array_push(&cp.args, oid_to_hex(new_oid));

	prepare_submodule_repo_env(&cp.env_array);
	if (start_command(&cp))
		diff_emit_submodule_error(o, "(diff failed)\n");

	while (strbuf_getwholeline_fd(&sb, cp.out, '\n') != EOF)
		diff_emit_submodule_pipethrough(o, sb.buf, sb.len);

	if (finish_command(&cp))
		diff_emit_submodule_error(o, "(diff failed)\n");

done:
	strbuf_release(&sb);
	if (merge_bases)
		free_commit_list(merge_bases);
	if (left)
		clear_commit_marks(left, ~0);
	if (right)
		clear_commit_marks(right, ~0);
	if (sub) {
		repo_clear(sub);
		free(sub);
	}
}

int should_update_submodules(void)
{
	return config_update_recurse_submodules == RECURSE_SUBMODULES_ON;
}

const struct submodule *submodule_from_ce(const struct cache_entry *ce)
{
	if (!S_ISGITLINK(ce->ce_mode))
		return NULL;

	if (!should_update_submodules())
		return NULL;

	return submodule_from_path(the_repository, &null_oid, ce->name);
}

static struct oid_array *submodule_commits(struct string_list *submodules,
					   const char *name)
{
	struct string_list_item *item;

	item = string_list_insert(submodules, name);
	if (item->util)
		return (struct oid_array *) item->util;

	/* NEEDSWORK: should we have oid_array_init()? */
	item->util = xcalloc(1, sizeof(struct oid_array));
	return (struct oid_array *) item->util;
}

struct collect_changed_submodules_cb_data {
	struct repository *repo;
	struct string_list *changed;
	const struct object_id *commit_oid;
};

/*
 * this would normally be two functions: default_name_from_path() and
 * path_from_default_name(). Since the default name is the same as
 * the submodule path we can get away with just one function which only
 * checks whether there is a submodule in the working directory at that
 * location.
 */
static const char *default_name_or_path(const char *path_or_name)
{
	int error_code;

	if (!is_submodule_populated_gently(path_or_name, &error_code))
		return NULL;

	return path_or_name;
}

static void collect_changed_submodules_cb(struct diff_queue_struct *q,
					  struct diff_options *options,
					  void *data)
{
	struct collect_changed_submodules_cb_data *me = data;
	struct string_list *changed = me->changed;
	const struct object_id *commit_oid = me->commit_oid;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct oid_array *commits;
		const struct submodule *submodule;
		const char *name;

		if (!S_ISGITLINK(p->two->mode))
			continue;

		submodule = submodule_from_path(me->repo,
						commit_oid, p->two->path);
		if (submodule)
			name = submodule->name;
		else {
			name = default_name_or_path(p->two->path);
			/* make sure name does not collide with existing one */
			if (name)
				submodule = submodule_from_name(me->repo,
								commit_oid, name);
			if (submodule) {
				warning("Submodule in commit %s at path: "
					"'%s' collides with a submodule named "
					"the same. Skipping it.",
					oid_to_hex(commit_oid), p->two->path);
				name = NULL;
			}
		}

		if (!name)
			continue;

		commits = submodule_commits(changed, name);
		oid_array_append(commits, &p->two->oid);
	}
}

/*
 * Collect the paths of submodules in 'changed' which have changed based on
 * the revisions as specified in 'argv'.  Each entry in 'changed' will also
 * have a corresponding 'struct oid_array' (in the 'util' field) which lists
 * what the submodule pointers were updated to during the change.
 */
static void collect_changed_submodules(struct repository *r,
				       struct string_list *changed,
				       struct argv_array *argv)
{
	struct rev_info rev;
	const struct commit *commit;

	repo_init_revisions(r, &rev, NULL);
	setup_revisions(argv->argc, argv->argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&rev))) {
		struct rev_info diff_rev;
		struct collect_changed_submodules_cb_data data;
		data.repo = r;
		data.changed = changed;
		data.commit_oid = &commit->object.oid;

		repo_init_revisions(r, &diff_rev, NULL);
		diff_rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
		diff_rev.diffopt.format_callback = collect_changed_submodules_cb;
		diff_rev.diffopt.format_callback_data = &data;
		diff_tree_combined_merge(commit, 1, &diff_rev);
	}

	reset_revision_walk();
}

static void free_submodules_oids(struct string_list *submodules)
{
	struct string_list_item *item;
	for_each_string_list_item(item, submodules)
		oid_array_clear((struct oid_array *) item->util);
	string_list_clear(submodules, 1);
}

static int has_remote(const char *refname, const struct object_id *oid,
		      int flags, void *cb_data)
{
	return 1;
}

static int append_oid_to_argv(const struct object_id *oid, void *data)
{
	struct argv_array *argv = data;
	argv_array_push(argv, oid_to_hex(oid));
	return 0;
}

struct has_commit_data {
	struct repository *repo;
	int result;
	const char *path;
};

static int check_has_commit(const struct object_id *oid, void *data)
{
	struct has_commit_data *cb = data;

	enum object_type type = oid_object_info(cb->repo, oid, NULL);

	switch (type) {
	case OBJ_COMMIT:
		return 0;
	case OBJ_BAD:
		/*
		 * Object is missing or invalid. If invalid, an error message
		 * has already been printed.
		 */
		cb->result = 0;
		return 0;
	default:
		die(_("submodule entry '%s' (%s) is a %s, not a commit"),
		    cb->path, oid_to_hex(oid), type_name(type));
	}
}

static int submodule_has_commits(struct repository *r,
				 const char *path,
				 struct oid_array *commits)
{
	struct has_commit_data has_commit = { r, 1, path };

	/*
	 * Perform a cheap, but incorrect check for the existence of 'commits'.
	 * This is done by adding the submodule's object store to the in-core
	 * object store, and then querying for each commit's existence.  If we
	 * do not have the commit object anywhere, there is no chance we have
	 * it in the object store of the correct submodule and have it
	 * reachable from a ref, so we can fail early without spawning rev-list
	 * which is expensive.
	 */
	if (add_submodule_odb(path))
		return 0;

	oid_array_for_each_unique(commits, check_has_commit, &has_commit);

	if (has_commit.result) {
		/*
		 * Even if the submodule is checked out and the commit is
		 * present, make sure it exists in the submodule's object store
		 * and that it is reachable from a ref.
		 */
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;

		argv_array_pushl(&cp.args, "rev-list", "-n", "1", NULL);
		oid_array_for_each_unique(commits, append_oid_to_argv, &cp.args);
		argv_array_pushl(&cp.args, "--not", "--all", NULL);

		prepare_submodule_repo_env(&cp.env_array);
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.dir = path;

		if (capture_command(&cp, &out, GIT_MAX_HEXSZ + 1) || out.len)
			has_commit.result = 0;

		strbuf_release(&out);
	}

	return has_commit.result;
}

static int submodule_needs_pushing(struct repository *r,
				   const char *path,
				   struct oid_array *commits)
{
	if (!submodule_has_commits(r, path, commits))
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
		oid_array_for_each_unique(commits, append_oid_to_argv, &cp.args);
		argv_array_pushl(&cp.args, "--not", "--remotes", "-n", "1" , NULL);

		prepare_submodule_repo_env(&cp.env_array);
		cp.git_cmd = 1;
		cp.no_stdin = 1;
		cp.out = -1;
		cp.dir = path;
		if (start_command(&cp))
			die("Could not run 'git rev-list <commits> --not --remotes -n 1' command in submodule %s",
					path);
		if (strbuf_read(&buf, cp.out, the_hash_algo->hexsz + 1))
			needs_pushing = 1;
		finish_command(&cp);
		close(cp.out);
		strbuf_release(&buf);
		return needs_pushing;
	}

	return 0;
}

int find_unpushed_submodules(struct repository *r,
			     struct oid_array *commits,
			     const char *remotes_name,
			     struct string_list *needs_pushing)
{
	struct string_list submodules = STRING_LIST_INIT_DUP;
	struct string_list_item *name;
	struct argv_array argv = ARGV_ARRAY_INIT;

	/* argv.argv[0] will be ignored by setup_revisions */
	argv_array_push(&argv, "find_unpushed_submodules");
	oid_array_for_each_unique(commits, append_oid_to_argv, &argv);
	argv_array_push(&argv, "--not");
	argv_array_pushf(&argv, "--remotes=%s", remotes_name);

	collect_changed_submodules(r, &submodules, &argv);

	for_each_string_list_item(name, &submodules) {
		struct oid_array *commits = name->util;
		const struct submodule *submodule;
		const char *path = NULL;

		submodule = submodule_from_name(r, &null_oid, name->string);
		if (submodule)
			path = submodule->path;
		else
			path = default_name_or_path(name->string);

		if (!path)
			continue;

		if (submodule_needs_pushing(r, path, commits))
			string_list_insert(needs_pushing, path);
	}

	free_submodules_oids(&submodules);
	argv_array_clear(&argv);

	return needs_pushing->nr;
}

static int push_submodule(const char *path,
			  const struct remote *remote,
			  const struct refspec *rs,
			  const struct string_list *push_options,
			  int dry_run)
{
	if (for_each_remote_ref_submodule(path, has_remote, NULL) > 0) {
		struct child_process cp = CHILD_PROCESS_INIT;
		argv_array_push(&cp.args, "push");
		if (dry_run)
			argv_array_push(&cp.args, "--dry-run");

		if (push_options && push_options->nr) {
			const struct string_list_item *item;
			for_each_string_list_item(item, push_options)
				argv_array_pushf(&cp.args, "--push-option=%s",
						 item->string);
		}

		if (remote->origin != REMOTE_UNCONFIGURED) {
			int i;
			argv_array_push(&cp.args, remote->name);
			for (i = 0; i < rs->raw_nr; i++)
				argv_array_push(&cp.args, rs->raw[i]);
		}

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

/*
 * Perform a check in the submodule to see if the remote and refspec work.
 * Die if the submodule can't be pushed.
 */
static void submodule_push_check(const char *path, const char *head,
				 const struct remote *remote,
				 const struct refspec *rs)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int i;

	argv_array_push(&cp.args, "submodule--helper");
	argv_array_push(&cp.args, "push-check");
	argv_array_push(&cp.args, head);
	argv_array_push(&cp.args, remote->name);

	for (i = 0; i < rs->raw_nr; i++)
		argv_array_push(&cp.args, rs->raw[i]);

	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.no_stdout = 1;
	cp.dir = path;

	/*
	 * Simply indicate if 'submodule--helper push-check' failed.
	 * More detailed error information will be provided by the
	 * child process.
	 */
	if (run_command(&cp))
		die("process for submodule '%s' failed", path);
}

int push_unpushed_submodules(struct repository *r,
			     struct oid_array *commits,
			     const struct remote *remote,
			     const struct refspec *rs,
			     const struct string_list *push_options,
			     int dry_run)
{
	int i, ret = 1;
	struct string_list needs_pushing = STRING_LIST_INIT_DUP;

	if (!find_unpushed_submodules(r, commits,
				      remote->name, &needs_pushing))
		return 1;

	/*
	 * Verify that the remote and refspec can be propagated to all
	 * submodules.  This check can be skipped if the remote and refspec
	 * won't be propagated due to the remote being unconfigured (e.g. a URL
	 * instead of a remote name).
	 */
	if (remote->origin != REMOTE_UNCONFIGURED) {
		char *head;
		struct object_id head_oid;

		head = resolve_refdup("HEAD", 0, &head_oid, NULL);
		if (!head)
			die(_("Failed to resolve HEAD as a valid ref."));

		for (i = 0; i < needs_pushing.nr; i++)
			submodule_push_check(needs_pushing.items[i].string,
					     head, remote, rs);
		free(head);
	}

	/* Actually push the submodules */
	for (i = 0; i < needs_pushing.nr; i++) {
		const char *path = needs_pushing.items[i].string;
		fprintf(stderr, "Pushing submodule '%s'\n", path);
		if (!push_submodule(path, remote, rs,
				    push_options, dry_run)) {
			fprintf(stderr, "Unable to push submodule '%s'\n", path);
			ret = 0;
		}
	}

	string_list_clear(&needs_pushing, 0);

	return ret;
}

static int append_oid_to_array(const char *ref, const struct object_id *oid,
			       int flags, void *data)
{
	struct oid_array *array = data;
	oid_array_append(array, oid);
	return 0;
}

void check_for_new_submodule_commits(struct object_id *oid)
{
	if (!initialized_fetch_ref_tips) {
		for_each_ref(append_oid_to_array, &ref_tips_before_fetch);
		initialized_fetch_ref_tips = 1;
	}

	oid_array_append(&ref_tips_after_fetch, oid);
}

static void calculate_changed_submodule_paths(struct repository *r,
		struct string_list *changed_submodule_names)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	struct string_list_item *name;

	/* No need to check if there are no submodules configured */
	if (!submodule_from_path(r, NULL, NULL))
		return;

	argv_array_push(&argv, "--"); /* argv[0] program name */
	oid_array_for_each_unique(&ref_tips_after_fetch,
				   append_oid_to_argv, &argv);
	argv_array_push(&argv, "--not");
	oid_array_for_each_unique(&ref_tips_before_fetch,
				   append_oid_to_argv, &argv);

	/*
	 * Collect all submodules (whether checked out or not) for which new
	 * commits have been recorded upstream in "changed_submodule_names".
	 */
	collect_changed_submodules(r, changed_submodule_names, &argv);

	for_each_string_list_item(name, changed_submodule_names) {
		struct oid_array *commits = name->util;
		const struct submodule *submodule;
		const char *path = NULL;

		submodule = submodule_from_name(r, &null_oid, name->string);
		if (submodule)
			path = submodule->path;
		else
			path = default_name_or_path(name->string);

		if (!path)
			continue;

		if (submodule_has_commits(r, path, commits)) {
			oid_array_clear(commits);
			*name->string = '\0';
		}
	}

	string_list_remove_empty_items(changed_submodule_names, 1);

	argv_array_clear(&argv);
	oid_array_clear(&ref_tips_before_fetch);
	oid_array_clear(&ref_tips_after_fetch);
	initialized_fetch_ref_tips = 0;
}

int submodule_touches_in_range(struct repository *r,
			       struct object_id *excl_oid,
			       struct object_id *incl_oid)
{
	struct string_list subs = STRING_LIST_INIT_DUP;
	struct argv_array args = ARGV_ARRAY_INIT;
	int ret;

	/* No need to check if there are no submodules configured */
	if (!submodule_from_path(r, NULL, NULL))
		return 0;

	argv_array_push(&args, "--"); /* args[0] program name */
	argv_array_push(&args, oid_to_hex(incl_oid));
	if (!is_null_oid(excl_oid)) {
		argv_array_push(&args, "--not");
		argv_array_push(&args, oid_to_hex(excl_oid));
	}

	collect_changed_submodules(r, &subs, &args);
	ret = subs.nr;

	argv_array_clear(&args);

	free_submodules_oids(&subs);
	return ret;
}

struct submodule_parallel_fetch {
	int count;
	struct argv_array args;
	struct repository *r;
	const char *prefix;
	int command_line_option;
	int default_option;
	int quiet;
	int result;

	struct string_list changed_submodule_names;

	/* Pending fetches by OIDs */
	struct fetch_task **oid_fetch_tasks;
	int oid_fetch_tasks_nr, oid_fetch_tasks_alloc;
};
#define SPF_INIT {0, ARGV_ARRAY_INIT, NULL, NULL, 0, 0, 0, 0, \
		  STRING_LIST_INIT_DUP, \
		  NULL, 0, 0}

static int get_fetch_recurse_config(const struct submodule *submodule,
				    struct submodule_parallel_fetch *spf)
{
	if (spf->command_line_option != RECURSE_SUBMODULES_DEFAULT)
		return spf->command_line_option;

	if (submodule) {
		char *key;
		const char *value;

		int fetch_recurse = submodule->fetch_recurse;
		key = xstrfmt("submodule.%s.fetchRecurseSubmodules", submodule->name);
		if (!repo_config_get_string_const(spf->r, key, &value)) {
			fetch_recurse = parse_fetch_recurse_submodules_arg(key, value);
		}
		free(key);

		if (fetch_recurse != RECURSE_SUBMODULES_NONE)
			/* local config overrules everything except commandline */
			return fetch_recurse;
	}

	return spf->default_option;
}

/*
 * Fetch in progress (if callback data) or
 * pending (if in oid_fetch_tasks in struct submodule_parallel_fetch)
 */
struct fetch_task {
	struct repository *repo;
	const struct submodule *sub;
	unsigned free_sub : 1; /* Do we need to free the submodule? */

	struct oid_array *commits; /* Ensure these commits are fetched */
};

/**
 * When a submodule is not defined in .gitmodules, we cannot access it
 * via the regular submodule-config. Create a fake submodule, which we can
 * work on.
 */
static const struct submodule *get_non_gitmodules_submodule(const char *path)
{
	struct submodule *ret = NULL;
	const char *name = default_name_or_path(path);

	if (!name)
		return NULL;

	ret = xmalloc(sizeof(*ret));
	memset(ret, 0, sizeof(*ret));
	ret->path = name;
	ret->name = name;

	return (const struct submodule *) ret;
}

static struct fetch_task *fetch_task_create(struct repository *r,
					    const char *path)
{
	struct fetch_task *task = xmalloc(sizeof(*task));
	memset(task, 0, sizeof(*task));

	task->sub = submodule_from_path(r, &null_oid, path);
	if (!task->sub) {
		/*
		 * No entry in .gitmodules? Technically not a submodule,
		 * but historically we supported repositories that happen to be
		 * in-place where a gitlink is. Keep supporting them.
		 */
		task->sub = get_non_gitmodules_submodule(path);
		if (!task->sub) {
			free(task);
			return NULL;
		}

		task->free_sub = 1;
	}

	return task;
}

static void fetch_task_release(struct fetch_task *p)
{
	if (p->free_sub)
		free((void*)p->sub);
	p->free_sub = 0;
	p->sub = NULL;

	if (p->repo)
		repo_clear(p->repo);
	FREE_AND_NULL(p->repo);
}

static struct repository *get_submodule_repo_for(struct repository *r,
						 const struct submodule *sub)
{
	struct repository *ret = xmalloc(sizeof(*ret));

	if (repo_submodule_init(ret, r, sub)) {
		/*
		 * No entry in .gitmodules? Technically not a submodule,
		 * but historically we supported repositories that happen to be
		 * in-place where a gitlink is. Keep supporting them.
		 */
		struct strbuf gitdir = STRBUF_INIT;
		strbuf_repo_worktree_path(&gitdir, r, "%s/.git", sub->path);
		if (repo_init(ret, gitdir.buf, NULL)) {
			strbuf_release(&gitdir);
			free(ret);
			return NULL;
		}
		strbuf_release(&gitdir);
	}

	return ret;
}

static int get_next_submodule(struct child_process *cp,
			      struct strbuf *err, void *data, void **task_cb)
{
	struct submodule_parallel_fetch *spf = data;

	for (; spf->count < spf->r->index->cache_nr; spf->count++) {
		const struct cache_entry *ce = spf->r->index->cache[spf->count];
		const char *default_argv;
		struct fetch_task *task;

		if (!S_ISGITLINK(ce->ce_mode))
			continue;

		task = fetch_task_create(spf->r, ce->name);
		if (!task)
			continue;

		switch (get_fetch_recurse_config(task->sub, spf))
		{
		default:
		case RECURSE_SUBMODULES_DEFAULT:
		case RECURSE_SUBMODULES_ON_DEMAND:
			if (!task->sub ||
			    !string_list_lookup(
					&spf->changed_submodule_names,
					task->sub->name))
				continue;
			default_argv = "on-demand";
			break;
		case RECURSE_SUBMODULES_ON:
			default_argv = "yes";
			break;
		case RECURSE_SUBMODULES_OFF:
			continue;
		}

		task->repo = get_submodule_repo_for(spf->r, task->sub);
		if (task->repo) {
			struct strbuf submodule_prefix = STRBUF_INIT;
			child_process_init(cp);
			cp->dir = task->repo->gitdir;
			prepare_submodule_repo_env_in_gitdir(&cp->env_array);
			cp->git_cmd = 1;
			if (!spf->quiet)
				strbuf_addf(err, "Fetching submodule %s%s\n",
					    spf->prefix, ce->name);
			argv_array_init(&cp->args);
			argv_array_pushv(&cp->args, spf->args.argv);
			argv_array_push(&cp->args, default_argv);
			argv_array_push(&cp->args, "--submodule-prefix");

			strbuf_addf(&submodule_prefix, "%s%s/",
						       spf->prefix,
						       task->sub->path);
			argv_array_push(&cp->args, submodule_prefix.buf);

			spf->count++;
			*task_cb = task;

			strbuf_release(&submodule_prefix);
			return 1;
		} else {

			fetch_task_release(task);
			free(task);

			/*
			 * An empty directory is normal,
			 * the submodule is not initialized
			 */
			if (S_ISGITLINK(ce->ce_mode) &&
			    !is_empty_dir(ce->name)) {
				spf->result = 1;
				strbuf_addf(err,
					    _("Could not access submodule '%s'"),
					    ce->name);
			}
		}
	}

	if (spf->oid_fetch_tasks_nr) {
		struct fetch_task *task =
			spf->oid_fetch_tasks[spf->oid_fetch_tasks_nr - 1];
		struct strbuf submodule_prefix = STRBUF_INIT;
		spf->oid_fetch_tasks_nr--;

		strbuf_addf(&submodule_prefix, "%s%s/",
			    spf->prefix, task->sub->path);

		child_process_init(cp);
		prepare_submodule_repo_env_in_gitdir(&cp->env_array);
		cp->git_cmd = 1;
		cp->dir = task->repo->gitdir;

		argv_array_init(&cp->args);
		argv_array_pushv(&cp->args, spf->args.argv);
		argv_array_push(&cp->args, "on-demand");
		argv_array_push(&cp->args, "--submodule-prefix");
		argv_array_push(&cp->args, submodule_prefix.buf);

		/* NEEDSWORK: have get_default_remote from submodule--helper */
		argv_array_push(&cp->args, "origin");
		oid_array_for_each_unique(task->commits,
					  append_oid_to_argv, &cp->args);

		*task_cb = task;
		strbuf_release(&submodule_prefix);
		return 1;
	}

	return 0;
}

static int fetch_start_failure(struct strbuf *err,
			       void *cb, void *task_cb)
{
	struct submodule_parallel_fetch *spf = cb;
	struct fetch_task *task = task_cb;

	spf->result = 1;

	fetch_task_release(task);
	return 0;
}

static int commit_missing_in_sub(const struct object_id *oid, void *data)
{
	struct repository *subrepo = data;

	enum object_type type = oid_object_info(subrepo, oid, NULL);

	return type != OBJ_COMMIT;
}

static int fetch_finish(int retvalue, struct strbuf *err,
			void *cb, void *task_cb)
{
	struct submodule_parallel_fetch *spf = cb;
	struct fetch_task *task = task_cb;

	struct string_list_item *it;
	struct oid_array *commits;

	if (retvalue)
		/*
		 * NEEDSWORK: This indicates that the overall fetch
		 * failed, even though there may be a subsequent fetch
		 * by commit hash that might work. It may be a good
		 * idea to not indicate failure in this case, and only
		 * indicate failure if the subsequent fetch fails.
		 */
		spf->result = 1;

	if (!task || !task->sub)
		BUG("callback cookie bogus");

	/* Is this the second time we process this submodule? */
	if (task->commits)
		goto out;

	it = string_list_lookup(&spf->changed_submodule_names, task->sub->name);
	if (!it)
		/* Could be an unchanged submodule, not contained in the list */
		goto out;

	commits = it->util;
	oid_array_filter(commits,
			 commit_missing_in_sub,
			 task->repo);

	/* Are there commits we want, but do not exist? */
	if (commits->nr) {
		task->commits = commits;
		ALLOC_GROW(spf->oid_fetch_tasks,
			   spf->oid_fetch_tasks_nr + 1,
			   spf->oid_fetch_tasks_alloc);
		spf->oid_fetch_tasks[spf->oid_fetch_tasks_nr] = task;
		spf->oid_fetch_tasks_nr++;
		return 0;
	}

out:
	fetch_task_release(task);

	return 0;
}

int fetch_populated_submodules(struct repository *r,
			       const struct argv_array *options,
			       const char *prefix, int command_line_option,
			       int default_option,
			       int quiet, int max_parallel_jobs)
{
	int i;
	struct submodule_parallel_fetch spf = SPF_INIT;

	spf.r = r;
	spf.command_line_option = command_line_option;
	spf.default_option = default_option;
	spf.quiet = quiet;
	spf.prefix = prefix;

	if (!r->worktree)
		goto out;

	if (repo_read_index(r) < 0)
		die("index file corrupt");

	argv_array_push(&spf.args, "fetch");
	for (i = 0; i < options->argc; i++)
		argv_array_push(&spf.args, options->argv[i]);
	argv_array_push(&spf.args, "--recurse-submodules-default");
	/* default value, "--submodule-prefix" and its value are added later */

	calculate_changed_submodule_paths(r, &spf.changed_submodule_names);
	string_list_sort(&spf.changed_submodule_names);
	run_processes_parallel_tr2(max_parallel_jobs,
				   get_next_submodule,
				   fetch_start_failure,
				   fetch_finish,
				   &spf,
				   "submodule", "parallel/fetch");

	argv_array_clear(&spf.args);
out:
	free_submodules_oids(&spf.changed_submodule_names);
	return spf.result;
}

unsigned is_submodule_modified(const char *path, int ignore_untracked)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	FILE *fp;
	unsigned dirty_submodule = 0;
	const char *git_dir;
	int ignore_cp_exit_code = 0;

	strbuf_addf(&buf, "%s/.git", path);
	git_dir = read_gitfile(buf.buf);
	if (!git_dir)
		git_dir = buf.buf;
	if (!is_git_directory(git_dir)) {
		if (is_directory(git_dir))
			die(_("'%s' not recognized as a git repository"), git_dir);
		strbuf_release(&buf);
		/* The submodule is not checked out, so it is not modified */
		return 0;
	}
	strbuf_reset(&buf);

	argv_array_pushl(&cp.args, "status", "--porcelain=2", NULL);
	if (ignore_untracked)
		argv_array_push(&cp.args, "-uno");

	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp))
		die("Could not run 'git status --porcelain=2' in submodule %s", path);

	fp = xfdopen(cp.out, "r");
	while (strbuf_getwholeline(&buf, fp, '\n') != EOF) {
		/* regular untracked files */
		if (buf.buf[0] == '?')
			dirty_submodule |= DIRTY_SUBMODULE_UNTRACKED;

		if (buf.buf[0] == 'u' ||
		    buf.buf[0] == '1' ||
		    buf.buf[0] == '2') {
			/* T = line type, XY = status, SSSS = submodule state */
			if (buf.len < strlen("T XY SSSS"))
				BUG("invalid status --porcelain=2 line %s",
				    buf.buf);

			if (buf.buf[5] == 'S' && buf.buf[8] == 'U')
				/* nested untracked file */
				dirty_submodule |= DIRTY_SUBMODULE_UNTRACKED;

			if (buf.buf[0] == 'u' ||
			    buf.buf[0] == '2' ||
			    memcmp(buf.buf + 5, "S..U", 4))
				/* other change */
				dirty_submodule |= DIRTY_SUBMODULE_MODIFIED;
		}

		if ((dirty_submodule & DIRTY_SUBMODULE_MODIFIED) &&
		    ((dirty_submodule & DIRTY_SUBMODULE_UNTRACKED) ||
		     ignore_untracked)) {
			/*
			 * We're not interested in any further information from
			 * the child any more, neither output nor its exit code.
			 */
			ignore_cp_exit_code = 1;
			break;
		}
	}
	fclose(fp);

	if (finish_command(&cp) && !ignore_cp_exit_code)
		die("'git status --porcelain=2' failed in submodule %s", path);

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
			die(_("could not start 'git status' in submodule '%s'"),
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
			die(_("could not run 'git status' in submodule '%s'"),
				path);
		ret = -1;
	}
out:
	strbuf_release(&buf);
	return ret;
}

void submodule_unset_core_worktree(const struct submodule *sub)
{
	char *config_path = xstrfmt("%s/modules/%s/config",
				    get_git_common_dir(), sub->name);

	if (git_config_set_in_file_gently(config_path, "core.worktree", NULL))
		warning(_("Could not unset core.worktree setting in submodule '%s'"),
			  sub->path);

	free(config_path);
}

static const char *get_super_prefix_or_empty(void)
{
	const char *s = get_super_prefix();
	if (!s)
		s = "";
	return s;
}

static int submodule_has_dirty_index(const struct submodule *sub)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	prepare_submodule_repo_env(&cp.env_array);

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-index", "--quiet",
				   "--cached", "HEAD", NULL);
	cp.no_stdin = 1;
	cp.no_stdout = 1;
	cp.dir = sub->path;
	if (start_command(&cp))
		die("could not recurse into submodule '%s'", sub->path);

	return finish_command(&cp);
}

static void submodule_reset_index(const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	prepare_submodule_repo_env(&cp.env_array);

	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.dir = path;

	argv_array_pushf(&cp.args, "--super-prefix=%s%s/",
				   get_super_prefix_or_empty(), path);
	argv_array_pushl(&cp.args, "read-tree", "-u", "--reset", NULL);

	argv_array_push(&cp.args, empty_tree_oid_hex());

	if (run_command(&cp))
		die("could not reset submodule index");
}

/**
 * Moves a submodule at a given path from a given head to another new head.
 * For edge cases (a submodule coming into existence or removing a submodule)
 * pass NULL for old or new respectively.
 */
int submodule_move_head(const char *path,
			 const char *old_head,
			 const char *new_head,
			 unsigned flags)
{
	int ret = 0;
	struct child_process cp = CHILD_PROCESS_INIT;
	const struct submodule *sub;
	int *error_code_ptr, error_code;

	if (!is_submodule_active(the_repository, path))
		return 0;

	if (flags & SUBMODULE_MOVE_HEAD_FORCE)
		/*
		 * Pass non NULL pointer to is_submodule_populated_gently
		 * to prevent die()-ing. We'll use connect_work_tree_and_git_dir
		 * to fixup the submodule in the force case later.
		 */
		error_code_ptr = &error_code;
	else
		error_code_ptr = NULL;

	if (old_head && !is_submodule_populated_gently(path, error_code_ptr))
		return 0;

	sub = submodule_from_path(the_repository, &null_oid, path);

	if (!sub)
		BUG("could not get submodule information for '%s'", path);

	if (old_head && !(flags & SUBMODULE_MOVE_HEAD_FORCE)) {
		/* Check if the submodule has a dirty index. */
		if (submodule_has_dirty_index(sub))
			return error(_("submodule '%s' has dirty index"), path);
	}

	if (!(flags & SUBMODULE_MOVE_HEAD_DRY_RUN)) {
		if (old_head) {
			if (!submodule_uses_gitfile(path))
				absorb_git_dir_into_superproject(path,
					ABSORB_GITDIR_RECURSE_SUBMODULES);
		} else {
			char *gitdir = xstrfmt("%s/modules/%s",
				    get_git_common_dir(), sub->name);
			connect_work_tree_and_git_dir(path, gitdir, 0);
			free(gitdir);

			/* make sure the index is clean as well */
			submodule_reset_index(path);
		}

		if (old_head && (flags & SUBMODULE_MOVE_HEAD_FORCE)) {
			char *gitdir = xstrfmt("%s/modules/%s",
				    get_git_common_dir(), sub->name);
			connect_work_tree_and_git_dir(path, gitdir, 1);
			free(gitdir);
		}
	}

	prepare_submodule_repo_env(&cp.env_array);

	cp.git_cmd = 1;
	cp.no_stdin = 1;
	cp.dir = path;

	argv_array_pushf(&cp.args, "--super-prefix=%s%s/",
			get_super_prefix_or_empty(), path);
	argv_array_pushl(&cp.args, "read-tree", "--recurse-submodules", NULL);

	if (flags & SUBMODULE_MOVE_HEAD_DRY_RUN)
		argv_array_push(&cp.args, "-n");
	else
		argv_array_push(&cp.args, "-u");

	if (flags & SUBMODULE_MOVE_HEAD_FORCE)
		argv_array_push(&cp.args, "--reset");
	else
		argv_array_push(&cp.args, "-m");

	if (!(flags & SUBMODULE_MOVE_HEAD_FORCE))
		argv_array_push(&cp.args, old_head ? old_head : empty_tree_oid_hex());

	argv_array_push(&cp.args, new_head ? new_head : empty_tree_oid_hex());

	if (run_command(&cp)) {
		ret = error(_("Submodule '%s' could not be updated."), path);
		goto out;
	}

	if (!(flags & SUBMODULE_MOVE_HEAD_DRY_RUN)) {
		if (new_head) {
			child_process_init(&cp);
			/* also set the HEAD accordingly */
			cp.git_cmd = 1;
			cp.no_stdin = 1;
			cp.dir = path;

			prepare_submodule_repo_env(&cp.env_array);
			argv_array_pushl(&cp.args, "update-ref", "HEAD",
					 "--no-deref", new_head, NULL);

			if (run_command(&cp)) {
				ret = -1;
				goto out;
			}
		} else {
			struct strbuf sb = STRBUF_INIT;

			strbuf_addf(&sb, "%s/.git", path);
			unlink_or_warn(sb.buf);
			strbuf_release(&sb);

			if (is_empty_dir(path))
				rmdir_or_warn(path);

			submodule_unset_core_worktree(sub);
		}
	}
out:
	return ret;
}

/*
 * Embeds a single submodules git directory into the superprojects git dir,
 * non recursively.
 */
static void relocate_single_git_dir_into_superproject(const char *path)
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

	sub = submodule_from_path(the_repository, &null_oid, path);
	if (!sub)
		die(_("could not lookup name for submodule '%s'"), path);

	new_git_dir = git_path("modules/%s", sub->name);
	if (safe_create_leading_directories_const(new_git_dir) < 0)
		die(_("could not create directory '%s'"), new_git_dir);
	real_new_git_dir = real_pathdup(new_git_dir, 1);

	fprintf(stderr, _("Migrating git directory of '%s%s' from\n'%s' to\n'%s'\n"),
		get_super_prefix_or_empty(), path,
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
void absorb_git_dir_into_superproject(const char *path,
				      unsigned flags)
{
	int err_code;
	const char *sub_git_dir;
	struct strbuf gitdir = STRBUF_INIT;
	strbuf_addf(&gitdir, "%s/.git", path);
	sub_git_dir = resolve_gitdir_gently(gitdir.buf, &err_code);

	/* Not populated? */
	if (!sub_git_dir) {
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
		sub = submodule_from_path(the_repository, &null_oid, path);
		if (!sub)
			die(_("could not lookup name for submodule '%s'"), path);
		connect_work_tree_and_git_dir(path,
			git_path("modules/%s", sub->name), 0);
	} else {
		/* Is it already absorbed into the superprojects git dir? */
		char *real_sub_git_dir = real_pathdup(sub_git_dir, 1);
		char *real_common_git_dir = real_pathdup(get_git_common_dir(), 1);

		if (!starts_with(real_sub_git_dir, real_common_git_dir))
			relocate_single_git_dir_into_superproject(path);

		free(real_sub_git_dir);
		free(real_common_git_dir);
	}
	strbuf_release(&gitdir);

	if (flags & ABSORB_GITDIR_RECURSE_SUBMODULES) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf sb = STRBUF_INIT;

		if (flags & ~ABSORB_GITDIR_RECURSE_SUBMODULES)
			BUG("we don't know how to pass the flags down?");

		strbuf_addstr(&sb, get_super_prefix_or_empty());
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
		super_sub_len = strlen(super_sub);

		if (super_sub_len > cwd_len ||
		    strcmp(&cwd[cwd_len - super_sub_len], super_sub))
			BUG("returned path string doesn't match cwd?");

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

/*
 * Put the gitdir for a submodule (given relative to the main
 * repository worktree) into `buf`, or return -1 on error.
 */
int submodule_to_gitdir(struct strbuf *buf, const char *submodule)
{
	const struct submodule *sub;
	const char *git_dir;
	int ret = 0;

	strbuf_reset(buf);
	strbuf_addstr(buf, submodule);
	strbuf_complete(buf, '/');
	strbuf_addstr(buf, ".git");

	git_dir = read_gitfile(buf->buf);
	if (git_dir) {
		strbuf_reset(buf);
		strbuf_addstr(buf, git_dir);
	}
	if (!is_git_directory(buf->buf)) {
		sub = submodule_from_path(the_repository, &null_oid, submodule);
		if (!sub) {
			ret = -1;
			goto cleanup;
		}
		strbuf_reset(buf);
		strbuf_git_path(buf, "%s/%s", "modules", sub->name);
	}

cleanup:
	return ret;
}
