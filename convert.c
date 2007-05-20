#include "cache.h"
#include "attr.h"
#include "run-command.h"

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

	if ((action == CRLF_BINARY) || !auto_crlf)
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
	 * Ok, allocate a new buffer, fill it in, and return it
	 * to let the caller know that we switched buffers.
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
			if (! (c == '\r' && (1 < size && *src == '\n')))
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
	    auto_crlf <= 0)
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
	 * Ok, allocate a new buffer, fill it in, and return it
	 * to let the caller know that we switched buffers.
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

static int filter_buffer(const char *path, const char *src,
			 unsigned long size, const char *cmd)
{
	/*
	 * Spawn cmd and feed the buffer contents through its stdin.
	 */
	struct child_process child_process;
	int pipe_feed[2];
	int write_err, status;

	memset(&child_process, 0, sizeof(child_process));

	if (pipe(pipe_feed) < 0) {
		error("cannot create pipe to run external filter %s", cmd);
		return 1;
	}

	child_process.pid = fork();
	if (child_process.pid < 0) {
		error("cannot fork to run external filter %s", cmd);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		return 1;
	}
	if (!child_process.pid) {
		dup2(pipe_feed[0], 0);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		execlp("sh", "sh", "-c", cmd, NULL);
		return 1;
	}
	close(pipe_feed[0]);

	write_err = (write_in_full(pipe_feed[1], src, size) < 0);
	if (close(pipe_feed[1]))
		write_err = 1;
	if (write_err)
		error("cannot feed the input to external filter %s", cmd);

	status = finish_command(&child_process);
	if (status)
		error("external filter %s failed %d", cmd, -status);
	return (write_err || status);
}

static char *apply_filter(const char *path, const char *src,
			  unsigned long *sizep, const char *cmd)
{
	/*
	 * Create a pipeline to have the command filter the buffer's
	 * contents.
	 *
	 * (child --> cmd) --> us
	 */
	const int SLOP = 4096;
	int pipe_feed[2];
	int status;
	char *dst;
	unsigned long dstsize, dstalloc;
	struct child_process child_process;

	if (!cmd)
		return NULL;

	memset(&child_process, 0, sizeof(child_process));

	if (pipe(pipe_feed) < 0) {
		error("cannot create pipe to run external filter %s", cmd);
		return NULL;
	}

	fflush(NULL);
	child_process.pid = fork();
	if (child_process.pid < 0) {
		error("cannot fork to run external filter %s", cmd);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		return NULL;
	}
	if (!child_process.pid) {
		dup2(pipe_feed[1], 1);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		exit(filter_buffer(path, src, *sizep, cmd));
	}
	close(pipe_feed[1]);

	dstalloc = *sizep;
	dst = xmalloc(dstalloc);
	dstsize = 0;

	while (1) {
		ssize_t numread = xread(pipe_feed[0], dst + dstsize,
					dstalloc - dstsize);

		if (numread <= 0) {
			if (!numread)
				break;
			error("read from external filter %s failed", cmd);
			free(dst);
			dst = NULL;
			break;
		}
		dstsize += numread;
		if (dstalloc <= dstsize + SLOP) {
			dstalloc = dstsize + SLOP;
			dst = xrealloc(dst, dstalloc);
		}
	}
	if (close(pipe_feed[0])) {
		error("read from external filter %s failed", cmd);
		free(dst);
		dst = NULL;
	}

	status = finish_command(&child_process);
	if (status) {
		error("external filter %s failed %d", cmd, -status);
		free(dst);
		dst = NULL;
	}

	if (dst)
		*sizep = dstsize;
	return dst;
}

static struct convert_driver {
	const char *name;
	struct convert_driver *next;
	char *smudge;
	char *clean;
} *user_convert, **user_convert_tail;

