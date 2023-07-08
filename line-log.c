#include "git-compat-util.h"
#include "alloc.h"
#include "line-range.h"
#include "hex.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "diff.h"
#include "commit.h"
#include "decorate.h"
#include "repository.h"
#include "revision.h"
#include "xdiff-interface.h"
#include "strbuf.h"
#include "log-tree.h"
#include "graph.h"
#include "userdiff.h"
#include "line-log.h"
#include "setup.h"
#include "strvec.h"
#include "bloom.h"
#include "tree-walk.h"

static void range_set_grow(struct range_set *rs, size_t extra)
{
	ALLOC_GROW(rs->ranges, rs->nr + extra, rs->alloc);
}

/* Either initialization would be fine */
#define RANGE_SET_INIT {0}

void range_set_init(struct range_set *rs, size_t prealloc)
{
	rs->alloc = rs->nr = 0;
	rs->ranges = NULL;
	if (prealloc)
		range_set_grow(rs, prealloc);
}

void range_set_release(struct range_set *rs)
{
	FREE_AND_NULL(rs->ranges);
	rs->alloc = rs->nr = 0;
}

/* dst must be uninitialized! */
static void range_set_copy(struct range_set *dst, struct range_set *src)
{
	range_set_init(dst, src->nr);
	COPY_ARRAY(dst->ranges, src->ranges, src->nr);
	dst->nr = src->nr;
}

static void range_set_move(struct range_set *dst, struct range_set *src)
{
	range_set_release(dst);
	dst->ranges = src->ranges;
	dst->nr = src->nr;
	dst->alloc = src->alloc;
	src->ranges = NULL;
	src->alloc = src->nr = 0;
}

/* tack on a _new_ range _at the end_ */
void range_set_append_unsafe(struct range_set *rs, long a, long b)
{
	assert(a <= b);
	range_set_grow(rs, 1);
	rs->ranges[rs->nr].start = a;
	rs->ranges[rs->nr].end = b;
	rs->nr++;
}

void range_set_append(struct range_set *rs, long a, long b)
{
	assert(rs->nr == 0 || rs->ranges[rs->nr-1].end <= a);
	range_set_append_unsafe(rs, a, b);
}

static int range_cmp(const void *_r, const void *_s)
{
	const struct range *r = _r;
	const struct range *s = _s;

	/* this could be simply 'return r.start-s.start', but for the types */
	if (r->start == s->start)
		return 0;
	if (r->start < s->start)
		return -1;
	return 1;
}

/*
 * Check that the ranges are non-empty, sorted and non-overlapping
 */
static void range_set_check_invariants(struct range_set *rs)
{
	unsigned int i;

	if (!rs)
		return;

	if (rs->nr)
		assert(rs->ranges[0].start < rs->ranges[0].end);

	for (i = 1; i < rs->nr; i++) {
		assert(rs->ranges[i-1].end < rs->ranges[i].start);
		assert(rs->ranges[i].start < rs->ranges[i].end);
	}
}

/*
 * In-place pass of sorting and merging the ranges in the range set,
 * to establish the invariants when we get the ranges from the user
 */
void sort_and_merge_range_set(struct range_set *rs)
{
	unsigned int i;
	unsigned int o = 0; /* output cursor */

	QSORT(rs->ranges, rs->nr, range_cmp);

	for (i = 0; i < rs->nr; i++) {
		if (rs->ranges[i].start == rs->ranges[i].end)
			continue;
		if (o > 0 && rs->ranges[i].start <= rs->ranges[o-1].end) {
			if (rs->ranges[o-1].end < rs->ranges[i].end)
				rs->ranges[o-1].end = rs->ranges[i].end;
		} else {
			rs->ranges[o].start = rs->ranges[i].start;
			rs->ranges[o].end = rs->ranges[i].end;
			o++;
		}
	}
	assert(o <= rs->nr);
	rs->nr = o;

	range_set_check_invariants(rs);
}

/*
 * Union of range sets (i.e., sets of line numbers).  Used to merge
 * them when searches meet at a common ancestor.
 *
 * This is also where the ranges are consolidated into canonical form:
 * overlapping and adjacent ranges are merged, and empty ranges are
 * removed.
 */
static void range_set_union(struct range_set *out,
			     struct range_set *a, struct range_set *b)
{
	unsigned int i = 0, j = 0;
	struct range *ra = a->ranges;
	struct range *rb = b->ranges;
	/* cannot make an alias of out->ranges: it may change during grow */

