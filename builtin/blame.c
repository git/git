/*
 * Blame
 *
 * Copyright (c) 2006, Junio C Hamano
 */

#include "cache.h"
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
#include "parse-options.h"
#include "utf8.h"
#include "userdiff.h"

static char blame_usage[] = N_("git blame [options] [rev-opts] [rev] [--] file");

static const char *blame_opt_usage[] = {
	blame_usage,
	"",
	N_("[rev-opts] are documented in git-rev-list(1)"),
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

static enum date_mode blame_date_mode = DATE_ISO8601;
static size_t blame_date_width;

static struct string_list mailmap;

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

/* bits #0..7 in revision.h, #8..11 used for merge_bases() in commit.c */
#define METAINFO_SHOWN		(1u<<12)
#define MORE_THAN_ONE_PATH	(1u<<13)

/*
 * One blob in a commit that is being suspected
 */
struct origin {
	int refcnt;
	struct origin *previous;
	struct commit *commit;
	mmfile_t file;
	unsigned char blob_sha1[20];
	unsigned mode;
	char path[FLEX_ARRAY];
};

static int diff_hunks(mmfile_t *file_a, mmfile_t *file_b, long ctxlen,
		      xdl_emit_hunk_consume_func_t hunk_func, void *cb_data)
{
	xpparam_t xpp = {0};
	xdemitconf_t xecfg = {0};
	xdemitcb_t ecb = {NULL};

	xpp.flags = xdl_opts;
	xecfg.ctxlen = ctxlen;
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
		    const unsigned char *sha1,
		    int sha1_valid,
		    char **buf,
		    unsigned long *buf_size)
{
	struct diff_filespec *df;
	struct userdiff_driver *textconv;

	df = alloc_filespec(path);
	fill_filespec(df, sha1, sha1_valid, mode);
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
		    textconv_object(o->path, o->mode, o->blob_sha1, 1, &file->ptr, &file_size))
			;
		else
			file->ptr = read_sha1_file(o->blob_sha1, &type, &file_size);
		file->size = file_size;

		if (!file->ptr)
			die("Cannot read blob %s for path %s",
			    sha1_to_hex(o->blob_sha1),
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
		if (o->previous)
			origin_decref(o->previous);
		free(o->file.ptr);
		free(o);
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
 * as we pass blame to the parents.  They form a linked list in the
 * scoreboard structure, sorted by the target line number.
 */
struct blame_entry {
	struct blame_entry *prev;
	struct blame_entry *next;

	/* the first line of this group in the final image;
	 * internally all line numbers are 0 based.
	 */
	int lno;

	/* how many lines this group has */
	int num_lines;

	/* the commit that introduced this group into the final image */
	struct origin *suspect;

	/* true if the suspect is truly guilty; false while we have not
	 * checked if the group came from one of its parents.
	 */
	char guilty;

	/* true if the entry has been scanned for copies in the current parent
	 */
	char scanned;

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
 * The current state of the blame assignment.
 */
struct scoreboard {
	/* the final commit (i.e. where we started digging from) */
	struct commit *final;
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

static inline int same_suspect(struct origin *a, struct origin *b)
{
	if (a == b)
		return 1;
	if (a->commit != b->commit)
		return 0;
	return !strcmp(a->path, b->path);
}

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
		if (same_suspect(ent->suspect, next->suspect) &&
		    ent->guilty == next->guilty &&
		    ent->s_lno + ent->num_lines == next->s_lno) {
			ent->num_lines += next->num_lines;
			ent->next = next->next;
			if (ent->next)
				ent->next->prev = ent;
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
 * Given a commit and a path in it, create a new origin structure.
 * The callers that add blame to the scoreboard should use
 * get_origin() to obtain shared, refcounted copy instead of calling
 * this function directly.
 */
static struct origin *make_origin(struct commit *commit, const char *path)
{
	struct origin *o;
	o = xcalloc(1, sizeof(*o) + strlen(path) + 1);
	o->commit = commit;
	o->refcnt = 1;
	strcpy(o->path, path);
	return o;
}

/*
 * Locate an existing origin or create a new one.
 */
static struct origin *get_origin(struct scoreboard *sb,
				 struct commit *commit,
				 const char *path)
{
	struct blame_entry *e;

	for (e = sb->ent; e; e = e->next) {
		if (e->suspect->commit == commit &&
		    !strcmp(e->suspect->path, path))
			return origin_incref(e->suspect);
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
	if (!is_null_sha1(origin->blob_sha1))
		return 0;
	if (get_tree_entry(origin->commit->object.sha1,
			   origin->path,
			   origin->blob_sha1, &origin->mode))
		goto error_out;
	if (sha1_object_info(origin->blob_sha1, NULL) != OBJ_BLOB)
		goto error_out;
	return 0;
 error_out:
	hashclr(origin->blob_sha1);
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
	struct origin *porigin = NULL;
	struct diff_options diff_opts;
	const char *paths[2];

	if (parent->util) {
		/*
		 * Each commit object can cache one origin in that
		 * commit.  This is a freestanding copy of origin and
		 * not refcounted.
		 */
		struct origin *cached = parent->util;
		if (!strcmp(cached->path, origin->path)) {
			/*
			 * The same path between origin and its parent
			 * without renaming -- the most common case.
			 */
			porigin = get_origin(sb, parent, cached->path);

			/*
			 * If the origin was newly created (i.e. get_origin
			 * would call make_origin if none is found in the
			 * scoreboard), it does not know the blob_sha1/mode,
			 * so copy it.  Otherwise porigin was in the
			 * scoreboard and already knows blob_sha1/mode.
			 */
			if (porigin->refcnt == 1) {
				hashcpy(porigin->blob_sha1, cached->blob_sha1);
				porigin->mode = cached->mode;
			}
			return porigin;
		}
		/* otherwise it was not very useful; free it */
		free(parent->util);
		parent->util = NULL;
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

	diff_tree_setup_paths(paths, &diff_opts);
	diff_setup_done(&diff_opts);

	if (is_null_sha1(origin->commit->object.sha1))
		do_diff_cache(parent->tree->object.sha1, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.sha1,
			       origin->commit->tree->object.sha1,
			       "", &diff_opts);
	diffcore_std(&diff_opts);

	if (!diff_queued_diff.nr) {
		/* The path is the same as parent */
		porigin = get_origin(sb, parent, origin->path);
		hashcpy(porigin->blob_sha1, origin->blob_sha1);
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
			hashcpy(porigin->blob_sha1, p->one->sha1);
			porigin->mode = p->one->mode;
			break;
		case 'A':
		case 'T':
			/* Did not exist in parent, or type changed */
			break;
		}
	}
	diff_flush(&diff_opts);
	diff_tree_release_paths(&diff_opts);
	if (porigin) {
		/*
		 * Create a freestanding copy that is not part of
		 * the refcounted origin found in the scoreboard, and
		 * cache it in the commit.
		 */
		struct origin *cached;

		cached = make_origin(porigin->commit, porigin->path);
		hashcpy(cached->blob_sha1, porigin->blob_sha1);
		cached->mode = porigin->mode;
		parent->util = cached;
	}
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
	const char *paths[2];

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = origin->path;
	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	diff_setup_done(&diff_opts);

	if (is_null_sha1(origin->commit->object.sha1))
		do_diff_cache(parent->tree->object.sha1, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.sha1,
			       origin->commit->tree->object.sha1,
			       "", &diff_opts);
	diffcore_std(&diff_opts);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, origin->path)) {
			porigin = get_origin(sb, parent, p->one->path);
			hashcpy(porigin->blob_sha1, p->one->sha1);
			porigin->mode = p->one->mode;
			break;
		}
	}
	diff_flush(&diff_opts);
	diff_tree_release_paths(&diff_opts);
	return porigin;
}

/*
 * Link in a new blame entry to the scoreboard.  Entries that cover the
 * same line range have been removed from the scoreboard previously.
 */
static void add_blame_entry(struct scoreboard *sb, struct blame_entry *e)
{
	struct blame_entry *ent, *prev = NULL;

	origin_incref(e->suspect);

	for (ent = sb->ent; ent && ent->lno < e->lno; ent = ent->next)
		prev = ent;

	/* prev, if not NULL, is the last one that is below e */
	e->prev = prev;
	if (prev) {
		e->next = prev->next;
		prev->next = e;
	}
	else {
		e->next = sb->ent;
		sb->ent = e;
	}
	if (e->next)
		e->next->prev = e;
}

/*
 * src typically is on-stack; we want to copy the information in it to
 * a malloced blame_entry that is already on the linked list of the
 * scoreboard.  The origin of dst loses a refcnt while the origin of src
 * gains one.
 */
static void dup_entry(struct blame_entry *dst, struct blame_entry *src)
{
	struct blame_entry *p, *n;

	p = dst->prev;
	n = dst->next;
	origin_incref(src->suspect);
	origin_decref(dst->suspect);
	memcpy(dst, src, sizeof(*src));
	dst->prev = p;
	dst->next = n;
	dst->score = 0;
}

static const char *nth_line(struct scoreboard *sb, int lno)
{
	return sb->final_buf + sb->lineno[lno];
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
 * in split.  Adjust the linked list of blames in the scoreboard to
 * reflect the split.
 */
static void split_blame(struct scoreboard *sb,
			struct blame_entry *split,
			struct blame_entry *e)
{
	struct blame_entry *new_entry;

	if (split[0].suspect && split[2].suspect) {
		/* The first part (reuse storage for the existing entry e) */
		dup_entry(e, &split[0]);

		/* The last part -- me */
		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);

		/* ... and the middle part -- parent */
		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}
	else if (!split[0].suspect && !split[2].suspect)
		/*
		 * The parent covers the entire area; reuse storage for
		 * e and replace it with the parent.
		 */
		dup_entry(e, &split[1]);
	else if (split[0].suspect) {
		/* me and then parent */
		dup_entry(e, &split[0]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[1]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}
	else {
		/* parent and then me */
		dup_entry(e, &split[1]);

		new_entry = xmalloc(sizeof(*new_entry));
		memcpy(new_entry, &(split[2]), sizeof(struct blame_entry));
		add_blame_entry(sb, new_entry);
	}

	if (DEBUG) { /* sanity */
		struct blame_entry *ent;
		int lno = sb->ent->lno, corrupt = 0;

		for (ent = sb->ent; ent; ent = ent->next) {
			if (lno != ent->lno)
				corrupt = 1;
			if (ent->s_lno < 0)
				corrupt = 1;
			lno += ent->num_lines;
		}
		if (corrupt) {
			lno = sb->ent->lno;
			for (ent = sb->ent; ent; ent = ent->next) {
				printf("L %8d l %8d n %8d\n",
				       lno, ent->lno, ent->num_lines);
				lno = ent->lno + ent->num_lines;
			}
			die("oops");
		}
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
 * Helper for blame_chunk().  blame_entry e is known to overlap with
 * the patch hunk; split it and pass blame to the parent.
 */
static void blame_overlap(struct scoreboard *sb, struct blame_entry *e,
			  int tlno, int plno, int same,
			  struct origin *parent)
{
	struct blame_entry split[3];

	split_overlap(split, e, tlno, plno, same, parent);
	if (split[1].suspect)
		split_blame(sb, split, e);
	decref_split(split);
}

/*
 * Find the line number of the last line the target is suspected for.
 */
static int find_last_in_target(struct scoreboard *sb, struct origin *target)
{
	struct blame_entry *e;
	int last_in_target = -1;

	for (e = sb->ent; e; e = e->next) {
		if (e->guilty || !same_suspect(e->suspect, target))
			continue;
		if (last_in_target < e->s_lno + e->num_lines)
			last_in_target = e->s_lno + e->num_lines;
	}
	return last_in_target;
}

/*
 * Process one hunk from the patch between the current suspect for
 * blame_entry e and its parent.  Find and split the overlap, and
 * pass blame to the overlapping part to the parent.
 */
static void blame_chunk(struct scoreboard *sb,
			int tlno, int plno, int same,
			struct origin *target, struct origin *parent)
{
	struct blame_entry *e;

	for (e = sb->ent; e; e = e->next) {
		if (e->guilty || !same_suspect(e->suspect, target))
			continue;
		if (same <= e->s_lno)
			continue;
		if (tlno < e->s_lno + e->num_lines)
			blame_overlap(sb, e, tlno, plno, same, parent);
	}
}

struct blame_chunk_cb_data {
	struct scoreboard *sb;
	struct origin *target;
	struct origin *parent;
	long plno;
	long tlno;
};

static int blame_chunk_cb(long start_a, long count_a,
			  long start_b, long count_b, void *data)
{
	struct blame_chunk_cb_data *d = data;
	blame_chunk(d->sb, d->tlno, d->plno, start_b, d->target, d->parent);
	d->plno = start_a + count_a;
	d->tlno = start_b + count_b;
	return 0;
}

/*
 * We are looking at the origin 'target' and aiming to pass blame
 * for the lines it is suspected to its parent.  Run diff to find
 * which lines came from parent and pass blame for them.
 */
static int pass_blame_to_parent(struct scoreboard *sb,
				struct origin *target,
				struct origin *parent)
{
	int last_in_target;
	mmfile_t file_p, file_o;
	struct blame_chunk_cb_data d;

	memset(&d, 0, sizeof(d));
	d.sb = sb; d.target = target; d.parent = parent;
	last_in_target = find_last_in_target(sb, target);
	if (last_in_target < 0)
		return 1; /* nothing remains for this target */

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p);
	fill_origin_blob(&sb->revs->diffopt, target, &file_o);
	num_get_patch++;

	diff_hunks(&file_p, &file_o, 0, blame_chunk_cb, &d);
	/* The rest (i.e. anything after tlno) are the same as the parent */
	blame_chunk(sb, d.tlno, d.plno, last_in_target, target, parent);

	return 0;
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
	int cnt;
	mmfile_t file_o;
	struct handle_split_cb_data d;

	memset(&d, 0, sizeof(d));
	d.sb = sb; d.ent = ent; d.parent = parent; d.split = split;
	/*
	 * Prepare mmfile that contains only the lines in ent.
	 */
	cp = nth_line(sb, ent->lno);
	file_o.ptr = (char *) cp;
	cnt = ent->num_lines;

	while (cnt && cp < sb->final_buf + sb->final_buf_size) {
		if (*cp++ == '\n')
			cnt--;
	}
	file_o.size = cp - file_o.ptr;

	/*
	 * file_o is a part of final image we are annotating.
	 * file_p partially may match that image.
	 */
	memset(split, 0, sizeof(struct blame_entry [3]));
	diff_hunks(file_p, &file_o, 1, handle_split_cb, &d);
	/* remainder, if any, all match the preimage */
	handle_split(sb, ent, d.tlno, d.plno, ent->num_lines, parent, split);
}

/*
 * See if lines currently target is suspected for can be attributed to
 * parent.
 */
static int find_move_in_parent(struct scoreboard *sb,
			       struct origin *target,
			       struct origin *parent)
{
	int last_in_target, made_progress;
	struct blame_entry *e, split[3];
	mmfile_t file_p;

	last_in_target = find_last_in_target(sb, target);
	if (last_in_target < 0)
		return 1; /* nothing remains for this target */

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p);
	if (!file_p.ptr)
		return 0;

	made_progress = 1;
	while (made_progress) {
		made_progress = 0;
		for (e = sb->ent; e; e = e->next) {
			if (e->guilty || !same_suspect(e->suspect, target) ||
			    ent_score(sb, e) < blame_move_score)
				continue;
			find_copy_in_blob(sb, e, parent, split, &file_p);
			if (split[1].suspect &&
			    blame_move_score < ent_score(sb, &split[1])) {
				split_blame(sb, split, e);
				made_progress = 1;
			}
			decref_split(split);
		}
	}
	return 0;
}

struct blame_list {
	struct blame_entry *ent;
	struct blame_entry split[3];
};

/*
 * Count the number of entries the target is suspected for,
 * and prepare a list of entry and the best split.
 */
static struct blame_list *setup_blame_list(struct scoreboard *sb,
					   struct origin *target,
					   int min_score,
					   int *num_ents_p)
{
	struct blame_entry *e;
	int num_ents, i;
	struct blame_list *blame_list = NULL;

	for (e = sb->ent, num_ents = 0; e; e = e->next)
		if (!e->scanned && !e->guilty &&
		    same_suspect(e->suspect, target) &&
		    min_score < ent_score(sb, e))
			num_ents++;
	if (num_ents) {
		blame_list = xcalloc(num_ents, sizeof(struct blame_list));
		for (e = sb->ent, i = 0; e; e = e->next)
			if (!e->scanned && !e->guilty &&
			    same_suspect(e->suspect, target) &&
			    min_score < ent_score(sb, e))
				blame_list[i++].ent = e;
	}
	*num_ents_p = num_ents;
	return blame_list;
}

/*
 * Reset the scanned status on all entries.
 */
static void reset_scanned_flag(struct scoreboard *sb)
{
	struct blame_entry *e;
	for (e = sb->ent; e; e = e->next)
		e->scanned = 0;
}

/*
 * For lines target is suspected for, see if we can find code movement
 * across file boundary from the parent commit.  porigin is the path
 * in the parent we already tried.
 */
static int find_copy_in_parent(struct scoreboard *sb,
			       struct origin *target,
			       struct commit *parent,
			       struct origin *porigin,
			       int opt)
{
	struct diff_options diff_opts;
	const char *paths[1];
	int i, j;
	int retval;
	struct blame_list *blame_list;
	int num_ents;

	blame_list = setup_blame_list(sb, target, blame_copy_score, &num_ents);
	if (!blame_list)
		return 1; /* nothing remains for this target */

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;

	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
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

	if (is_null_sha1(target->commit->object.sha1))
		do_diff_cache(parent->tree->object.sha1, &diff_opts);
	else
		diff_tree_sha1(parent->tree->object.sha1,
			       target->commit->tree->object.sha1,
			       "", &diff_opts);

	if (!DIFF_OPT_TST(&diff_opts, FIND_COPIES_HARDER))
		diffcore_std(&diff_opts);

	retval = 0;
	while (1) {
		int made_progress = 0;

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
			hashcpy(norigin->blob_sha1, p->one->sha1);
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
				split_blame(sb, split, blame_list[j].ent);
				made_progress = 1;
			}
			else
				blame_list[j].ent->scanned = 1;
			decref_split(split);
		}
		free(blame_list);

		if (!made_progress)
			break;
		blame_list = setup_blame_list(sb, target, blame_copy_score, &num_ents);
		if (!blame_list) {
			retval = 1;
			break;
		}
	}
	reset_scanned_flag(sb);
	diff_flush(&diff_opts);
	diff_tree_release_paths(&diff_opts);
	return retval;
}

/*
 * The blobs of origin and porigin exactly match, so everything
 * origin is suspected for can be blamed on the parent.
 */
static void pass_whole_blame(struct scoreboard *sb,
			     struct origin *origin, struct origin *porigin)
{
	struct blame_entry *e;

	if (!porigin->file.ptr && origin->file.ptr) {
		/* Steal its file */
		porigin->file = origin->file;
		origin->file.ptr = NULL;
	}
	for (e = sb->ent; e; e = e->next) {
		if (!same_suspect(e->suspect, origin))
			continue;
		origin_incref(porigin);
		origin_decref(e->suspect);
		e->suspect = porigin;
	}
}

/*
 * We pass blame from the current commit to its parents.  We keep saying
 * "parent" (and "porigin"), but what we mean is to find scapegoat to
 * exonerate ourselves.
 */
static struct commit_list *first_scapegoat(struct rev_info *revs, struct commit *commit)
{
	if (!reverse)
		return commit->parents;
	return lookup_decoration(&revs->children, &commit->object);
}

static int num_scapegoats(struct rev_info *revs, struct commit *commit)
{
	int cnt;
	struct commit_list *l = first_scapegoat(revs, commit);
	for (cnt = 0; l; l = l->next)
		cnt++;
	return cnt;
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
	for (pass = 0; pass < 2; pass++) {
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
			if (!hashcmp(porigin->blob_sha1, origin->blob_sha1)) {
				pass_whole_blame(sb, origin, porigin);
				origin_decref(porigin);
				goto finish;
			}
			for (j = same = 0; j < i; j++)
				if (sg_origin[j] &&
				    !hashcmp(sg_origin[j]->blob_sha1,
					     porigin->blob_sha1)) {
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
		if (pass_blame_to_parent(sb, origin, porigin))
			goto finish;
	}

	/*
	 * Optionally find moves in parents' files.
	 */
	if (opt & PICKAXE_BLAME_MOVE)
		for (i = 0, sg = first_scapegoat(revs, commit);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct origin *porigin = sg_origin[i];
			if (!porigin)
				continue;
			if (find_move_in_parent(sb, origin, porigin))
				goto finish;
		}

	/*
	 * Optionally find copies from parents' files.
	 */
	if (opt & PICKAXE_BLAME_COPY)
		for (i = 0, sg = first_scapegoat(revs, commit);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct origin *porigin = sg_origin[i];
			if (find_copy_in_parent(sb, origin, sg->item,
						porigin, opt))
				goto finish;
		}

 finish:
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
	const char *author;
	const char *author_mail;
	unsigned long author_time;
	const char *author_tz;

	/* filled only when asked for details */
	const char *committer;
	const char *committer_mail;
	unsigned long committer_time;
	const char *committer_tz;

	const char *summary;
};

/*
 * Parse author/committer line in the commit object buffer
 */
static void get_ac_line(const char *inbuf, const char *what,
			int person_len, char *person,
			int mail_len, char *mail,
			unsigned long *time, const char **tz)
{
	int len, tzlen, maillen;
	char *tmp, *endp, *timepos, *mailpos;

	tmp = strstr(inbuf, what);
	if (!tmp)
		goto error_out;
	tmp += strlen(what);
	endp = strchr(tmp, '\n');
	if (!endp)
		len = strlen(tmp);
	else
		len = endp - tmp;
	if (person_len <= len) {
	error_out:
		/* Ugh */
		*tz = "(unknown)";
		strcpy(person, *tz);
		strcpy(mail, *tz);
		*time = 0;
		return;
	}
	memcpy(person, tmp, len);

	tmp = person;
	tmp += len;
	*tmp = 0;
	while (person < tmp && *tmp != ' ')
		tmp--;
	if (tmp <= person)
		goto error_out;
	*tz = tmp+1;
	tzlen = (person+len)-(tmp+1);

	*tmp = 0;
	while (person < tmp && *tmp != ' ')
		tmp--;
	if (tmp <= person)
		goto error_out;
	*time = strtoul(tmp, NULL, 10);
	timepos = tmp;

	*tmp = 0;
	while (person < tmp && !(*tmp == ' ' && tmp[1] == '<'))
		tmp--;
	if (tmp <= person)
		return;
	mailpos = tmp + 1;
	*tmp = 0;
	maillen = timepos - tmp;
	memcpy(mail, mailpos, maillen);

	if (!mailmap.nr)
		return;

	/*
	 * mailmap expansion may make the name longer.
	 * make room by pushing stuff down.
	 */
	tmp = person + person_len - (tzlen + 1);
	memmove(tmp, *tz, tzlen);
	tmp[tzlen] = 0;
	*tz = tmp;

	/*
	 * Now, convert both name and e-mail using mailmap
	 */
	if (map_user(&mailmap, mail+1, mail_len-1, person, tmp-person-1)) {
		/* Add a trailing '>' to email, since map_user returns plain emails
		   Note: It already has '<', since we replace from mail+1 */
		mailpos = memchr(mail, '\0', mail_len);
		if (mailpos && mailpos-mail < mail_len - 1) {
			*mailpos = '>';
			*(mailpos+1) = '\0';
		}
	}
}

static void get_commit_info(struct commit *commit,
			    struct commit_info *ret,
			    int detailed)
{
	int len;
	const char *subject;
	char *reencoded, *message;
	static char author_name[1024];
	static char author_mail[1024];
	static char committer_name[1024];
	static char committer_mail[1024];
	static char summary_buf[1024];

	/*
	 * We've operated without save_commit_buffer, so
	 * we now need to populate them for output.
	 */
	if (!commit->buffer) {
		enum object_type type;
		unsigned long size;
		commit->buffer =
			read_sha1_file(commit->object.sha1, &type, &size);
		if (!commit->buffer)
			die("Cannot read commit %s",
			    sha1_to_hex(commit->object.sha1));
	}
	reencoded = reencode_commit_message(commit, NULL);
	message   = reencoded ? reencoded : commit->buffer;
	ret->author = author_name;
	ret->author_mail = author_mail;
	get_ac_line(message, "\nauthor ",
		    sizeof(author_name), author_name,
		    sizeof(author_mail), author_mail,
		    &ret->author_time, &ret->author_tz);

	if (!detailed) {
		free(reencoded);
		return;
	}

	ret->committer = committer_name;
	ret->committer_mail = committer_mail;
	get_ac_line(message, "\ncommitter ",
		    sizeof(committer_name), committer_name,
		    sizeof(committer_mail), committer_mail,
		    &ret->committer_time, &ret->committer_tz);

	ret->summary = summary_buf;
	len = find_commit_subject(message, &subject);
	if (len && len < sizeof(summary_buf)) {
		memcpy(summary_buf, subject, len);
		summary_buf[len] = 0;
	} else {
		sprintf(summary_buf, "(%s)", sha1_to_hex(commit->object.sha1));
	}
	free(reencoded);
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
	printf("author %s\n", ci.author);
	printf("author-mail %s\n", ci.author_mail);
	printf("author-time %lu\n", ci.author_time);
	printf("author-tz %s\n", ci.author_tz);
	printf("committer %s\n", ci.committer);
	printf("committer-mail %s\n", ci.committer_mail);
	printf("committer-time %lu\n", ci.committer_time);
	printf("committer-tz %s\n", ci.committer_tz);
	printf("summary %s\n", ci.summary);
	if (suspect->commit->object.flags & UNINTERESTING)
		printf("boundary\n");
	if (suspect->previous) {
		struct origin *prev = suspect->previous;
		printf("previous %s ", sha1_to_hex(prev->commit->object.sha1));
		write_name_quoted(prev->path, stdout, '\n');
	}
	return 1;
}

/*
 * The blame_entry is found to be guilty for the range.  Mark it
 * as such, and show it in incremental output.
 */
static void found_guilty_entry(struct blame_entry *ent)
{
	if (ent->guilty)
		return;
	ent->guilty = 1;
	if (incremental) {
		struct origin *suspect = ent->suspect;

		printf("%s %d %d %d\n",
		       sha1_to_hex(suspect->commit->object.sha1),
		       ent->s_lno + 1, ent->lno + 1, ent->num_lines);
		emit_one_suspect_detail(suspect, 0);
		write_filename_info(suspect->path);
		maybe_flush_or_die(stdout, "stdout");
	}
}

/*
 * The main loop -- while the scoreboard has lines whose true origin
 * is still unknown, pick one blame_entry, and allow its current
 * suspect to pass blames to its parents.
 */
static void assign_blame(struct scoreboard *sb, int opt)
{
	struct rev_info *revs = sb->revs;

	while (1) {
		struct blame_entry *ent;
		struct commit *commit;
		struct origin *suspect = NULL;

		/* find one suspect to break down */
		for (ent = sb->ent; !suspect && ent; ent = ent->next)
			if (!ent->guilty)
				suspect = ent->suspect;
		if (!suspect)
			return; /* all done */

		/*
		 * We will use this suspect later in the loop,
		 * so hold onto it in the meantime.
		 */
		origin_incref(suspect);
		commit = suspect->commit;
		if (!commit->object.parsed)
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
		for (ent = sb->ent; ent; ent = ent->next)
			if (same_suspect(ent->suspect, suspect))
				found_guilty_entry(ent);
		origin_decref(suspect);

		if (DEBUG) /* sanity */
			sanity_check_refcnt(sb);
	}
}

static const char *format_time(unsigned long time, const char *tz_str,
			       int show_raw_time)
{
	static char time_buf[128];
	const char *time_str;
	int time_len;
	int tz;

	if (show_raw_time) {
		snprintf(time_buf, sizeof(time_buf), "%lu %s", time, tz_str);
	}
	else {
		tz = atoi(tz_str);
		time_str = show_date(time, tz, blame_date_mode);
		time_len = strlen(time_str);
		memcpy(time_buf, time_str, time_len);
		memset(time_buf + time_len, ' ', blame_date_width - time_len);
	}
	return time_buf;
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
	char hex[41];

	strcpy(hex, sha1_to_hex(suspect->commit->object.sha1));
	printf("%s%c%d %d %d\n",
	       hex,
	       ent->guilty ? ' ' : '*', /* purely for debugging */
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
	char hex[41];
	int show_raw_time = !!(opt & OUTPUT_RAW_TIMESTAMP);

	get_commit_info(suspect->commit, &ci, 1);
	strcpy(hex, sha1_to_hex(suspect->commit->object.sha1));

	cp = nth_line(sb, ent->lno);
	for (cnt = 0; cnt < ent->num_lines; cnt++) {
		char ch;
		int length = (opt & OUTPUT_LONG_OBJECT_NAME) ? 40 : abbrev;

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
				name = ci.author_mail;
			else
				name = ci.author;
			printf("\t(%10s\t%10s\t%d)", name,
			       format_time(ci.author_time, ci.author_tz,
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
					name = ci.author_mail;
				else
					name = ci.author;
				pad = longest_author - utf8_strwidth(name);
				printf(" (%s%*s %10s",
				       name, pad, "",
				       format_time(ci.author_time,
						   ci.author_tz,
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
}

static void output(struct scoreboard *sb, int option)
{
	struct blame_entry *ent;

	if (option & OUTPUT_PORCELAIN) {
		for (ent = sb->ent; ent; ent = ent->next) {
			struct blame_entry *oth;
			struct origin *suspect = ent->suspect;
			struct commit *commit = suspect->commit;
			if (commit->object.flags & MORE_THAN_ONE_PATH)
				continue;
			for (oth = ent->next; oth; oth = oth->next) {
				if ((oth->suspect->commit != commit) ||
				    !strcmp(oth->suspect->path, suspect->path))
					continue;
				commit->object.flags |= MORE_THAN_ONE_PATH;
				break;
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

/*
 * To allow quick access to the contents of nth line in the
 * final image, prepare an index in the scoreboard.
 */
static int prepare_lines(struct scoreboard *sb)
{
	const char *buf = sb->final_buf;
	unsigned long len = sb->final_buf_size;
	int num = 0, incomplete = 0, bol = 1;

	if (len && buf[len-1] != '\n')
		incomplete++; /* incomplete line at the end */
	while (len--) {
		if (bol) {
			sb->lineno = xrealloc(sb->lineno,
					      sizeof(int *) * (num + 1));
			sb->lineno[num] = buf - sb->final_buf;
			bol = 0;
		}
		if (*buf++ == '\n') {
			num++;
			bol = 1;
		}
	}
	sb->lineno = xrealloc(sb->lineno,
			      sizeof(int *) * (num + incomplete + 1));
	sb->lineno[num + incomplete] = buf - sb->final_buf;
	sb->num_lines = num + incomplete;
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
	char buf[1024];
	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp)) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		int len = strlen(buf);
		struct commit_graft *graft = read_graft_line(buf, len);
		if (graft)
			register_commit_graft(graft, 0);
	}
	fclose(fp);
	return 0;
}

static int update_auto_abbrev(int auto_abbrev, struct origin *suspect)
{
	const char *uniq = find_unique_abbrev(suspect->commit->object.sha1,
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
	int auto_abbrev = default_abbrev;

	for (e = sb->ent; e; e = e->next) {
		struct origin *suspect = e->suspect;
		struct commit_info ci;
		int num;

		if (compute_auto_abbrev)
			auto_abbrev = update_auto_abbrev(auto_abbrev, suspect);
		if (strcmp(suspect->path, sb->path))
			*option |= OUTPUT_SHOW_NAME;
		num = strlen(suspect->path);
		if (longest_file < num)
			longest_file = num;
		if (!(suspect->commit->object.flags & METAINFO_SHOWN)) {
			suspect->commit->object.flags |= METAINFO_SHOWN;
			get_commit_info(suspect->commit, &ci, 1);
			if (*option & OUTPUT_SHOW_EMAIL)
				num = utf8_strwidth(ci.author_mail);
			else
				num = utf8_strwidth(ci.author);
			if (longest_author < num)
				longest_author = num;
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
				sha1_to_hex(ent->suspect->commit->object.sha1),
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

/*
 * Used for the command line parsing; check if the path exists
 * in the working tree.
 */
static int has_string_in_work_tree(const char *path)
{
	struct stat st;
	return !lstat(path, &st);
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

/*
 * Parsing of (comma separated) one item in the -L option
 */
static const char *parse_loc(const char *spec,
			     struct scoreboard *sb, long lno,
			     long begin, long *ret)
{
	char *term;
	const char *line;
	long num;
	int reg_error;
	regex_t regexp;
	regmatch_t match[1];

	/* Allow "-L <something>,+20" to mean starting at <something>
	 * for 20 lines, or "-L <something>,-5" for 5 lines ending at
	 * <something>.
	 */
	if (1 < begin && (spec[0] == '+' || spec[0] == '-')) {
		num = strtol(spec + 1, &term, 10);
		if (term != spec + 1) {
			if (spec[0] == '-')
				num = 0 - num;
			if (0 < num)
				*ret = begin + num - 2;
			else if (!num)
				*ret = begin;
			else
				*ret = begin + num;
			return term;
		}
		return spec;
	}
	num = strtol(spec, &term, 10);
	if (term != spec) {
		*ret = num;
		return term;
	}
	if (spec[0] != '/')
		return spec;

	/* it could be a regexp of form /.../ */
	for (term = (char *) spec + 1; *term && *term != '/'; term++) {
		if (*term == '\\')
			term++;
	}
	if (*term != '/')
		return spec;

	/* try [spec+1 .. term-1] as regexp */
	*term = 0;
	begin--; /* input is in human terms */
	line = nth_line(sb, begin);

	if (!(reg_error = regcomp(&regexp, spec + 1, REG_NEWLINE)) &&
	    !(reg_error = regexec(&regexp, line, 1, match, 0))) {
		const char *cp = line + match[0].rm_so;
		const char *nline;

		while (begin++ < lno) {
			nline = nth_line(sb, begin);
			if (line <= cp && cp < nline)
				break;
			line = nline;
		}
		*ret = begin;
		regfree(&regexp);
		*term++ = '/';
		return term;
	}
	else {
		char errbuf[1024];
		regerror(reg_error, &regexp, errbuf, 1024);
		die("-L parameter '%s': %s", spec + 1, errbuf);
	}
}

/*
 * Parsing of -L option
 */
static void prepare_blame_range(struct scoreboard *sb,
				const char *bottomtop,
				long lno,
				long *bottom, long *top)
{
	const char *term;

	term = parse_loc(bottomtop, sb, lno, 1, bottom);
	if (*term == ',') {
		term = parse_loc(term + 1, sb, lno, *bottom + 1, top);
		if (*term)
			usage(blame_usage);
	}
	if (*term)
		usage(blame_usage);
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
	if (!strcmp(var, "blame.date")) {
		if (!value)
			return config_error_nonbool(var);
		blame_date_mode = parse_date_format(value);
		return 0;
	}

	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, cb);
}

static void verify_working_tree_path(struct commit *work_tree, const char *path)
{
	struct commit_list *parents;

	for (parents = work_tree->parents; parents; parents = parents->next) {
		const unsigned char *commit_sha1 = parents->item->object.sha1;
		unsigned char blob_sha1[20];
		unsigned mode;

		if (!get_tree_entry(commit_sha1, path, blob_sha1, &mode) &&
		    sha1_object_info(blob_sha1, NULL) == OBJ_BLOB)
			return;
	}
	die("no such path '%s' in HEAD", path);
}

static struct commit_list **append_parent(struct commit_list **tail, const unsigned char *sha1)
{
	struct commit *parent;

	parent = lookup_commit_reference(sha1);
	if (!parent)
		die("no such commit %s", sha1_to_hex(sha1));
	return &commit_list_insert(parent, tail)->next;
}

static void append_merge_parents(struct commit_list **tail)
{
	int merge_head;
	const char *merge_head_file = git_path("MERGE_HEAD");
	struct strbuf line = STRBUF_INIT;

	merge_head = open(merge_head_file, O_RDONLY);
	if (merge_head < 0) {
		if (errno == ENOENT)
			return;
		die("cannot open '%s' for reading", merge_head_file);
	}

	while (!strbuf_getwholeline_fd(&line, merge_head, '\n')) {
		unsigned char sha1[20];
		if (line.len < 40 || get_sha1_hex(line.buf, sha1))
			die("unknown line in '%s': %s", merge_head_file, line.buf);
		tail = append_parent(tail, sha1);
	}
	close(merge_head);
	strbuf_release(&line);
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
	unsigned char head_sha1[20];
	struct strbuf buf = STRBUF_INIT;
	const char *ident;
	time_t now;
	int size, len;
	struct cache_entry *ce;
	unsigned mode;
	struct strbuf msg = STRBUF_INIT;

	time(&now);
	commit = xcalloc(1, sizeof(*commit));
	commit->object.parsed = 1;
	commit->date = now;
	commit->object.type = OBJ_COMMIT;
	parent_tail = &commit->parents;

	if (!resolve_ref_unsafe("HEAD", head_sha1, 1, NULL))
		die("no such ref: HEAD");

	parent_tail = append_parent(parent_tail, head_sha1);
	append_merge_parents(parent_tail);
	verify_working_tree_path(commit, path);

	origin = make_origin(commit, path);

	ident = fmt_ident("Not Committed Yet", "not.committed.yet", NULL, 0);
	strbuf_addstr(&msg, "tree 0000000000000000000000000000000000000000\n");
	for (parent = commit->parents; parent; parent = parent->next)
		strbuf_addf(&msg, "parent %s\n",
			    sha1_to_hex(parent->item->object.sha1));
	strbuf_addf(&msg,
		    "author %s\n"
		    "committer %s\n\n"
		    "Version of %s from %s\n",
		    ident, ident, path,
		    (!contents_from ? path :
		     (!strcmp(contents_from, "-") ? "standard input" : contents_from)));
	commit->buffer = strbuf_detach(&msg, NULL);

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
			    textconv_object(read_from, mode, null_sha1, 0, &buf_ptr, &buf_len))
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
	pretend_sha1_file(buf.buf, buf.len, OBJ_BLOB, origin->blob_sha1);
	commit->util = origin;

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
	hashcpy(ce->sha1, origin->blob_sha1);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(0);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);

	/*
	 * We are not going to write this out, so this does not matter
	 * right now, but someday we might optimize diff-index --cached
	 * with cache-tree information.
	 */
	cache_tree_invalidate_path(active_cache_tree, path);

	return commit;
}

static const char *prepare_final(struct scoreboard *sb)
{
	int i;
	const char *final_commit_name = NULL;
	struct rev_info *revs = sb->revs;

	/*
	 * There must be one and only one positive commit in the
	 * revs->pending array.
	 */
	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		while (obj->type == OBJ_TAG)
			obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (sb->final)
			die("More than one commit to dig from %s and %s?",
			    revs->pending.objects[i].name,
			    final_commit_name);
		sb->final = (struct commit *) obj;
		final_commit_name = revs->pending.objects[i].name;
	}
	return final_commit_name;
}

static const char *prepare_initial(struct scoreboard *sb)
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
		while (obj->type == OBJ_TAG)
			obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (sb->final)
			die("More than one commit to dig down to %s and %s?",
			    revs->pending.objects[i].name,
			    final_commit_name);
		sb->final = (struct commit *) obj;
		final_commit_name = revs->pending.objects[i].name;
	}
	if (!final_commit_name)
		die("No commit to dig down to?");
	return final_commit_name;
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

static int blame_bottomtop_callback(const struct option *option, const char *arg, int unset)
{
	const char **bottomtop = option->value;
	if (!arg)
		return -1;
	if (*bottomtop)
		die("More than one '-L n,m' option given");
	*bottomtop = arg;
	return 0;
}

int cmd_blame(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	const char *path;
	struct scoreboard sb;
	struct origin *o;
	struct blame_entry *ent;
	long dashdash_pos, bottom, top, lno;
	const char *final_commit_name = NULL;
	enum object_type type;

	static const char *bottomtop = NULL;
	static int output_option = 0, opt = 0;
	static int show_stats = 0;
	static const char *revs_file = NULL;
	static const char *contents_from = NULL;
	static const struct option options[] = {
		OPT_BOOLEAN(0, "incremental", &incremental, N_("Show blame entries as we find them, incrementally")),
		OPT_BOOLEAN('b', NULL, &blank_boundary, N_("Show blank SHA-1 for boundary commits (Default: off)")),
		OPT_BOOLEAN(0, "root", &show_root, N_("Do not treat root commits as boundaries (Default: off)")),
		OPT_BOOLEAN(0, "show-stats", &show_stats, N_("Show work cost statistics")),
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
		OPT_BIT(0, "minimal", &xdl_opts, N_("Spend extra cycles to find better match"), XDF_NEED_MINIMAL),
		OPT_STRING('S', NULL, &revs_file, N_("file"), N_("Use revisions from <file> instead of calling git-rev-list")),
		OPT_STRING(0, "contents", &contents_from, N_("file"), N_("Use <file>'s contents as the final image")),
		{ OPTION_CALLBACK, 'C', NULL, &opt, N_("score"), N_("Find line copies within and across files"), PARSE_OPT_OPTARG, blame_copy_callback },
		{ OPTION_CALLBACK, 'M', NULL, &opt, N_("score"), N_("Find line movements within and across files"), PARSE_OPT_OPTARG, blame_move_callback },
		OPT_CALLBACK('L', NULL, &bottomtop, N_("n,m"), N_("Process only line range n,m, counting from 1"), blame_bottomtop_callback),
		OPT__ABBREV(&abbrev),
		OPT_END()
	};

	struct parse_opt_ctx_t ctx;
	int cmd_is_annotate = !strcmp(argv[0], "annotate");

	git_config(git_blame_config, NULL);
	init_revisions(&revs, NULL);
	revs.date_mode = blame_date_mode;
	DIFF_OPT_SET(&revs.diffopt, ALLOW_TEXTCONV);

	save_commit_buffer = 0;
	dashdash_pos = 0;

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
	argc = parse_options_end(&ctx);

	if (0 < abbrev)
		/* one more abbrev length is needed for the boundary commit */
		abbrev++;

	if (revs_file && read_ancestry(revs_file))
		die_errno("reading graft file '%s' failed", revs_file);

	if (cmd_is_annotate) {
		output_option |= OUTPUT_ANNOTATE_COMPAT;
		blame_date_mode = DATE_ISO8601;
	} else {
		blame_date_mode = revs.date_mode;
	}

	/* The maximum width used to show the dates */
	switch (blame_date_mode) {
	case DATE_RFC2822:
		blame_date_width = sizeof("Thu, 19 Oct 2006 16:00:04 -0700");
		break;
	case DATE_ISO8601:
		blame_date_width = sizeof("2006-10-19 16:00:04 -0700");
		break;
	case DATE_RAW:
		blame_date_width = sizeof("1161298804 -0700");
		break;
	case DATE_SHORT:
		blame_date_width = sizeof("2006-10-19");
		break;
	case DATE_RELATIVE:
		/* "normal" is used as the fallback for "relative" */
	case DATE_LOCAL:
	case DATE_NORMAL:
		blame_date_width = sizeof("Thu Oct 19 16:00:04 2006 -0700");
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
		if (argc == 3 && !has_string_in_work_tree(path)) { /* (2b) */
			path = add_prefix(prefix, argv[1]);
			argv[1] = argv[2];
		}
		argv[argc - 1] = "--";

		setup_work_tree();
		if (!has_string_in_work_tree(path))
			die_errno("cannot stat path '%s'", path);
	}

	revs.disable_stdin = 1;
	setup_revisions(argc, argv, &revs, NULL);
	memset(&sb, 0, sizeof(sb));

	sb.revs = &revs;
	if (!reverse)
		final_commit_name = prepare_final(&sb);
	else if (contents_from)
		die("--contents and --children do not blend well.");
	else
		final_commit_name = prepare_initial(&sb);

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
		die("Cannot use --contents with final commit object name");

	/*
	 * If we have bottom, this will mark the ancestors of the
	 * bottom commits we would reach while traversing as
	 * uninteresting.
	 */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	if (is_null_sha1(sb.final->object.sha1)) {
		char *buf;
		o = sb.final->util;
		buf = xmalloc(o->file.size + 1);
		memcpy(buf, o->file.ptr, o->file.size + 1);
		sb.final_buf = buf;
		sb.final_buf_size = o->file.size;
	}
	else {
		o = get_origin(&sb, sb.final, path);
		if (fill_blob_sha1_and_mode(o))
			die("no such path %s in %s", path, final_commit_name);

		if (DIFF_OPT_TST(&sb.revs->diffopt, ALLOW_TEXTCONV) &&
		    textconv_object(path, o->mode, o->blob_sha1, 1, (char **) &sb.final_buf,
				    &sb.final_buf_size))
			;
		else
			sb.final_buf = read_sha1_file(o->blob_sha1, &type,
						      &sb.final_buf_size);

		if (!sb.final_buf)
			die("Cannot read blob %s for path %s",
			    sha1_to_hex(o->blob_sha1),
			    path);
	}
	num_read_blob++;
	lno = prepare_lines(&sb);

	bottom = top = 0;
	if (bottomtop)
		prepare_blame_range(&sb, bottomtop, lno, &bottom, &top);
	if (bottom && top && top < bottom) {
		long tmp;
		tmp = top; top = bottom; bottom = tmp;
	}
	if (bottom < 1)
		bottom = 1;
	if (top < 1)
		top = lno;
	bottom--;
	if (lno < top || lno < bottom)
		die("file %s has only %lu lines", path, lno);

	ent = xcalloc(1, sizeof(*ent));
	ent->lno = bottom;
	ent->num_lines = top - bottom;
	ent->suspect = o;
	ent->s_lno = bottom;

	sb.ent = ent;
	sb.path = path;

	read_mailmap(&mailmap, NULL);

	if (!incremental)
		setup_pager();

	assign_blame(&sb, opt);

	if (incremental)
		return 0;

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
