#ifndef FOR_EACH_REF_H
#define FOR_EACH_REF_H

struct repository;

/*
 * Shared usage string for options common to git-for-each-ref(1)
 * and git-refs-list(1). The command-specific part (e.g., "git refs list ")
 * must be prepended by the caller.
 */
#define COMMON_USAGE_FOR_EACH_REF \
	"[--count=<count>] [--shell|--perl|--python|--tcl]\n" \
	"                         [(--sort=<key>)...] [--format=<format>]\n" \
	"                         [--include-root-refs] [--points-at=<object>]\n" \
	"                         [--merged[=<object>]] [--no-merged[=<object>]]\n" \
	"                         [--contains[=<object>]] [--no-contains[=<object>]]\n" \
	"                         [(--exclude=<pattern>)...] [--start-after=<marker>]\n" \
	"                         [ --stdin | (<pattern>...)]"

/*
 * The core logic for for-each-ref and its clones.
 */
int for_each_ref_core(int argc, const char **argv, const char *prefix,
		      struct repository *repo, const char *const *usage);

#endif /* FOR_EACH_REF_H */
