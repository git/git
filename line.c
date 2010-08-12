#include "cache.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "diff.h"
#include "commit.h"
#include "decorate.h"
#include "revision.h"
#include "xdiff-interface.h"
#include "strbuf.h"
#include "log-tree.h"
#include "line.h"

static void cleanup(struct diff_line_range *r)
{
	while (r) {
		struct diff_line_range *next = r->next;
		DIFF_LINE_RANGE_CLEAR(r);
		free(r);
		r = next;
	}
}

static struct object *verify_commit(struct rev_info *revs)
{
	struct object *commit = NULL;
	int found = -1;
	int i;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		while (obj->type == OBJ_TAG)
			obj = deref_tag(obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (commit)
			die("More than one commit to dig from: %s and %s?",
			    revs->pending.objects[i].name,
				revs->pending.objects[found].name);
		commit = obj;
		found = i;
	}

	if (commit == NULL)
		die("No commit specified?");

	return commit;
}

static void fill_blob_sha1(struct commit *commit, struct diff_line_range *r)
{
	unsigned mode;
	unsigned char sha1[20];

	while (r) {
		if (get_tree_entry(commit->object.sha1, r->spec->path,
			sha1, &mode))
			goto error;
		fill_filespec(r->spec, sha1, mode);
		r = r->next;
	}

	return;
error:
	die("There is no path %s in the commit", r->spec->path);
}

static void fill_line_ends(struct diff_filespec *spec, long *lines,
	unsigned long **line_ends)
{
	int num = 0, size = 50;
	long cur = 0;
	unsigned long *ends = NULL;
	char *data = NULL;

	if (diff_populate_filespec(spec, 0))
		die("Cannot read blob %s", sha1_to_hex(spec->sha1));

	ends = xmalloc(size * sizeof(*ends));
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
	ends = xrealloc(ends, cur * sizeof(*ends));
	*lines = cur;
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
	assert(d && line < d->lines);
	assert(d->spec && d->spec->data);

	if (line == 0)
		return (char *)d->spec->data;
	else
		return (char *)d->spec->data + d->line_ends[line] + 1;
}

/*
 * Parsing of (comma separated) one item in the -L option
 */
const char *parse_loc(const char *spec, nth_line_fn_t nth_line,
		void *data, long lines, long begin, long *ret)
{
	char *term;
	const char *line;
	long num;
	int reg_error;
	regex_t regexp;
	regmatch_t match[1];

	/* Catch the '$' matcher, now it is used to match the last
	 * line of the file. */
	if (spec[0] == '$') {
		*ret = lines;
		return spec + 1;
	}

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
	line = nth_line(data, begin);

	if (!(reg_error = regcomp(&regexp, spec + 1, REG_NEWLINE)) &&
	    !(reg_error = regexec(&regexp, line, 1, match, 0))) {
		const char *cp = line + match[0].rm_so;
		const char *nline;

		while (begin++ < lines) {
			nline = nth_line(data, begin);
			if (line <= cp && cp < nline)
				break;
			line = nline;
		}
		*ret = begin;
		regfree(&regexp);
		*term++ = '/';
		return term;
	} else {
		char errbuf[1024];
		regerror(reg_error, &regexp, errbuf, 1024);
		die("-L parameter '%s': %s", spec + 1, errbuf);
	}
}

static void parse_range(long lines, unsigned long *line_ends,
		struct line_range *r, struct diff_filespec *spec)
{
	const char *term;
	struct nth_line_cb data = {spec, lines, line_ends};

	term = parse_loc(r->arg, nth_line, &data, lines - 1, 1, &r->start);
	if (*term == ',') {
		term = parse_loc(term + 1, nth_line, &data, lines - 1,
			r->start + 1, &r->end);
		if (*term)
			die("-L parameter's argument should be <start>,<end>");
	}

	if (*term)
		die("-L parameter's argument should be <start>,<end>");

	if (r->start < 1)
		r->start = 1;
	if (r->end >= lines)
		r->end = lines - 1;

	if (r->start > r->end) {
		long tmp = r->start;
		r->start = r->end;
		r->end = tmp;
	}
}