static int read_convert_config(const char *var, const char *value)
{
	const char *ep, *name;
	int namelen;
	struct convert_driver *drv;

	/*
	 * External conversion drivers are configured using
	 * "filter.<name>.variable".
	 */
	if (prefixcmp(var, "filter.") || (ep = strrchr(var, '.')) == var + 6)
		return 0;
	name = var + 7;
	namelen = ep - name;
	for (drv = user_convert; drv; drv = drv->next)
		if (!strncmp(drv->name, name, namelen) && !drv->name[namelen])
			break;
	if (!drv) {
		char *namebuf;
		drv = xcalloc(1, sizeof(struct convert_driver));
		namebuf = xmalloc(namelen + 1);
		memcpy(namebuf, name, namelen);
		namebuf[namelen] = 0;
		drv->name = namebuf;
		drv->next = NULL;
		*user_convert_tail = drv;
		user_convert_tail = &(drv->next);
	}

	ep++;

	/*
	 * filter.<name>.smudge and filter.<name>.clean specifies
	 * the command line:
	 *
	 *	command-line
	 *
	 * The command-line will not be interpolated in any way.
	 */

	if (!strcmp("smudge", ep)) {
		if (!value)
			return error("%s: lacks value", var);
		drv->smudge = strdup(value);
		return 0;
	}

	if (!strcmp("clean", ep)) {
		if (!value)
			return error("%s: lacks value", var);
		drv->clean = strdup(value);
		return 0;
	}
	return 0;
}

static void setup_convert_check(struct git_attr_check *check)
{
	static struct git_attr *attr_crlf;
	static struct git_attr *attr_ident;
	static struct git_attr *attr_filter;

	if (!attr_crlf) {
		attr_crlf = git_attr("crlf", 4);
		attr_ident = git_attr("ident", 5);
		attr_filter = git_attr("filter", 6);
		user_convert_tail = &user_convert;
		git_config(read_convert_config);
	}
	check[0].attr = attr_crlf;
	check[1].attr = attr_ident;
	check[2].attr = attr_filter;
}

static int count_ident(const char *cp, unsigned long size)
{
	/*
	 * "$Id: 0000000000000000000000000000000000000000 $" <=> "$Id$"
	 */
	int cnt = 0;
	char ch;

	while (size) {
		ch = *cp++;
		size--;
		if (ch != '$')
			continue;
		if (size < 3)
			break;
		if (memcmp("Id", cp, 2))
			continue;
		ch = cp[2];
		cp += 3;
		size -= 3;
		if (ch == '$')
			cnt++; /* $Id$ */
		if (ch != ':')
			continue;

		/*
		 * "$Id: ... "; scan up to the closing dollar sign and discard.
		 */
		while (size) {
			ch = *cp++;
			size--;
			if (ch == '$') {
				cnt++;
				break;
			}
		}
	}
	return cnt;
}

static char *ident_to_git(const char *path, const char *src, unsigned long *sizep, int ident)
{
	int cnt;
	unsigned long size;
	char *dst, *buf;

	if (!ident)
		return NULL;
	size = *sizep;
	cnt = count_ident(src, size);
	if (!cnt)
		return NULL;
	buf = xmalloc(size);

	for (dst = buf; size; size--) {
		char ch = *src++;
		*dst++ = ch;
		if ((ch == '$') && (3 <= size) &&
		    !memcmp("Id:", src, 3)) {
			unsigned long rem = size - 3;
			const char *cp = src + 3;
			do {
				ch = *cp++;
				if (ch == '$')
					break;
				rem--;
			} while (rem);
			if (!rem)
				continue;
			memcpy(dst, "Id$", 3);
			dst += 3;
			size -= (cp - src);
			src = cp;
		}
	}

	*sizep = dst - buf;
	return buf;
}

