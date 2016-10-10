/*
 * Blame
 *
 * Copyright (c) 2006, 2014 by its authors
 * See COPYING for licensing conditions
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "quote.h"
#include "xdiff-interface.h"
#include "cache-tree.h"
#include "string-list.h"
#include "mailmap.h"
#include "mergesort.h"
#include "parse-options.h"
#include "prio-queue.h"
#include "utf8.h"
#include "userdiff.h"
#include "line-range.h"
#include "line-log.h"
#include "dir.h"
#include "progress.h"

static char blame_usage[] = N_("git blame [<options>] [<rev-opts>] [<rev>] [--] <file>");

static const char *blame_opt_usage[] = {
	blame_usage,
	"",
	N_("<rev-opts> are documented in git-rev-list(1)"),
	NULL
};

static int longest_file;
static int longest_author;
static int max_orig_digits;
static int max_digits;
static int max_score_digits;
static int show_root;
static int reverse;
static int blank_boundary;
static int incremental;
static int xdl_opts;
static int abbrev = -1;
static int no_whole_file_rename;
static int show_progress;

static struct date_mode blame_date_mode = { DATE_ISO8601 };
static size_t blame_date_width;

static struct string_list mailmap = STRING_LIST_INIT_NODUP;

#ifndef DEBUG
#define DEBUG 0
#endif

/* stats */
static int num_read_blob;
static int num_get_patch;
static int num_commits;

#define PICKAXE_BLAME_MOVE		01
#define PICKAXE_BLAME_COPY		02
#define PICKAXE_BLAME_COPY_HARDER	04
#define PICKAXE_BLAME_COPY_HARDEST	010

/*
 * blame for a blame_entry with score lower than these thresholds
 * is not passed to the parent using move/copy logic.
 */
static unsigned blame_move_score;
static unsigned blame_copy_score;
#define BLAME_DEFAULT_MOVE_SCORE	20
#define BLAME_DEFAULT_COPY_SCORE	40

/* Remember to update object flag allocation in object.h */
#define METAINFO_SHOWN		(1u<<12)
#define MORE_THAN_ONE_PATH	(1u<<13)

/*
 * One blob in a commit that is being suspected
 */
struct origin {
	int refcnt;
	/* Record preceding blame record for this blob */
	struct origin *previous;
	/* origins are put in a list linked via `next' hanging off the
	 * corresponding commit's util field in order to make finding
	 * them fast.  The presence in this chain does not count
	 * towards the origin's reference count.  It is tempting to
	 * let it count as long as the commit is pending examination,
	 * but even under circumstances where the commit will be
	 * present multiple times in the priority queue of unexamined
	 * commits, processing the first instance will not leave any
	 * work requiring the origin data for the second instance.  An
	 * interspersed commit changing that would have to be
	 * preexisting with a different ancestry and with the same
	 * commit date in order to wedge itself between two instances
	 * of the same commit in the priority queue _and_ produce
	 * blame entries relevant for it.  While we don't want to let
	 * us get tripped up by this case, it certainly does not seem
	 * worth optimizing for.
	 */
	struct origin *next;
	struct commit *commit;
	/* `suspects' contains blame entries that may be attributed to
	 * this origin's commit or to parent commits.  When a commit
	 * is being processed, all suspects will be moved, either by
	 * assigning them to an origin in a different commit, or by
	 * shipping them to the scoreboard's ent list because they
	 * cannot be attributed to a different commit.
	 */
	struct blame_entry *suspects;
	mmfile_t file;
	struct object_id blob_oid;
	unsigned mode;
	/* guilty gets set when shipping any suspects to the final
	 * blame list instead of other commits
	 */
	char guilty;
	char path[FLEX_ARRAY];
};

struct progress_info {
	struct progress *progress;
	int blamed_lines;
};

static int diff_hunks(mmfile_t *file_a, mmfile_t *file_b,
		      xdl_emit_hunk_consume_func_t hunk_func, void *cb_data)
{
	xpparam_t xpp = {0};
	xdemitconf_t xecfg = {0};
	xdemitcb_t ecb = {NULL};

	xpp.flags = xdl_opts;
	xecfg.hunk_func = hunk_func;
	ecb.priv = cb_data;
	return xdi_diff(file_a, file_b, &xpp, &xecfg, &ecb);
}

/*
 * Prepare diff_filespec and convert it using diff textconv API
 * if the textconv driver exists.
 * Return 1 if the conversion succeeds, 0 otherwise.
 */
int textconv_object(const char *path,
		    unsigned mode,
		    const struct object_id *oid,
		    int oid_valid,
		    char **buf,
		    unsigned long *buf_size)
{
	struct diff_filespec *df;
	struct userdiff_driver *textconv;

	df = alloc_filespec(path);
	fill_filespec(df, oid->hash, oid_valid, mode);
	textconv = get_textconv(df);
	if (!textconv) {
		free_filespec(df);
		return 0;
	}

	*buf_size = fill_textconv(textconv, df, buf);
	free_filespec(df);
	return 1;
}

/*
 * Given an origin, prepare mmfile_t structure to be used by the
 * diff machinery
 */
static void fill_origin_blob(struct diff_options *opt,
			     struct origin *o, mmfile_t *file)
{
	if (!o->file.ptr) {
		enum object_type type;
		unsigned long file_size;

		num_read_blob++;
		if (DIFF_OPT_TST(opt, ALLOW_TEXTCONV) &&
		    textconv_object(o->path, o->mode, &o->blob_oid, 1, &file->ptr, &file_size))
			;
		else
			file->ptr = read_sha1_file(o->blob_oid.hash, &type,
						   &file_size);
		file->size = file_size;

		if (!file->ptr)
			die("Cannot read blob %s for path %s",
			    oid_to_hex(&o->blob_oid),
			    o->path);
		o->file = *file;
	}
	else
		*file = o->file;
}

/*
 * Origin is refcounted and usually we keep the blob contents to be
 * reused.
 */
static inline struct origin *origin_incref(struct origin *o)
{
	if (o)
		o->refcnt++;
	return o;
}

static void origin_decref(struct origin *o)
{
	if (o && --o->refcnt <= 0) {
		struct origin *p, *l = NULL;
		if (o->previous)
			origin_decref(o->previous);
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
		die("internal error in blame::origin_decref");
	}
}

static void drop_origin_blob(struct origin *o)
{
	if (o->file.ptr) {
		free(o->file.ptr);
		o->file.ptr = NULL;
	}
}

/*
 * Each group of lines is described by a blame_entry; it can be split
 * as we pass blame to the parents.  They are arranged in linked lists
 * kept as `suspects' of some unprocessed origin, or entered (when the
 * blame origin has been finalized) into the scoreboard structure.
 * While the scoreboard structure is only sorted at the end of
 * processing (according to final image line number), the lists
 * attached to an origin are sorted by the target line number.
 */
struct blame_entry {
	struct blame_entry *next;

	/* the first line of this group in the final image;
	 * internally all line numbers are 0 based.
	 */
	int lno;

	/* how many lines this group has */
	int num_lines;

	/* the commit that introduced this group into the final image */
	struct origin *suspect;

	/* the line number of the first line of this group in the
	 * suspect's file; internally all line numbers are 0 based.
	 */
	int s_lno;

	/* how significant this entry is -- cached to avoid
	 * scanning the lines over and over.
	 */
	unsigned score;
};

/*
 * Any merge of blames happens on lists of blames that arrived via
 * different parents in a single suspect.  In this case, we want to
 * sort according to the suspect line numbers as opposed to the final
 * image line numbers.  The function body is somewhat longish because
 * it avoids unnecessary writes.
 */

static struct blame_entry *blame_merge(struct blame_entry *list1,
				       struct blame_entry *list2)
{
	struct blame_entry *p1 = list1, *p2 = list2,
		**tail = &list1;

	if (!p1)
		return p2;
	if (!p2)
		return p1;

	if (p1->s_lno <= p2->s_lno) {
		do {
			tail = &p1->next;
			if ((p1 = *tail) == NULL) {
				*tail = p2;
				return list1;
			}
		} while (p1->s_lno <= p2->s_lno);
	}
	for (;;) {
		*tail = p2;
		do {
			tail = &p2->next;
			if ((p2 = *tail) == NULL)  {
				*tail = p1;
				return list1;
			}
		} while (p1->s_lno > p2->s_lno);
		*tail = p1;
		do {
			tail = &p1->next;
			if ((p1 = *tail) == NULL) {
				*tail = p2;
				return list1;
			}
		} while (p1->s_lno <= p2->s_lno);
	}
}

static void *get_next_blame(const void *p)
{
	return ((struct blame_entry *)p)->next;
}

static void set_next_blame(void *p1, void *p2)
{
	((struct blame_entry *)p1)->next = p2;
}

/*
 * Final image line numbers are all different, so we don't need a
 * three-way comparison here.
 */

static int compare_blame_final(const void *p1, const void *p2)
{
	return ((struct blame_entry *)p1)->lno > ((struct blame_entry *)p2)->lno
		? 1 : -1;
}

static int compare_blame_suspect(const void *p1, const void *p2)
{
	const struct blame_entry *s1 = p1, *s2 = p2;
	/*
	 * to allow for collating suspects, we sort according to the
	 * respective pointer value as the primary sorting criterion.
	 * The actual relation is pretty unimportant as long as it
	 * establishes a total order.  Comparing as integers gives us
	 * that.
	 */
	if (s1->suspect != s2->suspect)
		return (intptr_t)s1->suspect > (intptr_t)s2->suspect ? 1 : -1;
	if (s1->s_lno == s2->s_lno)
		return 0;
	return s1->s_lno > s2->s_lno ? 1 : -1;
}

static struct blame_entry *blame_sort(struct blame_entry *head,
				      int (*compare_fn)(const void *, const void *))
{
	return llist_mergesort (head, get_next_blame, set_next_blame, compare_fn);
}

static int compare_commits_by_reverse_commit_date(const void *a,
						  const void *b,
						  void *c)
{
	return -compare_commits_by_commit_date(a, b, c);
}

/*
 * The current state of the blame assignment.
 */
struct scoreboard {
	/* the final commit (i.e. where we started digging from) */
	struct commit *final;
	/* Priority queue for commits with unassigned blame records */
	struct prio_queue commits;
	struct rev_info *revs;
	const char *path;

	/*
	 * The contents in the final image.
	 * Used by many functions to obtain contents of the nth line,
	 * indexed with scoreboard.lineno[blame_entry.lno].
	 */
	const char *final_buf;
	unsigned long final_buf_size;

	/* linked list of blames */
	struct blame_entry *ent;

	/* look-up a line in the final buffer */
	int num_lines;
	int *lineno;
};

static void sanity_check_refcnt(struct scoreboard *);

/*
 * If two blame entries that are next to each other came from
 * contiguous lines in the same origin (i.e. <commit, path> pair),
 * merge them together.
 */
