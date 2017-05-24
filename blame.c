#include "blame.h"

void blame_origin_decref(struct blame_origin *o)
{
	if (o && --o->refcnt <= 0) {
		struct blame_origin *p, *l = NULL;
		if (o->previous)
			blame_origin_decref(o->previous);
		free(o->file.ptr);
		/* Should be present exactly once in commit chain */
		for (p = o->commit->util; p; l = p, p = p->next) {
			if (p == o) {
				if (l)
					l->next = p->next;
				else
					o->commit->util = p->next;
				free(o);
				return;
			}
		}
		die("internal error in blame_origin_decref");
	}
}

/*
 * Given a commit and a path in it, create a new origin structure.
 * The callers that add blame to the scoreboard should use
 * get_origin() to obtain shared, refcounted copy instead of calling
 * this function directly.
 */
struct blame_origin *make_origin(struct commit *commit, const char *path)
{
	struct blame_origin *o;
	FLEX_ALLOC_STR(o, path, path);
	o->commit = commit;
	o->refcnt = 1;
	o->next = commit->util;
	commit->util = o;
	return o;
}

/*
 * Locate an existing origin or create a new one.
 * This moves the origin to front position in the commit util list.
 */
struct blame_origin *get_origin(struct commit *commit, const char *path)
{
	struct blame_origin *o, *l;

	for (o = commit->util, l = NULL; o; l = o, o = o->next) {
		if (!strcmp(o->path, path)) {
			/* bump to front */
			if (l) {
				l->next = o->next;
				o->next = commit->util;
				commit->util = o;
			}
			return blame_origin_incref(o);
		}
	}
	return make_origin(commit, path);
}
