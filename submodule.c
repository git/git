
#include "cache.h"
#include "repository.h"
#include "config.h"
#include "submodule-config.h"
#include "submodule.h"
#include "dir.h"
#include "diff.h"
#include "cummit.h"
#include "revision.h"
#include "run-command.h"
#include "diffcore.h"
#include "refs.h"
#include "string-list.h"
#include "oid-array.h"
#include "strvec.h"
#include "blob.h"
#include "thread-utils.h"
#include "quote.h"
#include "remote.h"
#include "worktree.h"
#include "parse-options.h"
#include "object-store.h"
#include "cummit-reach.h"
#include "shallow.h"

static int config_update_recurse_submodules = RECURSE_SUBMODULES_OFF;
static int initialized_fetch_ref_tips;
static struct oid_array ref_tips_before_fetch;
static struct oid_array ref_tips_after_fetch;

/*
 * Check if the .butmodules file is unmerged. Parsing of the .butmodules file
 * will be disabled because we can't guess what might be configured in
 * .butmodules unless the user resolves the conflict.
 */
int is_butmodules_unmerged(struct index_state *istate)
{
	int pos = index_name_pos(istate, BUTMODULES_FILE, strlen(BUTMODULES_FILE));
	if (pos < 0) { /* .butmodules not found or isn't merged */
		pos = -1 - pos;
		if (istate->cache_nr > pos) {  /* there is a .butmodules */
			const struct cache_entry *ce = istate->cache[pos];
			if (ce_namelen(ce) == strlen(BUTMODULES_FILE) &&
			    !strcmp(ce->name, BUTMODULES_FILE))
				return 1;
		}
	}

	return 0;
}

/*
 * Check if the .butmodules file is safe to write.
 *
 * Writing to the .butmodules file requires that the file exists in the
 * working tree or, if it doesn't, that a brand new .butmodules file is going
 * to be created (i.e. it's neither in the index nor in the current branch).
 *
 * It is not safe to write to .butmodules if it's not in the working tree but
 * it is in the index or in the current branch, because writing new values
 * (and staging them) would blindly overwrite ALL the old content.
 */
int is_writing_butmodules_ok(void)
{
	struct object_id oid;
	return file_exists(BUTMODULES_FILE) ||
		(get_oid(BUTMODULES_INDEX, &oid) < 0 && get_oid(BUTMODULES_HEAD, &oid) < 0);
}

/*
 * Check if the .butmodules file has unstaged modifications.  This must be
 * checked before allowing modifications to the .butmodules file with the
 * intention to stage them later, because when continuing we would stage the
 * modifications the user didn't stage herself too. That might change in a
 * future version when we learn to stage the changes we do ourselves without
 * staging any previous modifications.
 */
int is_staging_butmodules_ok(struct index_state *istate)
{
	int pos = index_name_pos(istate, BUTMODULES_FILE, strlen(BUTMODULES_FILE));

	if ((pos >= 0) && (pos < istate->cache_nr)) {
		struct stat st;
		if (lstat(BUTMODULES_FILE, &st) == 0 &&
		    ie_modified(istate, istate->cache[pos], &st, 0) & DATA_CHANGED)
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
 * .butmodules file. Return 0 only if a .butmodules file was found, a section
 * with the correct path=<oldpath> setting was found and we could update it.
 */
int update_path_in_butmodules(const char *oldpath, const char *newpath)
{
	struct strbuf entry = STRBUF_INIT;
	const struct submodule *submodule;
	int ret;

	if (!file_exists(BUTMODULES_FILE)) /* Do nothing without .butmodules */
		return -1;

	if (is_butmodules_unmerged(the_repository->index))
		die(_("Cannot change unmerged .butmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(the_repository, null_oid(), oldpath);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .butmodules where path=%s"), oldpath);
		return -1;
	}
	strbuf_addstr(&entry, "submodule.");
	strbuf_addstr(&entry, submodule->name);
	strbuf_addstr(&entry, ".path");
	ret = config_set_in_butmodules_file_gently(entry.buf, newpath);
	strbuf_release(&entry);
	return ret;
}

/*
 * Try to remove the "submodule.<name>" section from .butmodules where the given
 * path is configured. Return 0 only if a .butmodules file was found, a section
 * with the correct path=<path> setting was found and we could remove it.
 */
int remove_path_from_butmodules(const char *path)
{
	struct strbuf sect = STRBUF_INIT;
	const struct submodule *submodule;

	if (!file_exists(BUTMODULES_FILE)) /* Do nothing without .butmodules */
		return -1;

	if (is_butmodules_unmerged(the_repository->index))
		die(_("Cannot change unmerged .butmodules, resolve merge conflicts first"));

	submodule = submodule_from_path(the_repository, null_oid(), path);
	if (!submodule || !submodule->name) {
		warning(_("Could not find section in .butmodules where path=%s"), path);
		return -1;
	}
	strbuf_addstr(&sect, "submodule.");
	strbuf_addstr(&sect, submodule->name);
	if (but_config_rename_section_in_file(BUTMODULES_FILE, sect.buf, NULL) < 0) {
		/* Maybe the user already did that, don't error out here */
		warning(_("Could not remove .butmodules entry for %s"), path);
		strbuf_release(&sect);
		return -1;
	}
	strbuf_release(&sect);
	return 0;
}

void stage_updated_butmodules(struct index_state *istate)
{
	if (add_file_to_index(istate, BUTMODULES_FILE, 0))
		die(_("staging updated .butmodules failed"));
}

static struct string_list added_submodule_odb_paths = STRING_LIST_INIT_NODUP;

void add_submodule_odb_by_path(const char *path)
{
	string_list_insert(&added_submodule_odb_paths, xstrdup(path));
}

int register_all_submodule_odb_as_alternates(void)
{
	int i;
	int ret = added_submodule_odb_paths.nr;

	for (i = 0; i < added_submodule_odb_paths.nr; i++)
		add_to_alternates_memory(added_submodule_odb_paths.items[i].string);
	if (ret) {
		string_list_clear(&added_submodule_odb_paths, 0);
		trace2_data_intmax("submodule", the_repository,
				   "register_all_submodule_odb_as_alternates/registered", ret);
		if (but_env_bool("BUT_TEST_FATAL_REGISTER_SUBMODULE_ODB", 0))
			BUG("register_all_submodule_odb_as_alternates() called");
	}
	return ret;
}

void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
					     const char *path)
{
	const struct submodule *submodule = submodule_from_path(the_repository,
								null_oid(),
								path);
	if (submodule) {
		const char *ignore;
		char *key;

		key = xstrfmt("submodule.%s.ignore", submodule->name);
		if (repo_config_get_string_tmp(the_repository, key, &ignore))
			ignore = submodule->ignore;
		free(key);

		if (ignore)
			handle_ignore_submodules_arg(diffopt, ignore);
		else if (is_butmodules_unmerged(the_repository->index))
			diffopt->flags.ignore_submodules = 1;
	}
}