static void coalesce(struct scoreboard *sb)
{
	struct blame_entry *ent, *next;

	for (ent = sb->ent; ent && (next = ent->next); ent = next) {
		if (ent->suspect == next->suspect &&
		    ent->s_lno + ent->num_lines == next->s_lno) {
			ent->num_lines += next->num_lines;
			ent->next = next->next;
			origin_decref(next->suspect);
			free(next);
			ent->score = 0;
			next = ent; /* again */
		}
	}

	if (DEBUG) /* sanity */
		sanity_check_refcnt(sb);
}

/*
 * Merge the given sorted list of blames into a preexisting origin.
 * If there were no previous blames to that commit, it is entered into
 * the commit priority queue of the score board.
 */

static void queue_blames(struct scoreboard *sb, struct origin *porigin,
			 struct blame_entry *sorted)
{
	if (porigin->suspects)
		porigin->suspects = blame_merge(porigin->suspects, sorted);
	else {
		struct origin *o;
		for (o = porigin->commit->util; o; o = o->next) {
			if (o->suspects) {
				porigin->suspects = sorted;
				return;
			}
		}
		porigin->suspects = sorted;
		prio_queue_put(&sb->commits, porigin->commit);
	}
}

/*
 * Given a commit and a path in it, create a new origin structure.
 * The callers that add blame to the scoreboard should use
 * get_origin() to obtain shared, refcounted copy instead of calling
 * this function directly.
 */
static struct origin *make_origin(struct commit *commit, const char *path)
{
	struct origin *o;
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
static struct origin *get_origin(struct scoreboard *sb,
				 struct commit *commit,
				 const char *path)
{
	struct origin *o, *l;

	for (o = commit->util, l = NULL; o; l = o, o = o->next) {
		if (!strcmp(o->path, path)) {
			/* bump to front */
			if (l) {
				l->next = o->next;
				o->next = commit->util;
				commit->util = o;
			}
			return origin_incref(o);
		}
	}
	return make_origin(commit, path);
}

/*
 * Fill the blob_sha1 field of an origin if it hasn't, so that later
 * call to fill_origin_blob() can use it to locate the data.  blob_sha1
 * for an origin is also used to pass the blame for the entire file to
 * the parent to detect the case where a child's blob is identical to
 * that of its parent's.
 *
 * This also fills origin->mode for corresponding tree path.
 */
static int fill_blob_sha1_and_mode(struct origin *origin)
{
	if (!is_null_oid(&origin->blob_oid))
		return 0;
	if (get_tree_entry(origin->commit->object.oid.hash,
			   origin->path,
			   origin->blob_oid.hash, &origin->mode))
		goto error_out;
	if (sha1_object_info(origin->blob_oid.hash, NULL) != OBJ_BLOB)
		goto error_out;
	return 0;
 error_out:
	oidclr(&origin->blob_oid);
	origin->mode = S_IFINVALID;
	return -1;
}

/*
 * We have an origin -- check if the same path exists in the
 * parent and return an origin structure to represent it.
 */
static struct origin *find_origin(struct scoreboard *sb,
				  struct commit *parent,
				  struct origin *origin)
{
	struct origin *porigin;
	struct diff_options diff_opts;
	const char *paths[2];

	/* First check any existing origins */
	for (porigin = parent->util; porigin; porigin = porigin->next)
		if (!strcmp(porigin->path, origin->path)) {
			/*
			 * The same path between origin and its parent
			 * without renaming -- the most common case.
			 */
			return origin_incref (porigin);
		}

	/* See if the origin->path is different between parent
	 * and origin first.  Most of the time they are the
	 * same and diff-tree is fairly efficient about this.
	 */
	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	diff_opts.detect_rename = 0;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	paths[0] = origin->path;
	paths[1] = NULL;

	parse_pathspec(&diff_opts.pathspec,
		       PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
		       PATHSPEC_LITERAL_PATH, "", paths);
	diff_setup_done(&diff_opts);

	if (is_null_oid(&origin->commit->object.oid))
		do_diff_cache(parent->tree->object.oid.hash, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.oid.hash,
			       origin->commit->tree->object.oid.hash,
			       "", &diff_opts);
	diffcore_std(&diff_opts);

	if (!diff_queued_diff.nr) {
		/* The path is the same as parent */
		porigin = get_origin(sb, parent, origin->path);
		oidcpy(&porigin->blob_oid, &origin->blob_oid);
		porigin->mode = origin->mode;
	} else {
		/*
		 * Since origin->path is a pathspec, if the parent
		 * commit had it as a directory, we will see a whole
		 * bunch of deletion of files in the directory that we
		 * do not care about.
		 */
		int i;
		struct diff_filepair *p = NULL;
		for (i = 0; i < diff_queued_diff.nr; i++) {
			const char *name;
			p = diff_queued_diff.queue[i];
			name = p->one->path ? p->one->path : p->two->path;
			if (!strcmp(name, origin->path))
				break;
		}
		if (!p)
			die("internal error in blame::find_origin");
		switch (p->status) {
		default:
			die("internal error in blame::find_origin (%c)",
			    p->status);
		case 'M':
			porigin = get_origin(sb, parent, origin->path);
			oidcpy(&porigin->blob_oid, &p->one->oid);
			porigin->mode = p->one->mode;
			break;
		case 'A':
		case 'T':
			/* Did not exist in parent, or type changed */
			break;
		}
	}
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
	return porigin;
}

/*
 * We have an origin -- find the path that corresponds to it in its
 * parent and return an origin structure to represent it.
 */
static struct origin *find_rename(struct scoreboard *sb,
				  struct commit *parent,
				  struct origin *origin)
{
	struct origin *porigin = NULL;
	struct diff_options diff_opts;
	int i;

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = origin->path;
	diff_setup_done(&diff_opts);

	if (is_null_oid(&origin->commit->object.oid))
		do_diff_cache(parent->tree->object.oid.hash, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.oid.hash,
			       origin->commit->tree->object.oid.hash,
			       "", &diff_opts);
	diffcore_std(&diff_opts);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, origin->path)) {
			porigin = get_origin(sb, parent, p->one->path);
			oidcpy(&porigin->blob_oid, &p->one->oid);
			porigin->mode = p->one->mode;
			break;
		}
	}
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
	return porigin;
}

/*
 * Append a new blame entry to a given output queue.
 */
static void add_blame_entry(struct blame_entry ***queue, struct blame_entry *e)
{
	origin_incref(e->suspect);

	e->next = **queue;
	**queue = e;
	*queue = &e->next;
}

/*
 * src typically is on-stack; we want to copy the information in it to
 * a malloced blame_entry that gets added to the given queue.  The
 * origin of dst loses a refcnt.
 */
static void dup_entry(struct blame_entry ***queue,
		      struct blame_entry *dst, struct blame_entry *src)
{
	origin_incref(src->suspect);
	origin_decref(dst->suspect);
	memcpy(dst, src, sizeof(*src));
	dst->next = **queue;
	**queue = dst;
	*queue = &dst->next;
}

static const char *nth_line(struct scoreboard *sb, long lno)
{
	return sb->final_buf + sb->lineno[lno];
}

static const char *nth_line_cb(void *data, long lno)
{
	return nth_line((struct scoreboard *)data, lno);
}

/*
 * It is known that lines between tlno to same came from parent, and e
 * has an overlap with that range.  it also is known that parent's
 * line plno corresponds to e's line tlno.
 *
 *                <---- e ----->
 *                   <------>
 *                   <------------>
 *             <------------>
 *             <------------------>
 *
 * Split e into potentially three parts; before this chunk, the chunk
 * to be blamed for the parent, and after that portion.
 */
static void split_overlap(struct blame_entry *split,
			  struct blame_entry *e,
			  int tlno, int plno, int same,
			  struct origin *parent)
{
	int chunk_end_lno;
	memset(split, 0, sizeof(struct blame_entry [3]));

	if (e->s_lno < tlno) {
		/* there is a pre-chunk part not blamed on parent */
		split[0].suspect = origin_incref(e->suspect);
		split[0].lno = e->lno;
		split[0].s_lno = e->s_lno;
		split[0].num_lines = tlno - e->s_lno;
		split[1].lno = e->lno + tlno - e->s_lno;
		split[1].s_lno = plno;
	}
	else {
		split[1].lno = e->lno;
		split[1].s_lno = plno + (e->s_lno - tlno);
	}

	if (same < e->s_lno + e->num_lines) {
		/* there is a post-chunk part not blamed on parent */
		split[2].suspect = origin_incref(e->suspect);
		split[2].lno = e->lno + (same - e->s_lno);
		split[2].s_lno = e->s_lno + (same - e->s_lno);
		split[2].num_lines = e->s_lno + e->num_lines - same;
		chunk_end_lno = split[2].lno;
	}
	else
		chunk_end_lno = e->lno + e->num_lines;
	split[1].num_lines = chunk_end_lno - split[1].lno;

	/*
	 * if it turns out there is nothing to blame the parent for,
	 * forget about the splitting.  !split[1].suspect signals this.
	 */
	if (split[1].num_lines < 1)
		return;
	split[1].suspect = origin_incref(parent);
}

/*
 * split_overlap() divided an existing blame e into up to three parts
 * in split.  Any assigned blame is moved to queue to
 * reflect the split.
 */
static void split_blame(struct blame_entry ***blamed,
			struct blame_entry ***unblamed,
			struct blame_entry *split,
			struct blame_entry *e)
{
	struct blame_entry *new_entry;

