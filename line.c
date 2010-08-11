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

static const char *nth_line(struct diff_filespec *spec, long line,
		long lines, unsigned long *line_ends)
{
	assert(line < lines);
	assert(spec && spec->data);

	if (line == 0)
		return (char *)spec->data;
	else
		return (char *)spec->data + line_ends[line] + 1;
}

/*
 * copied from blame.c, indeed, we can even to use this to test
 * whether line log works. :)
 */
static const char *parse_loc(const char *spec, struct diff_filespec *file,
			     long lines, unsigned long *line_ends,
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
	line = nth_line(file, begin, lines, line_ends);

	if (!(reg_error = regcomp(&regexp, spec + 1, REG_NEWLINE)) &&
	    !(reg_error = regexec(&regexp, line, 1, match, 0))) {
		const char *cp = line + match[0].rm_so;
		const char *nline;

		while (begin++ < lines) {
			nline = nth_line(file, begin, lines, line_ends);
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

	term = parse_loc(r->arg, spec, lines, line_ends, 1, &r->start);
	if (*term == ',') {
		term = parse_loc(term + 1, spec, lines, line_ends,
			r->start + 1, &r->end);
		if (*term)
			die("-L parameter's argument should be <start>,<end>");
	}

	if (*term)
		die("-L parameter's argument should be <start>,<end>");

	if (r->start > r->end) {
		long tmp = r->start;
		r->start = r->end;
		r->end = tmp;
	}

	if (r->start < 1)
		r->start = 1;
	if (r->end >= lines)
		r->end = lines - 1;
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
