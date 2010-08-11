#ifndef LINE_H
#define LINE_H

#include "diffcore.h"

struct rev_info;
struct commit;
struct diff_line_range;
struct diff_options;

struct print_range {
	int start, end;		/* Line range of post-image */
	int pstart, pend;	/* Line range of pre-image */
	int line_added : 1;	/* whether this range is added */
};

struct print_pair {
	int alloc, nr;
	struct print_range *ranges;
};

#define PRINT_RANGE_INIT(r) \
	do { \
		(r)->start = (r)->end = 0; \
		(r)->pstart = (r)->pend = 0; \
		(r)->line_added = 0; \
	} while (0)

#define PRINT_PAIR_INIT(p) \
	do { \
		(p)->alloc = (p)->nr = 0; \
		(p)->ranges = NULL; \
	} while (0)

#define PRINT_PAIR_GROW(p) \
	do { \
		(p)->nr++; \
		ALLOC_GROW((p)->ranges, (p)->nr, (p)->alloc); \
	} while (0)

#define PRINT_PAIR_CLEAR(p) \
	do { \
		(p)->alloc = (p)->nr = 0; \
		if ((p)->ranges) \
			free((p)->ranges); \
		(p)->ranges = NULL; \
	} while (0)

struct line_range {
	const char *arg;	/* The argument to specify this line range */
	long start, end;	/* The interesting line range of current commit */
	long pstart, pend;	/* The corresponding range of parent commit */
	struct print_pair pair;
			/* The changed lines inside this range */
	unsigned int diff:1;
};

struct diff_line_range {
	struct diff_filespec *prev;
	struct diff_filespec *spec;
	char status;
	int alloc;
	int nr;
	struct line_range *ranges;
	unsigned int	touch:1,
			diff:1;
	struct diff_line_range *next;
};

#define RANGE_INIT(r) \
	do { \
		(r)->arg = NULL; \
		(r)->start = (r)->end = 0; \
		(r)->pstart = (r)->pend = 0; \
		PRINT_PAIR_INIT(&((r)->pair)); \
		(r)->diff = 0; \
	} while (0)

#define RANGE_CLEAR(r) \
	do { \
		(r)->arg = NULL; \
		(r)->start = (r)->end = 0; \
		(r)->pstart = (r)->pend = 0; \
		PRINT_PAIR_CLEAR(&r->pair); \
		(r)->diff = 0; \
	} while (0)

#define DIFF_LINE_RANGE_INIT(r) \
	do { \
		(r)->prev = (r)->spec = NULL; \
		(r)->status = '\0'; \
		(r)->alloc = (r)->nr = 0; \
		(r)->ranges = NULL; \
		(r)->next = NULL; \
		(r)->touch = 0; \
		(r)->diff = 0; \
	} while (0)

#define DIFF_LINE_RANGE_GROW(r) \
	do { \
		(r)->nr++; \
		ALLOC_GROW((r)->ranges, (r)->nr, (r)->alloc); \
		RANGE_INIT(((r)->ranges + (r)->nr - 1)); \
	} while (0)

#define DIFF_LINE_RANGE_CLEAR(r) \
	diff_line_range_clear((r));

extern struct line_range *diff_line_range_insert(struct diff_line_range *r,
		const char *arg, int start, int end);

extern void diff_line_range_append(struct diff_line_range *r, const char *arg);

extern void diff_line_range_clear(struct diff_line_range *r);

extern struct diff_line_range *diff_line_range_merge(
		struct diff_line_range *out,
		struct diff_line_range *other);

extern void setup_line(struct rev_info *rev, struct diff_line_range *r);

extern void add_line_range(struct rev_info *revs, struct commit *commit,
		struct diff_line_range *r);

extern struct diff_line_range *lookup_line_range(struct rev_info *revs,
		struct commit *commit);

#endif