static void parse_lines(struct commit *commit, struct diff_line_range *r)
{
	int i;
	struct line_range *old_range = NULL;
	long lines = 0;
	unsigned long *ends = NULL;

	while (r) {
		struct diff_filespec *spec = r->spec;
		int num = r->nr;
		assert(spec);
		fill_blob_sha1(commit, r);
		old_range = r->ranges;
		r->ranges = NULL;
		r->nr = r->alloc = 0;
		fill_line_ends(spec, &lines, &ends);
		for (i = 0; i < num; i++) {
			parse_range(lines, ends, old_range + i, spec);
			diff_line_range_insert(r, old_range[i].arg,
				old_range[i].start, old_range[i].end);
		}

		free(ends);
		ends = NULL;

		r = r->next;
		free(old_range);
	}
}

/*
 * Insert a new line range into a diff_line_range struct, and keep the
 * r->ranges sorted by their starting line number.
 */
struct line_range *diff_line_range_insert(struct diff_line_range *r,
		const char *arg, int start, int end)
{
	int i = 0;
	struct line_range *rs = r->ranges;
	int left_merge = 0, right_merge = 0;

	assert(r != NULL);
	assert(start <= end);

	if (r->nr == 0 || rs[r->nr - 1].end < start - 1) {
		int num = 0;
		DIFF_LINE_RANGE_GROW(r);
		rs = r->ranges;
		num = r->nr - 1;
		rs[num].arg = arg;
		rs[num].start = start;
		rs[num].end = end;
		return rs + num;
	}

	for (; i < r->nr; i++) {
		if (rs[i].end < start - 1)
			continue;
		if (rs[i].end == start - 1) {
			rs[i].end = end;
			right_merge = 1;
			goto out;
		}

		assert(rs[i].end > start - 1);
		if (rs[i].start <= start) {
			if (rs[i].end < end) {
				rs[i].end = end;
				right_merge = 1;
			}
			goto out;
		} else if (rs[i].start <= end + 1) {
			rs[i].start = start;
			left_merge = 1;
			if (rs[i].end < end) {
				rs[i].end = end;
				right_merge = 1;
			}
			goto out;
		} else {
			int num = r->nr - i;
			DIFF_LINE_RANGE_GROW(r);
			rs = r->ranges;
			memmove(rs + i + 1, rs + i, num * sizeof(struct line_range));
			rs[i].arg = arg;
			rs[i].start = start;
			rs[i].end = end;
			goto out;
		}
	}

out:
	assert(r->nr != i);
	if (left_merge) {
		int j = i;
		for (; j > -1; j--) {
			if (rs[j].end >= rs[i].start - 1)
				if (rs[j].start < rs[i].start)
					rs[i].start = rs[j].start;
		}
		memmove(rs + j + 1, rs + i, (r->nr - i) * sizeof(struct line_range));
		r->nr -= i - j - 1;
	}
	if (right_merge) {
		int j = i;
		for (; j < r->nr; j++) {
			if (rs[j].start <= rs[i].end + 1)
				if (rs[j].end > rs[i].end)
					rs[i].end = rs[j].end;
		}
		if (j < r->nr)
			memmove(rs + i + 1, rs + j, (r->nr - j) * sizeof(struct line_range));
		r->nr -= j - i - 1;
	}
	assert(r->nr);

	return rs + i;
}

void diff_line_range_clear(struct diff_line_range *r)
{
	int i = 0, zero = 0;

	for (; i < r->nr; i++) {
		struct line_range *rg = r->ranges + i;
		RANGE_CLEAR(rg);
	}

	if (r->prev) {
		zero = 0;
		if (r->prev->count == 1)
			zero = 1;
		free_filespec(r->prev);
		if (zero)
			r->prev = NULL;
	}
	if (r->spec) {
		zero = 0;
		if (r->spec->count == 1)
			zero = 1;
		free_filespec(r->spec);
		if (zero)
			r->spec = NULL;
	}

	r->status = '\0';
	r->alloc = r->nr = 0;

	if (r->ranges)
		free(r->ranges);
	r->ranges = NULL;
	r->next = NULL;
}

void diff_line_range_append(struct diff_line_range *r, const char *arg)
{
	DIFF_LINE_RANGE_GROW(r);
	r->ranges[r->nr - 1].arg = arg;
}

struct diff_line_range *diff_line_range_clone(struct diff_line_range *r)
{
	struct diff_line_range *ret = xmalloc(sizeof(*ret));
	int i = 0;

	DIFF_LINE_RANGE_INIT(ret);
	ret->ranges = xcalloc(r->nr, sizeof(struct line_range));
	memcpy(ret->ranges, r->ranges, sizeof(struct line_range) * r->nr);

	ret->alloc = ret->nr = r->nr;

