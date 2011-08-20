/*
 * Copyright (C) 2005 Junio C Hamano
 * Copyright (C) 2010 Google Inc.
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "xdiff-interface.h"
#include "kwset.h"

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

static int diff_grep(struct diff_filepair *p, regex_t *regexp, struct diff_options *o)
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
	struct diff_queue_struct *q = &diff_queued_diff;
	int i, has_changes, err;
	regex_t regex;
	struct diff_queue_struct outq;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	err = regcomp(&regex, o->pickaxe, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		char errbuf[1024];
		regerror(err, &regex, errbuf, 1024);
		regfree(&regex);
		die("invalid log-grep regex: %s", errbuf);
	}

	if (o->pickaxe_opts & DIFF_PICKAXE_ALL) {
		/* Showing the whole changeset if needle exists */
		for (i = has_changes = 0; !has_changes && i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (diff_grep(p, &regex, o))
				has_changes++;
		}
		if (has_changes)
			return; /* do not munge the queue */

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
			if (diff_grep(p, &regex, o))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}

	regfree(&regex);

	free(q->queue);
	*q = outq;
	return;
}

static unsigned int contains(struct diff_filespec *one,
			     const char *needle, unsigned long len,
			     regex_t *regexp, kwset_t kws)
{
	unsigned int cnt;
	unsigned long sz;
	const char *data;
	if (diff_populate_filespec(one, 0))
		return 0;
	if (!len)
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
			size_t offset = kwsexec(kws, data, sz, NULL);
			const char *found;
			if (offset == -1)
				break;
			else
				found = data + offset;
			sz -= found - data + len;
			data = found + len;
			cnt++;
		}
	}
	diff_free_filespec_data(one);
	return cnt;
}

static void diffcore_pickaxe_count(struct diff_options *o)
{
	const char *needle = o->pickaxe;
	int opts = o->pickaxe_opts;
	struct diff_queue_struct *q = &diff_queued_diff;
	unsigned long len = strlen(needle);
	int i, has_changes;
	regex_t regex, *regexp = NULL;
	kwset_t kws = NULL;
	struct diff_queue_struct outq;
	DIFF_QUEUE_CLEAR(&outq);

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

	if (opts & DIFF_PICKAXE_ALL) {
		/* Showing the whole changeset if needle exists */
		for (i = has_changes = 0; !has_changes && i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (!DIFF_FILE_VALID(p->one)) {
				if (!DIFF_FILE_VALID(p->two))
					continue; /* ignore unmerged */
				/* created */
				if (contains(p->two, needle, len, regexp, kws))
					has_changes++;
			}
			else if (!DIFF_FILE_VALID(p->two)) {
				if (contains(p->one, needle, len, regexp, kws))
					has_changes++;
			}
			else if (!diff_unmodified_pair(p) &&
				 contains(p->one, needle, len, regexp, kws) !=
				 contains(p->two, needle, len, regexp, kws))
				has_changes++;
		}
		if (has_changes)
			return; /* not munge the queue */

		/* otherwise we will clear the whole queue
		 * by copying the empty outq at the end of this
		 * function, but first clear the current entries
		 * in the queue.
		 */
		for (i = 0; i < q->nr; i++)
			diff_free_filepair(q->queue[i]);
	}
	else
		/* Showing only the filepairs that has the needle */
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			has_changes = 0;
			if (!DIFF_FILE_VALID(p->one)) {
				if (!DIFF_FILE_VALID(p->two))
					; /* ignore unmerged */
				/* created */
				else if (contains(p->two, needle, len, regexp,
						  kws))
					has_changes = 1;
			}
			else if (!DIFF_FILE_VALID(p->two)) {
				if (contains(p->one, needle, len, regexp, kws))
					has_changes = 1;
			}
			else if (!diff_unmodified_pair(p) &&
				 contains(p->one, needle, len, regexp, kws) !=
				 contains(p->two, needle, len, regexp, kws))
				has_changes = 1;

			if (has_changes)
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}

	if (opts & DIFF_PICKAXE_REGEX)
		regfree(&regex);
	else
		kwsfree(kws);

	free(q->queue);
	*q = outq;
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