	assert(out->nr == 0);
	while (i < a->nr || j < b->nr) {
		struct range *new_range;
		if (i < a->nr && j < b->nr) {
			if (ra[i].start < rb[j].start)
				new_range = &ra[i++];
			else if (ra[i].start > rb[j].start)
				new_range = &rb[j++];
			else if (ra[i].end < rb[j].end)
				new_range = &ra[i++];
			else
				new_range = &rb[j++];
		} else if (i < a->nr)      /* b exhausted */
			new_range = &ra[i++];
		else                       /* a exhausted */
			new_range = &rb[j++];
		if (new_range->start == new_range->end)
			; /* empty range */
		else if (!out->nr || out->ranges[out->nr-1].end < new_range->start) {
			range_set_grow(out, 1);
			out->ranges[out->nr].start = new_range->start;
			out->ranges[out->nr].end = new_range->end;
			out->nr++;
		} else if (out->ranges[out->nr-1].end < new_range->end) {
			out->ranges[out->nr-1].end = new_range->end;
		}
	}
}

/*
 * Difference of range sets (out = a \ b).  Pass the "interesting"
 * ranges as 'a' and the target side of the diff as 'b': it removes
 * the ranges for which the commit is responsible.
 */
static void range_set_difference(struct range_set *out,
				  struct range_set *a, struct range_set *b)
{
	unsigned int i, j =  0;
	for (i = 0; i < a->nr; i++) {
		long start = a->ranges[i].start;
		long end = a->ranges[i].end;
		while (start < end) {
			while (j < b->nr && start >= b->ranges[j].end)
				/*
				 * a:         |-------
				 * b: ------|
				 */
				j++;
			if (j >= b->nr || end < b->ranges[j].start) {
				/*
				 * b exhausted, or
				 * a:  ----|
				 * b:         |----
				 */
				range_set_append(out, start, end);
				break;
			}
			if (start >= b->ranges[j].start) {
				/*
				 * a:     |--????
				 * b: |------|
				 */
				start = b->ranges[j].end;
			} else if (end > b->ranges[j].start) {
				/*
				 * a: |-----|
				 * b:    |--?????
				 */
				if (start < b->ranges[j].start)
					range_set_append(out, start, b->ranges[j].start);
				start = b->ranges[j].end;
			}
		}
	}
}

static void diff_ranges_init(struct diff_ranges *diff)
{
	range_set_init(&diff->parent, 0);
	range_set_init(&diff->target, 0);
}

static void diff_ranges_release(struct diff_ranges *diff)
{
	range_set_release(&diff->parent);
	range_set_release(&diff->target);
}

static void line_log_data_init(struct line_log_data *r)
{
	memset(r, 0, sizeof(struct line_log_data));
	range_set_init(&r->ranges, 0);
}

static void line_log_data_clear(struct line_log_data *r)
{
	range_set_release(&r->ranges);
	if (r->pair)
		diff_free_filepair(r->pair);
}

static void free_line_log_data(struct line_log_data *r)
{
	while (r) {
		struct line_log_data *next = r->next;
		line_log_data_clear(r);
		free(r);
		r = next;
	}
}

static struct line_log_data *
search_line_log_data(struct line_log_data *list, const char *path,
		     struct line_log_data **insertion_point)
{
	struct line_log_data *p = list;
	if (insertion_point)
		*insertion_point = NULL;
	while (p) {
		int cmp = strcmp(p->path, path);
		if (!cmp)
			return p;
		if (insertion_point && cmp < 0)
			*insertion_point = p;
		p = p->next;
	}
	return NULL;
}

/*
 * Note: takes ownership of 'path', which happens to be what the only
 * caller needs.
 */
static void line_log_data_insert(struct line_log_data **list,
				 char *path,
				 long begin, long end)
{
	struct line_log_data *ip;
	struct line_log_data *p = search_line_log_data(*list, path, &ip);

	if (p) {
		range_set_append_unsafe(&p->ranges, begin, end);
		free(path);
		return;
	}

	CALLOC_ARRAY(p, 1);
	p->path = path;
	range_set_append(&p->ranges, begin, end);
	if (ip) {
		p->next = ip->next;
		ip->next = p;
	} else {
		p->next = *list;
		*list = p;
	}
}

struct collect_diff_cbdata {
	struct diff_ranges *diff;
};

static int collect_diff_cb(long start_a, long count_a,
			   long start_b, long count_b,
			   void *data)
{
	struct collect_diff_cbdata *d = data;

	if (count_a >= 0)
		range_set_append(&d->diff->parent, start_a, start_a + count_a);
	if (count_b >= 0)
		range_set_append(&d->diff->target, start_b, start_b + count_b);

	return 0;
}

static int collect_diff(mmfile_t *parent, mmfile_t *target, struct diff_ranges *out)
{
	struct collect_diff_cbdata cbdata = {NULL};
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;

	memset(&xpp, 0, sizeof(xpp));
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = xecfg.interhunkctxlen = 0;

	cbdata.diff = out;
	xecfg.hunk_func = collect_diff_cb;
	memset(&ecb, 0, sizeof(ecb));
	ecb.priv = &cbdata;
	return xdi_diff(parent, target, &xpp, &xecfg, &ecb);
}

/*
 * These are handy for debugging.  Removing them with #if 0 silences
 * the "unused function" warning.
 */
#if 0
static void dump_range_set(struct range_set *rs, const char *desc)
{
	int i;
	printf("range set %s (%d items):\n", desc, rs->nr);
	for (i = 0; i < rs->nr; i++)
		printf("\t[%ld,%ld]\n", rs->ranges[i].start, rs->ranges[i].end);
}

