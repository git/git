#include "strmap.h"
static struct strbuf *additional_headers(struct diff_options *o,
					 const char *path)
{
	if (!o->additional_path_headers)
		return NULL;
	return strmap_get(o->additional_path_headers, path);
}

static void add_formatted_headers(struct strbuf *msg,
				  struct strbuf *more_headers,
				  const char *line_prefix,
				  const char *meta,
				  const char *reset)
{
	char *next, *newline;

	for (next = more_headers->buf; *next; next = newline) {
		newline = strchrnul(next, '\n');
		strbuf_addf(msg, "%s%s%.*s%s\n", line_prefix, meta,
			    (int)(newline - next), next, reset);
		if (*newline)
			newline++;
	}
}

	if (!DIFF_FILE_VALID(one) && !DIFF_FILE_VALID(two)) {
		/*
		 * We should only reach this point for pairs from
		 * create_filepairs_for_header_only_notifications().  For
		 * these, we should avoid the "/dev/null" special casing
		 * above, meaning we avoid showing such pairs as either
		 * "new file" or "deleted file" below.
		 */
		lbl[0] = a_one;
		lbl[1] = b_two;
	}
	struct strbuf *more_headers = NULL;
	if ((more_headers = additional_headers(o, name))) {
		add_formatted_headers(msg, more_headers,
				      line_prefix, set, reset);
		*must_show_header = 1;
	}
	/*
	 * Check if we can return early without showing a diff.  Note that
	 * diff_filepair only stores {oid, path, mode, is_valid}
	 * information for each path, and thus diff_unmodified_pair() only
	 * considers those bits of info.  However, we do not want pairs
	 * created by create_filepairs_for_header_only_notifications() to
	 * be ignored, so return early if both p is unmodified AND
	 * p->one->path is not in additional headers.
	 */
	if (diff_unmodified_pair(p) && !additional_headers(o, p->one->path))
	/* Actually, we can also return early to avoid showing tree diffs */
		return;
int diff_queue_is_empty(struct diff_options *o)

	if (o->additional_path_headers &&
	    !strmap_empty(o->additional_path_headers))
		return 0;
static void create_filepairs_for_header_only_notifications(struct diff_options *o)
{
	struct strset present;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct hashmap_iter iter;
	struct strmap_entry *e;
	int i;

	strset_init_with_options(&present, /*pool*/ NULL, /*strdup*/ 0);

	/*
	 * Find out which paths exist in diff_queued_diff, preferring
	 * one->path for any pair that has multiple paths.
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		char *path = p->one->path ? p->one->path : p->two->path;

		if (strmap_contains(o->additional_path_headers, path))
			strset_add(&present, path);
	}

	/*
	 * Loop over paths in additional_path_headers; for each NOT already
	 * in diff_queued_diff, create a synthetic filepair and insert that
	 * into diff_queued_diff.
	 */
	strmap_for_each_entry(o->additional_path_headers, &iter, e) {
		if (!strset_contains(&present, e->key)) {
			struct diff_filespec *one, *two;
			struct diff_filepair *p;

			one = alloc_filespec(e->key);
			two = alloc_filespec(e->key);
			fill_filespec(one, null_oid(), 0, 0);
			fill_filespec(two, null_oid(), 0, 0);
			p = diff_queue(q, one, two);
			p->status = DIFF_STATUS_MODIFIED;
		}
	}

	/* Re-sort the filepairs */
	diffcore_fix_diff_index();

	/* Cleanup */
	strset_clear(&present);
}

	if (o->additional_path_headers)
		create_filepairs_for_header_only_notifications(o);

	if (!q->nr && !options->additional_path_headers)