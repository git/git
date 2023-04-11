#include "git-compat-util.h"
#include "alloc.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "repository.h"
#include "config.h"
#include "submodule-config.h"
#include "submodule.h"
#include "strbuf.h"
#include "object-name.h"
#include "object-store.h"
#include "parse-options.h"
#include "tree-walk.h"

/*
 * submodule cache lookup structure
 * There is one shared set of 'struct submodule' entries which can be
 * looked up by their sha1 blob id of the .gitmodules file and either
 * using path or name as key.
 * for_path stores submodule entries with path as key
 * for_name stores submodule entries with name as key
 */
struct submodule_cache {
	struct hashmap for_path;
	struct hashmap for_name;
	unsigned initialized:1;
	unsigned gitmodules_read:1;
};

/*
 * thin wrapper struct needed to insert 'struct submodule' entries to
 * the hashmap
 */
struct submodule_entry {
	struct hashmap_entry ent;
	struct submodule *config;
};

enum lookup_type {
	lookup_name,
	lookup_path
};

static int config_path_cmp(const void *cmp_data UNUSED,
			   const struct hashmap_entry *eptr,
			   const struct hashmap_entry *entry_or_key,
			   const void *keydata UNUSED)
{
	const struct submodule_entry *a, *b;

	a = container_of(eptr, const struct submodule_entry, ent);
	b = container_of(entry_or_key, const struct submodule_entry, ent);

	return strcmp(a->config->path, b->config->path) ||
	       !oideq(&a->config->gitmodules_oid, &b->config->gitmodules_oid);
}

static int config_name_cmp(const void *cmp_data UNUSED,
			   const struct hashmap_entry *eptr,
			   const struct hashmap_entry *entry_or_key,
			   const void *keydata UNUSED)
{
	const struct submodule_entry *a, *b;

	a = container_of(eptr, const struct submodule_entry, ent);
	b = container_of(entry_or_key, const struct submodule_entry, ent);

	return strcmp(a->config->name, b->config->name) ||
	       !oideq(&a->config->gitmodules_oid, &b->config->gitmodules_oid);
}

static struct submodule_cache *submodule_cache_alloc(void)
{
	return xcalloc(1, sizeof(struct submodule_cache));
}

static void submodule_cache_init(struct submodule_cache *cache)
{
	hashmap_init(&cache->for_path, config_path_cmp, NULL, 0);
	hashmap_init(&cache->for_name, config_name_cmp, NULL, 0);
	cache->initialized = 1;
}

static void free_one_config(struct submodule_entry *entry)
{
	free((void *) entry->config->path);
	free((void *) entry->config->name);
	free((void *) entry->config->branch);
	free((void *) entry->config->update_strategy.command);
	free(entry->config);
}

static void submodule_cache_clear(struct submodule_cache *cache)
{
	struct hashmap_iter iter;
	struct submodule_entry *entry;

	if (!cache->initialized)
		return;

	/*
	 * We iterate over the name hash here to be symmetric with the
	 * allocation of struct submodule entries. Each is allocated by
	 * their .gitmodules blob sha1 and submodule name.
	 */
	hashmap_for_each_entry(&cache->for_name, &iter, entry,
				ent /* member name */)
		free_one_config(entry);

	hashmap_clear_and_free(&cache->for_path, struct submodule_entry, ent);
	hashmap_clear_and_free(&cache->for_name, struct submodule_entry, ent);
	cache->initialized = 0;
	cache->gitmodules_read = 0;
}

void submodule_cache_free(struct submodule_cache *cache)
{
	submodule_cache_clear(cache);
	free(cache);
}

static unsigned int hash_oid_string(const struct object_id *oid,
				    const char *string)
{
	return memhash(oid->hash, the_hash_algo->rawsz) + strhash(string);
}

