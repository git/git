#ifndef SUBMODULE_CONFIG_CACHE_H
#define SUBMODULE_CONFIG_CACHE_H

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
	/* the sha1 blob id of the responsible .gitmodules file */
	unsigned char gitmodules_sha1[20];
	int recommend_shallow;
};

#define SUBMODULE_INIT { NULL, NULL, NULL, RECURSE_SUBMODULES_NONE, \
	NULL, NULL, SUBMODULE_UPDATE_STRATEGY_INIT, {0}, -1 };

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

#endif /* SUBMODULE_CONFIG_H */
