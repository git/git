/*
 * Copyright (C) 2005 Junio C Hamano
 * Copyright (C) 2010 Google Inc.
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "xdiff-interface.h"
#include "kwset.h"

typedef int (*pickaxe_fn)(struct diff_filepair *p, struct diff_options *o, regex_t *regexp, kwset_t kws);

static void pickaxe(struct diff_queue_struct *q, struct diff_options *o,
		    regex_t *regexp, kwset_t kws, pickaxe_fn fn)
{
	int i;
	struct diff_queue_struct outq;

	DIFF_QUEUE_CLEAR(&outq);

	if (o->pickaxe_opts & DIFF_PICKAXE_ALL) {
		/* Showing the whole changeset if needle exists */
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (fn(p, o, regexp, kws))
				return; /* do not munge the queue */
		}

		/*
		 * Otherwise we will clear the whole queue by copying
		 * the empty outq at the end of this function, but
		 * first clear the current entries in the queue.
		 */
		for (i = 0; i < q->nr; i++)
			diff_free_filepair(q->queue[i]);
	} else {
		/* Showing only the filepairs that has the needle */
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (fn(p, o, regexp, kws))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}

	free(q->queue);
	*q = outq;
}

struct diffgrep_cb {
	regex_t *regexp;
	int hit;
};

static void diffgrep_consume(void *priv, char *line, unsigned long len)
{
	struct diffgrep_cb *data = priv;
	regmatch_t regmatch;
	int hold;

	if (line[0] != '+' && line[0] != '-')
		return;
	if (data->hit)
		/*
		 * NEEDSWORK: we should have a way to terminate the
		 * caller early.
		 */
		return;
	/* Yuck -- line ought to be "const char *"! */
	hold = line[len];
	line[len] = '\0';
	data->hit = !regexec(data->regexp, line + 1, 1, &regmatch, 0);
	line[len] = hold;
}

static void fill_one(struct diff_filespec *one,
		     mmfile_t *mf, struct userdiff_driver **textconv)
{
	if (DIFF_FILE_VALID(one)) {
		*textconv = get_textconv(one);
		mf->size = fill_textconv(*textconv, one, &mf->ptr);
	} else {
		memset(mf, 0, sizeof(*mf));
	}
}

static int diff_grep(struct diff_filepair *p, struct diff_options *o,
		     regex_t *regexp, kwset_t kws)
{
	regmatch_t regmatch;
	struct userdiff_driver *textconv_one = NULL;
	struct userdiff_driver *textconv_two = NULL;
	mmfile_t mf1, mf2;
	int hit;

	if (diff_unmodified_pair(p))
		return 0;

	fill_one(p->one, &mf1, &textconv_one);
	fill_one(p->two, &mf2, &textconv_two);

	if (!mf1.ptr) {
		if (!mf2.ptr)
			return 0; /* ignore unmerged */
		/* created "two" -- does it have what we are looking for? */
		hit = !regexec(regexp, p->two->data, 1, &regmatch, 0);
	} else if (!mf2.ptr) {
		/* removed "one" -- did it have what we are looking for? */
		hit = !regexec(regexp, p->one->data, 1, &regmatch, 0);
	} else {
		/*
		 * We have both sides; need to run textual diff and see if
		 * the pattern appears on added/deleted lines.
		 */
		struct diffgrep_cb ecbdata;
		xpparam_t xpp;
		xdemitconf_t xecfg;

		memset(&xpp, 0, sizeof(xpp));
		memset(&xecfg, 0, sizeof(xecfg));
		ecbdata.regexp = regexp;
		ecbdata.hit = 0;
		xecfg.ctxlen = o->context;
		xecfg.interhunkctxlen = o->interhunkcontext;
		xdi_diff_outf(&mf1, &mf2, diffgrep_consume, &ecbdata,
			      &xpp, &xecfg);
		hit = ecbdata.hit;
	}
	if (textconv_one)
		free(mf1.ptr);
	if (textconv_two)
		free(mf2.ptr);
	return hit;
}

static void diffcore_pickaxe_grep(struct diff_options *o)
{
	int err;
	regex_t regex;

	err = regcomp(&regex, o->pickaxe, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		char errbuf[1024];
		regerror(err, &regex, errbuf, 1024);
		regfree(&regex);
		die("invalid log-grep regex: %s", errbuf);
	}

	pickaxe(&diff_queued_diff, o, &regex, NULL, diff_grep);

	regfree(&regex);
	return;
}

static unsigned int contains(struct diff_filespec *one, struct diff_options *o,
			     regex_t *regexp, kwset_t kws)
{
	unsigned int cnt;
	unsigned long sz;
	const char *data;
	if (!o->pickaxe[0])
		return 0;
	if (diff_populate_filespec(one, 0))
		return 0;

	sz = one->size;
	data = one->data;
	cnt = 0;

	if (regexp) {
		regmatch_t regmatch;
		int flags = 0;

		assert(data[sz] == '\0');
		while (*data && !regexec(regexp, data, 1, &regmatch, flags)) {
			flags |= REG_NOTBOL;
			data += regmatch.rm_eo;
			if (*data && regmatch.rm_so == regmatch.rm_eo)
				data++;
			cnt++;
		}

	} else { /* Classic exact string match */
		while (sz) {
			struct kwsmatch kwsm;
			size_t offset = kwsexec(kws, data, sz, &kwsm);
			const char *found;
			if (offset == -1)
				break;
			else
				found = data + offset;
			sz -= found - data + kwsm.size[0];
			data = found + kwsm.size[0];
			cnt++;
		}
	}
	diff_free_filespec_data(one);
	return cnt;
}

static int has_changes(struct diff_filepair *p, struct diff_options *o,
		       regex_t *regexp, kwset_t kws)
{
	if (!DIFF_FILE_VALID(p->one)) {
		if (!DIFF_FILE_VALID(p->two))
			return 0; /* ignore unmerged */
		/* created */
		return contains(p->two, o, regexp, kws) != 0;
	}
	if (!DIFF_FILE_VALID(p->two))
		return contains(p->one, o, regexp, kws) != 0;
	if (!diff_unmodified_pair(p)) {
		return contains(p->one, o, regexp, kws) !=
		       contains(p->two, o, regexp, kws);
	}
	return 0;
}

static void diffcore_pickaxe_count(struct diff_options *o)
{
	const char *needle = o->pickaxe;
	int opts = o->pickaxe_opts;
	unsigned long len = strlen(needle);
	regex_t regex, *regexp = NULL;
	kwset_t kws = NULL;

	if (opts & DIFF_PICKAXE_REGEX) {
		int err;
		err = regcomp(&regex, needle, REG_EXTENDED | REG_NEWLINE);
		if (err) {
			/* The POSIX.2 people are surely sick */
			char errbuf[1024];
			regerror(err, &regex, errbuf, 1024);
			regfree(&regex);
			die("invalid pickaxe regex: %s", errbuf);
		}
		regexp = &regex;
	} else {
		kws = kwsalloc(NULL);
		kwsincr(kws, needle, len);
		kwsprep(kws);
	}

	pickaxe(&diff_queued_diff, o, regexp, kws, has_changes);

	if (opts & DIFF_PICKAXE_REGEX)
		regfree(&regex);
	else
		kwsfree(kws);
	return;
}

void diffcore_pickaxe(struct diff_options *o)
{
	/* Might want to warn when both S and G are on; I don't care... */
	if (o->pickaxe_opts & DIFF_PICKAXE_KIND_G)
		diffcore_pickaxe_grep(o);
	else
		diffcore_pickaxe_count(o);
}
