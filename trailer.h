#ifndef TRAILER_H
#define TRAILER_H

#include "list.h"
#include "strbuf.h"

enum trailer_where {
	WHERE_DEFAULT,
	WHERE_END,
	WHERE_AFTER,
	WHERE_BEFORE,
	WHERE_START
};
enum trailer_if_exists {
	EXISTS_DEFAULT,
	EXISTS_ADD_IF_DIFFERENT_NEIGHBOR,
	EXISTS_ADD_IF_DIFFERENT,
	EXISTS_ADD,
	EXISTS_REPLACE,
	EXISTS_DO_NOTHING
};
enum trailer_if_missing {
	MISSING_DEFAULT,
	MISSING_ADD,
	MISSING_DO_NOTHING
};

int trailer_set_where(enum trailer_where *item, const char *value);
int trailer_set_if_exists(enum trailer_if_exists *item, const char *value);
int trailer_set_if_missing(enum trailer_if_missing *item, const char *value);

struct trailer_info {
	/*
	 * True if there is a blank line before the location pointed to by
	 * trailer_block_start.
	 */
	int blank_line_before_trailer;

	/*
	 * Offsets to the trailer block start and end positions in the input
	 * string. If no trailer block is found, these are both set to the
	 * "true" end of the input (find_end_of_log_message()).
	 */
	size_t trailer_block_start, trailer_block_end;

	/*
	 * Array of trailers found.
	 */
	char **trailers;
	size_t trailer_nr;
};

/*
 * A list that represents newly-added trailers, such as those provided
 * with the --trailer command line option of git-interpret-trailers.
 */
struct new_trailer_item {
	struct list_head list;

	const char *text;

	enum trailer_where where;
	enum trailer_if_exists if_exists;
	enum trailer_if_missing if_missing;
};

struct process_trailer_options {
	int in_place;
	int trim_empty;
	int only_trailers;
	int only_input;
	int unfold;
	int no_divider;
	int key_only;
	int value_only;
	const struct strbuf *separator;
	const struct strbuf *key_value_separator;
	int (*filter)(const struct strbuf *, void *);
	void *filter_data;
};

#define PROCESS_TRAILER_OPTIONS_INIT {0}

void process_trailers(const char *file,
		      const struct process_trailer_options *opts,
		      struct list_head *new_trailer_head);

void trailer_info_get(struct trailer_info *info, const char *str,
		      const struct process_trailer_options *opts);

void trailer_info_release(struct trailer_info *info);

/*
 * Format the trailers from the commit msg "msg" into the strbuf "out".
 * Note two caveats about "opts":
 *
 *   - this is primarily a helper for pretty.c, and not
 *     all of the flags are supported.
 *
 *   - this differs from process_trailers slightly in that we always format
 *     only the trailer block itself, even if the "only_trailers" option is not
 *     set.
 */
void format_trailers_from_commit(struct strbuf *out, const char *msg,
				 const struct process_trailer_options *opts);

/*
 * An interface for iterating over the trailers found in a particular commit
 * message. Use like:
 *
 *   struct trailer_iterator iter;
 *   trailer_iterator_init(&iter, msg);
 *   while (trailer_iterator_advance(&iter))
 *      ... do something with iter.key and iter.val ...
 *   trailer_iterator_release(&iter);
 */
struct trailer_iterator {
	struct strbuf key;
	struct strbuf val;

	/* private */
	struct {
		struct trailer_info info;
		size_t cur;
	} internal;
};

/*
 * Initialize "iter" in preparation for walking over the trailers in the commit
 * message "msg". The "msg" pointer must remain valid until the iterator is
 * released.
 *
 * After initializing, note that key/val will not yet point to any trailer.
 * Call advance() to parse the first one (if any).
 */
void trailer_iterator_init(struct trailer_iterator *iter, const char *msg);

/*
 * Advance to the next trailer of the iterator. Returns 0 if there is no such
 * trailer, and 1 otherwise. The key and value of the trailer can be
 * fetched from the iter->key and iter->value fields (which are valid
 * only until the next advance).
 */
int trailer_iterator_advance(struct trailer_iterator *iter);

/*
 * Release all resources associated with the trailer iteration.
 */
void trailer_iterator_release(struct trailer_iterator *iter);

#endif /* TRAILER_H */
