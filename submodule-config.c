#include "cache.h"
#include "repository.h"
#include "config.h"
#include "submodule-config.h"
#include "submodule.h"
#include "strbuf.h"
#include "parse-options.h"

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

static int config_path_cmp(const void *unused_cmp_data,
			   const void *entry,
			   const void *entry_or_key,
			   const void *unused_keydata)
{
	const struct submodule_entry *a = entry;
	const struct submodule_entry *b = entry_or_key;

	return strcmp(a->config->path, b->config->path) ||
	       hashcmp(a->config->gitmodules_sha1, b->config->gitmodules_sha1);
}

static int config_name_cmp(const void *unused_cmp_data,
			   const void *entry,
			   const void *entry_or_key,
			   const void *unused_keydata)
{
	const struct submodule_entry *a = entry;
	const struct submodule_entry *b = entry_or_key;

	return strcmp(a->config->name, b->config->name) ||
	       hashcmp(a->config->gitmodules_sha1, b->config->gitmodules_sha1);
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
	hashmap_iter_init(&cache->for_name, &iter);
	while ((entry = hashmap_iter_next(&iter)))
		free_one_config(entry);

	hashmap_free(&cache->for_path, 1);
	hashmap_free(&cache->for_name, 1);
	cache->initialized = 0;
	cache->gitmodules_read = 0;
}

void submodule_cache_free(struct submodule_cache *cache)
{
	submodule_cache_clear(cache);
	free(cache);
}

static unsigned int hash_sha1_string(const unsigned char *sha1,
				     const char *string)
{
	return memhash(sha1, 20) + strhash(string);
}

static void cache_put_path(struct submodule_cache *cache,
			   struct submodule *submodule)
{
	unsigned int hash = hash_sha1_string(submodule->gitmodules_sha1,
					     submodule->path);
	struct submodule_entry *e = xmalloc(sizeof(*e));
	hashmap_entry_init(e, hash);
	e->config = submodule;
	hashmap_put(&cache->for_path, e);
}

static void cache_remove_path(struct submodule_cache *cache,
			      struct submodule *submodule)
{
	unsigned int hash = hash_sha1_string(submodule->gitmodules_sha1,
					     submodule->path);
	struct submodule_entry e;
	struct submodule_entry *removed;
	hashmap_entry_init(&e, hash);
	e.config = submodule;
	removed = hashmap_remove(&cache->for_path, &e, NULL);
	free(removed);
}

static void cache_add(struct submodule_cache *cache,
		      struct submodule *submodule)
{
	unsigned int hash = hash_sha1_string(submodule->gitmodules_sha1,
					     submodule->name);
	struct submodule_entry *e = xmalloc(sizeof(*e));
	hashmap_entry_init(e, hash);
	e->config = submodule;
	hashmap_add(&cache->for_name, e);
}

static const struct submodule *cache_lookup_path(struct submodule_cache *cache,
		const unsigned char *gitmodules_sha1, const char *path)
{
	struct submodule_entry *entry;
	unsigned int hash = hash_sha1_string(gitmodules_sha1, path);
	struct submodule_entry key;
	struct submodule key_config;

	hashcpy(key_config.gitmodules_sha1, gitmodules_sha1);
	key_config.path = path;

	hashmap_entry_init(&key, hash);
	key.config = &key_config;

	entry = hashmap_get(&cache->for_path, &key, NULL);
	if (entry)
		return entry->config;
	return NULL;
}

static struct submodule *cache_lookup_name(struct submodule_cache *cache,
		const unsigned char *gitmodules_sha1, const char *name)
{
	struct submodule_entry *entry;
	unsigned int hash = hash_sha1_string(gitmodules_sha1, name);
	struct submodule_entry key;
	struct submodule key_config;

	hashcpy(key_config.gitmodules_sha1, gitmodules_sha1);
	key_config.name = name;

	hashmap_entry_init(&key, hash);
	key.config = &key_config;

	entry = hashmap_get(&cache->for_name, &key, NULL);
	if (entry)
		return entry->config;
	return NULL;
}

static int name_and_item_from_var(const char *var, struct strbuf *name,
				  struct strbuf *item)
{
	const char *subsection, *key;
	int subsection_len, parse;
	parse = parse_config_key(var, "submodule", &subsection,
			&subsection_len, &key);
	if (parse < 0 || !subsection)
		return 0;

	strbuf_add(name, subsection, subsection_len);
	strbuf_addstr(item, key);

	return 1;
}

