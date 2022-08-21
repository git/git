/*
 * Copyright (C) 2005 Junio C Hamano
 * Copyright (C) 2010 Google Inc.
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "xdiff-interface.h"
#include "kwset.h"
#include "commit.h"
#include "quote.h"

typedef int (*pickaxe_fn)(mmfile_t *one, mmfile_t *two,
			  struct diff_options *o,
			  regex_t *regexp, kwset_t kws);

struct diffgrep_cb {
	regex_t *regexp;
	int hit;
};

static int diffgrep_consume(void *priv, char *line, unsigned long len)
{
	struct diffgrep_cb *data = priv;
	regmatch_t regmatch;

	if (line[0] != '+' && line[0] != '-')
		return 0;
	if (data->hit)
		BUG("Already matched in diffgrep_consume! Broken xdiff_emit_line_fn?");
	if (!regexec_buf(data->regexp, line + 1, len - 1, 1,
			 &regmatch, 0)) {
		data->hit = 1;
		return 1;
	}
	return 0;
}

static int diff_grep(mmfile_t *one, mmfile_t *two,
		     struct diff_options *o,
		     regex_t *regexp, kwset_t kws)
{
	struct diffgrep_cb ecbdata;
	xpparam_t xpp;
	xdemitconf_t xecfg;
	int ret;

	/*
	 * We have both sides; need to run textual diff and see if
	 * the pattern appears on added/deleted lines.
	 */
	memset(&xpp, 0, sizeof(xpp));
	memset(&xecfg, 0, sizeof(xecfg));
	ecbdata.regexp = regexp;
	ecbdata.hit = 0;
	xecfg.flags = XDL_EMIT_NO_HUNK_HDR;
	xecfg.ctxlen = o->context;
	xecfg.interhunkctxlen = o->interhunkcontext;

	/*
	 * An xdiff error might be our "data->hit" from above. See the
	 * comment for xdiff_emit_line_fn in xdiff-interface.h
	 */
	ret = xdi_diff_outf(one, two, NULL, diffgrep_consume,
			    &ecbdata, &xpp, &xecfg);
	if (ecbdata.hit)
		return 1;
	if (ret)
		return ret;
	return 0;
}

static unsigned int contains(mmfile_t *mf, regex_t *regexp, kwset_t kws,
			     unsigned int limit)
{
	unsigned int cnt = 0;
	unsigned long sz = mf->size;
	const char *data = mf->ptr;

	if (regexp) {
		regmatch_t regmatch;
		int flags = 0;

		while (sz &&
		       !regexec_buf(regexp, data, sz, 1, &regmatch, flags)) {
			flags |= REG_NOTBOL;
			data += regmatch.rm_eo;
			sz -= regmatch.rm_eo;
			if (sz && regmatch.rm_so == regmatch.rm_eo) {
				data++;
				sz--;
			}
			cnt++;

			if (limit && cnt == limit)
				return cnt;
		}

	} else { /* Classic exact string match */
		while (sz) {
			struct kwsmatch kwsm;
			size_t offset = kwsexec(kws, data, sz, &kwsm);
			if (offset == -1)
				break;
			sz -= offset + kwsm.size[0];
			data += offset + kwsm.size[0];
			cnt++;

			if (limit && cnt == limit)
				return cnt;
		}
	}
	return cnt;
}

static int has_changes(mmfile_t *one, mmfile_t *two,
		       struct diff_options *o,
		       regex_t *regexp, kwset_t kws)
{
	unsigned int c1 = one ? contains(one, regexp, kws, 0) : 0;
	unsigned int c2 = two ? contains(two, regexp, kws, c1 + 1) : 0;
	return c1 != c2;
}