	if (split[0].suspect && split[2].suspect) {
		/* The first part (reuse storage for the existing entry e) */
		dup_entry(unblamed, e, &split[0]);

		/* The last part -- me */
		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(unblamed, new_entry);

		/* ... and the middle part -- parent */
		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(blamed, new_entry);
	}
	else if (!split[0].suspect && !split[2].suspect)
		/*
		 * The parent covers the entire area; reuse storage for
		 * e and replace it with the parent.
		 */
		dup_entry(blamed, e, &split[1]);
	else if (split[0].suspect) {
		/* me and then parent */
		dup_entry(unblamed, e, &split[0]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(blamed, new_entry);
	}
	else {
		/* parent and then me */
		dup_entry(blamed, e, &split[1]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(unblamed, new_entry);
	}
}

/*
 * After splitting the blame, the origins used by the
 * on-stack blame_entry should lose one refcnt each.
 */
static void decref_split(struct blame_entry *split)
{
	int i;

	for (i = 0; i < 3; i++)
		origin_decref(split[i].suspect);
}

/*
 * reverse_blame reverses the list given in head, appending tail.
 * That allows us to build lists in reverse order, then reverse them
 * afterwards.  This can be faster than building the list in proper
 * order right away.  The reason is that building in proper order
 * requires writing a link in the _previous_ element, while building
 * in reverse order just requires placing the list head into the
 * _current_ element.
 */

static struct blame_entry *reverse_blame(struct blame_entry *head,
					 struct blame_entry *tail)
{
	while (head) {
		struct blame_entry *next = head->next;
		head->next = tail;
		tail = head;
		head = next;
	}
	return tail;
}

/*
 * Process one hunk from the patch between the current suspect for
 * blame_entry e and its parent.  This first blames any unfinished
 * entries before the chunk (which is where target and parent start
 * differing) on the parent, and then splits blame entries at the
 * start and at the end of the difference region.  Since use of -M and
 * -C options may lead to overlapping/duplicate source line number
 * ranges, all we can rely on from sorting/merging is the order of the
 * first suspect line number.
 */
static void blame_chunk(struct blame_entry ***dstq, struct blame_entry ***srcq,
			int tlno, int offset, int same,
			struct origin *parent)
{
	struct blame_entry *e = **srcq;
	struct blame_entry *samep = NULL, *diffp = NULL;

	while (e && e->s_lno < tlno) {
		struct blame_entry *next = e->next;
		/*
		 * current record starts before differing portion.  If
		 * it reaches into it, we need to split it up and
		 * examine the second part separately.
		 */
		if (e->s_lno + e->num_lines > tlno) {
			/* Move second half to a new record */
			int len = tlno - e->s_lno;
			struct blame_entry *n = xcalloc(1, sizeof (struct blame_entry));
			n->suspect = e->suspect;
			n->lno = e->lno + len;
			n->s_lno = e->s_lno + len;
			n->num_lines = e->num_lines - len;
			e->num_lines = len;
			e->score = 0;
			/* Push new record to diffp */
			n->next = diffp;
			diffp = n;
		} else
			origin_decref(e->suspect);
		/* Pass blame for everything before the differing
		 * chunk to the parent */
		e->suspect = origin_incref(parent);
		e->s_lno += offset;
		e->next = samep;
		samep = e;
		e = next;
	}
	/*
	 * As we don't know how much of a common stretch after this
	 * diff will occur, the currently blamed parts are all that we
	 * can assign to the parent for now.
	 */

	if (samep) {
		**dstq = reverse_blame(samep, **dstq);
		*dstq = &samep->next;
	}
	/*
	 * Prepend the split off portions: everything after e starts
	 * after the blameable portion.
	 */
	e = reverse_blame(diffp, e);

	/*
	 * Now retain records on the target while parts are different
	 * from the parent.
	 */
	samep = NULL;
	diffp = NULL;
	while (e && e->s_lno < same) {
		struct blame_entry *next = e->next;

		/*
		 * If current record extends into sameness, need to split.
		 */
		if (e->s_lno + e->num_lines > same) {
			/*
			 * Move second half to a new record to be
			 * processed by later chunks
			 */
			int len = same - e->s_lno;
			struct blame_entry *n = xcalloc(1, sizeof (struct blame_entry));
			n->suspect = origin_incref(e->suspect);
			n->lno = e->lno + len;
			n->s_lno = e->s_lno + len;
			n->num_lines = e->num_lines - len;
			e->num_lines = len;
			e->score = 0;
			/* Push new record to samep */
			n->next = samep;
			samep = n;
		}
		e->next = diffp;
		diffp = e;
		e = next;
	}
	**srcq = reverse_blame(diffp, reverse_blame(samep, e));
	/* Move across elements that are in the unblamable portion */
	if (diffp)
		*srcq = &diffp->next;
}

struct blame_chunk_cb_data {
	struct origin *parent;
	long offset;
	struct blame_entry **dstq;
	struct blame_entry **srcq;
};

/* diff chunks are from parent to target */
static int blame_chunk_cb(long start_a, long count_a,
			  long start_b, long count_b, void *data)
{
	struct blame_chunk_cb_data *d = data;
	if (start_a - start_b != d->offset)
		die("internal error in blame::blame_chunk_cb");
	blame_chunk(&d->dstq, &d->srcq, start_b, start_a - start_b,
		    start_b + count_b, d->parent);
	d->offset = start_a + count_a - (start_b + count_b);
	return 0;
}

/*
 * We are looking at the origin 'target' and aiming to pass blame
 * for the lines it is suspected to its parent.  Run diff to find
 * which lines came from parent and pass blame for them.
 */
static void pass_blame_to_parent(struct scoreboard *sb,
				 struct origin *target,
				 struct origin *parent)
{
	mmfile_t file_p, file_o;
	struct blame_chunk_cb_data d;
	struct blame_entry *newdest = NULL;

	if (!target->suspects)
		return; /* nothing remains for this target */

	d.parent = parent;
	d.offset = 0;
	d.dstq = &newdest; d.srcq = &target->suspects;

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p);
	fill_origin_blob(&sb->revs->diffopt, target, &file_o);
	num_get_patch++;

	if (diff_hunks(&file_p, &file_o, blame_chunk_cb, &d))
		die("unable to generate diff (%s -> %s)",
		    oid_to_hex(&parent->commit->object.oid),
		    oid_to_hex(&target->commit->object.oid));
	/* The rest are the same as the parent */
	blame_chunk(&d.dstq, &d.srcq, INT_MAX, d.offset, INT_MAX, parent);
	*d.dstq = NULL;
	queue_blames(sb, parent, newdest);

	return;
}

/*
 * The lines in blame_entry after splitting blames many times can become
 * very small and trivial, and at some point it becomes pointless to
 * blame the parents.  E.g. "\t\t}\n\t}\n\n" appears everywhere in any
 * ordinary C program, and it is not worth to say it was copied from
 * totally unrelated file in the parent.
 *
 * Compute how trivial the lines in the blame_entry are.
 */
static unsigned ent_score(struct scoreboard *sb, struct blame_entry *e)
{
	unsigned score;
	const char *cp, *ep;

	if (e->score)
		return e->score;

	score = 1;
	cp = nth_line(sb, e->lno);
	ep = nth_line(sb, e->lno + e->num_lines);
	while (cp < ep) {
		unsigned ch = *((unsigned char *)cp);
		if (isalnum(ch))
			score++;
		cp++;
	}
	e->score = score;
	return score;
}

/*
 * best_so_far[] and this[] are both a split of an existing blame_entry
 * that passes blame to the parent.  Maintain best_so_far the best split
 * so far, by comparing this and best_so_far and copying this into
 * bst_so_far as needed.
 */
static void copy_split_if_better(struct scoreboard *sb,
				 struct blame_entry *best_so_far,
				 struct blame_entry *this)
{
	int i;

	if (!this[1].suspect)
		return;
	if (best_so_far[1].suspect) {
		if (ent_score(sb, &this[1]) < ent_score(sb, &best_so_far[1]))
			return;
	}

	for (i = 0; i < 3; i++)
		origin_incref(this[i].suspect);
	decref_split(best_so_far);
	memcpy(best_so_far, this, sizeof(struct blame_entry [3]));
}

/*
 * We are looking at a part of the final image represented by
 * ent (tlno and same are offset by ent->s_lno).
 * tlno is where we are looking at in the final image.
 * up to (but not including) same match preimage.
 * plno is where we are looking at in the preimage.
 *
 * <-------------- final image ---------------------->
 *       <------ent------>
 *         ^tlno ^same
 *    <---------preimage----->
 *         ^plno
 *
 * All line numbers are 0-based.
 */
static void handle_split(struct scoreboard *sb,
			 struct blame_entry *ent,
			 int tlno, int plno, int same,
			 struct origin *parent,
			 struct blame_entry *split)
{
	if (ent->num_lines <= tlno)
		return;
	if (tlno < same) {
		struct blame_entry this[3];
		tlno += ent->s_lno;
		same += ent->s_lno;
		split_overlap(this, ent, tlno, plno, same, parent);
		copy_split_if_better(sb, split, this);
		decref_split(this);
	}
}

struct handle_split_cb_data {
	struct scoreboard *sb;
	struct blame_entry *ent;
	struct origin *parent;
	struct blame_entry *split;
	long plno;
	long tlno;
};

static int handle_split_cb(long start_a, long count_a,
			   long start_b, long count_b, void *data)
{
	struct handle_split_cb_data *d = data;
	handle_split(d->sb, d->ent, d->tlno, d->plno, start_b, d->parent,
		     d->split);
	d->plno = start_a + count_a;
	d->tlno = start_b + count_b;
	return 0;
}

/*
 * Find the lines from parent that are the same as ent so that
 * we can pass blames to it.  file_p has the blob contents for
 * the parent.
 */
static void find_copy_in_blob(struct scoreboard *sb,
			      struct blame_entry *ent,
			      struct origin *parent,
			      struct blame_entry *split,
			      mmfile_t *file_p)
{
	const char *cp;
	mmfile_t file_o;
	struct handle_split_cb_data d;

	memset(&d, 0, sizeof(d));
	d.sb = sb; d.ent = ent; d.parent = parent; d.split = split;
	/*
	 * Prepare mmfile that contains only the lines in ent.
	 */
	cp = nth_line(sb, ent->lno);
	file_o.ptr = (char *) cp;
	file_o.size = nth_line(sb, ent->lno + ent->num_lines) - cp;

	/*
	 * file_o is a part of final image we are annotating.
	 * file_p partially may match that image.
	 */
	memset(split, 0, sizeof(struct blame_entry [3]));
	if (diff_hunks(file_p, &file_o, handle_split_cb, &d))
		die("unable to generate diff (%s)",
		    oid_to_hex(&parent->commit->object.oid));
	/* remainder, if any, all match the preimage */
	handle_split(sb, ent, d.tlno, d.plno, ent->num_lines, parent, split);
}

/* Move all blame entries from list *source that have a score smaller
 * than score_min to the front of list *small.
 * Returns a pointer to the link pointing to the old head of the small list.
 */

static struct blame_entry **filter_small(struct scoreboard *sb,
					 struct blame_entry **small,
					 struct blame_entry **source,
					 unsigned score_min)
{
	struct blame_entry *p = *source;
	struct blame_entry *oldsmall = *small;
	while (p) {
		if (ent_score(sb, p) <= score_min) {
			*small = p;
			small = &p->next;
			p = *small;
		} else {
			*source = p;
			source = &p->next;
			p = *source;
		}
	}
	*small = oldsmall;
	*source = NULL;
	return small;
}

/*
 * See if lines currently target is suspected for can be attributed to
 * parent.
 */
static void find_move_in_parent(struct scoreboard *sb,
				struct blame_entry ***blamed,
				struct blame_entry **toosmall,
				struct origin *target,
				struct origin *parent)
{
	struct blame_entry *e, split[3];
	struct blame_entry *unblamed = target->suspects;
	struct blame_entry *leftover = NULL;
	mmfile_t file_p;

	if (!unblamed)
		return; /* nothing remains for this target */

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p);
	if (!file_p.ptr)
		return;

	/* At each iteration, unblamed has a NULL-terminated list of
	 * entries that have not yet been tested for blame.  leftover
	 * contains the reversed list of entries that have been tested
	 * without being assignable to the parent.
	 */
	do {
		struct blame_entry **unblamedtail = &unblamed;
		struct blame_entry *next;
		for (e = unblamed; e; e = next) {
			next = e->next;
			find_copy_in_blob(sb, e, parent, split, &file_p);
			if (split[1].suspect &&
			    blame_move_score < ent_score(sb, &split[1])) {
				split_blame(blamed, &unblamedtail, split, e);
			} else {
				e->next = leftover;
				leftover = e;
			}
			decref_split(split);
		}
		*unblamedtail = NULL;
		toosmall = filter_small(sb, toosmall, &unblamed, blame_move_score);
	} while (unblamed);
	target->suspects = reverse_blame(leftover, NULL);
}

