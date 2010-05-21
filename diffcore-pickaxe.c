/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"

static unsigned int contains(struct diff_filespec *one,
			     const char *needle, unsigned long len,
			     regex_t *regexp)
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
			const char *found = memmem(data, sz, needle, len);
			if (!found)
				break;
			sz -= found - data + len;
			data = found + len;
			cnt++;
		}
	}
	diff_free_filespec_data(one);
	return cnt;
}

void diffcore_pickaxe(const char *needle, int opts)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	unsigned long len = strlen(needle);
	int i, has_changes;
	regex_t regex, *regexp = NULL;
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
	}

	if (opts & DIFF_PICKAXE_ALL) {
		/* Showing the whole changeset if needle exists */
		for (i = has_changes = 0; !has_changes && i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (!DIFF_FILE_VALID(p->one)) {
				if (!DIFF_FILE_VALID(p->two))
					continue; /* ignore unmerged */
				/* created */
				if (contains(p->two, needle, len, regexp))
					has_changes++;
			}
			else if (!DIFF_FILE_VALID(p->two)) {
				if (contains(p->one, needle, len, regexp))
					has_changes++;
			}
			else if (!diff_unmodified_pair(p) &&
				 contains(p->one, needle, len, regexp) !=
				 contains(p->two, needle, len, regexp))
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
				else if (contains(p->two, needle, len, regexp))
					has_changes = 1;
			}
			else if (!DIFF_FILE_VALID(p->two)) {
				if (contains(p->one, needle, len, regexp))
					has_changes = 1;
			}
			else if (!diff_unmodified_pair(p) &&
				 contains(p->one, needle, len, regexp) !=
				 contains(p->two, needle, len, regexp))
				has_changes = 1;

			if (has_changes)
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}

	if (opts & DIFF_PICKAXE_REGEX) {
		regfree(&regex);
	}

	free(q->queue);
	*q = outq;
	return;
}