static void cache_put_path(struct submodule_cache *cache,
			   struct submodule *submodule)
{
	unsigned int hash = hash_oid_string(&submodule->gitmodules_oid,
					    submodule->path);
	struct submodule_entry *e = xmalloc(sizeof(*e));
	hashmap_entry_init(&e->ent, hash);
	e->config = submodule;
	hashmap_put(&cache->for_path, &e->ent);
}

static void cache_remove_path(struct submodule_cache *cache,
			      struct submodule *submodule)
{
	unsigned int hash = hash_oid_string(&submodule->gitmodules_oid,
					    submodule->path);
	struct submodule_entry e;
	struct submodule_entry *removed;
	hashmap_entry_init(&e.ent, hash);
	e.config = submodule;
	removed = hashmap_remove_entry(&cache->for_path, &e, ent, NULL);
	free(removed);
}

static void cache_add(struct submodule_cache *cache,
		      struct submodule *submodule)
{
	unsigned int hash = hash_oid_string(&submodule->gitmodules_oid,
					    submodule->name);
	struct submodule_entry *e = xmalloc(sizeof(*e));
	hashmap_entry_init(&e->ent, hash);
	e->config = submodule;
	hashmap_add(&cache->for_name, &e->ent);
}

static const struct submodule *cache_lookup_path(struct submodule_cache *cache,
		const struct object_id *gitmodules_oid, const char *path)
{
	struct submodule_entry *entry;
	unsigned int hash = hash_oid_string(gitmodules_oid, path);
	struct submodule_entry key;
	struct submodule key_config;

	oidcpy(&key_config.gitmodules_oid, gitmodules_oid);
	key_config.path = path;

	hashmap_entry_init(&key.ent, hash);
	key.config = &key_config;

	entry = hashmap_get_entry(&cache->for_path, &key, ent, NULL);
	if (entry)
		return entry->config;
	return NULL;
}

static struct submodule *cache_lookup_name(struct submodule_cache *cache,
		const struct object_id *gitmodules_oid, const char *name)
{
	struct submodule_entry *entry;
	unsigned int hash = hash_oid_string(gitmodules_oid, name);
	struct submodule_entry key;
	struct submodule key_config;

	oidcpy(&key_config.gitmodules_oid, gitmodules_oid);
	key_config.name = name;

	hashmap_entry_init(&key.ent, hash);
	key.config = &key_config;

	entry = hashmap_get_entry(&cache->for_name, &key, ent, NULL);
	if (entry)
		return entry->config;
	return NULL;
}

int check_submodule_name(const char *name)
{
	/* Disallow empty names */
	if (!*name)
		return -1;

	/*
	 * Look for '..' as a path component. Check is_xplatform_dir_sep() as
	 * separators rather than is_dir_sep(), because we want the name rules
	 * to be consistent across platforms.
	 */
	goto in_component; /* always start inside component */
	while (*name) {
		char c = *name++;
		if (is_xplatform_dir_sep(c)) {
in_component:
			if (name[0] == '.' && name[1] == '.' &&
			    (!name[2] || is_xplatform_dir_sep(name[2])))
				return -1;
		}
	}

	return 0;
}

static int name_and_item_from_var(const char *var, struct strbuf *name,
				  struct strbuf *item)
{
	const char *subsection, *key;
	size_t subsection_len;
	int parse;
	parse = parse_config_key(var, "submodule", &subsection,
			&subsection_len, &key);
	if (parse < 0 || !subsection)
		return 0;

	strbuf_add(name, subsection, subsection_len);
	if (check_submodule_name(name->buf) < 0) {
		warning(_("ignoring suspicious submodule name: %s"), name->buf);
		strbuf_release(name);
		return 0;
	}

	strbuf_addstr(item, key);

	return 1;
}

