#ifndef PACK_REFS_H
#define PACK_REFS_H

struct repository;

/*
 * Shared usage string for options common to git-pack-refs(1)
 * and git-refs-optimize(1). The command-specific part (e.g., "git refs optimize ")
 * must be prepended by the caller.
 */
#define PACK_REFS_OPTS \
	"[--all] [--no-prune] [--auto] [--include <pattern>] [--exclude <pattern>]"

/*
 * The core logic for pack-refs and its clones.
 */
int pack_refs_core(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo,
		   const char * const *usage_opts);

#endif /* PACK_REFS_H */