/* Cheap function that only determines if we're interested in submodules at all */
int but_default_submodule_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "submodule.recurse")) {
		int v = but_config_bool(var, value) ?
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
/*
 * NEEDSWORK: Emit a warning if submodule.active exists, but is valueless,
 * ie, the config looks like: "[submodule] active\n".
 * Since that is an invalid pathspec, we should inform the user.
 */
int is_tree_submodule_active(struct repository *repo,
			     const struct object_id *treeish_name,
			     const char *path)
{
	int ret = 0;
	char *key = NULL;
	char *value = NULL;
	const struct string_list *sl;
	const struct submodule *module;

	module = submodule_from_path(repo, treeish_name, path);

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
		struct strvec args = STRVEC_INIT;
		const struct string_list_item *item;

		for_each_string_list_item(item, sl) {
			strvec_push(&args, item->string);
		}

		parse_pathspec(&ps, 0, 0, NULL, args.v);
		ret = match_pathspec(repo->index, &ps, path, strlen(path), 0, NULL, 1);

		strvec_clear(&args);
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

int is_submodule_active(struct repository *repo, const char *path)
{
	return is_tree_submodule_active(repo, null_oid(), path);
}

int is_submodule_populated_gently(const char *path, int *return_error_code)
{
	int ret = 0;
	char *butdir = xstrfmt("%s/.but", path);

	if (resolve_butdir_gently(butdir, return_error_code))
		ret = 1;

	free(butdir);
	return ret;
}

/*
 * Dies if the provided 'prefix' corresponds to an unpopulated submodule
 */
void die_in_unpopulated_submodule(struct index_state *istate,
				  const char *prefix)
{
	int i, prefixlen;

	if (!prefix)
		return;

	prefixlen = strlen(prefix);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		int ce_len = ce_namelen(ce);

		if (!S_ISBUTLINK(ce->ce_mode))
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
void die_path_inside_submodule(struct index_state *istate,
			       const struct pathspec *ps)
{
	int i, j;

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		int ce_len = ce_namelen(ce);

		if (!S_ISBUTLINK(ce->ce_mode))
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
	diffopt->flags.ignore_submodule_set = 1;
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
		die(_("bad --ignore-submodules argument: %s"), arg);
	/*
	 * Please update _but_status() in but-completion.bash when you
	 * add new options
	 */
}

static int prepare_submodule_diff_summary(struct repository *r, struct rev_info *rev,
					  const char *path,
					  struct cummit *left, struct cummit *right,
					  struct cummit_list *merge_bases)
{
	struct cummit_list *list;

	repo_init_revisions(r, rev, NULL);
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

static void print_submodule_diff_summary(struct repository *r, struct rev_info *rev, struct diff_options *o)
{
	static const char format[] = "  %m %s";
	struct strbuf sb = STRBUF_INIT;
	struct cummit *cummit;

	while ((cummit = get_revision(rev))) {
		struct pretty_print_context ctx = {0};
		ctx.date_mode = rev->date_mode;
		ctx.output_encoding = get_log_output_encoding();
		strbuf_setlen(&sb, 0);
		repo_format_cummit_message(r, cummit, format, &sb,
				      &ctx);
		strbuf_addch(&sb, '\n');
		if (cummit->object.flags & SYMMETRIC_LEFT)
			diff_emit_submodule_del(o, sb.buf);
		else
			diff_emit_submodule_add(o, sb.buf);
	}
	strbuf_release(&sb);
}

void prepare_submodule_repo_env(struct strvec *out)
{
	prepare_other_repo_env(out, DEFAULT_BUT_DIR_ENVIRONMENT);
}

static void prepare_submodule_repo_env_in_butdir(struct strvec *out)
{
	prepare_other_repo_env(out, ".");
}

/*
 * Initialize a repository struct for a submodule based on the provided 'path'.
 *
 * Returns the repository struct on success,
 * NULL when the submodule is not present.
 */
static struct repository *open_submodule(const char *path)
{
	struct strbuf sb = STRBUF_INIT;
	struct repository *out = xmalloc(sizeof(*out));

	if (submodule_to_butdir(&sb, path) || repo_init(out, sb.buf, NULL)) {
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
 * If it can locate the submodule but directory it will create a repository
 * handle for the submodule and lookup both the left and right cummits and
 * put them into the left and right pointers.
 */
static void show_submodule_header(struct diff_options *o,
		const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule,
		struct repository *sub,
		struct cummit **left, struct cummit **right,
		struct cummit_list **merge_bases)
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
			message = "(cummits not present)";
		goto output_header;
	}

	/*
	 * Attempt to lookup the cummit references, and determine if this is
	 * a fast forward or fast backwards update.
	 */
	*left = lookup_cummit_reference(sub, one);
	*right = lookup_cummit_reference(sub, two);

	/*
	 * Warn about missing cummits in the submodule project, but only if
	 * they aren't null.
	 */
	if ((!is_null_oid(one) && !*left) ||
	     (!is_null_oid(two) && !*right))
		message = "(cummits not present)";

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

void show_submodule_diff_summary(struct diff_options *o, const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule)
{
	struct rev_info rev;
	struct cummit *left = NULL, *right = NULL;
	struct cummit_list *merge_bases = NULL;
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

	/* Treat revision walker failure the same as missing cummits */
	if (prepare_submodule_diff_summary(sub, &rev, path, left, right, merge_bases)) {
		diff_emit_submodule_error(o, "(revision walker failed)\n");
		goto out;
	}

	print_submodule_diff_summary(sub, &rev, o);

out:
	if (merge_bases)
		free_cummit_list(merge_bases);
	clear_cummit_marks(left, ~0);
	clear_cummit_marks(right, ~0);
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
	struct cummit *left = NULL, *right = NULL;
	struct cummit_list *merge_bases = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;
	struct repository *sub;

	sub = open_submodule(path);
	show_submodule_header(o, path, one, two, dirty_submodule,
			      sub, &left, &right, &merge_bases);

	/* We need a valid left and right cummit to display a difference */
	if (!(left || is_null_oid(one)) ||
	    !(right || is_null_oid(two)))
		goto done;

	if (left)
		old_oid = one;
	if (right)
		new_oid = two;

	cp.but_cmd = 1;
	cp.dir = path;
	cp.out = -1;
	cp.no_stdin = 1;

	/* TODO: other options may need to be passed here. */
	strvec_pushl(&cp.args, "diff", "--submodule=diff", NULL);
	strvec_pushf(&cp.args, "--color=%s", want_color(o->use_color) ?
			 "always" : "never");

	if (o->flags.reverse_diff) {
		strvec_pushf(&cp.args, "--src-prefix=%s%s/",
			     o->b_prefix, path);
		strvec_pushf(&cp.args, "--dst-prefix=%s%s/",
			     o->a_prefix, path);
	} else {
		strvec_pushf(&cp.args, "--src-prefix=%s%s/",
			     o->a_prefix, path);
		strvec_pushf(&cp.args, "--dst-prefix=%s%s/",
			     o->b_prefix, path);
	}
	strvec_push(&cp.args, oid_to_hex(old_oid));
	/*
	 * If the submodule has modified content, we will diff against the
	 * work tree, under the assumption that the user has asked for the
	 * diff format and wishes to actually see all differences even if they
	 * haven't yet been cummitted to the submodule yet.
	 */
	if (!(dirty_submodule & DIRTY_SUBMODULE_MODIFIED))
		strvec_push(&cp.args, oid_to_hex(new_oid));

	prepare_submodule_repo_env(&cp.env_array);

	if (!is_directory(path)) {
		/* fall back to absorbed but dir, if any */
		if (!sub)
			goto done;
		cp.dir = sub->butdir;
		strvec_push(&cp.env_array, BUT_DIR_ENVIRONMENT "=.");
		strvec_push(&cp.env_array, BUT_WORK_TREE_ENVIRONMENT "=.");
	}

	if (start_command(&cp)) {
		diff_emit_submodule_error(o, "(diff failed)\n");
		goto done;
	}

	while (strbuf_getwholeline_fd(&sb, cp.out, '\n') != EOF)
		diff_emit_submodule_pipethrough(o, sb.buf, sb.len);

	if (finish_command(&cp))
		diff_emit_submodule_error(o, "(diff failed)\n");

done:
	strbuf_release(&sb);
	if (merge_bases)
		free_cummit_list(merge_bases);
	if (left)
		clear_cummit_marks(left, ~0);
	if (right)
		clear_cummit_marks(right, ~0);
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
	if (!S_ISBUTLINK(ce->ce_mode))
		return NULL;

	if (!should_update_submodules())
		return NULL;

	return submodule_from_path(the_repository, null_oid(), ce->name);
}


struct collect_changed_submodules_cb_data {
	struct repository *repo;
	struct string_list *changed;
	const struct object_id *cummit_oid;
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

/*
 * Holds relevant information for a changed submodule. Used as the .util
 * member of the changed submodule name string_list_item.
 *
 * (super_oid, path) allows the submodule config to be read from _some_
 * .butmodules file. We store this information the first time we find a
 * superproject cummit that points to the submodule, but this is
 * arbitrary - we can choose any (super_oid, path) that matches the
 * submodule's name.
 *
 * NEEDSWORK: Storing an arbitrary cummit is undesirable because we can't
 * guarantee that we're reading the cummit that the user would expect. A better
 * scheme would be to just fetch a submodule by its name. This requires two
 * steps:
 * - Create a function that behaves like repo_submodule_init(), but accepts a
 *   submodule name instead of treeish_name and path. This should be easy
 *   because repo_submodule_init() internally uses the submodule's name.
 *
 * - Replace most instances of 'struct submodule' (which is the .butmodules
 *   config) with just the submodule name. This is OK because we expect
 *   submodule settings to be stored in .but/config (via "but submodule init"),
 *   not .butmodules. This also lets us delete get_non_butmodules_submodule(),
 *   which constructs a bogus 'struct submodule' for the sake of giving a
 *   placeholder name to a butlink.
 */
struct changed_submodule_data {
	/*
	 * The first superproject cummit in the rev walk that points to
	 * the submodule.
	 */
	const struct object_id *super_oid;
	/*
	 * Path to the submodule in the superproject cummit referenced
	 * by 'super_oid'.
	 */
	char *path;
	/* The submodule cummits that have changed in the rev walk. */
	struct oid_array new_cummits;
};

static void changed_submodule_data_clear(struct changed_submodule_data *cs_data)
{
	oid_array_clear(&cs_data->new_cummits);
	free(cs_data->path);
}

static void collect_changed_submodules_cb(struct diff_queue_struct *q,
					  struct diff_options *options,
					  void *data)
{
	struct collect_changed_submodules_cb_data *me = data;
	struct string_list *changed = me->changed;
	const struct object_id *cummit_oid = me->cummit_oid;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const struct submodule *submodule;
		const char *name;
		struct string_list_item *item;
		struct changed_submodule_data *cs_data;

		if (!S_ISBUTLINK(p->two->mode))
			continue;

		submodule = submodule_from_path(me->repo,
						cummit_oid, p->two->path);
		if (submodule)
			name = submodule->name;
		else {
			name = default_name_or_path(p->two->path);
			/* make sure name does not collide with existing one */
			if (name)
				submodule = submodule_from_name(me->repo,
								cummit_oid, name);
			if (submodule) {
				warning(_("Submodule in cummit %s at path: "
					"'%s' collides with a submodule named "
					"the same. Skipping it."),
					oid_to_hex(cummit_oid), p->two->path);
				name = NULL;
			}
		}

		if (!name)
			continue;

		item = string_list_insert(changed, name);
		if (item->util)
			cs_data = item->util;
		else {
			item->util = xcalloc(1, sizeof(struct changed_submodule_data));
			cs_data = item->util;
			cs_data->super_oid = cummit_oid;
			cs_data->path = xstrdup(p->two->path);
		}
		oid_array_append(&cs_data->new_cummits, &p->two->oid);
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
				       struct strvec *argv)
{
	struct rev_info rev;
	const struct cummit *cummit;
	int save_warning;
	struct setup_revision_opt s_r_opt = {
		.assume_dashdash = 1,
	};

	save_warning = warn_on_object_refname_ambiguity;
	warn_on_object_refname_ambiguity = 0;
	repo_init_revisions(r, &rev, NULL);
	setup_revisions(argv->nr, argv->v, &rev, &s_r_opt);
	warn_on_object_refname_ambiguity = save_warning;
	if (prepare_revision_walk(&rev))
		die(_("revision walk setup failed"));

	while ((cummit = get_revision(&rev))) {
		struct rev_info diff_rev;
		struct collect_changed_submodules_cb_data data;
		data.repo = r;
		data.changed = changed;
		data.cummit_oid = &cummit->object.oid;

		repo_init_revisions(r, &diff_rev, NULL);
		diff_rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
		diff_rev.diffopt.format_callback = collect_changed_submodules_cb;
		diff_rev.diffopt.format_callback_data = &data;
		diff_rev.dense_combined_merges = 1;
		diff_tree_combined_merge(cummit, &diff_rev);
	}

	reset_revision_walk();
}

static void free_submodules_data(struct string_list *submodules)
{
	struct string_list_item *item;
	for_each_string_list_item(item, submodules)
		changed_submodule_data_clear(item->util);

	string_list_clear(submodules, 1);
}

static int has_remote(const char *refname, const struct object_id *oid,
		      int flags, void *cb_data)
{
	return 1;
}

static int append_oid_to_argv(const struct object_id *oid, void *data)
{
	struct strvec *argv = data;
	strvec_push(argv, oid_to_hex(oid));
	return 0;
}

struct has_cummit_data {
	struct repository *repo;
	int result;
	const char *path;
	const struct object_id *super_oid;
};

static int check_has_cummit(const struct object_id *oid, void *data)
{
	struct has_cummit_data *cb = data;
	struct repository subrepo;
	enum object_type type;

	if (repo_submodule_init(&subrepo, cb->repo, cb->path, cb->super_oid)) {
		cb->result = 0;
		/* subrepo failed to init, so don't clean it up. */
		return 0;
	}

	type = oid_object_info(&subrepo, oid, NULL);

	switch (type) {
	case OBJ_CUMMIT:
		goto cleanup;
	case OBJ_BAD:
		/*
		 * Object is missing or invalid. If invalid, an error message
		 * has already been printed.
		 */
		cb->result = 0;
		goto cleanup;
	default:
		die(_("submodule entry '%s' (%s) is a %s, not a cummit"),
		    cb->path, oid_to_hex(oid), type_name(type));
	}
cleanup:
	repo_clear(&subrepo);
	return 0;
}

static int submodule_has_cummits(struct repository *r,
				 const char *path,
				 const struct object_id *super_oid,
				 struct oid_array *cummits)
{
	struct has_cummit_data has_cummit = {
		.repo = r,
		.result = 1,
		.path = path,
		.super_oid = super_oid
	};

	oid_array_for_each_unique(cummits, check_has_cummit, &has_cummit);

	if (has_cummit.result) {
		/*
		 * Even if the submodule is checked out and the cummit is
		 * present, make sure it exists in the submodule's object store
		 * and that it is reachable from a ref.
		 */
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;

		strvec_pushl(&cp.args, "rev-list", "-n", "1", NULL);
		oid_array_for_each_unique(cummits, append_oid_to_argv, &cp.args);
		strvec_pushl(&cp.args, "--not", "--all", NULL);

		prepare_submodule_repo_env(&cp.env_array);
		cp.but_cmd = 1;
		cp.no_stdin = 1;
		cp.dir = path;

		if (capture_command(&cp, &out, BUT_MAX_HEXSZ + 1) || out.len)
			has_cummit.result = 0;

		strbuf_release(&out);
	}

	return has_cummit.result;
}

static int submodule_needs_pushing(struct repository *r,
				   const char *path,
				   struct oid_array *cummits)
{
	if (!submodule_has_cummits(r, path, null_oid(), cummits))
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

		strvec_push(&cp.args, "rev-list");
		oid_array_for_each_unique(cummits, append_oid_to_argv, &cp.args);
		strvec_pushl(&cp.args, "--not", "--remotes", "-n", "1" , NULL);

		prepare_submodule_repo_env(&cp.env_array);
		cp.but_cmd = 1;
		cp.no_stdin = 1;
		cp.out = -1;
		cp.dir = path;
		if (start_command(&cp))
			die(_("Could not run 'but rev-list <cummits> --not --remotes -n 1' command in submodule %s"),
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
			     struct oid_array *cummits,
			     const char *remotes_name,
			     struct string_list *needs_pushing)
{
	struct string_list submodules = STRING_LIST_INIT_DUP;
	struct string_list_item *name;
	struct strvec argv = STRVEC_INIT;

	/* argv.v[0] will be ignored by setup_revisions */
	strvec_push(&argv, "find_unpushed_submodules");
	oid_array_for_each_unique(cummits, append_oid_to_argv, &argv);
	strvec_push(&argv, "--not");
	strvec_pushf(&argv, "--remotes=%s", remotes_name);

	collect_changed_submodules(r, &submodules, &argv);

	for_each_string_list_item(name, &submodules) {
		struct changed_submodule_data *cs_data = name->util;
		const struct submodule *submodule;
		const char *path = NULL;

		submodule = submodule_from_name(r, null_oid(), name->string);
		if (submodule)
			path = submodule->path;
		else
			path = default_name_or_path(name->string);

		if (!path)
			continue;

		if (submodule_needs_pushing(r, path, &cs_data->new_cummits))
			string_list_insert(needs_pushing, path);
	}

	free_submodules_data(&submodules);
	strvec_clear(&argv);

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
		strvec_push(&cp.args, "push");
		if (dry_run)
			strvec_push(&cp.args, "--dry-run");

		if (push_options && push_options->nr) {
			const struct string_list_item *item;
			for_each_string_list_item(item, push_options)
				strvec_pushf(&cp.args, "--push-option=%s",
					     item->string);
		}

		if (remote->origin != REMOTE_UNCONFIGURED) {
			int i;
			strvec_push(&cp.args, remote->name);
			for (i = 0; i < rs->raw_nr; i++)
				strvec_push(&cp.args, rs->raw[i]);
		}

		prepare_submodule_repo_env(&cp.env_array);
		cp.but_cmd = 1;
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

	strvec_push(&cp.args, "submodule--helper");
	strvec_push(&cp.args, "push-check");
	strvec_push(&cp.args, head);
	strvec_push(&cp.args, remote->name);

	for (i = 0; i < rs->raw_nr; i++)
		strvec_push(&cp.args, rs->raw[i]);

	prepare_submodule_repo_env(&cp.env_array);
	cp.but_cmd = 1;
	cp.no_stdin = 1;
	cp.no_stdout = 1;
	cp.dir = path;

	/*
	 * Simply indicate if 'submodule--helper push-check' failed.
	 * More detailed error information will be provided by the
	 * child process.
	 */
	if (run_command(&cp))
		die(_("process for submodule '%s' failed"), path);
}

int push_unpushed_submodules(struct repository *r,
			     struct oid_array *cummits,
			     const struct remote *remote,
			     const struct refspec *rs,
			     const struct string_list *push_options,
			     int dry_run)
{
	int i, ret = 1;
	struct string_list needs_pushing = STRING_LIST_INIT_DUP;

	if (!find_unpushed_submodules(r, cummits,
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
		fprintf(stderr, _("Pushing submodule '%s'\n"), path);
		if (!push_submodule(path, remote, rs,
				    push_options, dry_run)) {
			fprintf(stderr, _("Unable to push submodule '%s'\n"), path);
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

void check_for_new_submodule_cummits(struct object_id *oid)
{
	if (!initialized_fetch_ref_tips) {
		for_each_ref(append_oid_to_array, &ref_tips_before_fetch);
		initialized_fetch_ref_tips = 1;
	}

	oid_array_append(&ref_tips_after_fetch, oid);
}

/*
 * Returns 1 if there is at least one submodule butdir in
 * $BUT_DIR/modules and 0 otherwise. This follows
 * submodule_name_to_butdir(), which looks for submodules in
 * $BUT_DIR/modules, not $BUT_COMMON_DIR.
 *
 * A submodule can be moved to $BUT_DIR/modules manually by running "but
 * submodule absorbbutdirs", or it may be initialized there by "but
 * submodule update".
 */
static int repo_has_absorbed_submodules(struct repository *r)
{
	int ret;
	struct strbuf buf = STRBUF_INIT;

	strbuf_repo_but_path(&buf, r, "modules/");
	ret = file_exists(buf.buf) && !is_empty_dir(buf.buf);
	strbuf_release(&buf);
	return ret;
}

static void calculate_changed_submodule_paths(struct repository *r,
		struct string_list *changed_submodule_names)
{
	struct strvec argv = STRVEC_INIT;
	struct string_list_item *name;

	/* No need to check if no submodules would be fetched */
	if (!submodule_from_path(r, NULL, NULL) &&
	    !repo_has_absorbed_submodules(r))
		return;

	strvec_push(&argv, "--"); /* argv[0] program name */
	oid_array_for_each_unique(&ref_tips_after_fetch,
				   append_oid_to_argv, &argv);
	strvec_push(&argv, "--not");
	oid_array_for_each_unique(&ref_tips_before_fetch,
				   append_oid_to_argv, &argv);

	/*
	 * Collect all submodules (whether checked out or not) for which new
	 * cummits have been recorded upstream in "changed_submodule_names".
	 */
	collect_changed_submodules(r, changed_submodule_names, &argv);

	for_each_string_list_item(name, changed_submodule_names) {
		struct changed_submodule_data *cs_data = name->util;
		const struct submodule *submodule;
		const char *path = NULL;

		submodule = submodule_from_name(r, null_oid(), name->string);
		if (submodule)
			path = submodule->path;
		else
			path = default_name_or_path(name->string);

		if (!path)
			continue;

		if (submodule_has_cummits(r, path, null_oid(), &cs_data->new_cummits)) {
			changed_submodule_data_clear(cs_data);
			*name->string = '\0';
		}
	}

	string_list_remove_empty_items(changed_submodule_names, 1);

	strvec_clear(&argv);
	oid_array_clear(&ref_tips_before_fetch);
	oid_array_clear(&ref_tips_after_fetch);
	initialized_fetch_ref_tips = 0;
}

int submodule_touches_in_range(struct repository *r,
			       struct object_id *excl_oid,
			       struct object_id *incl_oid)
{
	struct string_list subs = STRING_LIST_INIT_DUP;
	struct strvec args = STRVEC_INIT;
	int ret;

	/* No need to check if there are no submodules configured */
	if (!submodule_from_path(r, NULL, NULL))
		return 0;

	strvec_push(&args, "--"); /* args[0] program name */
	strvec_push(&args, oid_to_hex(incl_oid));
	if (!is_null_oid(excl_oid)) {
		strvec_push(&args, "--not");
		strvec_push(&args, oid_to_hex(excl_oid));
	}

	collect_changed_submodules(r, &subs, &args);
	ret = subs.nr;

	strvec_clear(&args);

	free_submodules_data(&subs);
	return ret;
}

struct submodule_parallel_fetch {
	/*
	 * The index of the last index entry processed by
	 * get_fetch_task_from_index().
	 */
	int index_count;
	/*
	 * The index of the last string_list entry processed by
	 * get_fetch_task_from_changed().
	 */
	int changed_count;
	struct strvec args;
	struct repository *r;
	const char *prefix;
	int command_line_option;
	int default_option;
	int quiet;
	int result;

	/*
	 * Names of submodules that have new cummits. Generated by
	 * walking the newly fetched superproject cummits.
	 */
	struct string_list changed_submodule_names;
	/*
	 * Names of submodules that have already been processed. Lets us
	 * avoid fetching the same submodule more than once.
	 */
	struct string_list seen_submodule_names;

	/* Pending fetches by OIDs */
	struct fetch_task **oid_fetch_tasks;
	int oid_fetch_tasks_nr, oid_fetch_tasks_alloc;

	struct strbuf submodules_with_errors;
};
#define SPF_INIT { \
	.args = STRVEC_INIT, \
	.changed_submodule_names = STRING_LIST_INIT_DUP, \
	.seen_submodule_names = STRING_LIST_INIT_DUP, \
	.submodules_with_errors = STRBUF_INIT, \
}

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
		if (!repo_config_get_string_tmp(spf->r, key, &value)) {
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
	const char *default_argv; /* The default fetch mode. */
	struct strvec but_args; /* Args for the child but process. */

	struct oid_array *cummits; /* Ensure these cummits are fetched */
};

/**
 * When a submodule is not defined in .butmodules, we cannot access it
 * via the regular submodule-config. Create a fake submodule, which we can
 * work on.
 */
static const struct submodule *get_non_butmodules_submodule(const char *path)
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

static void fetch_task_release(struct fetch_task *p)
{
	if (p->free_sub)
		free((void*)p->sub);
	p->free_sub = 0;
	p->sub = NULL;

	if (p->repo)
		repo_clear(p->repo);
	FREE_AND_NULL(p->repo);

	strvec_clear(&p->but_args);
}

static struct repository *get_submodule_repo_for(struct repository *r,
						 const char *path,
						 const struct object_id *treeish_name)
{
	struct repository *ret = xmalloc(sizeof(*ret));

	if (repo_submodule_init(ret, r, path, treeish_name)) {
		free(ret);
		return NULL;
	}

	return ret;
}

static struct fetch_task *fetch_task_create(struct submodule_parallel_fetch *spf,
					    const char *path,
					    const struct object_id *treeish_name)
{
	struct fetch_task *task = xmalloc(sizeof(*task));
	memset(task, 0, sizeof(*task));

	task->sub = submodule_from_path(spf->r, treeish_name, path);

	if (!task->sub) {
		/*
		 * No entry in .butmodules? Technically not a submodule,
		 * but historically we supported repositories that happen to be
		 * in-place where a butlink is. Keep supporting them.
		 */
		task->sub = get_non_butmodules_submodule(path);
		if (!task->sub)
			goto cleanup;

		task->free_sub = 1;
	}

	if (string_list_lookup(&spf->seen_submodule_names, task->sub->name))
		goto cleanup;

	switch (get_fetch_recurse_config(task->sub, spf))
	{
	default:
	case RECURSE_SUBMODULES_DEFAULT:
	case RECURSE_SUBMODULES_ON_DEMAND:
		if (!task->sub ||
			!string_list_lookup(
				&spf->changed_submodule_names,
				task->sub->name))
			goto cleanup;
		task->default_argv = "on-demand";
		break;
	case RECURSE_SUBMODULES_ON:
		task->default_argv = "yes";
		break;
	case RECURSE_SUBMODULES_OFF:
		goto cleanup;
	}

	task->repo = get_submodule_repo_for(spf->r, path, treeish_name);

	return task;

 cleanup:
	fetch_task_release(task);
	free(task);
	return NULL;
}

static struct fetch_task *
get_fetch_task_from_index(struct submodule_parallel_fetch *spf,
			  struct strbuf *err)
{
	for (; spf->index_count < spf->r->index->cache_nr; spf->index_count++) {
		const struct cache_entry *ce =
			spf->r->index->cache[spf->index_count];
		struct fetch_task *task;

		if (!S_ISBUTLINK(ce->ce_mode))
			continue;

		task = fetch_task_create(spf, ce->name, null_oid());
		if (!task)
			continue;

		if (task->repo) {
			if (!spf->quiet)
				strbuf_addf(err, _("Fetching submodule %s%s\n"),
					    spf->prefix, ce->name);

			spf->index_count++;
			return task;
		} else {
			struct strbuf empty_submodule_path = STRBUF_INIT;

			fetch_task_release(task);
			free(task);

			/*
			 * An empty directory is normal,
			 * the submodule is not initialized
			 */
			strbuf_addf(&empty_submodule_path, "%s/%s/",
							spf->r->worktree,
							ce->name);
			if (S_ISBUTLINK(ce->ce_mode) &&
			    !is_empty_dir(empty_submodule_path.buf)) {
				spf->result = 1;
				strbuf_addf(err,
					    _("Could not access submodule '%s'\n"),
					    ce->name);
			}
			strbuf_release(&empty_submodule_path);
		}
	}
	return NULL;
}

static struct fetch_task *
get_fetch_task_from_changed(struct submodule_parallel_fetch *spf,
			    struct strbuf *err)
{
	for (; spf->changed_count < spf->changed_submodule_names.nr;
	     spf->changed_count++) {
		struct string_list_item item =
			spf->changed_submodule_names.items[spf->changed_count];
		struct changed_submodule_data *cs_data = item.util;
		struct fetch_task *task;

		if (!is_tree_submodule_active(spf->r, cs_data->super_oid,cs_data->path))
			continue;

		task = fetch_task_create(spf, cs_data->path,
					 cs_data->super_oid);
		if (!task)
			continue;

		if (!task->repo) {
			strbuf_addf(err, _("Could not access submodule '%s' at cummit %s\n"),
				    cs_data->path,
				    find_unique_abbrev(cs_data->super_oid, DEFAULT_ABBREV));

			fetch_task_release(task);
			free(task);
			continue;
		}

		if (!spf->quiet)
			strbuf_addf(err,
				    _("Fetching submodule %s%s at cummit %s\n"),
				    spf->prefix, task->sub->path,
				    find_unique_abbrev(cs_data->super_oid,
						       DEFAULT_ABBREV));

		spf->changed_count++;
		/*
		 * NEEDSWORK: Submodules set/unset a value for
		 * core.worktree when they are populated/unpopulated by
		 * "but checkout" (and similar commands, see
		 * submodule_move_head() and
		 * connect_work_tree_and_but_dir()), but if the
		 * submodule is unpopulated in another way (e.g. "but
		 * rm", "rm -r"), core.worktree will still be set even
		 * though the directory doesn't exist, and the child
		 * process will crash while trying to chdir into the
		 * nonexistent directory.
		 *
		 * In this case, we know that the submodule has no
		 * working tree, so we can work around this by
		 * setting "--work-tree=." (--bare does not work because
		 * worktree settings take precedence over bare-ness).
		 * However, this is not necessarily true in other cases,
		 * so a generalized solution is still necessary.
		 *
		 * Possible solutions:
		 * - teach "but [add|rm]" to unset core.worktree and
		 *   discourage users from removing submodules without
		 *   using a Git command.
		 * - teach submodule child processes to ignore stale
		 *   core.worktree values.
		 */
		strvec_push(&task->but_args, "--work-tree=.");
		return task;
	}
	return NULL;
}

static int get_next_submodule(struct child_process *cp, struct strbuf *err,
			      void *data, void **task_cb)
{
	struct submodule_parallel_fetch *spf = data;
	struct fetch_task *task =
		get_fetch_task_from_index(spf, err);
	if (!task)
		task = get_fetch_task_from_changed(spf, err);

	if (task) {
		struct strbuf submodule_prefix = STRBUF_INIT;

		child_process_init(cp);
		cp->dir = task->repo->butdir;
		prepare_submodule_repo_env_in_butdir(&cp->env_array);
		cp->but_cmd = 1;
		strvec_init(&cp->args);
		if (task->but_args.nr)
			strvec_pushv(&cp->args, task->but_args.v);
		strvec_pushv(&cp->args, spf->args.v);
		strvec_push(&cp->args, task->default_argv);
		strvec_push(&cp->args, "--submodule-prefix");

		strbuf_addf(&submodule_prefix, "%s%s/",
						spf->prefix,
						task->sub->path);
		strvec_push(&cp->args, submodule_prefix.buf);
		*task_cb = task;

		strbuf_release(&submodule_prefix);
		string_list_insert(&spf->seen_submodule_names, task->sub->name);
		return 1;
	}

	if (spf->oid_fetch_tasks_nr) {
		struct fetch_task *task =
			spf->oid_fetch_tasks[spf->oid_fetch_tasks_nr - 1];
		struct strbuf submodule_prefix = STRBUF_INIT;
		spf->oid_fetch_tasks_nr--;

		strbuf_addf(&submodule_prefix, "%s%s/",
			    spf->prefix, task->sub->path);

		child_process_init(cp);
		prepare_submodule_repo_env_in_butdir(&cp->env_array);
		cp->but_cmd = 1;
		cp->dir = task->repo->butdir;

		strvec_init(&cp->args);
		strvec_pushv(&cp->args, spf->args.v);
		strvec_push(&cp->args, "on-demand");
		strvec_push(&cp->args, "--submodule-prefix");
		strvec_push(&cp->args, submodule_prefix.buf);

		/* NEEDSWORK: have get_default_remote from submodule--helper */
		strvec_push(&cp->args, "origin");
		oid_array_for_each_unique(task->cummits,
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

static int cummit_missing_in_sub(const struct object_id *oid, void *data)
{
	struct repository *subrepo = data;

	enum object_type type = oid_object_info(subrepo, oid, NULL);

	return type != OBJ_CUMMIT;
}

static int fetch_finish(int retvalue, struct strbuf *err,
			void *cb, void *task_cb)
{
	struct submodule_parallel_fetch *spf = cb;
	struct fetch_task *task = task_cb;

	struct string_list_item *it;
	struct changed_submodule_data *cs_data;

	if (!task || !task->sub)
		BUG("callback cookie bogus");

	if (retvalue) {
		/*
		 * NEEDSWORK: This indicates that the overall fetch
		 * failed, even though there may be a subsequent fetch
		 * by commit hash that might work. It may be a good
		 * idea to not indicate failure in this case, and only
		 * indicate failure if the subsequent fetch fails.
		 */
		spf->result = 1;

		strbuf_addf(&spf->submodules_with_errors, "\t%s\n",
			    task->sub->name);
	}

	/* Is this the second time we process this submodule? */
	if (task->cummits)
		goto out;

	it = string_list_lookup(&spf->changed_submodule_names, task->sub->name);
	if (!it)
		/* Could be an unchanged submodule, not contained in the list */
		goto out;

	cs_data = it->util;
	oid_array_filter(&cs_data->new_cummits,
			 cummit_missing_in_sub,
			 task->repo);

	/* Are there cummits we want, but do not exist? */
	if (cs_data->new_cummits.nr) {
		task->cummits = &cs_data->new_cummits;
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

int fetch_submodules(struct repository *r,
		     const struct strvec *options,
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
		die(_("index file corrupt"));

	strvec_push(&spf.args, "fetch");
	for (i = 0; i < options->nr; i++)
		strvec_push(&spf.args, options->v[i]);
	strvec_push(&spf.args, "--recurse-submodules-default");
	/* default value, "--submodule-prefix" and its value are added later */

	calculate_changed_submodule_paths(r, &spf.changed_submodule_names);
	string_list_sort(&spf.changed_submodule_names);
	run_processes_parallel_tr2(max_parallel_jobs,
				   get_next_submodule,
				   fetch_start_failure,
				   fetch_finish,
				   &spf,
				   "submodule", "parallel/fetch");

	if (spf.submodules_with_errors.len > 0)
		fprintf(stderr, _("Errors during submodule fetch:\n%s"),
			spf.submodules_with_errors.buf);


	strvec_clear(&spf.args);
out:
	free_submodules_data(&spf.changed_submodule_names);
	return spf.result;
}

unsigned is_submodule_modified(const char *path, int ignore_untracked)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	FILE *fp;
	unsigned dirty_submodule = 0;
	const char *but_dir;
	int ignore_cp_exit_code = 0;

	strbuf_addf(&buf, "%s/.but", path);
	but_dir = read_butfile(buf.buf);
	if (!but_dir)
		but_dir = buf.buf;
	if (!is_but_directory(but_dir)) {
		if (is_directory(but_dir))
			die(_("'%s' not recognized as a but repository"), but_dir);
		strbuf_release(&buf);
		/* The submodule is not checked out, so it is not modified */
		return 0;
	}
	strbuf_reset(&buf);

	strvec_pushl(&cp.args, "status", "--porcelain=2", NULL);
	if (ignore_untracked)
		strvec_push(&cp.args, "-uno");

	prepare_submodule_repo_env(&cp.env_array);
	cp.but_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp))
		die(_("Could not run 'but status --porcelain=2' in submodule %s"), path);

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
		die(_("'but status --porcelain=2' failed in submodule %s"), path);

	strbuf_release(&buf);
	return dirty_submodule;
}

int submodule_uses_butfile(const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	const char *but_dir;

	strbuf_addf(&buf, "%s/.but", path);
	but_dir = read_butfile(buf.buf);
	if (!but_dir) {
		strbuf_release(&buf);
		return 0;
	}
	strbuf_release(&buf);

	/* Now test that all nested submodules use a butfile too */
	strvec_pushl(&cp.args,
		     "submodule", "foreach", "--quiet",	"--recursive",
		     "test -f .but", NULL);

	prepare_submodule_repo_env(&cp.env_array);
	cp.but_cmd = 1;
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

	if (!submodule_uses_butfile(path))
		return 1;

	strvec_pushl(&cp.args, "status", "--porcelain",
		     "--ignore-submodules=none", NULL);

	if (flags & SUBMODULE_REMOVAL_IGNORE_UNTRACKED)
		strvec_push(&cp.args, "-uno");
	else
		strvec_push(&cp.args, "-uall");

	if (!(flags & SUBMODULE_REMOVAL_IGNORE_IGNORED_UNTRACKED))
		strvec_push(&cp.args, "--ignored");

	prepare_submodule_repo_env(&cp.env_array);
	cp.but_cmd = 1;
	cp.no_stdin = 1;
	cp.out = -1;
	cp.dir = path;
	if (start_command(&cp)) {
		if (flags & SUBMODULE_REMOVAL_DIE_ON_ERROR)
			die(_("could not start 'but status' in submodule '%s'"),
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
			die(_("could not run 'but status' in submodule '%s'"),
				path);
		ret = -1;
	}
out:
	strbuf_release(&buf);
	return ret;
}

void submodule_unset_core_worktree(const struct submodule *sub)
{
	struct strbuf config_path = STRBUF_INIT;

	submodule_name_to_butdir(&config_path, the_repository, sub->name);
	strbuf_addstr(&config_path, "/config");

	if (but_config_set_in_file_gently(config_path.buf, "core.worktree", NULL))
		warning(_("Could not unset core.worktree setting in submodule '%s'"),
			  sub->path);

	strbuf_release(&config_path);
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

	cp.but_cmd = 1;
	strvec_pushl(&cp.args, "diff-index", "--quiet",
		     "--cached", "HEAD", NULL);
	cp.no_stdin = 1;
	cp.no_stdout = 1;
	cp.dir = sub->path;
	if (start_command(&cp))
		die(_("could not recurse into submodule '%s'"), sub->path);

	return finish_command(&cp);
}

static void submodule_reset_index(const char *path)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	prepare_submodule_repo_env(&cp.env_array);

	cp.but_cmd = 1;
	cp.no_stdin = 1;
	cp.dir = path;

	strvec_pushf(&cp.args, "--super-prefix=%s%s/",
		     get_super_prefix_or_empty(), path);
	/* TODO: determine if this might overwright untracked files */
	strvec_pushl(&cp.args, "read-tree", "-u", "--reset", NULL);

	strvec_push(&cp.args, empty_tree_oid_hex());

	if (run_command(&cp))
		die(_("could not reset submodule index"));
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
		 * to prevent die()-ing. We'll use connect_work_tree_and_but_dir
		 * to fixup the submodule in the force case later.
		 */
		error_code_ptr = &error_code;
	else
		error_code_ptr = NULL;

	if (old_head && !is_submodule_populated_gently(path, error_code_ptr))
		return 0;

	sub = submodule_from_path(the_repository, null_oid(), path);

	if (!sub)
		BUG("could not get submodule information for '%s'", path);

	if (old_head && !(flags & SUBMODULE_MOVE_HEAD_FORCE)) {
		/* Check if the submodule has a dirty index. */
		if (submodule_has_dirty_index(sub))
			return error(_("submodule '%s' has dirty index"), path);
	}

	if (!(flags & SUBMODULE_MOVE_HEAD_DRY_RUN)) {
		if (old_head) {
			if (!submodule_uses_butfile(path))
				absorb_but_dir_into_superproject(path,
					ABSORB_BUTDIR_RECURSE_SUBMODULES);
		} else {
			struct strbuf butdir = STRBUF_INIT;
			submodule_name_to_butdir(&butdir, the_repository,
						 sub->name);
			connect_work_tree_and_but_dir(path, butdir.buf, 0);
			strbuf_release(&butdir);

			/* make sure the index is clean as well */
			submodule_reset_index(path);
		}

		if (old_head && (flags & SUBMODULE_MOVE_HEAD_FORCE)) {
			struct strbuf butdir = STRBUF_INIT;
			submodule_name_to_butdir(&butdir, the_repository,
						 sub->name);
			connect_work_tree_and_but_dir(path, butdir.buf, 1);
			strbuf_release(&butdir);
		}
	}

	prepare_submodule_repo_env(&cp.env_array);

	cp.but_cmd = 1;
	cp.no_stdin = 1;
	cp.dir = path;

	strvec_pushf(&cp.args, "--super-prefix=%s%s/",
		     get_super_prefix_or_empty(), path);
	strvec_pushl(&cp.args, "read-tree", "--recurse-submodules", NULL);

	if (flags & SUBMODULE_MOVE_HEAD_DRY_RUN)
		strvec_push(&cp.args, "-n");
	else
		strvec_push(&cp.args, "-u");

	if (flags & SUBMODULE_MOVE_HEAD_FORCE)
		strvec_push(&cp.args, "--reset");
	else
		strvec_push(&cp.args, "-m");

	if (!(flags & SUBMODULE_MOVE_HEAD_FORCE))
		strvec_push(&cp.args, old_head ? old_head : empty_tree_oid_hex());

	strvec_push(&cp.args, new_head ? new_head : empty_tree_oid_hex());

	if (run_command(&cp)) {
		ret = error(_("Submodule '%s' could not be updated."), path);
		goto out;
	}

	if (!(flags & SUBMODULE_MOVE_HEAD_DRY_RUN)) {
		if (new_head) {
			child_process_init(&cp);
			/* also set the HEAD accordingly */
			cp.but_cmd = 1;
			cp.no_stdin = 1;
			cp.dir = path;

			prepare_submodule_repo_env(&cp.env_array);
			strvec_pushl(&cp.args, "update-ref", "HEAD",
				     "--no-deref", new_head, NULL);

			if (run_command(&cp)) {
				ret = -1;
				goto out;
			}
		} else {
			struct strbuf sb = STRBUF_INIT;

			strbuf_addf(&sb, "%s/.but", path);
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

int validate_submodule_but_dir(char *but_dir, const char *submodule_name)
{
	size_t len = strlen(but_dir), suffix_len = strlen(submodule_name);
	char *p;
	int ret = 0;

	if (len <= suffix_len || (p = but_dir + len - suffix_len)[-1] != '/' ||
	    strcmp(p, submodule_name))
		BUG("submodule name '%s' not a suffix of but dir '%s'",
		    submodule_name, but_dir);

	/*
	 * We prevent the contents of sibling submodules' but directories to
	 * clash.
	 *
	 * Example: having a submodule named `hippo` and another one named
	 * `hippo/hooks` would result in the but directories
	 * `.but/modules/hippo/` and `.but/modules/hippo/hooks/`, respectively,
	 * but the latter directory is already designated to contain the hooks
	 * of the former.
	 */
	for (; *p; p++) {
		if (is_dir_sep(*p)) {
			char c = *p;

			*p = '\0';
			if (is_but_directory(but_dir))
				ret = -1;
			*p = c;

			if (ret < 0)
				return error(_("submodule but dir '%s' is "
					       "inside but dir '%.*s'"),
					     but_dir,
					     (int)(p - but_dir), but_dir);
		}
	}

	return 0;
}

/*
 * Embeds a single submodules but directory into the superprojects but dir,
 * non recursively.
 */
static void relocate_single_but_dir_into_superproject(const char *path)
{
	char *old_but_dir = NULL, *real_old_but_dir = NULL, *real_new_but_dir = NULL;
	struct strbuf new_butdir = STRBUF_INIT;
	const struct submodule *sub;

	if (submodule_uses_worktrees(path))
		die(_("relocate_butdir for submodule '%s' with "
		      "more than one worktree not supported"), path);

	old_but_dir = xstrfmt("%s/.but", path);
	if (read_butfile(old_but_dir))
		/* If it is an actual butfile, it doesn't need migration. */
		return;

	real_old_but_dir = real_pathdup(old_but_dir, 1);

	sub = submodule_from_path(the_repository, null_oid(), path);
	if (!sub)
		die(_("could not lookup name for submodule '%s'"), path);

	submodule_name_to_butdir(&new_butdir, the_repository, sub->name);
	if (validate_submodule_but_dir(new_butdir.buf, sub->name) < 0)
		die(_("refusing to move '%s' into an existing but dir"),
		    real_old_but_dir);
	if (safe_create_leading_directories_const(new_butdir.buf) < 0)
		die(_("could not create directory '%s'"), new_butdir.buf);
	real_new_but_dir = real_pathdup(new_butdir.buf, 1);

	fprintf(stderr, _("Migrating but directory of '%s%s' from\n'%s' to\n'%s'\n"),
		get_super_prefix_or_empty(), path,
		real_old_but_dir, real_new_but_dir);

	relocate_butdir(path, real_old_but_dir, real_new_but_dir);

	free(old_but_dir);
	free(real_old_but_dir);
	free(real_new_but_dir);
	strbuf_release(&new_butdir);
}

/*
 * Migrate the but directory of the submodule given by path from
 * having its but directory within the working tree to the but dir nested
 * in its superprojects but dir under modules/.
 */
void absorb_but_dir_into_superproject(const char *path,
				      unsigned flags)
{
	int err_code;
	const char *sub_but_dir;
	struct strbuf butdir = STRBUF_INIT;
	strbuf_addf(&butdir, "%s/.but", path);
	sub_but_dir = resolve_butdir_gently(butdir.buf, &err_code);

	/* Not populated? */
	if (!sub_but_dir) {
		const struct submodule *sub;
		struct strbuf sub_butdir = STRBUF_INIT;

		if (err_code == READ_BUTFILE_ERR_STAT_FAILED) {
			/* unpopulated as expected */
			strbuf_release(&butdir);
			return;
		}

		if (err_code != READ_BUTFILE_ERR_NOT_A_REPO)
			/* We don't know what broke here. */
			read_butfile_error_die(err_code, path, NULL);

		/*
		* Maybe populated, but no but directory was found?
		* This can happen if the superproject is a submodule
		* itself and was just absorbed. The absorption of the
		* superproject did not rewrite the but file links yet,
		* fix it now.
		*/
		sub = submodule_from_path(the_repository, null_oid(), path);
		if (!sub)
			die(_("could not lookup name for submodule '%s'"), path);
		submodule_name_to_butdir(&sub_butdir, the_repository, sub->name);
		connect_work_tree_and_but_dir(path, sub_butdir.buf, 0);
		strbuf_release(&sub_butdir);
	} else {
		/* Is it already absorbed into the superprojects but dir? */
		char *real_sub_but_dir = real_pathdup(sub_but_dir, 1);
		char *real_common_but_dir = real_pathdup(get_but_common_dir(), 1);

		if (!starts_with(real_sub_but_dir, real_common_but_dir))
			relocate_single_but_dir_into_superproject(path);

		free(real_sub_but_dir);
		free(real_common_but_dir);
	}
	strbuf_release(&butdir);

	if (flags & ABSORB_BUTDIR_RECURSE_SUBMODULES) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf sb = STRBUF_INIT;

		if (flags & ~ABSORB_BUTDIR_RECURSE_SUBMODULES)
			BUG("we don't know how to pass the flags down?");

		strbuf_addstr(&sb, get_super_prefix_or_empty());
		strbuf_addstr(&sb, path);
		strbuf_addch(&sb, '/');

		cp.dir = path;
		cp.but_cmd = 1;
		cp.no_stdin = 1;
		strvec_pushl(&cp.args, "--super-prefix", sb.buf,
			     "submodule--helper",
			     "absorb-but-dirs", NULL);
		prepare_submodule_repo_env(&cp.env_array);
		if (run_command(&cp))
			die(_("could not recurse into submodule '%s'"), path);

		strbuf_release(&sb);
	}
}

int get_superproject_working_tree(struct strbuf *buf)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;
	struct strbuf one_up = STRBUF_INIT;
	const char *cwd = xgetcwd();
	int ret = 0;
	const char *subpath;
	int code;
	ssize_t len;

	if (!is_inside_work_tree())
		/*
		 * FIXME:
		 * We might have a superproject, but it is harder
		 * to determine.
		 */
		return 0;

	if (!strbuf_realpath(&one_up, "../", 0))
		return 0;

	subpath = relative_path(cwd, one_up.buf, &sb);
	strbuf_release(&one_up);

	prepare_submodule_repo_env(&cp.env_array);
	strvec_pop(&cp.env_array);

	strvec_pushl(&cp.args, "--literal-pathspecs", "-C", "..",
		     "ls-files", "-z", "--stage", "--full-name", "--",
		     subpath, NULL);
	strbuf_reset(&sb);

	cp.no_stdin = 1;
	cp.no_stderr = 1;
	cp.out = -1;
	cp.but_cmd = 1;

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

		strbuf_realpath(buf, super_wt, 1);
		ret = 1;
		free(super_wt);
	}
	strbuf_release(&sb);

	code = finish_command(&cp);

	if (code == 128)
		/* '../' is not a but repository */
		return 0;
	if (code == 0 && len == 0)
		/* There is an unrelated but repository at '../' */
		return 0;
	if (code)
		die(_("ls-tree returned unexpected return code %d"), code);

	return ret;
}

/*
 * Put the butdir for a submodule (given relative to the main
 * repository worktree) into `buf`, or return -1 on error.
 */
int submodule_to_butdir(struct strbuf *buf, const char *submodule)
{
	const struct submodule *sub;
	const char *but_dir;
	int ret = 0;

	strbuf_reset(buf);
	strbuf_addstr(buf, submodule);
	strbuf_complete(buf, '/');
	strbuf_addstr(buf, ".but");

	but_dir = read_butfile(buf->buf);
	if (but_dir) {
		strbuf_reset(buf);
		strbuf_addstr(buf, but_dir);
	}
	if (!is_but_directory(buf->buf)) {
		sub = submodule_from_path(the_repository, null_oid(),
					  submodule);
		if (!sub) {
			ret = -1;
			goto cleanup;
		}
		strbuf_reset(buf);
		submodule_name_to_butdir(buf, the_repository, sub->name);
	}

cleanup:
	return ret;
}

void submodule_name_to_butdir(struct strbuf *buf, struct repository *r,
			      const char *submodule_name)
{
	/*
	 * NEEDSWORK: The current way of mapping a submodule's name to
	 * its location in .but/modules/ has problems with some naming
	 * schemes. For example, if a submodule is named "foo" and
	 * another is named "foo/bar" (whether present in the same
	 * superproject cummit or not - the problem will arise if both
	 * superproject cummits have been checked out at any point in
	 * time), or if two submodule names only have different cases in
	 * a case-insensitive filesystem.
	 *
	 * There are several solutions, including encoding the path in
	 * some way, introducing a submodule.<name>.butdir config in
	 * .but/config (not .butmodules) that allows overriding what the
	 * butdir of a submodule would be (and teach Git, upon noticing
	 * a clash, to automatically determine a non-clashing name and
	 * to write such a config), or introducing a
	 * submodule.<name>.butdir config in .butmodules that repo
	 * administrators can explicitly set. Nothing has been decided,
	 * so for now, just append the name at the end of the path.
	 */
	strbuf_repo_but_path(buf, r, "modules/");
	strbuf_addstr(buf, submodule_name);
}