static void dump_line_log_data(struct line_log_data *r)
{
	char buf[4096];
	while (r) {
		snprintf(buf, 4096, "file %s\n", r->path);
		dump_range_set(&r->ranges, buf);
		r = r->next;
	}
}

static void dump_diff_ranges(struct diff_ranges *diff, const char *desc)
{
	int i;
	assert(diff->parent.nr == diff->target.nr);
	printf("diff ranges %s (%d items):\n", desc, diff->parent.nr);
	printf("\tparent\ttarget\n");
	for (i = 0; i < diff->parent.nr; i++) {
		printf("\t[%ld,%ld]\t[%ld,%ld]\n",
		       diff->parent.ranges[i].start,
		       diff->parent.ranges[i].end,
		       diff->target.ranges[i].start,
		       diff->target.ranges[i].end);
	}
}
#endif


static int ranges_overlap(struct range *a, struct range *b)
{
	return !(a->end <= b->start || b->end <= a->start);
}

/*
 * Given a diff and the set of interesting ranges, determine all hunks
 * of the diff which touch (overlap) at least one of the interesting
 * ranges in the target.
 */
static void diff_ranges_filter_touched(struct diff_ranges *out,
				       struct diff_ranges *diff,
				       struct range_set *rs)
{
	unsigned int i, j = 0;

	assert(out->target.nr == 0);

	for (i = 0; i < diff->target.nr; i++) {
		while (diff->target.ranges[i].start > rs->ranges[j].end) {
			j++;
			if (j == rs->nr)
				return;
		}
		if (ranges_overlap(&diff->target.ranges[i], &rs->ranges[j])) {
			range_set_append(&out->parent,
					 diff->parent.ranges[i].start,
					 diff->parent.ranges[i].end);
			range_set_append(&out->target,
					 diff->target.ranges[i].start,
					 diff->target.ranges[i].end);
		}
	}
}

/*
 * Adjust the line counts in 'rs' to account for the lines
 * added/removed in the diff.
 */
static void range_set_shift_diff(struct range_set *out,
				 struct range_set *rs,
				 struct diff_ranges *diff)
{
	unsigned int i, j = 0;
	long offset = 0;
	struct range *src = rs->ranges;
	struct range *target = diff->target.ranges;
	struct range *parent = diff->parent.ranges;

	for (i = 0; i < rs->nr; i++) {
		while (j < diff->target.nr && src[i].start >= target[j].start) {
			offset += (parent[j].end-parent[j].start)
				- (target[j].end-target[j].start);
			j++;
		}
		range_set_append(out, src[i].start+offset, src[i].end+offset);
	}
}

/*
 * Given a diff and the set of interesting ranges, map the ranges
 * across the diff.  That is: observe that the target commit takes
 * blame for all the + (target-side) ranges.  So for every pair of
 * ranges in the diff that was touched, we remove the latter and add
 * its parent side.
 */
static void range_set_map_across_diff(struct range_set *out,
				      struct range_set *rs,
				      struct diff_ranges *diff,
				      struct diff_ranges **touched_out)
{
	struct diff_ranges *touched = xmalloc(sizeof(*touched));
	struct range_set tmp1 = RANGE_SET_INIT;
	struct range_set tmp2 = RANGE_SET_INIT;

	diff_ranges_init(touched);
	diff_ranges_filter_touched(touched, diff, rs);
	range_set_difference(&tmp1, rs, &touched->target);
	range_set_shift_diff(&tmp2, &tmp1, diff);
	range_set_union(out, &tmp2, &touched->parent);
	range_set_release(&tmp1);
	range_set_release(&tmp2);

	*touched_out = touched;
}

static struct commit *check_single_commit(struct rev_info *revs)
{
	struct object *commit = NULL;
	int found = -1;
	int i;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		obj = deref_tag(revs->repo, obj, NULL, 0);
		if (!obj || obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (commit)
			die("More than one commit to dig from: %s and %s?",
			    revs->pending.objects[i].name,
			    revs->pending.objects[found].name);
		commit = obj;
		found = i;
	}

	if (!commit)
		die("No commit specified?");

	return (struct commit *) commit;
}

static void fill_blob_sha1(struct repository *r, struct commit *commit,
			   struct diff_filespec *spec)
{
	unsigned short mode;
	struct object_id oid;

	if (get_tree_entry(r, &commit->object.oid, spec->path, &oid, &mode))
		die("There is no path %s in the commit", spec->path);
	fill_filespec(spec, &oid, 1, mode);

	return;
}

