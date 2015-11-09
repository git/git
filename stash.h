#ifndef STASH_H
#define STASH_H

#include "git-compat-util.h"
#include "gettext.h"
#include "run-command.h"

extern int stash_non_patch(const char *tmp_indexfile, const char *i_tree,
	const char *prefix);

#endif /* STASH_H */