static struct submodule *lookup_or_create_by_name(struct submodule_cache *cache,
		const struct object_id *gitmodules_oid, const char *name)
{
	struct submodule *submodule;
	struct strbuf name_buf = STRBUF_INIT;

	submodule = cache_lookup_name(cache, gitmodules_oid, name);
	if (submodule)
		return submodule;

	submodule = xmalloc(sizeof(*submodule));

	strbuf_addstr(&name_buf, name);
	submodule->name = strbuf_detach(&name_buf, NULL);

	submodule->path = NULL;
	submodule->url = NULL;
	submodule->update_strategy.type = SM_UPDATE_UNSPECIFIED;
	submodule->update_strategy.command = NULL;
	submodule->fetch_recurse = RECURSE_SUBMODULES_NONE;
	submodule->ignore = NULL;
	submodule->branch = NULL;
	submodule->recommend_shallow = -1;

	oidcpy(&submodule->gitmodules_oid, gitmodules_oid);

	cache_add(cache, submodule);

	return submodule;
}

static int parse_fetch_recurse(const char *opt, const char *arg,
			       int die_on_error)
{
	switch (git_parse_maybe_bool(arg)) {
	case 1:
		return RECURSE_SUBMODULES_ON;
	case 0:
		return RECURSE_SUBMODULES_OFF;
	default:
		if (!strcmp(arg, "on-demand"))
			return RECURSE_SUBMODULES_ON_DEMAND;
		/*
		 * Please update $__git_fetch_recurse_submodules in
		 * git-completion.bash when you add new options.
		 */
		if (die_on_error)
			die("bad %s argument: %s", opt, arg);
		else
			return RECURSE_SUBMODULES_ERROR;
	}
}

int parse_submodule_fetchjobs(const char *var, const char *value)
{
	int fetchjobs = git_config_int(var, value);
	if (fetchjobs < 0)
		die(_("negative values not allowed for submodule.fetchJobs"));
	if (!fetchjobs)
		fetchjobs = online_cpus();
	return fetchjobs;
}

int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg)
{
	return parse_fetch_recurse(opt, arg, 1);
}

int option_fetch_parse_recurse_submodules(const struct option *opt,
					  const char *arg, int unset)
{
	int *v;

	if (!opt->value)
		return -1;

	v = opt->value;

	if (unset) {
		*v = RECURSE_SUBMODULES_OFF;
	} else {
		if (arg)
			*v = parse_fetch_recurse_submodules_arg(opt->long_name, arg);
		else
			*v = RECURSE_SUBMODULES_ON;
	}
	return 0;
}

static int parse_update_recurse(const char *opt, const char *arg,
				int die_on_error)
{
	switch (git_parse_maybe_bool(arg)) {
	case 1:
		return RECURSE_SUBMODULES_ON;
	case 0:
		return RECURSE_SUBMODULES_OFF;
	default:
		if (die_on_error)
			die("bad %s argument: %s", opt, arg);
		return RECURSE_SUBMODULES_ERROR;
	}
}

int parse_update_recurse_submodules_arg(const char *opt, const char *arg)
{
	return parse_update_recurse(opt, arg, 1);
}

static int parse_push_recurse(const char *opt, const char *arg,
			       int die_on_error)
{
	switch (git_parse_maybe_bool(arg)) {
	case 1:
		/* There's no simple "on" value when pushing */
		if (die_on_error)
			die("bad %s argument: %s", opt, arg);
		else
			return RECURSE_SUBMODULES_ERROR;
	case 0:
		return RECURSE_SUBMODULES_OFF;
	default:
		if (!strcmp(arg, "on-demand"))
			return RECURSE_SUBMODULES_ON_DEMAND;
		else if (!strcmp(arg, "check"))
			return RECURSE_SUBMODULES_CHECK;
		else if (!strcmp(arg, "only"))
			return RECURSE_SUBMODULES_ONLY;
		/*
		 * Please update $__git_push_recurse_submodules in
		 * git-completion.bash when you add new modes.
		 */
		else if (die_on_error)
			die("bad %s argument: %s", opt, arg);
		else
			return RECURSE_SUBMODULES_ERROR;
	}
}