static void fill_line_ends(struct repository *r,
			   struct diff_filespec *spec,
			   long *lines,
			   unsigned long **line_ends)
{
	int num = 0, size = 50;
	long cur = 0;
	unsigned long *ends = NULL;
	char *data = NULL;

	if (diff_populate_filespec(r, spec, NULL))
		die("Cannot read blob %s", oid_to_hex(&spec->oid));

	ALLOC_ARRAY(ends, size);
	ends[cur++] = 0;
	data = spec->data;
	while (num < spec->size) {
		if (data[num] == '\n' || num == spec->size - 1) {
			ALLOC_GROW(ends, (cur + 1), size);
			ends[cur++] = num;
		}
		num++;
	}

	/* shrink the array to fit the elements */
	REALLOC_ARRAY(ends, cur);
	*lines = cur-1;
	*line_ends = ends;
}

struct nth_line_cb {
	struct diff_filespec *spec;
	long lines;
	unsigned long *line_ends;
};

static const char *nth_line(void *data, long line)
{
	struct nth_line_cb *d = data;
	assert(d && line <= d->lines);
	assert(d->spec && d->spec->data);

	if (line == 0)
		return (char *)d->spec->data;
	else
		return (char *)d->spec->data + d->line_ends[line] + 1;
}

static struct line_log_data *
parse_lines(struct repository *r, struct commit *commit,
	    const char *prefix, struct string_list *args)
{
	long lines = 0;
	unsigned long *ends = NULL;
	struct nth_line_cb cb_data;
	struct string_list_item *item;
	struct line_log_data *ranges = NULL;
	struct line_log_data *p;

	for_each_string_list_item(item, args) {
		const char *name_part, *range_part;
		char *full_name;
		struct diff_filespec *spec;
		long begin = 0, end = 0;
		long anchor;

		name_part = skip_range_arg(item->string, r->index);
		if (!name_part || *name_part != ':' || !name_part[1])
			die("-L argument not 'start,end:file' or ':funcname:file': %s",
			    item->string);
		range_part = xstrndup(item->string, name_part - item->string);
		name_part++;

		full_name = prefix_path(prefix, prefix ? strlen(prefix) : 0,
					name_part);

		spec = alloc_filespec(full_name);
		fill_blob_sha1(r, commit, spec);
		fill_line_ends(r, spec, &lines, &ends);
		cb_data.spec = spec;
		cb_data.lines = lines;
		cb_data.line_ends = ends;

		p = search_line_log_data(ranges, full_name, NULL);
		if (p && p->ranges.nr)
			anchor = p->ranges.ranges[p->ranges.nr - 1].end + 1;
		else
			anchor = 1;

		if (parse_range_arg(range_part, nth_line, &cb_data,
				    lines, anchor, &begin, &end,
				    full_name, r->index))
			die("malformed -L argument '%s'", range_part);
		if ((!lines && (begin || end)) || lines < begin)
			die("file %s has only %lu lines", name_part, lines);
		if (begin < 1)
			begin = 1;
		if (end < 1 || lines < end)
			end = lines;
		begin--;
		line_log_data_insert(&ranges, full_name, begin, end);

		free_filespec(spec);
		FREE_AND_NULL(ends);
	}

	for (p = ranges; p; p = p->next)
		sort_and_merge_range_set(&p->ranges);

	return ranges;
}

static struct line_log_data *line_log_data_copy_one(struct line_log_data *r)
{
	struct line_log_data *ret = xmalloc(sizeof(*ret));

	assert(r);
	line_log_data_init(ret);
	range_set_copy(&ret->ranges, &r->ranges);

	ret->path = xstrdup(r->path);

	return ret;
}

static struct line_log_data *
line_log_data_copy(struct line_log_data *r)
{
	struct line_log_data *ret = NULL;
	struct line_log_data *tmp = NULL, *prev = NULL;

	assert(r);
	ret = tmp = prev = line_log_data_copy_one(r);
	r = r->next;
	while (r) {
		tmp = line_log_data_copy_one(r);
		prev->next = tmp;
		prev = tmp;
		r = r->next;
	}

	return ret;
}

/* merge two range sets across files */
static struct line_log_data *line_log_data_merge(struct line_log_data *a,
						 struct line_log_data *b)
{
	struct line_log_data *head = NULL, **pp = &head;

	while (a || b) {
		struct line_log_data *src;
		struct line_log_data *src2 = NULL;
		struct line_log_data *d;
		int cmp;
		if (!a)
			cmp = 1;
		else if (!b)
			cmp = -1;
		else
			cmp = strcmp(a->path, b->path);
		if (cmp < 0) {
			src = a;
			a = a->next;
		} else if (cmp == 0) {
			src = a;
			a = a->next;
			src2 = b;
			b = b->next;
		} else {
			src = b;
			b = b->next;
		}
		d = xmalloc(sizeof(struct line_log_data));
		line_log_data_init(d);
		d->path = xstrdup(src->path);
		*pp = d;
		pp = &d->next;
		if (src2)
			range_set_union(&d->ranges, &src->ranges, &src2->ranges);
		else
			range_set_copy(&d->ranges, &src->ranges);
	}

	return head;
}

static void add_line_range(struct rev_info *revs, struct commit *commit,
			   struct line_log_data *range)
{
	struct line_log_data *old_line = NULL;
	struct line_log_data *new_line = NULL;

