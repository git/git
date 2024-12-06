/*
 * Copyright (C) 2005 Junio C Hamano
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "gettext.h"
#include "diff.h"
#include "diffcore.h"
#include "wildmatch.h"

static char **order;
static int order_cnt;

static void prepare_order(const char *orderfile)
{
	int cnt, pass;
	struct strbuf sb = STRBUF_INIT;
	const char *cp, *endp;
	ssize_t sz;

	if (order)
		return;

	sz = strbuf_read_file(&sb, orderfile, 0);
	if (sz < 0)
		die_errno(_("failed to read orderfile '%s'"), orderfile);
	endp = sb.buf + sz;

	for (pass = 0; pass < 2; pass++) {
		cnt = 0;
		cp = sb.buf;
		while (cp < endp) {
			const char *ep;
			for (ep = cp; ep < endp && *ep != '\n'; ep++)
				;
			/* cp to ep has one line */
			if (*cp == '\n' || *cp == '#')
				; /* comment */
			else if (pass == 0)
				cnt++;
			else {
				order[cnt] = xmemdupz(cp, ep - cp);
				cnt++;
			}
			if (ep < endp)
				ep++;
			cp = ep;
		}
		if (pass == 0) {
			order_cnt = cnt;
			ALLOC_ARRAY(order, cnt);
		}
	}

	strbuf_release(&sb);
}

static int match_order(const char *path)
{
	int i;
	static struct strbuf p = STRBUF_INIT;

	for (i = 0; i < order_cnt; i++) {
		strbuf_reset(&p);
		strbuf_addstr(&p, path);
		while (p.buf[0]) {
			char *cp;
			if (!wildmatch(order[i], p.buf, 0))
				return i;
			cp = strrchr(p.buf, '/');
			if (!cp)
				break;
			*cp = 0;
		}
	}
	return order_cnt;
}

static int compare_objs_order(const void *a_, const void *b_)
{
	struct obj_order const *a, *b;
	a = (struct obj_order const *)a_;
	b = (struct obj_order const *)b_;
	if (a->order != b->order)
		return a->order - b->order;
	return a->orig_order - b->orig_order;
}

void order_objects(const char *orderfile, obj_path_fn_t obj_path,
		   struct obj_order *objs, int nr)
{
	int i;

	if (!nr)
		return;

	prepare_order(orderfile);
	for (i = 0; i < nr; i++) {
		objs[i].orig_order = i;
		objs[i].order = match_order(obj_path(objs[i].obj));
	}
	QSORT(objs, nr, compare_objs_order);
}

static const char *pair_pathtwo(void *obj)
{
	struct diff_filepair *pair = (struct diff_filepair *)obj;

	return pair->two->path;
}

void diffcore_order(const char *orderfile)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct obj_order *o;
	int i;

	if (!q->nr)
		return;

	ALLOC_ARRAY(o, q->nr);
	for (i = 0; i < q->nr; i++)
		o[i].obj = q->queue[i];
	order_objects(orderfile, pair_pathtwo, o, q->nr);
	for (i = 0; i < q->nr; i++)
		q->queue[i] = o[i].obj;
	free(o);
	return;
}