static struct submodule *lookup_or_create_by_name(struct submodule_cache *cache,
		const unsigned char *gitmodules_sha1, const char *name)
{
	struct submodule *submodule;
	struct strbuf name_buf = STRBUF_INIT;

	submodule = cache_lookup_name(cache, gitmodules_sha1, name);
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

	hashcpy(submodule->gitmodules_sha1, gitmodules_sha1);

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
		die(_("negative values not allowed for submodule.fetchjobs"));
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

static void warn_multiple_config(const unsigned char *treeish_name,
				 const char *name, const char *option)
{
	const char *commit_string = "WORKTREE";
	if (treeish_name)
		commit_string = sha1_to_hex(treeish_name);
	warning("%s:.gitmodules, multiple configurations found for "
			"'submodule.%s.%s'. Skipping second one!",
			commit_string, name, option);
}

struct parse_config_parameter {
	struct submodule_cache *cache;
	const unsigned char *treeish_name;
	const unsigned char *gitmodules_sha1;
	int overwrite;
};

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
					     me->gitmodules_sha1,
					     name.buf);

	if (!strcmp(item.buf, "path")) {
		if (!value)
			ret = config_error_nonbool(var);
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
		int die_on_error = is_null_sha1(me->gitmodules_sha1);
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
			 &submodule->update_strategy) < 0)
				die(_("invalid value for %s"), var);
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
	if (get_oid(rev->buf, gitmodules_oid) >= 0)
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

		entry = hashmap_iter_first(&cache->for_name, &iter);
		if (!entry)
			return NULL;
		return entry->config;
	}

	if (!gitmodule_oid_from_commit(treeish_name, &oid, &rev))
		goto out;

	switch (lookup_type) {
	case lookup_name:
		submodule = cache_lookup_name(cache, oid.hash, key);
		break;
	case lookup_path:
		submodule = cache_lookup_path(cache, oid.hash, key);
		break;
	}
	if (submodule)
		goto out;

	config = read_object_file(&oid, &type, &config_size);
	if (!config || type != OBJ_BLOB)
		goto out;

	/* fill the submodule config into the cache */
	parameter.cache = cache;
	parameter.treeish_name = treeish_name->hash;
	parameter.gitmodules_sha1 = oid.hash;
	parameter.overwrite = 0;
	git_config_from_mem(parse_config, CONFIG_ORIGIN_SUBMODULE_BLOB, rev.buf,
			config, config_size, &parameter);
	strbuf_release(&rev);
	free(config);

	switch (lookup_type) {
	case lookup_name:
		return cache_lookup_name(cache, oid.hash, key);
	case lookup_path:
		return cache_lookup_path(cache, oid.hash, key);
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

static int gitmodules_cb(const char *var, const char *value, void *data)
{
	struct repository *repo = data;
	struct parse_config_parameter parameter;

	parameter.cache = repo->submodule_cache;
	parameter.treeish_name = NULL;
	parameter.gitmodules_sha1 = null_sha1;
	parameter.overwrite = 1;

	return parse_config(var, value, &parameter);
}

void repo_read_gitmodules(struct repository *repo)
{
	submodule_cache_check_init(repo);

	if (repo->worktree) {
		char *gitmodules;

		if (repo_read_index(repo) < 0)
			return;

		gitmodules = repo_worktree_path(repo, GITMODULES_FILE);

		if (!is_gitmodules_unmerged(repo->index))
			git_config_from_file(gitmodules_cb, gitmodules, repo);

		free(gitmodules);
	}

	repo->submodule_cache->gitmodules_read = 1;
}

void gitmodules_config_oid(const struct object_id *commit_oid)
{
	struct strbuf rev = STRBUF_INIT;
	struct object_id oid;

	submodule_cache_check_init(the_repository);

	if (gitmodule_oid_from_commit(commit_oid, &oid, &rev)) {
		git_config_from_blob_oid(gitmodules_cb, rev.buf,
					 &oid, the_repository);
	}
	strbuf_release(&rev);

	the_repository->submodule_cache->gitmodules_read = 1;
}

static void gitmodules_read_check(struct repository *repo)
{
	submodule_cache_check_init(repo);

	/* read the repo's .gitmodules file if it hasn't been already */
	if (!repo->submodule_cache->gitmodules_read)
		repo_read_gitmodules(repo);
}

const struct submodule *submodule_from_name(struct repository *r,
					    const struct object_id *treeish_name,
		const char *name)
{
	gitmodules_read_check(r);
	return config_from(r->submodule_cache, treeish_name, name, lookup_name);
}

const struct submodule *submodule_from_path(struct repository *r,
					    const struct object_id *treeish_name,
		const char *path)
{
	gitmodules_read_check(r);
	return config_from(r->submodule_cache, treeish_name, path, lookup_path);
}

void submodule_free(struct repository *r)
{
	if (r->submodule_cache)
		submodule_cache_clear(r->submodule_cache);
}