int parse_push_recurse_submodules_arg(const char *opt, const char *arg)
{
	return parse_push_recurse(opt, arg, 1);
}

static void warn_multiple_config(const struct object_id *treeish_name,
				 const char *name, const char *option)
{
	const char *commit_string = "WORKTREE";
	if (treeish_name)
		commit_string = oid_to_hex(treeish_name);
	warning("%s:.gitmodules, multiple configurations found for "
			"'submodule.%s.%s'. Skipping second one!",
			commit_string, name, option);
}

static void warn_command_line_option(const char *var, const char *value)
{
	warning(_("ignoring '%s' which may be interpreted as"
		  " a command-line option: %s"), var, value);
}

struct parse_config_parameter {
	struct submodule_cache *cache;
	const struct object_id *treeish_name;
	const struct object_id *gitmodules_oid;
	int overwrite;
};

/*
 * Parse a config item from .gitmodules.
 *
 * This does not handle submodule-related configuration from the main
 * config store (.git/config, etc).  Callers are responsible for
 * checking for overrides in the main config store when appropriate.
 */
static int parse_config(const char *var, const char *value, void *data)
{
	struct parse_config_parameter *me = data;
	struct submodule *submodule;
	struct strbuf name = STRBUF_INIT, item = STRBUF_INIT;
	int ret = 0;

	/* this also ensures that we only parse submodule entries */
	if (!name_and_item_from_var(var, &name, &item))
		return 0;

	submodule = lookup_or_create_by_name(me->cache,
					     me->gitmodules_oid,
					     name.buf);

	if (!strcmp(item.buf, "path")) {
		if (!value)
			ret = config_error_nonbool(var);
		else if (looks_like_command_line_option(value))
			warn_command_line_option(var, value);
		else if (!me->overwrite && submodule->path)
			warn_multiple_config(me->treeish_name, submodule->name,
					"path");
		else {
			if (submodule->path)
				cache_remove_path(me->cache, submodule);
			free((void *) submodule->path);
			submodule->path = xstrdup(value);
			cache_put_path(me->cache, submodule);
		}
	} else if (!strcmp(item.buf, "fetchrecursesubmodules")) {
		/* when parsing worktree configurations we can die early */
		int die_on_error = is_null_oid(me->gitmodules_oid);
		if (!me->overwrite &&
		    submodule->fetch_recurse != RECURSE_SUBMODULES_NONE)
			warn_multiple_config(me->treeish_name, submodule->name,
					"fetchrecursesubmodules");
		else
			submodule->fetch_recurse = parse_fetch_recurse(
								var, value,
								die_on_error);
	} else if (!strcmp(item.buf, "ignore")) {
		if (!value)
			ret = config_error_nonbool(var);
		else if (!me->overwrite && submodule->ignore)
			warn_multiple_config(me->treeish_name, submodule->name,
					"ignore");
		else if (strcmp(value, "untracked") &&
			 strcmp(value, "dirty") &&
			 strcmp(value, "all") &&
			 strcmp(value, "none"))
			warning("Invalid parameter '%s' for config option "
					"'submodule.%s.ignore'", value, name.buf);
		else {
			free((void *) submodule->ignore);
			submodule->ignore = xstrdup(value);
		}
	} else if (!strcmp(item.buf, "url")) {
		if (!value) {
			ret = config_error_nonbool(var);
		} else if (looks_like_command_line_option(value)) {
			warn_command_line_option(var, value);
		} else if (!me->overwrite && submodule->url) {
			warn_multiple_config(me->treeish_name, submodule->name,
					"url");
		} else {
			free((void *) submodule->url);
			submodule->url = xstrdup(value);
		}
	} else if (!strcmp(item.buf, "update")) {
		if (!value)
			ret = config_error_nonbool(var);
		else if (!me->overwrite &&
			 submodule->update_strategy.type != SM_UPDATE_UNSPECIFIED)
			warn_multiple_config(me->treeish_name, submodule->name,
					     "update");
		else if (parse_submodule_update_strategy(value,
			 &submodule->update_strategy) < 0 ||
			 submodule->update_strategy.type == SM_UPDATE_COMMAND)
			die(_("invalid value for '%s'"), var);
	} else if (!strcmp(item.buf, "shallow")) {
		if (!me->overwrite && submodule->recommend_shallow != -1)
			warn_multiple_config(me->treeish_name, submodule->name,
					     "shallow");
		else
			submodule->recommend_shallow =
				git_config_bool(var, value);
	} else if (!strcmp(item.buf, "branch")) {
		if (!me->overwrite && submodule->branch)
			warn_multiple_config(me->treeish_name, submodule->name,
					     "branch");
		else {
			free((void *)submodule->branch);
			submodule->branch = xstrdup(value);
		}
	}

	strbuf_release(&name);
	strbuf_release(&item);

	return ret;
}