	for (; i < ret->nr; i++)
		PRINT_PAIR_INIT(&ret->ranges[i].pair);

	ret->spec = r->spec;
	assert(ret->spec);
	ret->spec->count++;

	return ret;
}

struct diff_line_range *diff_line_range_clone_deeply(struct diff_line_range *r)
{
	struct diff_line_range *ret = NULL;
	struct diff_line_range *tmp = NULL, *prev = NULL;

	assert(r);
	ret = tmp = prev = diff_line_range_clone(r);
	r = r->next;
	while (r) {
		tmp = diff_line_range_clone(r);
		prev->next = tmp;
		prev = tmp;
		r = r->next;
	}

	return ret;
}

struct diff_line_range *diff_line_range_merge(struct diff_line_range *out,
		struct diff_line_range *other)
{
	struct diff_line_range *one = out, *two = other;
	struct diff_line_range *pone = NULL;

	while (one) {
		struct diff_line_range *ptwo;
		two = other;
		ptwo = other;
		while (two) {
			if (!strcmp(one->spec->path, two->spec->path)) {
				int i = 0;
				for (; i < two->nr; i++) {
					diff_line_range_insert(one, NULL,
						two->ranges[i].start,
						two->ranges[i].end);
				}
				if (two == other)
					other = other->next;
				else
					ptwo->next = two->next;
				DIFF_LINE_RANGE_CLEAR(two);
				free(two);
				two = NULL;

				break;
			}

			ptwo = two;
			two = two->next;
		}

		pone = one;
		one = one->next;
	}
	pone->next = other;

	return out;
}

void add_line_range(struct rev_info *revs, struct commit *commit,
		struct diff_line_range *r)
{
	struct diff_line_range *ret = NULL;

	if (r != NULL) {
		ret = lookup_decoration(&revs->line_range, &commit->object);
		if (ret != NULL)
			diff_line_range_merge(ret, r);
		else
			add_decoration(&revs->line_range, &commit->object, r);
		commit->object.flags |= RANGE_UPDATE;
	}
}

struct diff_line_range *lookup_line_range(struct rev_info *revs,
		struct commit *commit)
{
	struct diff_line_range *ret = NULL;

	ret = lookup_decoration(&revs->line_range, &commit->object);
	return ret;
}

void setup_line(struct rev_info *rev, struct diff_line_range *r)
{
	struct commit *commit = NULL;
	struct diff_options *opt = &rev->diffopt;

	commit = (struct commit *)verify_commit(rev);
	parse_lines(commit, r);

	add_line_range(rev, commit, r);
	/*
	 * Note we support -M/-C to detect file rename
	 */
	opt->nr_paths = 0;
	diff_tree_release_paths(opt);
}

struct take_range_cb_data {
	struct diff_line_range *interesting;	/* currently interesting ranges */
	struct diff_line_range *range;
		/* the ranges corresponds to the interesting ranges of parent commit */
	long plno, tlno;
		/* the last line number of diff hunk */
	int diff;
		/* whether there is some line changes between the current
		 * commit and its parent */
};

#define SCALE_FACTOR 4
/*
 * [p_start, p_end] represents the pre-image of current diff hunk,
 * [t_start, t_end] represents the post-image of the current diff hunk,
 * [start, end] represents the currently interesting line range in
 * post-image,
 * [o_start, o_end] represents the original line range that coresponds
 * to current line range.
 */
void map_lines(long p_start, long p_end, long t_start, long t_end,
		long start, long end, long *o_start, long *o_end)
{
	/*
	 * Normally, p_start should be less than p_end, so does the
	 * t_start and t_end. But when the line range is added from
	 * scratch, p_start will be greater than p_end. When the line
	 * range is deleted, t_start will be greater than t_end.
	 */
	if (p_start > p_end) {
		*o_start = *o_end = 0;
		return;
	}
	/* A deletion */
	if (t_start > t_end) {
		*o_start = p_start;
		*o_end = p_end;
		return;
	}

	if (start == t_start && end == t_end) {
		*o_start = p_start;
		*o_end = p_end;
		return;
	}

	/*
	 * A heuristic for lines mapping:
	 *
	 * When the pre-image is no more than 1/SCALE_FACTOR of the post-image,
	 * there is no effective way to find out which part of pre-image
	 * corresponds to the currently interesting range of post-image.
	 * And we are in the danger of tracking totally useless lines.
	 * So, we just treat all the post-image lines as added from scratch.
	 */
	if (SCALE_FACTOR * (p_end - p_start + 1) < (t_end - t_start + 1)) {
		*o_start = *o_end = 0;
		return;
	}

	*o_start = p_start + start - t_start;
	*o_end = p_end - (t_end - end);

	if (*o_start > *o_end) {
		int temp = *o_start;
		*o_start = *o_end;
		*o_end = temp;
	}

	if (*o_start < p_start)
		*o_start = p_start;
	if (*o_end > p_end)
		*o_end = p_end;
}

