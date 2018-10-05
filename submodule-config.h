#ifndef SUBMODULE_CONFIG_CACHE_H
#define SUBMODULE_CONFIG_CACHE_H

#include "cache.h"
#include "config.h"
#include "hashmap.h"
#include "submodule.h"
#include "strbuf.h"

/*
 * Submodule entry containing the information about a certain submodule
 * in a certain revision.
 */
struct submodule {
	const char *path;
	const char *name;
	const char *url;
	int fetch_recurse;
	const char *ignore;
	const char *branch;
	struct submodule_update_strategy update_strategy;
	/* the object id of the responsible .gitmodules file */
	struct object_id gitmodules_oid;
	int recommend_shallow;
};

#define SUBMODULE_INIT { NULL, NULL, NULL, RECURSE_SUBMODULES_NONE, \
	NULL, NULL, SUBMODULE_UPDATE_STRATEGY_INIT, { { 0 } }, -1 };

struct submodule_cache;
struct repository;

extern void submodule_cache_free(struct submodule_cache *cache);

extern int parse_submodule_fetchjobs(const char *var, const char *value);
extern int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg);
struct option;
extern int option_fetch_parse_recurse_submodules(const struct option *opt,
						 const char *arg, int unset);
extern int parse_update_recurse_submodules_arg(const char *opt, const char *arg);
extern int parse_push_recurse_submodules_arg(const char *opt, const char *arg);
extern void repo_read_gitmodules(struct repository *repo);
extern void gitmodules_config_oid(const struct object_id *commit_oid);
const struct submodule *submodule_from_name(struct repository *r,
					    const struct object_id *commit_or_tree,
					    const char *name);
const struct submodule *submodule_from_path(struct repository *r,
					    const struct object_id *commit_or_tree,
					    const char *path);
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
extern void fetch_config_from_gitmodules(int *max_children, int *recurse_submodules);
extern void update_clone_config_from_gitmodules(int *max_jobs);

#endif /* SUBMODULE_CONFIG_H */
