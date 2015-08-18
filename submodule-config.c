#include "cache.h"
#include "submodule-config.h"
#include "submodule.h"
#include "strbuf.h"

/*
 * submodule cache lookup structure
 * There is one shared set of 'struct submodule' entries which can be
 * looked up by their sha1 blob id of the .gitmodule file and either
 * using path or name as key.
 * for_path stores submodule entries with path as key
 * for_name stores submodule entries with name as key
 */
struct submodule_cache {
	struct hashmap for_path;
	struct hashmap for_name;
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

static struct submodule_cache cache;
static int is_cache_init;

static int config_path_cmp(const struct submodule_entry *a,
			   const struct submodule_entry *b,
			   const void *unused)
{
	return strcmp(a->config->path, b->config->path) ||
	       hashcmp(a->config->gitmodules_sha1, b->config->gitmodules_sha1);
}

static int config_name_cmp(const struct submodule_entry *a,
			   const struct submodule_entry *b,
			   const void *unused)
{
	return strcmp(a->config->name, b->config->name) ||
	       hashcmp(a->config->gitmodules_sha1, b->config->gitmodules_sha1);
}

static void cache_init(struct submodule_cache *cache)
{
	hashmap_init(&cache->for_path, (hashmap_cmp_fn) config_path_cmp, 0);
	hashmap_init(&cache->for_name, (hashmap_cmp_fn) config_name_cmp, 0);
}

static void free_one_config(struct submodule_entry *entry)
{
	free((void *) entry->config->path);
	free((void *) entry->config->name);
	free(entry->config);
}

static void cache_free(struct submodule_cache *cache)
{
	struct hashmap_iter iter;
	struct submodule_entry *entry;

	/*
	 * We iterate over the name hash here to be symmetric with the
	 * allocation of struct submodule entries. Each is allocated by
	 * their .gitmodule blob sha1 and submodule name.
	 */
	hashmap_iter_init(&cache->for_name, &iter);
	while ((entry = hashmap_iter_next(&iter)))
		free_one_config(entry);

	hashmap_free(&cache->for_path, 1);
	hashmap_free(&cache->for_name, 1);
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
	submodule->fetch_recurse = RECURSE_SUBMODULES_NONE;
	submodule->ignore = NULL;

	hashcpy(submodule->gitmodules_sha1, gitmodules_sha1);

	cache_add(cache, submodule);

	return submodule;
}

static int parse_fetch_recurse(const char *opt, const char *arg,
			       int die_on_error)
{
	switch (git_config_maybe_bool(opt, arg)) {
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

int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg)
{
	return parse_fetch_recurse(opt, arg, 1);
}

static void warn_multiple_config(const unsigned char *commit_sha1,
				 const char *name, const char *option)
{
	const char *commit_string = "WORKTREE";
	if (commit_sha1)
		commit_string = sha1_to_hex(commit_sha1);
	warning("%s:.gitmodules, multiple configurations found for "
			"'submodule.%s.%s'. Skipping second one!",
			commit_string, name, option);
}

struct parse_config_parameter {
	struct submodule_cache *cache;
	const unsigned char *commit_sha1;
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

	submodule = lookup_or_create_by_name(me->cache, me->gitmodules_sha1,
			name.buf);

	if (!strcmp(item.buf, "path")) {
		struct strbuf path = STRBUF_INIT;
		if (!value) {
			ret = config_error_nonbool(var);
			goto release_return;
		}
		if (!me->overwrite && submodule->path != NULL) {
			warn_multiple_config(me->commit_sha1, submodule->name,
					"path");
			goto release_return;
		}

		if (submodule->path)
			cache_remove_path(me->cache, submodule);
		free((void *) submodule->path);
		strbuf_addstr(&path, value);
		submodule->path = strbuf_detach(&path, NULL);
		cache_put_path(me->cache, submodule);
	} else if (!strcmp(item.buf, "fetchrecursesubmodules")) {
		/* when parsing worktree configurations we can die early */
		int die_on_error = is_null_sha1(me->gitmodules_sha1);
		if (!me->overwrite &&
		    submodule->fetch_recurse != RECURSE_SUBMODULES_NONE) {
			warn_multiple_config(me->commit_sha1, submodule->name,
					"fetchrecursesubmodules");
			goto release_return;
		}

		submodule->fetch_recurse = parse_fetch_recurse(var, value,
								die_on_error);
	} else if (!strcmp(item.buf, "ignore")) {
		struct strbuf ignore = STRBUF_INIT;
		if (!me->overwrite && submodule->ignore != NULL) {
			warn_multiple_config(me->commit_sha1, submodule->name,
					"ignore");
			goto release_return;
		}
		if (!value) {
			ret = config_error_nonbool(var);
			goto release_return;
		}
		if (strcmp(value, "untracked") && strcmp(value, "dirty") &&
		    strcmp(value, "all") && strcmp(value, "none")) {
			warning("Invalid parameter '%s' for config option "
					"'submodule.%s.ignore'", value, var);
			goto release_return;
		}

		free((void *) submodule->ignore);
		strbuf_addstr(&ignore, value);
		submodule->ignore = strbuf_detach(&ignore, NULL);
	} else if (!strcmp(item.buf, "url")) {
		struct strbuf url = STRBUF_INIT;
		if (!value) {
			ret = config_error_nonbool(var);
			goto release_return;
		}
		if (!me->overwrite && submodule->url != NULL) {
			warn_multiple_config(me->commit_sha1, submodule->name,
					"url");
			goto release_return;
		}

		free((void *) submodule->url);
		strbuf_addstr(&url, value);
		submodule->url = strbuf_detach(&url, NULL);
	}

release_return:
	strbuf_release(&name);
	strbuf_release(&item);

	return ret;
}

static int gitmodule_sha1_from_commit(const unsigned char *commit_sha1,
				      unsigned char *gitmodules_sha1)
{
	struct strbuf rev = STRBUF_INIT;
	int ret = 0;

	if (is_null_sha1(commit_sha1)) {
		hashcpy(gitmodules_sha1, null_sha1);
		return 1;
	}

	strbuf_addf(&rev, "%s:.gitmodules", sha1_to_hex(commit_sha1));
	if (get_sha1(rev.buf, gitmodules_sha1) >= 0)
		ret = 1;

	strbuf_release(&rev);
	return ret;
}

/* This does a lookup of a submodule configuration by name or by path
 * (key) with on-demand reading of the appropriate .gitmodules from
 * revisions.
 */
static const struct submodule *config_from(struct submodule_cache *cache,
		const unsigned char *commit_sha1, const char *key,
		enum lookup_type lookup_type)
{
	struct strbuf rev = STRBUF_INIT;
	unsigned long config_size;
	char *config;
	unsigned char sha1[20];
	enum object_type type;
	const struct submodule *submodule = NULL;
	struct parse_config_parameter parameter;

	/*
	 * If any parameter except the cache is a NULL pointer just
	 * return the first submodule. Can be used to check whether
	 * there are any submodules parsed.
	 */
	if (!commit_sha1 || !key) {
		struct hashmap_iter iter;
		struct submodule_entry *entry;

		hashmap_iter_init(&cache->for_name, &iter);
		entry = hashmap_iter_next(&iter);
		if (!entry)
			return NULL;
		return entry->config;
	}

	if (!gitmodule_sha1_from_commit(commit_sha1, sha1))
		return NULL;

	switch (lookup_type) {
	case lookup_name:
		submodule = cache_lookup_name(cache, sha1, key);
		break;
	case lookup_path:
		submodule = cache_lookup_path(cache, sha1, key);
		break;
	}
	if (submodule)
		return submodule;

	config = read_sha1_file(sha1, &type, &config_size);
	if (!config)
		return NULL;

	if (type != OBJ_BLOB) {
		free(config);
		return NULL;
	}

	/* fill the submodule config into the cache */
	parameter.cache = cache;
	parameter.commit_sha1 = commit_sha1;
	parameter.gitmodules_sha1 = sha1;
	parameter.overwrite = 0;
	git_config_from_buf(parse_config, rev.buf, config, config_size,
			&parameter);
	free(config);

	switch (lookup_type) {
	case lookup_name:
		return cache_lookup_name(cache, sha1, key);
	case lookup_path:
		return cache_lookup_path(cache, sha1, key);
	default:
		return NULL;
	}
}

static const struct submodule *config_from_path(struct submodule_cache *cache,
		const unsigned char *commit_sha1, const char *path)
{
	return config_from(cache, commit_sha1, path, lookup_path);
}

static const struct submodule *config_from_name(struct submodule_cache *cache,
		const unsigned char *commit_sha1, const char *name)
{
	return config_from(cache, commit_sha1, name, lookup_name);
}

static void ensure_cache_init(void)
{
	if (is_cache_init)
		return;

	cache_init(&cache);
	is_cache_init = 1;
}

int parse_submodule_config_option(const char *var, const char *value)
{
	struct parse_config_parameter parameter;
	parameter.cache = &cache;
	parameter.commit_sha1 = NULL;
	parameter.gitmodules_sha1 = null_sha1;
	parameter.overwrite = 1;

	ensure_cache_init();
	return parse_config(var, value, &parameter);
}

const struct submodule *submodule_from_name(const unsigned char *commit_sha1,
		const char *name)
{
	ensure_cache_init();
	return config_from_name(&cache, commit_sha1, name);
}

const struct submodule *submodule_from_path(const unsigned char *commit_sha1,
		const char *path)
{
	ensure_cache_init();
	return config_from_path(&cache, commit_sha1, path);
}

void submodule_free(void)
{
	cache_free(&cache);
	is_cache_init = 0;
}