/*
 * When same == 1:
 * [p_start, p_end] represents the diff hunk line range of pre-image,
 * [t_start, t_end] represents the diff hunk line range of post-image.
 * When same == 0, they represent a range of identical lines between
 * two images.
 *
 * This function find out the corresponding line ranges of currently
 * interesting ranges which this diff hunk touches.
 */
static void map_range(struct take_range_cb_data *data, int same,
		long p_start, long p_end, long t_start, long t_end)
{
	struct line_range *ranges = data->interesting->ranges;
	long takens, takene, start, end;
	int i = 0, out = 0, added = 0;
	long op_start = p_start, op_end = p_end, ot_start = t_start, ot_end = t_end;

	for (; i < data->interesting->nr; i++) {
		added = 0;
		if (t_start > ranges[i].end)
			continue;
		if (t_end < ranges[i].start)
			break;

		if (t_start > ranges[i].start) {
			start = t_start;
			takens = p_start;
			if (t_end >= ranges[i].end) {
				end = ranges[i].end;
				takene = p_start + end - t_start;
			} else {
				end = t_end;
				takene = p_end;
				out = 1;
			}
		} else {
			start = ranges[i].start;
			takens = p_start + start - t_start;
			if (t_end >= ranges[i].end) {
				end = ranges[i].end;
				takene = p_start + end - t_start;
			} else {
				end = t_end;
				takene = p_end;
				out = 1;
			}
		}

		if (!same) {
			struct print_pair *pair = &ranges[i].pair;
			struct print_range *rr = NULL;
			PRINT_PAIR_GROW(pair);
			rr = pair->ranges + pair->nr - 1;
			PRINT_RANGE_INIT(rr);
			rr->start = start;
			rr->end = end;
			map_lines(op_start, op_end, ot_start, ot_end, start, end,
					&takens, &takene);
			if (takens == 0 && takene == 0) {
				added = 1;
				rr->line_added = 1;
			}
			rr->pstart = takens;
			rr->pend = takene;
			data->diff = 1;
			data->interesting->diff = 1;
			ranges[i].diff = 1;
		}
		if (added) {
			/* Code movement/copy detect here, now place two dummy statements here */
			int dummy = 0;
			dummy = 1;
		} else {
			struct line_range *added_range = diff_line_range_insert(data->range,
					NULL, takens, takene);
			assert(added_range);
			ranges[i].pstart = added_range->start;
			ranges[i].pend = added_range->end;
		}

		t_start = end + 1;
		p_start = takene + 1;

		if (out)
			break;
	}
}

/*
 * [p_start, p_end] represents the line range of pre-image,
 * [t_start, t_end] represents the line range of post-image,
 * and they are identical lines.
 *
 * This function substracts out the identical lines between current
 * commit and its parent, from currently interesting ranges.
 */
static void take_range(struct take_range_cb_data *data,
		long p_start, long p_end, long t_start, long t_end)
{
	struct line_range *ranges = data->interesting->ranges;
	long takens, takene, start, end;
	int i = 0, out = 0, added = 0;

	for (; i < data->interesting->nr; i++) {
		added = 0;
		if (t_start > ranges[i].end)
			continue;
		if (t_end < ranges[i].start)
			break;

		if (t_start > ranges[i].start) {
			long tmp = ranges[i].end;
			ranges[i].end = t_start - 1;
			start = t_start;
			takens = p_start;
			if (t_end >= tmp) {
				end = tmp;
				takene = p_start + end - t_start;
				p_start = takene + 1;
				t_start = end + 1;
			} else {
				end = t_end;
				takene = p_end;
				diff_line_range_insert(data->interesting, NULL,
					t_end + 1, tmp);
				out = 1;
			}
		} else {
			start = ranges[i].start;
			takens = p_start + start - t_start;
			if (t_end >= ranges[i].end) {
				int num = data->interesting->nr - 1;
				end = ranges[i].end;
				takene = p_start + end - t_start;
				t_start = end + 1;
				p_start = takene + 1;
				memmove(ranges + i, ranges + i + 1, (num - i) * sizeof(*ranges));
				data->interesting->nr = num;
				i--;
			} else {
				end = t_end;
				takene = p_end;
				ranges[i].start = t_end + 1;
				out = 1;
			}
		}

		diff_line_range_insert(data->range, NULL, takens, takene);

		if (out)
			break;
	}
}

