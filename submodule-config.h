#ifndef SUBMODULE_CONFIG_CACHE_H
#define SUBMODULE_CONFIG_CACHE_H

#include "config.h"
#include "hashmap.h"
#include "submodule.h"
#include "strbuf.h"
#include "tree-walk.h"

/**
 * The submodule config cache API allows to read submodule
 * configurations/information from specified revisions. Internally
 * information is lazily read into a cache that is used to avoid
 * unnecessary parsing of the same .gitmodules files. Lookups can be done by
 * submodule path or name.
 *
 * Usage
 * -----
 *
 * The caller can look up information about submodules by using the
 * `submodule_from_path()` or `submodule_from_name()` functions. They return
 * a `struct submodule` which contains the values. The API automatically
 * initializes and allocates the needed infrastructure on-demand. If the
 * caller does only want to lookup values from revisions the initialization
 * can be skipped.
 *
 * If the internal cache might grow too big or when the caller is done with
 * the API, all internally cached values can be freed with submodule_free().
 *
 */

/*
 * Submodule entry containing the information about a certain submodule
 * in a certain revision. It is returned by the lookup functions.
 */
struct submodule {
	const char *path;
	const char *name;
	const char *url;
	enum submodule_recurse_mode fetch_recurse;
	const char *ignore;
	const char *branch;
	struct submodule_update_strategy update_strategy;
	/* the object id of the responsible .gitmodules file */
	struct object_id gitmodules_oid;
	int recommend_shallow;
};
struct submodule_cache;
struct repository;

void submodule_cache_free(struct submodule_cache *cache);

int parse_submodule_fetchjobs(const char *var, const char *value,
			      const struct key_value_info *kvi);
int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg);
struct option;
int option_fetch_parse_recurse_submodules(const struct option *opt,
					  const char *arg, int unset);
int parse_update_recurse_submodules_arg(const char *opt, const char *arg);
int parse_push_recurse_submodules_arg(const char *opt, const char *arg);
void repo_read_gitmodules(struct repository *repo, int skip_if_read);
void gitmodules_config_oid(const struct object_id *commit_oid);

/**
 * Same as submodule_from_path but lookup by name.
 */
const struct submodule *submodule_from_name(struct repository *r,
					    const struct object_id *commit_or_tree,
					    const char *name);

/**
 * Given a tree-ish in the superproject and a path, return the submodule that
 * is bound at the path in the named tree.
 */
const struct submodule *submodule_from_path(struct repository *r,
					    const struct object_id *commit_or_tree,
					    const char *path);

/**
 * Use these to free the internally cached values.
 */
void submodule_free(struct repository *r);

int print_config_from_gitmodules(struct repository *repo, const char *key);
int config_set_in_gitmodules_file_gently(const char *key, const char *value);

/*
 * Returns 0 if the name is syntactically acceptable as a submodule "name"
 * (e.g., that may be found in the subsection of a .gitmodules file) and -1
 * otherwise.
 */
int check_submodule_name(const char *name);

/*
 * Note: these helper functions exist solely to maintain backward
 * compatibility with 'fetch' and 'update_clone' storing configuration in
 * '.gitmodules'.
 *
 * New helpers to retrieve arbitrary configuration from the '.gitmodules' file
 * should NOT be added.
 */
void fetch_config_from_gitmodules(int *max_children, int *recurse_submodules);
void update_clone_config_from_gitmodules(int *max_jobs);

/*
 * Submodule entry that contains relevant information about a
 * submodule in a tree.
 */
struct submodule_tree_entry {
	/* The submodule's tree entry. */
	struct name_entry *name_entry;
	/*
	 * A struct repository corresponding to the submodule. May be
	 * NULL if the submodule has not been updated.
	 */
	struct repository *repo;
	/*
	 * A struct submodule containing the submodule config in the
	 * tree's .gitmodules.
	 */
	const struct submodule *submodule;
};

struct submodule_entry_list {
	struct submodule_tree_entry *entries;
	int entry_nr;
	int entry_alloc;
};

/**
 * Given a treeish, return all submodules in the tree and its subtrees,
 * but excluding nested submodules. Callers that require nested
 * submodules are expected to recurse into the submodules themselves.
 */
void submodules_of_tree(struct repository *r,
			const struct object_id *treeish_name,
			struct submodule_entry_list *ret);
#endif /* SUBMODULE_CONFIG_H */
