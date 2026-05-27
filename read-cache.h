#ifndef READ_CACHE_H
#define READ_CACHE_H

#include "read-cache-ll.h"
#include "object.h"
#include "pathspec.h"

unsigned int ce_mode_from_stat(struct index_state *istate,
				const struct cache_entry *ce,
				unsigned int mode);

static inline int ce_to_dtype(const struct cache_entry *ce)
{
	unsigned ce_mode = ntohl(ce->ce_mode);
	if (S_ISREG(ce_mode))
		return DT_REG;
	else if (S_ISDIR(ce_mode) || S_ISGITLINK(ce_mode))
		return DT_DIR;
	else if (S_ISLNK(ce_mode))
		return DT_LNK;
	else
		return DT_UNKNOWN;
}

static inline int ce_path_match(struct index_state *istate,
				const struct cache_entry *ce,
				const struct pathspec *pathspec,
				char *seen)
{
	return match_pathspec(istate, pathspec, ce->name, ce_namelen(ce), 0, seen,
			      S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode));
}

#endif /* READ_CACHE_H */