static void take_range_cb(void *data, long same, long p_next, long t_next)
{
	struct take_range_cb_data *d = data;
	long p_start = d->plno + 1, t_start = d->tlno + 1;
	long p_end = p_start + same - t_start, t_end = same;

	/* If one file is added from scratch, we should not bother to call
	 * take_range, since there is nothing to take
	 */
	if (t_end >= t_start)
		take_range(d, p_start, p_end, t_start, t_end);
	d->plno = p_next;
	d->tlno = t_next;
}

static void map_range_cb(void *data, long same, long p_next, long t_next)
{
	struct take_range_cb_data *d = data;

	long p_start = d->plno + 1;
	long t_start = d->tlno + 1;
	long p_end = same - t_start + p_start;
	long t_end = same;

	/* Firstly, take the unchanged lines from child */
	if (t_end >= t_start)
		map_range(d, 1, p_start, p_end, t_start, t_end);

	/* find out which lines to print */
	t_start = same + 1;
	p_start = d->plno + t_start - d->tlno;
	map_range(d, 0, p_start, p_next, t_start, t_next);

	d->plno = p_next;
	d->tlno = t_next;
}

/*
 * We support two kinds of operation in this function:
 * 1. map == 0, take the same lines from the current commit and assign it
 *              to parent;
 * 2. map == 1, in addition to the same lines, we also map the changed lines
 *              from the current commit to the parent according to the
 *              diff output.
 * take_range_cb and take_range are used to take same lines from current commit
 * to parents.
 * map_range_cb and map_range are used to map line ranges to the parent.
 */
static void assign_range_to_parent(struct rev_info *rev, struct commit *c,
		struct commit *p, struct diff_line_range *r,
		struct diff_options *opt, int map)
{
	struct diff_line_range *rr = xmalloc(sizeof(*rr));
	struct diff_line_range *cr = rr, *prev_r = rr;
	struct diff_line_range *rg = NULL;
	struct tree_desc desc1, desc2;
	void *tree1 = NULL, *tree2 = NULL;
	unsigned long size1, size2;
	struct diff_queue_struct *queue;
	struct take_range_cb_data cb = {NULL, cr, 0, 0};
	xpparam_t xpp;
	xdemitconf_t xecfg;
	int i, diff = 0;
	xdiff_emit_hunk_consume_fn fn = map ? map_range_cb : take_range_cb;

	DIFF_LINE_RANGE_INIT(cr);
	memset(&xpp, 0, sizeof(xpp));
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = xecfg.interhunkctxlen = 0;

	/*
	 * Compose up two trees, for root commit, we make up a empty tree.
	 */
	assert(c);
	tree2 = read_object_with_reference(c->tree->object.sha1, "tree",
			&size2, NULL);
	if (tree2 == NULL)
		die("Unable to read tree (%s)", sha1_to_hex(c->tree->object.sha1));
	init_tree_desc(&desc2, tree2, size2);
	if (p) {
		tree1 = read_object_with_reference(p->tree->object.sha1,
				"tree", &size1, NULL);
		if (tree1 == NULL)
			die("Unable to read tree (%s)",
					sha1_to_hex(p->tree->object.sha1));
		init_tree_desc(&desc1, tree1, size1);
	} else {
		init_tree_desc(&desc1, "", 0);
	}

	DIFF_QUEUE_CLEAR(&diff_queued_diff);
	diff_tree(&desc1, &desc2, "", opt);
	diffcore_std(opt);

