/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"

static char **order;
static int order_cnt;

static void prepare_order(const char *orderfile)
{
	int cnt, pass;
	struct strbuf sb = STRBUF_INIT;
	void *map;
	char *cp, *endp;
	ssize_t sz;

	if (order)
		return;

	sz = strbuf_read_file(&sb, orderfile, 0);
	if (sz < 0)
		die_errno(_("failed to read orderfile '%s'"), orderfile);
	map = strbuf_detach(&sb, NULL);
	endp = (char *) map + sz;

	for (pass = 0; pass < 2; pass++) {
		cnt = 0;
		cp = map;
		while (cp < endp) {
			char *ep;
			for (ep = cp; ep < endp && *ep != '\n'; ep++)
				;
			/* cp to ep has one line */
			if (*cp == '\n' || *cp == '#')
				; /* comment */
			else if (pass == 0)
				cnt++;
			else {
				if (*ep == '\n') {
					*ep = 0;
					order[cnt] = cp;
				} else {
					order[cnt] = xmemdupz(cp, ep - cp);
				}
				cnt++;
			}
			if (ep < endp)
				ep++;
			cp = ep;
		}
		if (pass == 0) {
			order_cnt = cnt;
			order = xmalloc(sizeof(*order) * cnt);
		}
	}
}

struct pair_order {
	struct diff_filepair *pair;
	int orig_order;
	int order;
};

static int match_order(const char *path)
{
	int i;
	static struct strbuf p = STRBUF_INIT;

	for (i = 0; i < order_cnt; i++) {
		strbuf_reset(&p);
		strbuf_addstr(&p, path);
		while (p.buf[0]) {
			char *cp;
			if (!fnmatch(order[i], p.buf, 0))
				return i;
			cp = strrchr(p.buf, '/');
			if (!cp)
				break;
			*cp = 0;
		}
	}
	return order_cnt;
}

static int compare_pair_order(const void *a_, const void *b_)
{
	struct pair_order const *a, *b;
	a = (struct pair_order const *)a_;
	b = (struct pair_order const *)b_;
	if (a->order != b->order)
		return a->order - b->order;
	return a->orig_order - b->orig_order;
}

void diffcore_order(const char *orderfile)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct pair_order *o;
	int i;

	if (!q->nr)
		return;

	o = xmalloc(sizeof(*o) * q->nr);
	prepare_order(orderfile);
	for (i = 0; i < q->nr; i++) {
		o[i].pair = q->queue[i];
		o[i].orig_order = i;
		o[i].order = match_order(o[i].pair->two->path);
	}
	qsort(o, q->nr, sizeof(*o), compare_pair_order);
	for (i = 0; i < q->nr; i++)
		q->queue[i] = o[i].pair;
	free(o);
	return;
}