static int gitmodule_oid_from_commit(const struct object_id *treeish_name,
				     struct object_id *gitmodules_oid,
				     struct strbuf *rev)
{
	int ret = 0;

	if (is_null_oid(treeish_name)) {
		oidclr(gitmodules_oid);
		return 1;
	}

	strbuf_addf(rev, "%s:.gitmodules", oid_to_hex(treeish_name));
	if (repo_get_oid(the_repository, rev->buf, gitmodules_oid) >= 0)
		ret = 1;

	return ret;
}

/* This does a lookup of a submodule configuration by name or by path
 * (key) with on-demand reading of the appropriate .gitmodules from
 * revisions.
 */
static const struct submodule *config_from(struct submodule_cache *cache,
		const struct object_id *treeish_name, const char *key,
		enum lookup_type lookup_type)
{
	struct strbuf rev = STRBUF_INIT;
	unsigned long config_size;
	char *config = NULL;
	struct object_id oid;
	enum object_type type;
	const struct submodule *submodule = NULL;
	struct parse_config_parameter parameter;

	/*
	 * If any parameter except the cache is a NULL pointer just
	 * return the first submodule. Can be used to check whether
	 * there are any submodules parsed.
	 */
	if (!treeish_name || !key) {
		struct hashmap_iter iter;
		struct submodule_entry *entry;

		entry = hashmap_iter_first_entry(&cache->for_name, &iter,
						struct submodule_entry,
						ent /* member name */);
		if (!entry)
			return NULL;
		return entry->config;
	}

	if (!gitmodule_oid_from_commit(treeish_name, &oid, &rev))
		goto out;

	switch (lookup_type) {
	case lookup_name:
		submodule = cache_lookup_name(cache, &oid, key);
		break;
	case lookup_path:
		submodule = cache_lookup_path(cache, &oid, key);
		break;
	}
	if (submodule)
		goto out;

	config = repo_read_object_file(the_repository, &oid, &type,
				       &config_size);
	if (!config || type != OBJ_BLOB)
		goto out;

	/* fill the submodule config into the cache */
	parameter.cache = cache;
	parameter.treeish_name = treeish_name;
	parameter.gitmodules_oid = &oid;
	parameter.overwrite = 0;
	git_config_from_mem(parse_config, CONFIG_ORIGIN_SUBMODULE_BLOB, rev.buf,
			config, config_size, &parameter, NULL);
	strbuf_release(&rev);
	free(config);

	switch (lookup_type) {
	case lookup_name:
		return cache_lookup_name(cache, &oid, key);
	case lookup_path:
		return cache_lookup_path(cache, &oid, key);
	default:
		return NULL;
	}

out:
	strbuf_release(&rev);
	free(config);
	return submodule;
}

static void submodule_cache_check_init(struct repository *repo)
{
	if (repo->submodule_cache && repo->submodule_cache->initialized)
		return;

	if (!repo->submodule_cache)
		repo->submodule_cache = submodule_cache_alloc();

	submodule_cache_init(repo->submodule_cache);
}