struct blame_list {
	struct blame_entry *ent;
	struct blame_entry split[3];
};

/*
 * Count the number of entries the target is suspected for,
 * and prepare a list of entry and the best split.
 */
static struct blame_list *setup_blame_list(struct blame_entry *unblamed,
					   int *num_ents_p)
{
	struct blame_entry *e;
	int num_ents, i;
	struct blame_list *blame_list = NULL;

	for (e = unblamed, num_ents = 0; e; e = e->next)
		num_ents++;
	if (num_ents) {
		blame_list = xcalloc(num_ents, sizeof(struct blame_list));
		for (e = unblamed, i = 0; e; e = e->next)
			blame_list[i++].ent = e;
	}
	*num_ents_p = num_ents;
	return blame_list;
}

/*
 * For lines target is suspected for, see if we can find code movement
 * across file boundary from the parent commit.  porigin is the path
 * in the parent we already tried.
 */
static void find_copy_in_parent(struct scoreboard *sb,
				struct blame_entry ***blamed,
				struct blame_entry **toosmall,
				struct origin *target,
				struct commit *parent,
				struct origin *porigin,
				int opt)
{
	struct diff_options diff_opts;
	int i, j;
	struct blame_list *blame_list;
	int num_ents;
	struct blame_entry *unblamed = target->suspects;
	struct blame_entry *leftover = NULL;

	if (!unblamed)
		return; /* nothing remains for this target */

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;

	diff_setup_done(&diff_opts);

	/* Try "find copies harder" on new path if requested;
	 * we do not want to use diffcore_rename() actually to
	 * match things up; find_copies_harder is set only to
	 * force diff_tree_sha1() to feed all filepairs to diff_queue,
	 * and this code needs to be after diff_setup_done(), which
	 * usually makes find-copies-harder imply copy detection.
	 */
	if ((opt & PICKAXE_BLAME_COPY_HARDEST)
	    || ((opt & PICKAXE_BLAME_COPY_HARDER)
		&& (!porigin || strcmp(target->path, porigin->path))))
		DIFF_OPT_SET(&diff_opts, FIND_COPIES_HARDER);

	if (is_null_oid(&target->commit->object.oid))
		do_diff_cache(parent->tree->object.oid.hash, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.oid.hash,
			       target->commit->tree->object.oid.hash,
			       "", &diff_opts);

	if (!DIFF_OPT_TST(&diff_opts, FIND_COPIES_HARDER))
		diffcore_std(&diff_opts);

	do {
		struct blame_entry **unblamedtail = &unblamed;
		blame_list = setup_blame_list(unblamed, &num_ents);

		for (i = 0; i < diff_queued_diff.nr; i++) {
			struct diff_filepair *p = diff_queued_diff.queue[i];
			struct origin *norigin;
			mmfile_t file_p;
			struct blame_entry this[3];

			if (!DIFF_FILE_VALID(p->one))
				continue; /* does not exist in parent */
			if (S_ISGITLINK(p->one->mode))
				continue; /* ignore git links */
			if (porigin && !strcmp(p->one->path, porigin->path))
				/* find_move already dealt with this path */
				continue;

			norigin = get_origin(sb, parent, p->one->path);
			oidcpy(&norigin->blob_oid, &p->one->oid);
			norigin->mode = p->one->mode;
			fill_origin_blob(&sb->revs->diffopt, norigin, &file_p);
			if (!file_p.ptr)
				continue;

			for (j = 0; j < num_ents; j++) {
				find_copy_in_blob(sb, blame_list[j].ent,
						  norigin, this, &file_p);
				copy_split_if_better(sb, blame_list[j].split,
						     this);
				decref_split(this);
			}
			origin_decref(norigin);
		}

		for (j = 0; j < num_ents; j++) {
			struct blame_entry *split = blame_list[j].split;
			if (split[1].suspect &&
			    blame_copy_score < ent_score(sb, &split[1])) {
				split_blame(blamed, &unblamedtail, split,
					    blame_list[j].ent);
			} else {
				blame_list[j].ent->next = leftover;
				leftover = blame_list[j].ent;
			}
			decref_split(split);
		}
		free(blame_list);
		*unblamedtail = NULL;
		toosmall = filter_small(sb, toosmall, &unblamed, blame_copy_score);
	} while (unblamed);
	target->suspects = reverse_blame(leftover, NULL);
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
}

/*
 * The blobs of origin and porigin exactly match, so everything
 * origin is suspected for can be blamed on the parent.
 */
static void pass_whole_blame(struct scoreboard *sb,
			     struct origin *origin, struct origin *porigin)
{
	struct blame_entry *e, *suspects;

	if (!porigin->file.ptr && origin->file.ptr) {
		/* Steal its file */
		porigin->file = origin->file;
		origin->file.ptr = NULL;
	}
	suspects = origin->suspects;
	origin->suspects = NULL;
	for (e = suspects; e; e = e->next) {
		origin_incref(porigin);
		origin_decref(e->suspect);
		e->suspect = porigin;
	}
	queue_blames(sb, porigin, suspects);
}

/*
 * We pass blame from the current commit to its parents.  We keep saying
 * "parent" (and "porigin"), but what we mean is to find scapegoat to
 * exonerate ourselves.
 */
static struct commit_list *first_scapegoat(struct rev_info *revs, struct commit *commit)
{
	if (!reverse) {
		if (revs->first_parent_only &&
		    commit->parents &&
		    commit->parents->next) {
			free_commit_list(commit->parents->next);
			commit->parents->next = NULL;
		}
		return commit->parents;
	}
	return lookup_decoration(&revs->children, &commit->object);
}

static int num_scapegoats(struct rev_info *revs, struct commit *commit)
{
	struct commit_list *l = first_scapegoat(revs, commit);
	return commit_list_count(l);
}

/* Distribute collected unsorted blames to the respected sorted lists
 * in the various origins.
 */
static void distribute_blame(struct scoreboard *sb, struct blame_entry *blamed)
{
	blamed = blame_sort(blamed, compare_blame_suspect);
	while (blamed)
	{
		struct origin *porigin = blamed->suspect;
		struct blame_entry *suspects = NULL;
		do {
			struct blame_entry *next = blamed->next;
			blamed->next = suspects;
			suspects = blamed;
			blamed = next;
		} while (blamed && blamed->suspect == porigin);
		suspects = reverse_blame(suspects, NULL);
		queue_blames(sb, porigin, suspects);
	}
}

#define MAXSG 16

static void pass_blame(struct scoreboard *sb, struct origin *origin, int opt)
{
	struct rev_info *revs = sb->revs;
	int i, pass, num_sg;
	struct commit *commit = origin->commit;
	struct commit_list *sg;
	struct origin *sg_buf[MAXSG];
	struct origin *porigin, **sg_origin = sg_buf;
	struct blame_entry *toosmall = NULL;
	struct blame_entry *blames, **blametail = &blames;

	num_sg = num_scapegoats(revs, commit);
	if (!num_sg)
		goto finish;
	else if (num_sg < ARRAY_SIZE(sg_buf))
		memset(sg_buf, 0, sizeof(sg_buf));
	else
		sg_origin = xcalloc(num_sg, sizeof(*sg_origin));

	/*
	 * The first pass looks for unrenamed path to optimize for
	 * common cases, then we look for renames in the second pass.
	 */
	for (pass = 0; pass < 2 - no_whole_file_rename; pass++) {
		struct origin *(*find)(struct scoreboard *,
				       struct commit *, struct origin *);
		find = pass ? find_rename : find_origin;

		for (i = 0, sg = first_scapegoat(revs, commit);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct commit *p = sg->item;
			int j, same;

			if (sg_origin[i])
				continue;
			if (parse_commit(p))
				continue;
			porigin = find(sb, p, origin);
			if (!porigin)
				continue;
			if (!oidcmp(&porigin->blob_oid, &origin->blob_oid)) {
				pass_whole_blame(sb, origin, porigin);
				origin_decref(porigin);
				goto finish;
			}
			for (j = same = 0; j < i; j++)
				if (sg_origin[j] &&
				    !oidcmp(&sg_origin[j]->blob_oid, &porigin->blob_oid)) {
					same = 1;
					break;
				}
			if (!same)
				sg_origin[i] = porigin;
			else
				origin_decref(porigin);
		}
	}

	num_commits++;
	for (i = 0, sg = first_scapegoat(revs, commit);
	     i < num_sg && sg;
	     sg = sg->next, i++) {
		struct origin *porigin = sg_origin[i];
		if (!porigin)
			continue;
		if (!origin->previous) {
			origin_incref(porigin);
			origin->previous = porigin;
		}
		pass_blame_to_parent(sb, origin, porigin);
		if (!origin->suspects)
			goto finish;
	}

	/*
	 * Optionally find moves in parents' files.
	 */
	if (opt & PICKAXE_BLAME_MOVE) {
		filter_small(sb, &toosmall, &origin->suspects, blame_move_score);
		if (origin->suspects) {
			for (i = 0, sg = first_scapegoat(revs, commit);
			     i < num_sg && sg;
			     sg = sg->next, i++) {
				struct origin *porigin = sg_origin[i];
				if (!porigin)
					continue;
				find_move_in_parent(sb, &blametail, &toosmall, origin, porigin);
				if (!origin->suspects)
					break;
			}
		}
	}

	/*
	 * Optionally find copies from parents' files.
	 */
	if (opt & PICKAXE_BLAME_COPY) {
		if (blame_copy_score > blame_move_score)
			filter_small(sb, &toosmall, &origin->suspects, blame_copy_score);
		else if (blame_copy_score < blame_move_score) {
			origin->suspects = blame_merge(origin->suspects, toosmall);
			toosmall = NULL;
			filter_small(sb, &toosmall, &origin->suspects, blame_copy_score);
		}
		if (!origin->suspects)
			goto finish;

		for (i = 0, sg = first_scapegoat(revs, commit);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct origin *porigin = sg_origin[i];
			find_copy_in_parent(sb, &blametail, &toosmall,
					    origin, sg->item, porigin, opt);
			if (!origin->suspects)
				goto finish;
		}
	}

finish:
	*blametail = NULL;
	distribute_blame(sb, blames);
	/*
	 * prepend toosmall to origin->suspects
	 *
	 * There is no point in sorting: this ends up on a big
	 * unsorted list in the caller anyway.
	 */
	if (toosmall) {
		struct blame_entry **tail = &toosmall;
		while (*tail)
			tail = &(*tail)->next;
		*tail = origin->suspects;
		origin->suspects = toosmall;
	}
	for (i = 0; i < num_sg; i++) {
		if (sg_origin[i]) {
			drop_origin_blob(sg_origin[i]);
			origin_decref(sg_origin[i]);
		}
	}
	drop_origin_blob(origin);
	if (sg_buf != sg_origin)
		free(sg_origin);
}

/*
 * Information on commits, used for output.
 */
struct commit_info {
	struct strbuf author;
	struct strbuf author_mail;
	unsigned long author_time;
	struct strbuf author_tz;

	/* filled only when asked for details */
	struct strbuf committer;
	struct strbuf committer_mail;
	unsigned long committer_time;
	struct strbuf committer_tz;

