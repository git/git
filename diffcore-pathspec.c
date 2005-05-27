/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"

struct path_spec {
	const char *spec;
	int len;
};

static int matches_pathspec(const char *name, struct path_spec *s, int cnt)
{
	int i;
	int namelen;

	if (cnt == 0)
		return 1;

	namelen = strlen(name);
	for (i = 0; i < cnt; i++) {
		int len = s[i].len;
		if (! strncmp(s[i].spec, name, len) &&
		    len <= namelen &&
		    (name[len] == 0 || name[len] == '/'))
			return 1;
	}
	return 0;
}

void diffcore_pathspec(const char **pathspec)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i, speccnt;
	struct diff_queue_struct outq;
	struct path_spec *spec;

	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	for (i = 0; pathspec[i]; i++)
		;
	speccnt = i;
	spec = xmalloc(sizeof(*spec) * speccnt);
	for (i = 0; pathspec[i]; i++) {
		int l;
		spec[i].spec = pathspec[i];
		l = strlen(pathspec[i]);
		while (l > 0 && pathspec[i][l-1] == '/')
			l--;
		spec[i].len = l;
	}

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (matches_pathspec(p->one->path, spec, speccnt) ||
		    matches_pathspec(p->two->path, spec, speccnt))
			diff_q(&outq, p);
		else
			diff_free_filepair(p);
	}
	free(q->queue);
	*q = outq;
	return;
}
