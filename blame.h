#ifndef BLAME_H
#define BLAME_H

#include "commit.h"
#include "oidset.h"
#include "xdiff-interface.h"
#include "revision.h"
#include "prio-queue.h"
#include "diff.h"

#define PICKAXE_BLAME_MOVE		01
#define PICKAXE_BLAME_COPY		02
#define PICKAXE_BLAME_COPY_HARDER	04
#define PICKAXE_BLAME_COPY_HARDEST	010

#define BLAME_DEFAULT_MOVE_SCORE	20
#define BLAME_DEFAULT_COPY_SCORE	40

struct fingerprint;

/*
 * One blob in a commit that is being suspected
 */
struct blame_origin {
	int refcnt;
	/* Record preceding blame record for this blob */
	struct blame_origin *previous;
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
	struct blame_origin *next;
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
	int num_lines;
	struct fingerprint *fingerprints;
	struct object_id blob_oid;
	unsigned short mode;
	/* guilty gets set when shipping any suspects to the final
	 * blame list instead of other commits
	 */
	char guilty;
	char path[FLEX_ARRAY];
};

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
	struct blame_origin *suspect;

	/* the line number of the first line of this group in the
	 * suspect's file; internally all line numbers are 0 based.
	 */
	int s_lno;

	/* how significant this entry is -- cached to avoid
	 * scanning the lines over and over.
	 */
	unsigned score;
	int ignored;
	int unblamable;
};

struct blame_bloom_data;

/*
 * The current state of the blame assignment.
 */
struct blame_scoreboard {
	/* the final commit (i.e. where we started digging from) */
	struct commit *final;
	/* Priority queue for commits with unassigned blame records */
	struct prio_queue commits;
	struct repository *repo;
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

	struct oidset ignore_list;

	/* look-up a line in the final buffer */
	int num_lines;
	int *lineno;

	/* stats */
	int num_read_blob;
	int num_get_patch;
	int num_commits;

	/*
	 * blame for a blame_entry with score lower than these thresholds
	 * is not passed to the parent using move/copy logic.
	 */
	unsigned move_score;
	unsigned copy_score;

	/* use this file's contents as the final image */
	const char *contents_from;

	/* flags */
	int reverse;
	int show_root;
	int xdl_opts;
	int no_whole_file_rename;
	int debug;

	/* callbacks */
	void(*on_sanity_fail)(struct blame_scoreboard *, int);
	void(*found_guilty_entry)(struct blame_entry *, void *);

	void *found_guilty_entry_data;
	struct blame_bloom_data *bloom_data;
};

/*
 * Origin is refcounted and usually we keep the blob contents to be
 * reused.
 */
static inline struct blame_origin *blame_origin_incref(struct blame_origin *o)
{
	if (o)
		o->refcnt++;
	return o;
}
void blame_origin_decref(struct blame_origin *o);

void blame_coalesce(struct blame_scoreboard *sb);
void blame_sort_final(struct blame_scoreboard *sb);
unsigned blame_entry_score(struct blame_scoreboard *sb, struct blame_entry *e);
void assign_blame(struct blame_scoreboard *sb, int opt);
const char *blame_nth_line(struct blame_scoreboard *sb, long lno);

void init_scoreboard(struct blame_scoreboard *sb);
void setup_scoreboard(struct blame_scoreboard *sb,
		      struct blame_origin **orig);
void setup_blame_bloom_data(struct blame_scoreboard *sb);
void cleanup_scoreboard(struct blame_scoreboard *sb);

struct blame_entry *blame_entry_prepend(struct blame_entry *head,
					long start, long end,
					struct blame_origin *o);

struct blame_origin *get_blame_suspects(struct commit *commit);

#endif /* BLAME_H */