	struct strbuf summary;
};

/*
 * Parse author/committer line in the commit object buffer
 */
static void get_ac_line(const char *inbuf, const char *what,
	struct strbuf *name, struct strbuf *mail,
	unsigned long *time, struct strbuf *tz)
{
	struct ident_split ident;
	size_t len, maillen, namelen;
	char *tmp, *endp;
	const char *namebuf, *mailbuf;

	tmp = strstr(inbuf, what);
	if (!tmp)
		goto error_out;
	tmp += strlen(what);
	endp = strchr(tmp, '\n');
	if (!endp)
		len = strlen(tmp);
	else
		len = endp - tmp;

	if (split_ident_line(&ident, tmp, len)) {
	error_out:
		/* Ugh */
		tmp = "(unknown)";
		strbuf_addstr(name, tmp);
		strbuf_addstr(mail, tmp);
		strbuf_addstr(tz, tmp);
		*time = 0;
		return;
	}

	namelen = ident.name_end - ident.name_begin;
	namebuf = ident.name_begin;

	maillen = ident.mail_end - ident.mail_begin;
	mailbuf = ident.mail_begin;

	if (ident.date_begin && ident.date_end)
		*time = strtoul(ident.date_begin, NULL, 10);
	else
		*time = 0;

	if (ident.tz_begin && ident.tz_end)
		strbuf_add(tz, ident.tz_begin, ident.tz_end - ident.tz_begin);
	else
		strbuf_addstr(tz, "(unknown)");

	/*
	 * Now, convert both name and e-mail using mailmap
	 */
	map_user(&mailmap, &mailbuf, &maillen,
		 &namebuf, &namelen);

	strbuf_addf(mail, "<%.*s>", (int)maillen, mailbuf);
	strbuf_add(name, namebuf, namelen);
}

static void commit_info_init(struct commit_info *ci)
{

	strbuf_init(&ci->author, 0);
	strbuf_init(&ci->author_mail, 0);
	strbuf_init(&ci->author_tz, 0);
	strbuf_init(&ci->committer, 0);
	strbuf_init(&ci->committer_mail, 0);
	strbuf_init(&ci->committer_tz, 0);
	strbuf_init(&ci->summary, 0);
}

static void commit_info_destroy(struct commit_info *ci)
{

	strbuf_release(&ci->author);
	strbuf_release(&ci->author_mail);
	strbuf_release(&ci->author_tz);
	strbuf_release(&ci->committer);
	strbuf_release(&ci->committer_mail);
	strbuf_release(&ci->committer_tz);
	strbuf_release(&ci->summary);
}

static void get_commit_info(struct commit *commit,
			    struct commit_info *ret,
			    int detailed)
{
	int len;
	const char *subject, *encoding;
	const char *message;

	commit_info_init(ret);

	encoding = get_log_output_encoding();
	message = logmsg_reencode(commit, NULL, encoding);
	get_ac_line(message, "\nauthor ",
		    &ret->author, &ret->author_mail,
		    &ret->author_time, &ret->author_tz);

	if (!detailed) {
		unuse_commit_buffer(commit, message);
		return;
	}

	get_ac_line(message, "\ncommitter ",
		    &ret->committer, &ret->committer_mail,
		    &ret->committer_time, &ret->committer_tz);

	len = find_commit_subject(message, &subject);
	if (len)
		strbuf_add(&ret->summary, subject, len);
	else
		strbuf_addf(&ret->summary, "(%s)", oid_to_hex(&commit->object.oid));

	unuse_commit_buffer(commit, message);
}

/*
 * To allow LF and other nonportable characters in pathnames,
 * they are c-style quoted as needed.
 */
static void write_filename_info(const char *path)
{
	printf("filename ");
	write_name_quoted(path, stdout, '\n');
}

/*
 * Porcelain/Incremental format wants to show a lot of details per
 * commit.  Instead of repeating this every line, emit it only once,
 * the first time each commit appears in the output (unless the
 * user has specifically asked for us to repeat).
 */
static int emit_one_suspect_detail(struct origin *suspect, int repeat)
{
	struct commit_info ci;

	if (!repeat && (suspect->commit->object.flags & METAINFO_SHOWN))
		return 0;

	suspect->commit->object.flags |= METAINFO_SHOWN;
	get_commit_info(suspect->commit, &ci, 1);
	printf("author %s\n", ci.author.buf);
	printf("author-mail %s\n", ci.author_mail.buf);
	printf("author-time %lu\n", ci.author_time);
	printf("author-tz %s\n", ci.author_tz.buf);
	printf("committer %s\n", ci.committer.buf);
	printf("committer-mail %s\n", ci.committer_mail.buf);
	printf("committer-time %lu\n", ci.committer_time);
	printf("committer-tz %s\n", ci.committer_tz.buf);
	printf("summary %s\n", ci.summary.buf);
	if (suspect->commit->object.flags & UNINTERESTING)
		printf("boundary\n");
	if (suspect->previous) {
		struct origin *prev = suspect->previous;
		printf("previous %s ", oid_to_hex(&prev->commit->object.oid));
		write_name_quoted(prev->path, stdout, '\n');
	}

	commit_info_destroy(&ci);

	return 1;
}

/*
 * The blame_entry is found to be guilty for the range.
 * Show it in incremental output.
 */
static void found_guilty_entry(struct blame_entry *ent,
			   struct progress_info *pi)
{
	if (incremental) {
		struct origin *suspect = ent->suspect;

		printf("%s %d %d %d\n",
		       oid_to_hex(&suspect->commit->object.oid),
		       ent->s_lno + 1, ent->lno + 1, ent->num_lines);
		emit_one_suspect_detail(suspect, 0);
		write_filename_info(suspect->path);
		maybe_flush_or_die(stdout, "stdout");
	}
	pi->blamed_lines += ent->num_lines;
	display_progress(pi->progress, pi->blamed_lines);
}

/*
 * The main loop -- while we have blobs with lines whose true origin
 * is still unknown, pick one blob, and allow its lines to pass blames
 * to its parents. */
static void assign_blame(struct scoreboard *sb, int opt)
{
	struct rev_info *revs = sb->revs;
	struct commit *commit = prio_queue_get(&sb->commits);
	struct progress_info pi = { NULL, 0 };

	if (show_progress)
		pi.progress = start_progress_delay(_("Blaming lines"),
						   sb->num_lines, 50, 1);

	while (commit) {
		struct blame_entry *ent;
		struct origin *suspect = commit->util;

		/* find one suspect to break down */
		while (suspect && !suspect->suspects)
			suspect = suspect->next;

		if (!suspect) {
			commit = prio_queue_get(&sb->commits);
			continue;
		}

		assert(commit == suspect->commit);

		/*
		 * We will use this suspect later in the loop,
		 * so hold onto it in the meantime.
		 */
		origin_incref(suspect);
		parse_commit(commit);
		if (reverse ||
		    (!(commit->object.flags & UNINTERESTING) &&
		     !(revs->max_age != -1 && commit->date < revs->max_age)))
			pass_blame(sb, suspect, opt);
		else {
			commit->object.flags |= UNINTERESTING;
			if (commit->object.parsed)
				mark_parents_uninteresting(commit);
		}
		/* treat root commit as boundary */
		if (!commit->parents && !show_root)
			commit->object.flags |= UNINTERESTING;

		/* Take responsibility for the remaining entries */
		ent = suspect->suspects;
		if (ent) {
			suspect->guilty = 1;
			for (;;) {
				struct blame_entry *next = ent->next;
				found_guilty_entry(ent, &pi);
				if (next) {
					ent = next;
					continue;
				}
				ent->next = sb->ent;
				sb->ent = suspect->suspects;
				suspect->suspects = NULL;
				break;
			}
		}
		origin_decref(suspect);

		if (DEBUG) /* sanity */
			sanity_check_refcnt(sb);
	}

	stop_progress(&pi.progress);
}

static const char *format_time(unsigned long time, const char *tz_str,
			       int show_raw_time)
{
	static struct strbuf time_buf = STRBUF_INIT;

	strbuf_reset(&time_buf);
	if (show_raw_time) {
		strbuf_addf(&time_buf, "%lu %s", time, tz_str);
	}
	else {
		const char *time_str;
		size_t time_width;
		int tz;
		tz = atoi(tz_str);
		time_str = show_date(time, tz, &blame_date_mode);
		strbuf_addstr(&time_buf, time_str);
		/*
		 * Add space paddings to time_buf to display a fixed width
		 * string, and use time_width for display width calibration.
		 */
		for (time_width = utf8_strwidth(time_str);
		     time_width < blame_date_width;
		     time_width++)
			strbuf_addch(&time_buf, ' ');
	}
	return time_buf.buf;
}

#define OUTPUT_ANNOTATE_COMPAT	001
#define OUTPUT_LONG_OBJECT_NAME	002
#define OUTPUT_RAW_TIMESTAMP	004
#define OUTPUT_PORCELAIN	010
#define OUTPUT_SHOW_NAME	020
#define OUTPUT_SHOW_NUMBER	040
#define OUTPUT_SHOW_SCORE      0100
#define OUTPUT_NO_AUTHOR       0200
#define OUTPUT_SHOW_EMAIL	0400
#define OUTPUT_LINE_PORCELAIN 01000

static void emit_porcelain_details(struct origin *suspect, int repeat)
{
	if (emit_one_suspect_detail(suspect, repeat) ||
	    (suspect->commit->object.flags & MORE_THAN_ONE_PATH))
		write_filename_info(suspect->path);
}

static void emit_porcelain(struct scoreboard *sb, struct blame_entry *ent,
			   int opt)
{
	int repeat = opt & OUTPUT_LINE_PORCELAIN;
	int cnt;
	const char *cp;
	struct origin *suspect = ent->suspect;
	char hex[GIT_SHA1_HEXSZ + 1];

	sha1_to_hex_r(hex, suspect->commit->object.oid.hash);
	printf("%s %d %d %d\n",
	       hex,
	       ent->s_lno + 1,
	       ent->lno + 1,
	       ent->num_lines);
	emit_porcelain_details(suspect, repeat);

	cp = nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		if (cnt) {
			printf("%s %d %d\n", hex,
			       ent->s_lno + 1 + cnt,
			       ent->lno + 1 + cnt);
			if (repeat)
				emit_porcelain_details(suspect, 1);
		}
		putchar('\t');
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}

	if (sb->final_buf_size && cp[-1] != '\n')
		putchar('\n');
}