	old_line = lookup_decoration(&revs->line_log_data, &commit->object);
	if (old_line && range) {
		new_line = line_log_data_merge(old_line, range);
		free_line_log_data(old_line);
	} else if (range)
		new_line = line_log_data_copy(range);

	if (new_line)
		add_decoration(&revs->line_log_data, &commit->object, new_line);
}

static void clear_commit_line_range(struct rev_info *revs, struct commit *commit)
{
	struct line_log_data *r;
	r = lookup_decoration(&revs->line_log_data, &commit->object);
	if (!r)
		return;
	free_line_log_data(r);
	add_decoration(&revs->line_log_data, &commit->object, NULL);
}

static struct line_log_data *lookup_line_range(struct rev_info *revs,
					       struct commit *commit)
{
	struct line_log_data *ret = NULL;
	struct line_log_data *d;

	ret = lookup_decoration(&revs->line_log_data, &commit->object);

	for (d = ret; d; d = d->next)
		range_set_check_invariants(&d->ranges);

	return ret;
}

static int same_paths_in_pathspec_and_range(struct pathspec *pathspec,
					    struct line_log_data *range)
{
	int i;
	struct line_log_data *r;

	for (i = 0, r = range; i < pathspec->nr && r; i++, r = r->next)
		if (strcmp(pathspec->items[i].match, r->path))
			return 0;
	if (i < pathspec->nr || r)
		/* different number of pathspec items and ranges */
		return 0;

	return 1;
}

static void parse_pathspec_from_ranges(struct pathspec *pathspec,
				       struct line_log_data *range)
{
	struct line_log_data *r;
	struct strvec array = STRVEC_INIT;
	const char **paths;

	for (r = range; r; r = r->next)
		strvec_push(&array, r->path);
	paths = strvec_detach(&array);

	parse_pathspec(pathspec, 0, PATHSPEC_PREFER_FULL, "", paths);
	/* strings are now owned by pathspec */
	free(paths);
}

void line_log_init(struct rev_info *rev, const char *prefix, struct string_list *args)
{
	struct commit *commit = NULL;
	struct line_log_data *range;

	commit = check_single_commit(rev);
	range = parse_lines(rev->diffopt.repo, commit, prefix, args);
	add_line_range(rev, commit, range);

	parse_pathspec_from_ranges(&rev->diffopt.pathspec, range);
}

static void move_diff_queue(struct diff_queue_struct *dst,
			    struct diff_queue_struct *src)
{
	assert(src != dst);
	memcpy(dst, src, sizeof(struct diff_queue_struct));
	DIFF_QUEUE_CLEAR(src);
}

static void filter_diffs_for_paths(struct line_log_data *range, int keep_deletions)
{
	int i;
	struct diff_queue_struct outq;
	DIFF_QUEUE_CLEAR(&outq);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		struct line_log_data *rg = NULL;

		if (!DIFF_FILE_VALID(p->two)) {
			if (keep_deletions)
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
			continue;
		}
		for (rg = range; rg; rg = rg->next) {
			if (!strcmp(rg->path, p->two->path))
				break;
		}
		if (rg)
			diff_q(&outq, p);
		else
			diff_free_filepair(p);
	}
	free(diff_queued_diff.queue);
	diff_queued_diff = outq;
}

static inline int diff_might_be_rename(void)
{
	int i;
	for (i = 0; i < diff_queued_diff.nr; i++)
		if (!DIFF_FILE_VALID(diff_queued_diff.queue[i]->one)) {
			/* fprintf(stderr, "diff_might_be_rename found creation of: %s\n", */
			/* 	diff_queued_diff.queue[i]->two->path); */
			return 1;
		}
	return 0;
}

static void queue_diffs(struct line_log_data *range,
			struct diff_options *opt,
			struct diff_queue_struct *queue,
			struct commit *commit, struct commit *parent)
{
	struct object_id *tree_oid, *parent_tree_oid;

	assert(commit);

	tree_oid = get_commit_tree_oid(commit);
	parent_tree_oid = parent ? get_commit_tree_oid(parent) : NULL;

	if (opt->detect_rename &&
	    !same_paths_in_pathspec_and_range(&opt->pathspec, range)) {
		clear_pathspec(&opt->pathspec);
		parse_pathspec_from_ranges(&opt->pathspec, range);
	}
	DIFF_QUEUE_CLEAR(&diff_queued_diff);
	diff_tree_oid(parent_tree_oid, tree_oid, "", opt);
	if (opt->detect_rename && diff_might_be_rename()) {
		/* must look at the full tree diff to detect renames */
		clear_pathspec(&opt->pathspec);
		DIFF_QUEUE_CLEAR(&diff_queued_diff);

		diff_tree_oid(parent_tree_oid, tree_oid, "", opt);

		filter_diffs_for_paths(range, 1);
		diffcore_std(opt);
		filter_diffs_for_paths(range, 0);
	}
	move_diff_queue(queue, &diff_queued_diff);
}