/*
 * Note: This function is private for a reason, the '.gitmodules' file should
 * not be used as a mechanism to retrieve arbitrary configuration stored in
 * the repository.
 *
 * Runs the provided config function on the '.gitmodules' file found in the
 * working directory.
 */
static void config_from_gitmodules(config_fn_t fn, struct repository *repo, void *data)
{
	if (repo->worktree) {
		struct git_config_source config_source = {
			0, .scope = CONFIG_SCOPE_SUBMODULE
		};
		const struct config_options opts = { 0 };
		struct object_id oid;
		char *file;
		char *oidstr = NULL;

		file = repo_worktree_path(repo, GITMODULES_FILE);
		if (file_exists(file)) {
			config_source.file = file;
		} else if (repo_get_oid(repo, GITMODULES_INDEX, &oid) >= 0 ||
			   repo_get_oid(repo, GITMODULES_HEAD, &oid) >= 0) {
			config_source.repo = repo;
			config_source.blob = oidstr = xstrdup(oid_to_hex(&oid));
			if (repo != the_repository)
				add_submodule_odb_by_path(repo->objects->odb->path);
		} else {
			goto out;
		}

		config_with_options(fn, data, &config_source, &opts);

out:
		free(oidstr);
		free(file);
	}
}

static int gitmodules_cb(const char *var, const char *value, void *data)
{
	struct repository *repo = data;
	struct parse_config_parameter parameter;

	parameter.cache = repo->submodule_cache;
	parameter.treeish_name = NULL;
	parameter.gitmodules_oid = null_oid();
	parameter.overwrite = 1;

	return parse_config(var, value, &parameter);
}

void repo_read_gitmodules(struct repository *repo, int skip_if_read)
{
	submodule_cache_check_init(repo);

	if (repo->submodule_cache->gitmodules_read && skip_if_read)
		return;

	if (repo_read_index(repo) < 0)
		return;

	if (!is_gitmodules_unmerged(repo->index))
		config_from_gitmodules(gitmodules_cb, repo, repo);

	repo->submodule_cache->gitmodules_read = 1;
}

void gitmodules_config_oid(const struct object_id *commit_oid)
{
	struct strbuf rev = STRBUF_INIT;
	struct object_id oid;

	submodule_cache_check_init(the_repository);

	if (gitmodule_oid_from_commit(commit_oid, &oid, &rev)) {
		git_config_from_blob_oid(gitmodules_cb, rev.buf,
					 the_repository, &oid, the_repository);
	}
	strbuf_release(&rev);

	the_repository->submodule_cache->gitmodules_read = 1;
}

const struct submodule *submodule_from_name(struct repository *r,
					    const struct object_id *treeish_name,
		const char *name)
{
	repo_read_gitmodules(r, 1);
	return config_from(r->submodule_cache, treeish_name, name, lookup_name);
}

const struct submodule *submodule_from_path(struct repository *r,
					    const struct object_id *treeish_name,
		const char *path)
{
	repo_read_gitmodules(r, 1);
	return config_from(r->submodule_cache, treeish_name, path, lookup_path);
}

/**
 * Used internally by submodules_of_tree(). Recurses into 'treeish_name'
 * and appends submodule entries to 'out'. The submodule_cache expects
 * a root-level treeish_name and paths, so keep track of these values
 * with 'root_tree' and 'prefix'.
 */
static void traverse_tree_submodules(struct repository *r,
				     const struct object_id *root_tree,
				     char *prefix,
				     const struct object_id *treeish_name,
				     struct submodule_entry_list *out)
{
	struct tree_desc tree;
	struct submodule_tree_entry *st_entry;
	struct name_entry *name_entry;
	char *tree_path = NULL;

	name_entry = xmalloc(sizeof(*name_entry));