static void emit_other(struct scoreboard *sb, struct blame_entry *ent, int opt)
{
	int cnt;
	const char *cp;
	struct origin *suspect = ent->suspect;
	struct commit_info ci;
	char hex[GIT_SHA1_HEXSZ + 1];
	int show_raw_time = !!(opt & OUTPUT_RAW_TIMESTAMP);

	get_commit_info(suspect->commit, &ci, 1);
	sha1_to_hex_r(hex, suspect->commit->object.oid.hash);

	cp = nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		int length = (opt & OUTPUT_LONG_OBJECT_NAME) ? GIT_SHA1_HEXSZ : abbrev;

		if (suspect->commit->object.flags & UNINTERESTING) {
			if (blank_boundary)
				memset(hex, ' ', length);
			else if (!(opt & OUTPUT_ANNOTATE_COMPAT)) {
				length--;
				putchar('^');
			}
		}

		printf("%.*s", length, hex);
		if (opt & OUTPUT_ANNOTATE_COMPAT) {
			const char *name;
			if (opt & OUTPUT_SHOW_EMAIL)
				name = ci.author_mail.buf;
			else
				name = ci.author.buf;
			printf("\t(%10s\t%10s\t%d)", name,
			       format_time(ci.author_time, ci.author_tz.buf,
					   show_raw_time),
			       ent->lno + 1 + cnt);
		} else {
			if (opt & OUTPUT_SHOW_SCORE)
				printf(" %*d %02d",
				       max_score_digits, ent->score,
				       ent->suspect->refcnt);
			if (opt & OUTPUT_SHOW_NAME)
				printf(" %-*.*s", longest_file, longest_file,
				       suspect->path);
			if (opt & OUTPUT_SHOW_NUMBER)
				printf(" %*d", max_orig_digits,
				       ent->s_lno + 1 + cnt);

			if (!(opt & OUTPUT_NO_AUTHOR)) {
				const char *name;
				int pad;
				if (opt & OUTPUT_SHOW_EMAIL)
					name = ci.author_mail.buf;
				else
					name = ci.author.buf;
				pad = longest_author - utf8_strwidth(name);
				printf(" (%s%*s %10s",
				       name, pad, "",
				       format_time(ci.author_time,
						   ci.author_tz.buf,
						   show_raw_time));
			}
			printf(" %*d) ",
			       max_digits, ent->lno + 1 + cnt);
		}
		do {
			ch = *cp++;
			putchar(ch);
		} while (ch != '\n' &&
			 cp < sb->final_buf + sb->final_buf_size);
	}

	if (sb->final_buf_size && cp[-1] != '\n')
		putchar('\n');

	commit_info_destroy(&ci);
}

static void output(struct scoreboard *sb, int option)
{
	struct blame_entry *ent;

	if (option & OUTPUT_PORCELAIN) {
		for (ent = sb->ent; ent; ent = ent->next) {
			int count = 0;
			struct origin *suspect;
			struct commit *commit = ent->suspect->commit;
			if (commit->object.flags & MORE_THAN_ONE_PATH)
				continue;
			for (suspect = commit->util; suspect; suspect = suspect->next) {
				if (suspect->guilty && count++) {
					commit->object.flags |= MORE_THAN_ONE_PATH;
					break;
				}
			}
		}
	}

	for (ent = sb->ent; ent; ent = ent->next) {
		if (option & OUTPUT_PORCELAIN)
			emit_porcelain(sb, ent, option);
		else {
			emit_other(sb, ent, option);
		}
	}
}

static const char *get_next_line(const char *start, const char *end)
{
	const char *nl = memchr(start, '\n', end - start);
	return nl ? nl + 1 : end;
}

/*
 * To allow quick access to the contents of nth line in the
 * final image, prepare an index in the scoreboard.
 */
static int prepare_lines(struct scoreboard *sb)
{
	const char *buf = sb->final_buf;
	unsigned long len = sb->final_buf_size;
	const char *end = buf + len;
	const char *p;
	int *lineno;
	int num = 0;

	for (p = buf; p < end; p = get_next_line(p, end))
		num++;

	ALLOC_ARRAY(sb->lineno, num + 1);
	lineno = sb->lineno;

	for (p = buf; p < end; p = get_next_line(p, end))
		*lineno++ = p - buf;

	*lineno = len;

	sb->num_lines = num;
	return sb->num_lines;
}

/*
 * Add phony grafts for use with -S; this is primarily to
 * support git's cvsserver that wants to give a linear history
 * to its clients.
 */
static int read_ancestry(const char *graft_file)
{
	FILE *fp = fopen(graft_file, "r");
	struct strbuf buf = STRBUF_INIT;
	if (!fp)
		return -1;
	while (!strbuf_getwholeline(&buf, fp, '\n')) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		struct commit_graft *graft = read_graft_line(buf.buf, buf.len);
		if (graft)
			register_commit_graft(graft, 0);
	}
	fclose(fp);
	strbuf_release(&buf);
	return 0;
}

static int update_auto_abbrev(int auto_abbrev, struct origin *suspect)
{
	const char *uniq = find_unique_abbrev(suspect->commit->object.oid.hash,
					      auto_abbrev);
	int len = strlen(uniq);
	if (auto_abbrev < len)
		return len;
	return auto_abbrev;
}

/*
 * How many columns do we need to show line numbers, authors,
 * and filenames?
 */
static void find_alignment(struct scoreboard *sb, int *option)
{
	int longest_src_lines = 0;
	int longest_dst_lines = 0;
	unsigned largest_score = 0;
	struct blame_entry *e;
	int compute_auto_abbrev = (abbrev < 0);
	int auto_abbrev = DEFAULT_ABBREV;

	for (e = sb->ent; e; e = e->next) {
		struct origin *suspect = e->suspect;
		int num;

		if (compute_auto_abbrev)
			auto_abbrev = update_auto_abbrev(auto_abbrev, suspect);
		if (strcmp(suspect->path, sb->path))
			*option |= OUTPUT_SHOW_NAME;
		num = strlen(suspect->path);
		if (longest_file < num)
			longest_file = num;
		if (!(suspect->commit->object.flags & METAINFO_SHOWN)) {
			struct commit_info ci;
			suspect->commit->object.flags |= METAINFO_SHOWN;
			get_commit_info(suspect->commit, &ci, 1);
			if (*option & OUTPUT_SHOW_EMAIL)
				num = utf8_strwidth(ci.author_mail.buf);
			else
				num = utf8_strwidth(ci.author.buf);
			if (longest_author < num)
				longest_author = num;
			commit_info_destroy(&ci);
		}
		num = e->s_lno + e->num_lines;
		if (longest_src_lines < num)
			longest_src_lines = num;
		num = e->lno + e->num_lines;
		if (longest_dst_lines < num)
			longest_dst_lines = num;
		if (largest_score < ent_score(sb, e))
			largest_score = ent_score(sb, e);
	}
	max_orig_digits = decimal_width(longest_src_lines);
	max_digits = decimal_width(longest_dst_lines);
	max_score_digits = decimal_width(largest_score);

	if (compute_auto_abbrev)
		/* one more abbrev length is needed for the boundary commit */
		abbrev = auto_abbrev + 1;
}

/*
 * For debugging -- origin is refcounted, and this asserts that
 * we do not underflow.
 */
static void sanity_check_refcnt(struct scoreboard *sb)
{
	int baa = 0;
	struct blame_entry *ent;

	for (ent = sb->ent; ent; ent = ent->next) {
		/* Nobody should have zero or negative refcnt */
		if (ent->suspect->refcnt <= 0) {
			fprintf(stderr, "%s in %s has negative refcnt %d\n",
				ent->suspect->path,
				oid_to_hex(&ent->suspect->commit->object.oid),
				ent->suspect->refcnt);
			baa = 1;
		}
	}
	if (baa) {
		int opt = 0160;
		find_alignment(sb, &opt);
		output(sb, opt);
		die("Baa %d!", baa);
	}
}

static unsigned parse_score(const char *arg)
{
	char *end;
	unsigned long score = strtoul(arg, &end, 10);
	if (*end)
		return 0;
	return score;
}

static const char *add_prefix(const char *prefix, const char *path)
{
	return prefix_path(prefix, prefix ? strlen(prefix) : 0, path);
}

static int git_blame_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "blame.showroot")) {
		show_root = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "blame.blankboundary")) {
		blank_boundary = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "blame.showemail")) {
		int *output_option = cb;
		if (git_config_bool(var, value))
			*output_option |= OUTPUT_SHOW_EMAIL;
		else
			*output_option &= ~OUTPUT_SHOW_EMAIL;
		return 0;
	}
	if (!strcmp(var, "blame.date")) {
		if (!value)
			return config_error_nonbool(var);
		parse_date_format(value, &blame_date_mode);
		return 0;
	}

	if (git_diff_heuristic_config(var, value, cb) < 0)
		return -1;
	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, cb);
}

static void verify_working_tree_path(struct commit *work_tree, const char *path)
{
	struct commit_list *parents;
	int pos;

	for (parents = work_tree->parents; parents; parents = parents->next) {
		const struct object_id *commit_oid = &parents->item->object.oid;
		struct object_id blob_oid;
		unsigned mode;

		if (!get_tree_entry(commit_oid->hash, path, blob_oid.hash, &mode) &&
		    sha1_object_info(blob_oid.hash, NULL) == OBJ_BLOB)
			return;
	}

	pos = cache_name_pos(path, strlen(path));
	if (pos >= 0)
		; /* path is in the index */
	else if (-1 - pos < active_nr &&
		 !strcmp(active_cache[-1 - pos]->name, path))
		; /* path is in the index, unmerged */
	else
		die("no such path '%s' in HEAD", path);
}

static struct commit_list **append_parent(struct commit_list **tail, const struct object_id *oid)
{
	struct commit *parent;

	parent = lookup_commit_reference(oid->hash);
	if (!parent)
		die("no such commit %s", oid_to_hex(oid));
	return &commit_list_insert(parent, tail)->next;
}

static void append_merge_parents(struct commit_list **tail)
{
	int merge_head;
	struct strbuf line = STRBUF_INIT;

	merge_head = open(git_path_merge_head(), O_RDONLY);
	if (merge_head < 0) {
		if (errno == ENOENT)
			return;
		die("cannot open '%s' for reading", git_path_merge_head());
	}

	while (!strbuf_getwholeline_fd(&line, merge_head, '\n')) {
		struct object_id oid;
		if (line.len < GIT_SHA1_HEXSZ || get_oid_hex(line.buf, &oid))
			die("unknown line in '%s': %s", git_path_merge_head(), line.buf);
		tail = append_parent(tail, &oid);
	}
	close(merge_head);
	strbuf_release(&line);
}

/*
 * This isn't as simple as passing sb->buf and sb->len, because we
 * want to transfer ownership of the buffer to the commit (so we
 * must use detach).
 */
static void set_commit_buffer_from_strbuf(struct commit *c, struct strbuf *sb)
{
	size_t len;
	void *buf = strbuf_detach(sb, &len);
	set_commit_buffer(c, buf, len);
}

/*
 * Prepare a dummy commit that represents the work tree (or staged) item.
 * Note that annotating work tree item never works in the reverse.
 */