static char *get_nth_line(long line, unsigned long *ends, void *data)
{
	if (line == 0)
		return (char *)data;
	else
		return (char *)data + ends[line] + 1;
}

static void print_line(const char *prefix, char first,
		       long line, unsigned long *ends, void *data,
		       const char *color, const char *reset, FILE *file)
{
	char *begin = get_nth_line(line, ends, data);
	char *end = get_nth_line(line+1, ends, data);
	int had_nl = 0;

	if (end > begin && end[-1] == '\n') {
		end--;
		had_nl = 1;
	}

	fputs(prefix, file);
	fputs(color, file);
	putc(first, file);
	fwrite(begin, 1, end-begin, file);
	fputs(reset, file);
	putc('\n', file);
	if (!had_nl)
		fputs("\\ No newline at end of file\n", file);
}

static char *output_prefix(struct diff_options *opt)
{
	char *prefix = "";

	if (opt->output_prefix) {
		struct strbuf *sb = opt->output_prefix(opt, opt->output_prefix_data);
		prefix = sb->buf;
	}

	return prefix;
}

static void dump_diff_hacky_one(struct rev_info *rev, struct line_log_data *range)
{
	unsigned int i, j = 0;
	long p_lines, t_lines;
	unsigned long *p_ends = NULL, *t_ends = NULL;
	struct diff_filepair *pair = range->pair;
	struct diff_ranges *diff = &range->diff;

	struct diff_options *opt = &rev->diffopt;
	char *prefix = output_prefix(opt);
	const char *c_reset = diff_get_color(opt->use_color, DIFF_RESET);
	const char *c_frag = diff_get_color(opt->use_color, DIFF_FRAGINFO);
	const char *c_meta = diff_get_color(opt->use_color, DIFF_METAINFO);
	const char *c_old = diff_get_color(opt->use_color, DIFF_FILE_OLD);
	const char *c_new = diff_get_color(opt->use_color, DIFF_FILE_NEW);
	const char *c_context = diff_get_color(opt->use_color, DIFF_CONTEXT);

	if (!pair || !diff)
		return;

	if (pair->one->oid_valid)
		fill_line_ends(rev->diffopt.repo, pair->one, &p_lines, &p_ends);
	fill_line_ends(rev->diffopt.repo, pair->two, &t_lines, &t_ends);

	fprintf(opt->file, "%s%sdiff --git a/%s b/%s%s\n", prefix, c_meta, pair->one->path, pair->two->path, c_reset);
	fprintf(opt->file, "%s%s--- %s%s%s\n", prefix, c_meta,
	       pair->one->oid_valid ? "a/" : "",
	       pair->one->oid_valid ? pair->one->path : "/dev/null",
	       c_reset);
	fprintf(opt->file, "%s%s+++ b/%s%s\n", prefix, c_meta, pair->two->path, c_reset);
	for (i = 0; i < range->ranges.nr; i++) {
		long p_start, p_end;
		long t_start = range->ranges.ranges[i].start;
		long t_end = range->ranges.ranges[i].end;
		long t_cur = t_start;
		unsigned int j_last;

		while (j < diff->target.nr && diff->target.ranges[j].end < t_start)
			j++;
		if (j == diff->target.nr || diff->target.ranges[j].start > t_end)
			continue;

		/* Scan ahead to determine the last diff that falls in this range */
		j_last = j;
		while (j_last < diff->target.nr && diff->target.ranges[j_last].start < t_end)
			j_last++;
		if (j_last > j)
			j_last--;

		/*
		 * Compute parent hunk headers: we know that the diff
		 * has the correct line numbers (but not all hunks).
		 * So it suffices to shift the start/end according to
		 * the line numbers of the first/last hunk(s) that
		 * fall in this range.
		 */
		if (t_start < diff->target.ranges[j].start)
			p_start = diff->parent.ranges[j].start - (diff->target.ranges[j].start-t_start);
		else
			p_start = diff->parent.ranges[j].start;
		if (t_end > diff->target.ranges[j_last].end)
			p_end = diff->parent.ranges[j_last].end + (t_end-diff->target.ranges[j_last].end);
		else
			p_end = diff->parent.ranges[j_last].end;

		if (!p_start && !p_end) {
			p_start = -1;
			p_end = -1;
		}

		/* Now output a diff hunk for this range */
		fprintf(opt->file, "%s%s@@ -%ld,%ld +%ld,%ld @@%s\n",
		       prefix, c_frag,
		       p_start+1, p_end-p_start, t_start+1, t_end-t_start,
		       c_reset);
		while (j < diff->target.nr && diff->target.ranges[j].start < t_end) {
			int k;
			for (; t_cur < diff->target.ranges[j].start; t_cur++)
				print_line(prefix, ' ', t_cur, t_ends, pair->two->data,
					   c_context, c_reset, opt->file);
			for (k = diff->parent.ranges[j].start; k < diff->parent.ranges[j].end; k++)
				print_line(prefix, '-', k, p_ends, pair->one->data,
					   c_old, c_reset, opt->file);
			for (; t_cur < diff->target.ranges[j].end && t_cur < t_end; t_cur++)
				print_line(prefix, '+', t_cur, t_ends, pair->two->data,
					   c_new, c_reset, opt->file);
			j++;
		}
		for (; t_cur < t_end; t_cur++)
			print_line(prefix, ' ', t_cur, t_ends, pair->two->data,
				   c_context, c_reset, opt->file);
	}

	free(p_ends);
	free(t_ends);
}

