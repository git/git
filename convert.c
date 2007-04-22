#include "cache.h"
#include "attr.h"

/*
 * convert.c - convert a file when checking it out and checking it in.
 *
 * This should use the pathname to decide on whether it wants to do some
 * more interesting conversions (automatic gzip/unzip, general format
 * conversions etc etc), but by default it just does automatic CRLF<->LF
 * translation when the "auto_crlf" option is set.
 */

#define CRLF_GUESS	(-1)
#define CRLF_BINARY	0
#define CRLF_TEXT	1
#define CRLF_INPUT	2

struct text_stat {
	/* CR, LF and CRLF counts */
	unsigned cr, lf, crlf;

	/* These are just approximations! */
	unsigned printable, nonprintable;
};

static void gather_stats(const char *buf, unsigned long size, struct text_stat *stats)
{
	unsigned long i;

	memset(stats, 0, sizeof(*stats));

	for (i = 0; i < size; i++) {
		unsigned char c = buf[i];
		if (c == '\r') {
			stats->cr++;
			if (i+1 < size && buf[i+1] == '\n')
				stats->crlf++;
			continue;
		}
		if (c == '\n') {
			stats->lf++;
			continue;
		}
		if (c == 127)
			/* DEL */
			stats->nonprintable++;
		else if (c < 32) {
			switch (c) {
				/* BS, HT, ESC and FF */
			case '\b': case '\t': case '\033': case '\014':
				stats->printable++;
				break;
			default:
				stats->nonprintable++;
			}
		}
		else
			stats->printable++;
	}
}

/*
 * The same heuristics as diff.c::mmfile_is_binary()
 */
static int is_binary(unsigned long size, struct text_stat *stats)
{

	if ((stats->printable >> 7) < stats->nonprintable)
		return 1;
	/*
	 * Other heuristics? Average line length might be relevant,
	 * as might LF vs CR vs CRLF counts..
	 *
	 * NOTE! It might be normal to have a low ratio of CRLF to LF
	 * (somebody starts with a LF-only file and edits it with an editor
	 * that adds CRLF only to lines that are added..). But do  we
	 * want to support CR-only? Probably not.
	 */
	return 0;
}

static char *crlf_to_git(const char *path, const char *src, unsigned long *sizep, int action)
{
	char *buffer, *dst;
	unsigned long size, nsize;
	struct text_stat stats;

	if ((action == CRLF_BINARY) || (action == CRLF_GUESS && !auto_crlf))
		return NULL;

	size = *sizep;
	if (!size)
		return NULL;

	gather_stats(src, size, &stats);

	/* No CR? Nothing to convert, regardless. */
	if (!stats.cr)
		return NULL;

	if (action == CRLF_GUESS) {
		/*
		 * We're currently not going to even try to convert stuff
		 * that has bare CR characters. Does anybody do that crazy
		 * stuff?
		 */
		if (stats.cr != stats.crlf)
			return NULL;

		/*
		 * And add some heuristics for binary vs text, of course...
		 */
		if (is_binary(size, &stats))
			return NULL;
	}

	/*
	 * Ok, allocate a new buffer, fill it in, and return true
	 * to let the caller know that we switched buffers on it.
	 */
	nsize = size - stats.crlf;
	buffer = xmalloc(nsize);
	*sizep = nsize;

	dst = buffer;
	if (action == CRLF_GUESS) {
		/*
		 * If we guessed, we already know we rejected a file with
		 * lone CR, and we can strip a CR without looking at what
		 * follow it.
		 */
		do {
			unsigned char c = *src++;
			if (c != '\r')
				*dst++ = c;
		} while (--size);
	} else {
		do {
			unsigned char c = *src++;
			if (! (c == '\r' && (1 < size && *buffer == '\n')))
				*dst++ = c;
		} while (--size);
	}

	return buffer;
}

static char *crlf_to_worktree(const char *path, const char *src, unsigned long *sizep, int action)
{
	char *buffer, *dst;
	unsigned long size, nsize;
	struct text_stat stats;
	unsigned char last;

	if ((action == CRLF_BINARY) || (action == CRLF_INPUT) ||
	    (action == CRLF_GUESS && auto_crlf <= 0))
		return NULL;

	size = *sizep;
	if (!size)
		return NULL;

	gather_stats(src, size, &stats);

	/* No LF? Nothing to convert, regardless. */
	if (!stats.lf)
		return NULL;

	/* Was it already in CRLF format? */
	if (stats.lf == stats.crlf)
		return NULL;

	if (action == CRLF_GUESS) {
		/* If we have any bare CR characters, we're not going to touch it */
		if (stats.cr != stats.crlf)
			return NULL;

		if (is_binary(size, &stats))
			return NULL;
	}

	/*
	 * Ok, allocate a new buffer, fill it in, and return true
	 * to let the caller know that we switched buffers on it.
	 */
	nsize = size + stats.lf - stats.crlf;
	buffer = xmalloc(nsize);
	*sizep = nsize;
	last = 0;

	dst = buffer;
	do {
		unsigned char c = *src++;
		if (c == '\n' && last != '\r')
			*dst++ = '\r';
		*dst++ = c;
		last = c;
	} while (--size);

	return buffer;
}

static void setup_convert_check(struct git_attr_check *check)
{
	static struct git_attr *attr_crlf;

	if (!attr_crlf)
		attr_crlf = git_attr("crlf", 4);
	check->attr = attr_crlf;
}

static int git_path_check_crlf(const char *path, struct git_attr_check *check)
{
	const char *value = check->value;

	if (ATTR_TRUE(value))
		return CRLF_TEXT;
	else if (ATTR_FALSE(value))
		return CRLF_BINARY;
	else if (ATTR_UNSET(value))
		;
	else if (!strcmp(value, "input"))
		return CRLF_INPUT;
	return CRLF_GUESS;
}

char *convert_to_git(const char *path, const char *src, unsigned long *sizep)
{
	struct git_attr_check check[1];
	int crlf = CRLF_GUESS;

	setup_convert_check(check);
	if (!git_checkattr(path, 1, check)) {
		crlf = git_path_check_crlf(path, check);
	}
	return crlf_to_git(path, src, sizep, crlf);
}

char *convert_to_working_tree(const char *path, const char *src, unsigned long *sizep)
{
	struct git_attr_check check[1];
	int crlf = CRLF_GUESS;

	setup_convert_check(check);
	if (!git_checkattr(path, 1, check)) {
		crlf = git_path_check_crlf(path, check);
	}
	return crlf_to_worktree(path, src, sizep, crlf);
}