static struct commit *fake_working_tree_commit(struct diff_options *opt,
					       const char *path,
					       const char *contents_from)
{
	struct commit *commit;
	struct origin *origin;
	struct commit_list **parent_tail, *parent;
	struct object_id head_oid;
	struct strbuf buf = STRBUF_INIT;
	const char *ident;
	time_t now;
	int size, len;
	struct cache_entry *ce;
	unsigned mode;
	struct strbuf msg = STRBUF_INIT;

	read_cache();
	time(&now);
	commit = alloc_commit_node();
	commit->object.parsed = 1;
	commit->date = now;
	parent_tail = &commit->parents;

	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, head_oid.hash, NULL))
		die("no such ref: HEAD");

	parent_tail = append_parent(parent_tail, &head_oid);
	append_merge_parents(parent_tail);
	verify_working_tree_path(commit, path);

	origin = make_origin(commit, path);

	ident = fmt_ident("Not Committed Yet", "not.committed.yet", NULL, 0);
	strbuf_addstr(&msg, "tree 0000000000000000000000000000000000000000\n");
	for (parent = commit->parents; parent; parent = parent->next)
		strbuf_addf(&msg, "parent %s\n",
			    oid_to_hex(&parent->item->object.oid));
	strbuf_addf(&msg,
		    "author %s\n"
		    "committer %s\n\n"
		    "Version of %s from %s\n",
		    ident, ident, path,
		    (!contents_from ? path :
		     (!strcmp(contents_from, "-") ? "standard input" : contents_from)));
	set_commit_buffer_from_strbuf(commit, &msg);

	if (!contents_from || strcmp("-", contents_from)) {
		struct stat st;
		const char *read_from;
		char *buf_ptr;
		unsigned long buf_len;

		if (contents_from) {
			if (stat(contents_from, &st) < 0)
				die_errno("Cannot stat '%s'", contents_from);
			read_from = contents_from;
		}
		else {
			if (lstat(path, &st) < 0)
				die_errno("Cannot lstat '%s'", path);
			read_from = path;
		}
		mode = canon_mode(st.st_mode);

		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			if (DIFF_OPT_TST(opt, ALLOW_TEXTCONV) &&
			    textconv_object(read_from, mode, &null_oid, 0, &buf_ptr, &buf_len))
				strbuf_attach(&buf, buf_ptr, buf_len, buf_len + 1);
			else if (strbuf_read_file(&buf, read_from, st.st_size) != st.st_size)
				die_errno("cannot open or read '%s'", read_from);
			break;
		case S_IFLNK:
			if (strbuf_readlink(&buf, read_from, st.st_size) < 0)
				die_errno("cannot readlink '%s'", read_from);
			break;
		default:
			die("unsupported file type %s", read_from);
		}
	}
	else {
		/* Reading from stdin */
		mode = 0;
		if (strbuf_read(&buf, 0, 0) < 0)
			die_errno("failed to read from stdin");
	}
	convert_to_git(path, buf.buf, buf.len, &buf, 0);
	origin->file.ptr = buf.buf;
	origin->file.size = buf.len;
	pretend_sha1_file(buf.buf, buf.len, OBJ_BLOB, origin->blob_oid.hash);

	/*
	 * Read the current index, replace the path entry with
	 * origin->blob_sha1 without mucking with its mode or type
	 * bits; we are not going to write this index out -- we just
	 * want to run "diff-index --cached".
	 */
	discard_cache();
	read_cache();

	len = strlen(path);
	if (!mode) {
		int pos = cache_name_pos(path, len);
		if (0 <= pos)
			mode = active_cache[pos]->ce_mode;
		else
			/* Let's not bother reading from HEAD tree */
			mode = S_IFREG | 0644;
	}
	size = cache_entry_size(len);
	ce = xcalloc(1, size);
	oidcpy(&ce->oid, &origin->blob_oid);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(0);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);

	cache_tree_invalidate_path(&the_index, path);

	return commit;
}

static struct commit *find_single_final(struct rev_info *revs,
					const char **name_p)
{
	int i;
	struct commit *found = NULL;
	const char *name = NULL;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (found)
			die("More than one commit to dig from %s and %s?",
			    revs->pending.objects[i].name, name);
		found = (struct commit *)obj;
		name = revs->pending.objects[i].name;
	}
	if (name_p)
		*name_p = name;
	return found;
}

static char *prepare_final(struct scoreboard *sb)
{
	const char *name;
	sb->final = find_single_final(sb->revs, &name);
	return xstrdup_or_null(name);
}

static const char *dwim_reverse_initial(struct scoreboard *sb)
{
	/*
	 * DWIM "git blame --reverse ONE -- PATH" as
	 * "git blame --reverse ONE..HEAD -- PATH" but only do so
	 * when it makes sense.
	 */
	struct object *obj;
	struct commit *head_commit;
	unsigned char head_sha1[20];

	if (sb->revs->pending.nr != 1)
		return NULL;

	/* Is that sole rev a committish? */
	obj = sb->revs->pending.objects[0].item;
	obj = deref_tag(obj, NULL, 0);
	if (obj->type != OBJ_COMMIT)
		return NULL;

	/* Do we have HEAD? */
	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, head_sha1, NULL))
		return NULL;
	head_commit = lookup_commit_reference_gently(head_sha1, 1);
	if (!head_commit)
		return NULL;

	/* Turn "ONE" into "ONE..HEAD" then */
	obj->flags |= UNINTERESTING;
	add_pending_object(sb->revs, &head_commit->object, "HEAD");

	sb->final = (struct commit *)obj;
	return sb->revs->pending.objects[0].name;
}

static char *prepare_initial(struct scoreboard *sb)
{
	int i;
	const char *final_commit_name = NULL;
	struct rev_info *revs = sb->revs;

	/*
	 * There must be one and only one negative commit, and it must be
	 * the boundary.
	 */
	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (!(obj->flags & UNINTERESTING))
			continue;
		obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (sb->final)
			die("More than one commit to dig up from, %s and %s?",
			    revs->pending.objects[i].name,
			    final_commit_name);
		sb->final = (struct commit *) obj;
		final_commit_name = revs->pending.objects[i].name;
	}

	if (!final_commit_name)
		final_commit_name = dwim_reverse_initial(sb);
	if (!final_commit_name)
		die("No commit to dig up from?");
	return xstrdup(final_commit_name);
}

static int blame_copy_callback(const struct option *option, const char *arg, int unset)
{
	int *opt = option->value;

	/*
	 * -C enables copy from removed files;
	 * -C -C enables copy from existing files, but only
	 *       when blaming a new file;
	 * -C -C -C enables copy from existing files for
	 *          everybody
	 */
	if (*opt & PICKAXE_BLAME_COPY_HARDER)
		*opt |= PICKAXE_BLAME_COPY_HARDEST;
	if (*opt & PICKAXE_BLAME_COPY)
		*opt |= PICKAXE_BLAME_COPY_HARDER;
	*opt |= PICKAXE_BLAME_COPY | PICKAXE_BLAME_MOVE;

	if (arg)
		blame_copy_score = parse_score(arg);
	return 0;
}

static int blame_move_callback(const struct option *option, const char *arg, int unset)
{
	int *opt = option->value;

	*opt |= PICKAXE_BLAME_MOVE;

	if (arg)
		blame_move_score = parse_score(arg);
	return 0;
}