	fill_tree_descriptor(r, &tree, treeish_name);
	while (tree_entry(&tree, name_entry)) {
		if (prefix)
			tree_path =
				mkpathdup("%s/%s", prefix, name_entry->path);
		else
			tree_path = xstrdup(name_entry->path);

		if (S_ISGITLINK(name_entry->mode) &&
		    is_tree_submodule_active(r, root_tree, tree_path)) {
			ALLOC_GROW(out->entries, out->entry_nr + 1,
				   out->entry_alloc);
			st_entry = &out->entries[out->entry_nr++];

			st_entry->name_entry = xmalloc(sizeof(*st_entry->name_entry));
			*st_entry->name_entry = *name_entry;
			st_entry->submodule =
				submodule_from_path(r, root_tree, tree_path);
			st_entry->repo = xmalloc(sizeof(*st_entry->repo));
			if (repo_submodule_init(st_entry->repo, r, tree_path,
						root_tree))
				FREE_AND_NULL(st_entry->repo);

		} else if (S_ISDIR(name_entry->mode))
			traverse_tree_submodules(r, root_tree, tree_path,
						 &name_entry->oid, out);
		free(tree_path);
	}
}

void submodules_of_tree(struct repository *r,
			const struct object_id *treeish_name,
			struct submodule_entry_list *out)
{
	CALLOC_ARRAY(out->entries, 0);
	out->entry_nr = 0;
	out->entry_alloc = 0;

	traverse_tree_submodules(r, treeish_name, NULL, treeish_name, out);
}

void submodule_free(struct repository *r)
{
	if (r->submodule_cache)
		submodule_cache_clear(r->submodule_cache);
}

static int config_print_callback(const char *var, const char *value, void *cb_data)
{
	char *wanted_key = cb_data;

	if (!strcmp(wanted_key, var))
		printf("%s\n", value);

	return 0;
}

int print_config_from_gitmodules(struct repository *repo, const char *key)
{
	int ret;
	char *store_key;

	ret = git_config_parse_key(key, &store_key, NULL);
	if (ret < 0)
		return CONFIG_INVALID_KEY;

	config_from_gitmodules(config_print_callback, repo, store_key);

	free(store_key);
	return 0;
}

int config_set_in_gitmodules_file_gently(const char *key, const char *value)
{
	int ret;

	ret = git_config_set_in_file_gently(GITMODULES_FILE, key, value);
	if (ret < 0)
		/* Maybe the user already did that, don't error out here */
		warning(_("Could not update .gitmodules entry %s"), key);

	return ret;
}

struct fetch_config {
	int *max_children;
	int *recurse_submodules;
};

static int gitmodules_fetch_config(const char *var, const char *value, void *cb)
{
	struct fetch_config *config = cb;
	if (!strcmp(var, "submodule.fetchjobs")) {
		if (config->max_children)
			*(config->max_children) =
				parse_submodule_fetchjobs(var, value);
		return 0;
	} else if (!strcmp(var, "fetch.recursesubmodules")) {
		if (config->recurse_submodules)
			*(config->recurse_submodules) =
				parse_fetch_recurse_submodules_arg(var, value);
		return 0;
	}

	return 0;
}

void fetch_config_from_gitmodules(int *max_children, int *recurse_submodules)
{
	struct fetch_config config = {
		.max_children = max_children,
		.recurse_submodules = recurse_submodules
	};
	config_from_gitmodules(gitmodules_fetch_config, the_repository, &config);
}

static int gitmodules_update_clone_config(const char *var, const char *value,
					  void *cb)
{
	int *max_jobs = cb;
	if (!strcmp(var, "submodule.fetchjobs"))
		*max_jobs = parse_submodule_fetchjobs(var, value);
	return 0;
}

void update_clone_config_from_gitmodules(int *max_jobs)
{
	config_from_gitmodules(gitmodules_update_clone_config, the_repository, &max_jobs);
}