	queue = &diff_queued_diff;
	for (i = 0; i < queue->nr; i++) {
		struct diff_filepair *pair = queue->queue[i];
		struct diff_line_range *rg = r;
		mmfile_t file_p, file_t;
		assert(pair->two->path);
		while (rg) {
			assert(rg->spec->path);
			if (!strcmp(rg->spec->path, pair->two->path))
				break;
			rg = rg->next;
		}

		if (rg == NULL)
			continue;
		rg->touch = 1;
		if (rg->nr == 0)
			continue;

		rg->status = pair->status;
		assert(pair->two->sha1_valid);
		diff_populate_filespec(pair->two, 0);
		file_t.ptr = pair->two->data;
		file_t.size = pair->two->size;

		if (rg->prev)
			free_filespec(rg->prev);
		rg->prev = pair->one;
		rg->prev->count++;
		if (pair->one->sha1_valid) {
			diff_populate_filespec(pair->one, 0);
			file_p.ptr = pair->one->data;
			file_p.size = pair->one->size;
		} else {
			file_p.ptr = "";
			file_p.size = 0;
		}

		if (cr->nr != 0) {
			struct diff_line_range *tmp = xmalloc(sizeof(*tmp));
			cr->next = tmp;
			prev_r = cr;
			cr = tmp;
		} else if (cr->spec)
			DIFF_LINE_RANGE_CLEAR(cr);

		DIFF_LINE_RANGE_INIT(cr);
		if (pair->one->sha1_valid) {
			cr->spec = pair->one;
			cr->spec->count++;
		}

		cb.interesting = rg;
		cb.range = cr;
		cb.diff = 0;
		cb.plno = cb.tlno = 0;
		xdi_diff_hunks(&file_p, &file_t, fn, &cb, &xpp, &xecfg);
		if (cb.diff)
			diff = 1;
		/*
		 * The remain part is the same part.
		 * Instead of calculating the true line number of the two files,
		 * use the biggest integer.
		 */
		if (map)
			map_range(&cb, 1, cb.plno + 1, INT_MAX, cb.tlno + 1, INT_MAX);
		else
			take_range(&cb, cb.plno + 1, INT_MAX, cb.tlno + 1, INT_MAX);
	}
	opt->output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_flush(opt);

	/*
	 * Collect the untouch ranges, this comes from the files not changed
	 * between two commit.
	 */
	rg = r;
	while (rg) {
		/* clear the touch one to make it usable in next round */
		if (rg->touch) {
			rg->touch = 0;
		} else {
			struct diff_line_range *untouch = diff_line_range_clone(rg);
			if (prev_r == rr && rr->nr == 0) {
				rr = prev_r = untouch;
			} else {
				prev_r->next = untouch;
				prev_r = untouch;
			}
		}
		rg = rg->next;
	}

	if (cr->nr == 0) {
		DIFF_LINE_RANGE_CLEAR(cr);
		free(cr);
		if (prev_r == cr)
			rr = NULL;
		else
			prev_r->next = NULL;
	}

	if (rr) {
		assert(p);
		add_line_range(rev, p, rr);
	}

	/* and the ranges of current commit c is updated */
	c->object.flags &= ~RANGE_UPDATE;
	if (diff)
		c->object.flags |= NEED_PRINT;

	if (tree1)
		free(tree1);
	if (tree2)
		free(tree2);
}

static void diff_update_parent_range(struct rev_info *rev,
		struct commit *commit)
{
	struct diff_line_range *r = lookup_line_range(rev, commit);
	struct commit_list *parents = commit->parents;
	struct commit *c = NULL;
	if (parents) {
		assert(parents->next == NULL);
		c = parents->item;
	}

	assign_range_to_parent(rev, commit, c, r, &rev->diffopt, 1);
}

static void assign_parents_range(struct rev_info *rev, struct commit *commit)
{
	struct commit_list *parents = commit->parents;
	struct diff_line_range *r = lookup_line_range(rev, commit);
	struct diff_line_range *evil = NULL, *range = NULL;
	int nontrivial = 0;

	/*
	 * If we are in linear history, update range and flush the patch if
	 * necessary
	 */
	if (parents == NULL || parents->next == NULL)
		return diff_update_parent_range(rev, commit);

	/*
	 * Loop on the parents and assign the ranges to different
	 * parents, if there is any range left, this commit must
	 * be an evil merge.
	 */
	evil = diff_line_range_clone_deeply(r);
	parents = commit->parents;
	while (parents) {
		struct commit *p = parents->item;
		assign_range_to_parent(rev, commit, p, r, &rev->diffopt, 1);
		assign_range_to_parent(rev, commit, p, evil, &rev->diffopt, 0);
		parents = parents->next;
	}

	/*
	 * yes, this must be an evil merge.
	 */
	range = evil;
	while (range) {
		if (range->nr) {
			commit->object.flags |= NEED_PRINT | EVIL_MERGE;
			nontrivial = 1;
		}
		range = range->next;
	}

	if (nontrivial)
		add_decoration(&rev->nontrivial_merge, &commit->object, evil);
	else
		cleanup(evil);
}