/*
 * NEEDSWORK: manually building a diff here is not the Right
 * Thing(tm).  log -L should be built into the diff pipeline.
 */
static void dump_diff_hacky(struct rev_info *rev, struct line_log_data *range)
{
	fprintf(rev->diffopt.file, "%s\n", output_prefix(&rev->diffopt));
	while (range) {
		dump_diff_hacky_one(rev, range);
		range = range->next;
	}
}

/*
 * Unlike most other functions, this destructively operates on
 * 'range'.
 */
static int process_diff_filepair(struct rev_info *rev,
				 struct diff_filepair *pair,
				 struct line_log_data *range,
				 struct diff_ranges **diff_out)
{
	struct line_log_data *rg = range;
	struct range_set tmp;
	struct diff_ranges diff;
	mmfile_t file_parent, file_target;

	assert(pair->two->path);
	while (rg) {
		assert(rg->path);
		if (!strcmp(rg->path, pair->two->path))
			break;
		rg = rg->next;
	}

	if (!rg)
		return 0;
	if (rg->ranges.nr == 0)
		return 0;

	assert(pair->two->oid_valid);
	diff_populate_filespec(rev->diffopt.repo, pair->two, NULL);
	file_target.ptr = pair->two->data;
	file_target.size = pair->two->size;

	if (pair->one->oid_valid) {
		diff_populate_filespec(rev->diffopt.repo, pair->one, NULL);
		file_parent.ptr = pair->one->data;
		file_parent.size = pair->one->size;
	} else {
		file_parent.ptr = "";
		file_parent.size = 0;
	}

	diff_ranges_init(&diff);
	if (collect_diff(&file_parent, &file_target, &diff))
		die("unable to generate diff for %s", pair->one->path);

	/* NEEDSWORK should apply some heuristics to prevent mismatches */
	free(rg->path);
	rg->path = xstrdup(pair->one->path);

	range_set_init(&tmp, 0);
	range_set_map_across_diff(&tmp, &rg->ranges, &diff, diff_out);
	range_set_release(&rg->ranges);
	range_set_move(&rg->ranges, &tmp);

	diff_ranges_release(&diff);

	return ((*diff_out)->parent.nr > 0);
}

static struct diff_filepair *diff_filepair_dup(struct diff_filepair *pair)
{
	struct diff_filepair *new_filepair = xmalloc(sizeof(struct diff_filepair));
	new_filepair->one = pair->one;
	new_filepair->two = pair->two;
	new_filepair->one->count++;
	new_filepair->two->count++;
	return new_filepair;
}

static void free_diffqueues(int n, struct diff_queue_struct *dq)
{
	for (int i = 0; i < n; i++)
		diff_free_queue(&dq[i]);
	free(dq);
}

static int process_all_files(struct line_log_data **range_out,
			     struct rev_info *rev,
			     struct diff_queue_struct *queue,
			     struct line_log_data *range)
{
	int i, changed = 0;

	*range_out = line_log_data_copy(range);

	for (i = 0; i < queue->nr; i++) {
		struct diff_ranges *pairdiff = NULL;
		struct diff_filepair *pair = queue->queue[i];
		if (process_diff_filepair(rev, pair, *range_out, &pairdiff)) {
			/*
			 * Store away the diff for later output.  We
			 * tuck it in the ranges we got as _input_,
			 * since that's the commit that caused the
			 * diff.
			 *
			 * NEEDSWORK not enough when we get around to
			 * doing something interesting with merges;
			 * currently each invocation on a merge parent
			 * trashes the previous one's diff.
			 *
			 * NEEDSWORK tramples over data structures not owned here
			 */
			struct line_log_data *rg = range;
			changed++;
			while (rg && strcmp(rg->path, pair->two->path))
				rg = rg->next;
			assert(rg);
			rg->pair = diff_filepair_dup(queue->queue[i]);
			memcpy(&rg->diff, pairdiff, sizeof(struct diff_ranges));
		}
		free(pairdiff);
	}

	return changed;
}

int line_log_print(struct rev_info *rev, struct commit *commit)
{

	show_log(rev);
	if (!(rev->diffopt.output_format & DIFF_FORMAT_NO_OUTPUT)) {
		struct line_log_data *range = lookup_line_range(rev, commit);
		dump_diff_hacky(rev, range);
	}
	return 1;
}