static char *ident_to_worktree(const char *path, const char *src, unsigned long *sizep, int ident)
{
	int cnt;
	unsigned long size;
	char *dst, *buf;
	unsigned char sha1[20];

	if (!ident)
		return NULL;

	size = *sizep;
	cnt = count_ident(src, size);
	if (!cnt)
		return NULL;

	hash_sha1_file(src, size, "blob", sha1);
	buf = xmalloc(size + cnt * 43);

	for (dst = buf; size; size--) {
		const char *cp;
		char ch = *src++;
		*dst++ = ch;
		if ((ch != '$') || (size < 3) || memcmp("Id", src, 2))
			continue;

		if (src[2] == ':') {
			/* discard up to but not including the closing $ */
			unsigned long rem = size - 3;
			cp = src + 3;
			do {
				ch = *cp++;
				if (ch == '$')
					break;
				rem--;
			} while (rem);
			if (!rem)
				continue;
			size -= (cp - src);
		} else if (src[2] == '$')
			cp = src + 2;
		else
			continue;

		memcpy(dst, "Id: ", 4);
		dst += 4;
		memcpy(dst, sha1_to_hex(sha1), 40);
		dst += 40;
		*dst++ = ' ';
		size -= (cp - src);
		src = cp;
		*dst++ = *src++;
		size--;
	}

	*sizep = dst - buf;
	return buf;
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

static struct convert_driver *git_path_check_convert(const char *path,
					     struct git_attr_check *check)
{
	const char *value = check->value;
	struct convert_driver *drv;

	if (ATTR_TRUE(value) || ATTR_FALSE(value) || ATTR_UNSET(value))
		return NULL;
	for (drv = user_convert; drv; drv = drv->next)
		if (!strcmp(value, drv->name))
			return drv;
	return NULL;
}

static int git_path_check_ident(const char *path, struct git_attr_check *check)
{
	const char *value = check->value;

	return !!ATTR_TRUE(value);
}

char *convert_to_git(const char *path, const char *src, unsigned long *sizep)
{
	struct git_attr_check check[3];
	int crlf = CRLF_GUESS;
	int ident = 0;
	char *filter = NULL;
	char *buf, *buf2;

	setup_convert_check(check);
	if (!git_checkattr(path, ARRAY_SIZE(check), check)) {
		struct convert_driver *drv;
		crlf = git_path_check_crlf(path, check + 0);
		ident = git_path_check_ident(path, check + 1);
		drv = git_path_check_convert(path, check + 2);
		if (drv && drv->clean)
			filter = drv->clean;
	}

	buf = apply_filter(path, src, sizep, filter);

	buf2 = crlf_to_git(path, buf ? buf : src, sizep, crlf);
	if (buf2) {
		free(buf);
		buf = buf2;
	}

	buf2 = ident_to_git(path, buf ? buf : src, sizep, ident);
	if (buf2) {
		free(buf);
		buf = buf2;
	}

	return buf;
}

char *convert_to_working_tree(const char *path, const char *src, unsigned long *sizep)
{
	struct git_attr_check check[3];
	int crlf = CRLF_GUESS;
	int ident = 0;
	char *filter = NULL;
	char *buf, *buf2;

	setup_convert_check(check);
	if (!git_checkattr(path, ARRAY_SIZE(check), check)) {
		struct convert_driver *drv;
		crlf = git_path_check_crlf(path, check + 0);
		ident = git_path_check_ident(path, check + 1);
		drv = git_path_check_convert(path, check + 2);
		if (drv && drv->smudge)
			filter = drv->smudge;
	}

	buf = ident_to_worktree(path, src, sizep, ident);

	buf2 = crlf_to_worktree(path, buf ? buf : src, sizep, crlf);
	if (buf2) {
		free(buf);
		buf = buf2;
	}

	buf2 = apply_filter(path, buf ? buf : src, sizep, filter);
	if (buf2) {
		free(buf);
		buf = buf2;
	}

	return buf;
}

void *convert_sha1_file(const char *path, const unsigned char *sha1,
                        unsigned int mode, enum object_type *type,
                        unsigned long *size)
{
	void *buffer = read_sha1_file(sha1, type, size);
	if (S_ISREG(mode) && buffer) {
		void *converted = convert_to_working_tree(path, buffer, size);
		if (converted) {
			free(buffer);
			buffer = converted;
		}
	}
	return buffer;
}