int cmd_blame(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	const char *path;
	struct scoreboard sb;
	struct origin *o;
	struct blame_entry *ent = NULL;
	long dashdash_pos, lno;
	char *final_commit_name = NULL;
	enum object_type type;
	struct commit *final_commit = NULL;

	struct string_list range_list = STRING_LIST_INIT_NODUP;
	int output_option = 0, opt = 0;
	int show_stats = 0;
	const char *revs_file = NULL;
	const char *contents_from = NULL;
	const struct option options[] = {
		OPT_BOOL(0, "incremental", &incremental, N_("Show blame entries as we find them, incrementally")),
		OPT_BOOL('b', NULL, &blank_boundary, N_("Show blank SHA-1 for boundary commits (Default: off)")),
		OPT_BOOL(0, "root", &show_root, N_("Do not treat root commits as boundaries (Default: off)")),
		OPT_BOOL(0, "show-stats", &show_stats, N_("Show work cost statistics")),
		OPT_BOOL(0, "progress", &show_progress, N_("Force progress reporting")),
		OPT_BIT(0, "score-debug", &output_option, N_("Show output score for blame entries"), OUTPUT_SHOW_SCORE),
		OPT_BIT('f', "show-name", &output_option, N_("Show original filename (Default: auto)"), OUTPUT_SHOW_NAME),
		OPT_BIT('n', "show-number", &output_option, N_("Show original linenumber (Default: off)"), OUTPUT_SHOW_NUMBER),
		OPT_BIT('p', "porcelain", &output_option, N_("Show in a format designed for machine consumption"), OUTPUT_PORCELAIN),
		OPT_BIT(0, "line-porcelain", &output_option, N_("Show porcelain format with per-line commit information"), OUTPUT_PORCELAIN|OUTPUT_LINE_PORCELAIN),
		OPT_BIT('c', NULL, &output_option, N_("Use the same output mode as git-annotate (Default: off)"), OUTPUT_ANNOTATE_COMPAT),
		OPT_BIT('t', NULL, &output_option, N_("Show raw timestamp (Default: off)"), OUTPUT_RAW_TIMESTAMP),
		OPT_BIT('l', NULL, &output_option, N_("Show long commit SHA1 (Default: off)"), OUTPUT_LONG_OBJECT_NAME),
		OPT_BIT('s', NULL, &output_option, N_("Suppress author name and timestamp (Default: off)"), OUTPUT_NO_AUTHOR),
		OPT_BIT('e', "show-email", &output_option, N_("Show author email instead of name (Default: off)"), OUTPUT_SHOW_EMAIL),
		OPT_BIT('w', NULL, &xdl_opts, N_("Ignore whitespace differences"), XDF_IGNORE_WHITESPACE),

		/*
		 * The following two options are parsed by parse_revision_opt()
		 * and are only included here to get included in the "-h"
		 * output:
		 */
		{ OPTION_LOWLEVEL_CALLBACK, 0, "indent-heuristic", NULL, NULL, N_("Use an experimental indent-based heuristic to improve diffs"), PARSE_OPT_NOARG, parse_opt_unknown_cb },
		{ OPTION_LOWLEVEL_CALLBACK, 0, "compaction-heuristic", NULL, NULL, N_("Use an experimental blank-line-based heuristic to improve diffs"), PARSE_OPT_NOARG, parse_opt_unknown_cb },

		OPT_BIT(0, "minimal", &xdl_opts, N_("Spend extra cycles to find better match"), XDF_NEED_MINIMAL),
		OPT_STRING('S', NULL, &revs_file, N_("file"), N_("Use revisions from <file> instead of calling git-rev-list")),
		OPT_STRING(0, "contents", &contents_from, N_("file"), N_("Use <file>'s contents as the final image")),
		{ OPTION_CALLBACK, 'C', NULL, &opt, N_("score"), N_("Find line copies within and across files"), PARSE_OPT_OPTARG, blame_copy_callback },
		{ OPTION_CALLBACK, 'M', NULL, &opt, N_("score"), N_("Find line movements within and across files"), PARSE_OPT_OPTARG, blame_move_callback },
		OPT_STRING_LIST('L', NULL, &range_list, N_("n,m"), N_("Process only line range n,m, counting from 1")),
		OPT__ABBREV(&abbrev),
		OPT_END()
	};

	struct parse_opt_ctx_t ctx;
	int cmd_is_annotate = !strcmp(argv[0], "annotate");
	struct range_set ranges;
	unsigned int range_i;
	long anchor;

	git_config(git_blame_config, &output_option);
	init_revisions(&revs, NULL);
	revs.date_mode = blame_date_mode;
	DIFF_OPT_SET(&revs.diffopt, ALLOW_TEXTCONV);
	DIFF_OPT_SET(&revs.diffopt, FOLLOW_RENAMES);

	save_commit_buffer = 0;
	dashdash_pos = 0;
	show_progress = -1;

	parse_options_start(&ctx, argc, argv, prefix, options,
			    PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0);
	for (;;) {
		switch (parse_options_step(&ctx, options, blame_opt_usage)) {
		case PARSE_OPT_HELP:
			exit(129);
		case PARSE_OPT_DONE:
			if (ctx.argv[0])
				dashdash_pos = ctx.cpidx;
			goto parse_done;
		}

		if (!strcmp(ctx.argv[0], "--reverse")) {
			ctx.argv[0] = "--children";
			reverse = 1;
		}
		parse_revision_opt(&revs, &ctx, options, blame_opt_usage);
	}
parse_done:
	no_whole_file_rename = !DIFF_OPT_TST(&revs.diffopt, FOLLOW_RENAMES);
	xdl_opts |= revs.diffopt.xdl_opts & (XDF_COMPACTION_HEURISTIC | XDF_INDENT_HEURISTIC);
	DIFF_OPT_CLR(&revs.diffopt, FOLLOW_RENAMES);
	argc = parse_options_end(&ctx);

	if (incremental || (output_option & OUTPUT_PORCELAIN)) {
		if (show_progress > 0)
			die(_("--progress can't be used with --incremental or porcelain formats"));
		show_progress = 0;
	} else if (show_progress < 0)
		show_progress = isatty(2);

	if (0 < abbrev)
		/* one more abbrev length is needed for the boundary commit */
		abbrev++;

	if (revs_file && read_ancestry(revs_file))
		die_errno("reading graft file '%s' failed", revs_file);

	if (cmd_is_annotate) {
		output_option |= OUTPUT_ANNOTATE_COMPAT;
		blame_date_mode.type = DATE_ISO8601;
	} else {
		blame_date_mode = revs.date_mode;
	}

	/* The maximum width used to show the dates */
	switch (blame_date_mode.type) {
	case DATE_RFC2822:
		blame_date_width = sizeof("Thu, 19 Oct 2006 16:00:04 -0700");
		break;
	case DATE_ISO8601_STRICT:
		blame_date_width = sizeof("2006-10-19T16:00:04-07:00");
		break;
	case DATE_ISO8601:
		blame_date_width = sizeof("2006-10-19 16:00:04 -0700");
		break;
	case DATE_RAW:
		blame_date_width = sizeof("1161298804 -0700");
		break;
	case DATE_UNIX:
		blame_date_width = sizeof("1161298804");
		break;
	case DATE_SHORT:
		blame_date_width = sizeof("2006-10-19");
		break;
	case DATE_RELATIVE:
		/* TRANSLATORS: This string is used to tell us the maximum
		   display width for a relative timestamp in "git blame"
		   output.  For C locale, "4 years, 11 months ago", which
		   takes 22 places, is the longest among various forms of
		   relative timestamps, but your language may need more or
		   fewer display columns. */
		blame_date_width = utf8_strwidth(_("4 years, 11 months ago")) + 1; /* add the null */
		break;
	case DATE_NORMAL:
		blame_date_width = sizeof("Thu Oct 19 16:00:04 2006 -0700");
		break;
	case DATE_STRFTIME:
		blame_date_width = strlen(show_date(0, 0, &blame_date_mode)) + 1; /* add the null */
		break;
	}
	blame_date_width -= 1; /* strip the null */

	if (DIFF_OPT_TST(&revs.diffopt, FIND_COPIES_HARDER))
		opt |= (PICKAXE_BLAME_COPY | PICKAXE_BLAME_MOVE |
			PICKAXE_BLAME_COPY_HARDER);

	if (!blame_move_score)
		blame_move_score = BLAME_DEFAULT_MOVE_SCORE;
	if (!blame_copy_score)
		blame_copy_score = BLAME_DEFAULT_COPY_SCORE;

	/*
	 * We have collected options unknown to us in argv[1..unk]
	 * which are to be passed to revision machinery if we are
	 * going to do the "bottom" processing.
	 *
	 * The remaining are:
	 *
	 * (1) if dashdash_pos != 0, it is either
	 *     "blame [revisions] -- <path>" or
	 *     "blame -- <path> <rev>"
	 *
	 * (2) otherwise, it is one of the two:
	 *     "blame [revisions] <path>"
	 *     "blame <path> <rev>"
	 *
	 * Note that we must strip out <path> from the arguments: we do not
	 * want the path pruning but we may want "bottom" processing.
	 */
	if (dashdash_pos) {
		switch (argc - dashdash_pos - 1) {
		case 2: /* (1b) */
			if (argc != 4)
				usage_with_options(blame_opt_usage, options);
			/* reorder for the new way: <rev> -- <path> */
			argv[1] = argv[3];
			argv[3] = argv[2];
			argv[2] = "--";
			/* FALLTHROUGH */
		case 1: /* (1a) */
			path = add_prefix(prefix, argv[--argc]);
			argv[argc] = NULL;
			break;
		default:
			usage_with_options(blame_opt_usage, options);
		}
	} else {
		if (argc < 2)
			usage_with_options(blame_opt_usage, options);
		path = add_prefix(prefix, argv[argc - 1]);
		if (argc == 3 && !file_exists(path)) { /* (2b) */
			path = add_prefix(prefix, argv[1]);
			argv[1] = argv[2];
		}
		argv[argc - 1] = "--";

		setup_work_tree();
		if (!file_exists(path))
			die_errno("cannot stat path '%s'", path);
	}

	revs.disable_stdin = 1;
	setup_revisions(argc, argv, &revs, NULL);
	memset(&sb, 0, sizeof(sb));

	sb.revs = &revs;
	if (!reverse) {
		final_commit_name = prepare_final(&sb);
		sb.commits.compare = compare_commits_by_commit_date;
	}
	else if (contents_from)
		die(_("--contents and --reverse do not blend well."));
	else {
		final_commit_name = prepare_initial(&sb);
		sb.commits.compare = compare_commits_by_reverse_commit_date;
		if (revs.first_parent_only)
			revs.children.name = NULL;
	}

	if (!sb.final) {
		/*
		 * "--not A B -- path" without anything positive;
		 * do not default to HEAD, but use the working tree
		 * or "--contents".
		 */
		setup_work_tree();
		sb.final = fake_working_tree_commit(&sb.revs->diffopt,
						    path, contents_from);
		add_pending_object(&revs, &(sb.final->object), ":");
	}
	else if (contents_from)
		die(_("cannot use --contents with final commit object name"));

	if (reverse && revs.first_parent_only) {
		final_commit = find_single_final(sb.revs, NULL);
		if (!final_commit)
			die(_("--reverse and --first-parent together require specified latest commit"));
	}

	/*
	 * If we have bottom, this will mark the ancestors of the
	 * bottom commits we would reach while traversing as
	 * uninteresting.
	 */
	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	if (reverse && revs.first_parent_only) {
		struct commit *c = final_commit;

		sb.revs->children.name = "children";
		while (c->parents &&
		       oidcmp(&c->object.oid, &sb.final->object.oid)) {
			struct commit_list *l = xcalloc(1, sizeof(*l));

			l->item = c;
			if (add_decoration(&sb.revs->children,
					   &c->parents->item->object, l))
				die("BUG: not unique item in first-parent chain");
			c = c->parents->item;
		}

		if (oidcmp(&c->object.oid, &sb.final->object.oid))
			die(_("--reverse --first-parent together require range along first-parent chain"));
	}

	if (is_null_oid(&sb.final->object.oid)) {
		o = sb.final->util;
		sb.final_buf = xmemdupz(o->file.ptr, o->file.size);
		sb.final_buf_size = o->file.size;
	}
	else {
		o = get_origin(&sb, sb.final, path);
		if (fill_blob_sha1_and_mode(o))
			die(_("no such path %s in %s"), path, final_commit_name);

		if (DIFF_OPT_TST(&sb.revs->diffopt, ALLOW_TEXTCONV) &&
		    textconv_object(path, o->mode, &o->blob_oid, 1, (char **) &sb.final_buf,
				    &sb.final_buf_size))
			;
		else
			sb.final_buf = read_sha1_file(o->blob_oid.hash, &type,
						      &sb.final_buf_size);

		if (!sb.final_buf)
			die(_("cannot read blob %s for path %s"),
			    oid_to_hex(&o->blob_oid),
			    path);
	}
	num_read_blob++;
	lno = prepare_lines(&sb);

	if (lno && !range_list.nr)
		string_list_append(&range_list, "1");

	anchor = 1;
	range_set_init(&ranges, range_list.nr);
	for (range_i = 0; range_i < range_list.nr; ++range_i) {
		long bottom, top;
		if (parse_range_arg(range_list.items[range_i].string,
				    nth_line_cb, &sb, lno, anchor,
				    &bottom, &top, sb.path))
			usage(blame_usage);
		if (lno < top || ((lno || bottom) && lno < bottom))
			die(Q_("file %s has only %lu line",
			       "file %s has only %lu lines",
			       lno), path, lno);
		if (bottom < 1)
			bottom = 1;
		if (top < 1)
			top = lno;
		bottom--;
		range_set_append_unsafe(&ranges, bottom, top);
		anchor = top + 1;
	}
	sort_and_merge_range_set(&ranges);

	for (range_i = ranges.nr; range_i > 0; --range_i) {
		const struct range *r = &ranges.ranges[range_i - 1];
		long bottom = r->start;
		long top = r->end;
		struct blame_entry *next = ent;
		ent = xcalloc(1, sizeof(*ent));
		ent->lno = bottom;
		ent->num_lines = top - bottom;
		ent->suspect = o;
		ent->s_lno = bottom;
		ent->next = next;
		origin_incref(o);
	}

	o->suspects = ent;
	prio_queue_put(&sb.commits, o->commit);

	origin_decref(o);

	range_set_release(&ranges);
	string_list_clear(&range_list, 0);

	sb.ent = NULL;
	sb.path = path;

	read_mailmap(&mailmap, NULL);

	assign_blame(&sb, opt);

	if (!incremental)
		setup_pager();

	free(final_commit_name);

	if (incremental)
		return 0;

	sb.ent = blame_sort(sb.ent, compare_blame_final);

	coalesce(&sb);

	if (!(output_option & OUTPUT_PORCELAIN))
		find_alignment(&sb, &output_option);

	output(&sb, output_option);
	free((void *)sb.final_buf);
	for (ent = sb.ent; ent; ) {
		struct blame_entry *e = ent->next;
		free(ent);
		ent = e;
	}

	if (show_stats) {
		printf("num read blob: %d\n", num_read_blob);
		printf("num get patch: %d\n", num_get_patch);
		printf("num commits: %d\n", num_commits);
	}
	return 0;
}