static int bloom_filter_check(struct rev_info *rev,
			      struct commit *commit,
			      struct line_log_data *range)
{
	struct bloom_filter *filter;
	struct bloom_key key;
	int result = 0;

	if (!commit->parents)
		return 1;

	if (!rev->bloom_filter_settings ||
	    !(filter = get_bloom_filter(rev->repo, commit)))
		return 1;

	if (!range)
		return 0;

	while (!result && range) {
		fill_bloom_key(range->path, strlen(range->path), &key, rev->bloom_filter_settings);

		if (bloom_filter_contains(filter, &key, rev->bloom_filter_settings))
			result = 1;

		clear_bloom_key(&key);
		range = range->next;
	}

	return result;
}

static int process_ranges_ordinary_commit(struct rev_info *rev, struct commit *commit,
					  struct line_log_data *range)
{
	struct commit *parent = NULL;
	struct diff_queue_struct queue;
	struct line_log_data *parent_range;
	int changed;

	if (commit->parents)
		parent = commit->parents->item;

	queue_diffs(range, &rev->diffopt, &queue, commit, parent);
	changed = process_all_files(&parent_range, rev, &queue, range);

	if (parent)
		add_line_range(rev, parent, parent_range);
	free_line_log_data(parent_range);
	diff_free_queue(&queue);
	return changed;
}

static int process_ranges_merge_commit(struct rev_info *rev, struct commit *commit,
				       struct line_log_data *range)
{
	struct diff_queue_struct *diffqueues;
	struct line_log_data **cand;
	struct commit **parents;
	struct commit_list *p;
	int i;
	int nparents = commit_list_count(commit->parents);

	if (nparents > 1 && rev->first_parent_only)
		nparents = 1;

	ALLOC_ARRAY(diffqueues, nparents);
	ALLOC_ARRAY(cand, nparents);
	ALLOC_ARRAY(parents, nparents);

	p = commit->parents;
	for (i = 0; i < nparents; i++) {
		parents[i] = p->item;
		p = p->next;
		queue_diffs(range, &rev->diffopt, &diffqueues[i], commit, parents[i]);
	}

	for (i = 0; i < nparents; i++) {
		int changed;
		cand[i] = NULL;
		changed = process_all_files(&cand[i], rev, &diffqueues[i], range);
		if (!changed) {
			/*
			 * This parent can take all the blame, so we
			 * don't follow any other path in history
			 */
			add_line_range(rev, parents[i], cand[i]);
			clear_commit_line_range(rev, commit);
			commit_list_append(parents[i], &commit->parents);
			free(parents);
			free(cand);
			free_diffqueues(nparents, diffqueues);
			/* NEEDSWORK leaking like a sieve */
			return 0;
		}
	}

	/*
	 * No single parent took the blame.  We add the candidates
	 * from the above loop to the parents.
	 */
	for (i = 0; i < nparents; i++) {
		add_line_range(rev, parents[i], cand[i]);
	}

	clear_commit_line_range(rev, commit);
	free(parents);
	free(cand);
	free_diffqueues(nparents, diffqueues);
	return 1;

	/* NEEDSWORK evil merge detection stuff */
	/* NEEDSWORK leaking like a sieve */
}

int line_log_process_ranges_arbitrary_commit(struct rev_info *rev, struct commit *commit)
{
	struct line_log_data *range = lookup_line_range(rev, commit);
	int changed = 0;

	if (range) {
		if (commit->parents && !bloom_filter_check(rev, commit, range)) {
			struct line_log_data *prange = line_log_data_copy(range);
			add_line_range(rev, commit->parents->item, prange);
			clear_commit_line_range(rev, commit);
		} else if (!commit->parents || !commit->parents->next)
			changed = process_ranges_ordinary_commit(rev, commit, range);
		else
			changed = process_ranges_merge_commit(rev, commit, range);
	}

	if (!changed)
		commit->object.flags |= TREESAME;

	return changed;
}

static enum rewrite_result line_log_rewrite_one(struct rev_info *rev UNUSED,
						struct commit **pp)
{
	for (;;) {
		struct commit *p = *pp;
		if (p->parents && p->parents->next)
			return rewrite_one_ok;
		if (p->object.flags & UNINTERESTING)
			return rewrite_one_ok;
		if (!(p->object.flags & TREESAME))
			return rewrite_one_ok;
		if (!p->parents)
			return rewrite_one_noparents;
		*pp = p->parents->item;
	}
}

int line_log_filter(struct rev_info *rev)
{
	struct commit *commit;
	struct commit_list *list = rev->commits;
	struct commit_list *out = NULL, **pp = &out;

	while (list) {
		struct commit_list *to_free = NULL;
		commit = list->item;
		if (line_log_process_ranges_arbitrary_commit(rev, commit)) {
			*pp = list;
			pp = &list->next;
		} else
			to_free = list;
		list = list->next;
		free(to_free);
	}
	*pp = NULL;

	for (list = out; list; list = list->next)
		rewrite_parents(rev, list->item, line_log_rewrite_one);

	rev->commits = out;

	return 0;
}