static int pickaxe_match(struct diff_filepair *p, struct diff_options *o,
			 regex_t *regexp, kwset_t kws, pickaxe_fn fn)
{
	struct userdiff_driver *textconv_one = NULL;
	struct userdiff_driver *textconv_two = NULL;
	mmfile_t mf1, mf2;
	int ret;

	/* ignore unmerged */
	if (!DIFF_FILE_VALID(p->one) && !DIFF_FILE_VALID(p->two))
		return 0;

	if (o->objfind) {
		return  (DIFF_FILE_VALID(p->one) &&
			 oidset_contains(o->objfind, &p->one->oid)) ||
			(DIFF_FILE_VALID(p->two) &&
			 oidset_contains(o->objfind, &p->two->oid));
	}

	if (o->flags.allow_textconv) {
		textconv_one = get_textconv(o->repo, p->one);
		textconv_two = get_textconv(o->repo, p->two);
	}

	/*
	 * If we have an unmodified pair, we know that the count will be the
	 * same and don't even have to load the blobs. Unless textconv is in
	 * play, _and_ we are using two different textconv filters (e.g.,
	 * because a pair is an exact rename with different textconv attributes
	 * for each side, which might generate different content).
	 */
	if (textconv_one == textconv_two && diff_unmodified_pair(p))
		return 0;

	if ((o->pickaxe_opts & DIFF_PICKAXE_KIND_G) &&
	    !o->flags.text &&
	    ((!textconv_one && diff_filespec_is_binary(o->repo, p->one)) ||
	     (!textconv_two && diff_filespec_is_binary(o->repo, p->two))))
		return 0;

	mf1.size = fill_textconv(o->repo, textconv_one, p->one, &mf1.ptr);
	mf2.size = fill_textconv(o->repo, textconv_two, p->two, &mf2.ptr);

	ret = fn(&mf1, &mf2, o, regexp, kws);

	if (textconv_one)
		free(mf1.ptr);
	if (textconv_two)
		free(mf2.ptr);
	diff_free_filespec_data(p->one);
	diff_free_filespec_data(p->two);

	return ret;
}

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
			if (pickaxe_match(p, o, regexp, kws, fn))
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
			if (pickaxe_match(p, o, regexp, kws, fn))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}

	free(q->queue);
	*q = outq;
}

static void regcomp_or_die(regex_t *regex, const char *needle, int cflags)
{
	int err = regcomp(regex, needle, cflags);
	if (err) {
		/* The POSIX.2 people are surely sick */
		char errbuf[1024];
		regerror(err, regex, errbuf, 1024);
		die("invalid regex: %s", errbuf);
	}
}

void diffcore_pickaxe(struct diff_options *o)
{
	const char *needle = o->pickaxe;
	int opts = o->pickaxe_opts;
	regex_t regex, *regexp = NULL;
	kwset_t kws = NULL;
	pickaxe_fn fn;

	if (opts & ~DIFF_PICKAXE_KIND_OBJFIND &&
	    (!needle || !*needle))
		BUG("should have needle under -G or -S");
	if (opts & (DIFF_PICKAXE_REGEX | DIFF_PICKAXE_KIND_G)) {
		int cflags = REG_EXTENDED | REG_NEWLINE;
		if (o->pickaxe_opts & DIFF_PICKAXE_IGNORE_CASE)
			cflags |= REG_ICASE;
		regcomp_or_die(&regex, needle, cflags);
		regexp = &regex;

		if (opts & DIFF_PICKAXE_KIND_G)
			fn = diff_grep;
		else if (opts & DIFF_PICKAXE_REGEX)
			fn = has_changes;
		else
			/*
			 * We don't need to check the combination of
			 * -G and --pickaxe-regex, by the time we get
			 * here diff.c has already died if they're
			 * combined. See the usage tests in
			 * t4209-log-pickaxe.sh.
			 */
			BUG("unreachable");
	} else if (opts & DIFF_PICKAXE_KIND_S) {
		if (o->pickaxe_opts & DIFF_PICKAXE_IGNORE_CASE &&
		    has_non_ascii(needle)) {
			struct strbuf sb = STRBUF_INIT;
			int cflags = REG_NEWLINE | REG_ICASE;

			basic_regex_quote_buf(&sb, needle);
			regcomp_or_die(&regex, sb.buf, cflags);
			strbuf_release(&sb);
			regexp = &regex;
		} else {
			kws = kwsalloc(o->pickaxe_opts & DIFF_PICKAXE_IGNORE_CASE
				       ? tolower_trans_tbl : NULL);
			kwsincr(kws, needle, strlen(needle));
			kwsprep(kws);
		}
		fn = has_changes;
	} else if (opts & DIFF_PICKAXE_KIND_OBJFIND) {
		fn = NULL;
	} else {
		BUG("unknown pickaxe_opts flag");
	}

	pickaxe(&diff_queued_diff, o, regexp, kws, fn);

	if (regexp)
		regfree(regexp);
	if (kws)
		kwsfree(kws);
	return;
}
