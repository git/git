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

static int crlf_to_git(const char *path, const char *src, size_t len,
                       struct strbuf *buf, int action)
{
	struct text_stat stats;
	char *dst;

	if ((action == CRLF_BINARY) || !auto_crlf || !len)
		return 0;

	gather_stats(src, len, &stats);
	/* No CR? Nothing to convert, regardless. */
	if (!stats.cr)
		return 0;

	if (action == CRLF_GUESS) {
		/*
		 * We're currently not going to even try to convert stuff
		 * that has bare CR characters. Does anybody do that crazy
		 * stuff?
		 */
		if (stats.cr != stats.crlf)
			return 0;

		/*
		 * And add some heuristics for binary vs text, of course...
		 */
		if (is_binary(len, &stats))
			return 0;
	}

	/* only grow if not in place */
	if (strbuf_avail(buf) + buf->len < len)
		strbuf_grow(buf, len - buf->len);
	dst = buf->buf;
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
		} while (--len);
	} else {
		do {
			unsigned char c = *src++;
			if (! (c == '\r' && (1 < len && *src == '\n')))
				*dst++ = c;
		} while (--len);
	}
	strbuf_setlen(buf, dst - buf->buf);
	return 1;
}

static int crlf_to_worktree(const char *path, const char *src, size_t len,
                            struct strbuf *buf, int action)
{
	char *to_free = NULL;
	struct text_stat stats;

	if ((action == CRLF_BINARY) || (action == CRLF_INPUT) ||
	    auto_crlf <= 0)
		return 0;

	if (!len)
		return 0;

	gather_stats(src, len, &stats);

	/* No LF? Nothing to convert, regardless. */
	if (!stats.lf)
		return 0;

	/* Was it already in CRLF format? */
	if (stats.lf == stats.crlf)
		return 0;

	if (action == CRLF_GUESS) {
		/* If we have any bare CR characters, we're not going to touch it */
		if (stats.cr != stats.crlf)
			return 0;

		if (is_binary(len, &stats))
			return 0;
	}

	/* are we "faking" in place editing ? */
	if (src == buf->buf)
		to_free = strbuf_detach(buf, NULL);

	strbuf_grow(buf, len + stats.lf - stats.crlf);
	for (;;) {
		const char *nl = memchr(src, '\n', len);
		if (!nl)
			break;
		if (nl > src && nl[-1] == '\r') {
			strbuf_add(buf, src, nl + 1 - src);
		} else {
			strbuf_add(buf, src, nl - src);
			strbuf_addstr(buf, "\r\n");
		}
		len -= nl + 1 - src;
		src  = nl + 1;
	}
	strbuf_add(buf, src, len);

	free(to_free);
	return 1;
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

static int apply_filter(const char *path, const char *src, size_t len,
                        struct strbuf *dst, const char *cmd)
{
	/*
	 * Create a pipeline to have the command filter the buffer's
	 * contents.
	 *
	 * (child --> cmd) --> us
	 */
	int pipe_feed[2];
	int status, ret = 1;
	struct child_process child_process;
	struct strbuf nbuf;

	if (!cmd)
		return 0;

	memset(&child_process, 0, sizeof(child_process));

	if (pipe(pipe_feed) < 0) {
		error("cannot create pipe to run external filter %s", cmd);
		return 0;
	}

	fflush(NULL);
	child_process.pid = fork();
	if (child_process.pid < 0) {
		error("cannot fork to run external filter %s", cmd);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		return 0;
	}
	if (!child_process.pid) {
		dup2(pipe_feed[1], 1);
		close(pipe_feed[0]);
		close(pipe_feed[1]);
		exit(filter_buffer(path, src, len, cmd));
	}
	close(pipe_feed[1]);

	strbuf_init(&nbuf, 0);
	if (strbuf_read(&nbuf, pipe_feed[0], len) < 0) {
		error("read from external filter %s failed", cmd);
		ret = 0;
	}
	if (close(pipe_feed[0])) {
		error("read from external filter %s failed", cmd);
		ret = 0;
	}
	status = finish_command(&child_process);
	if (status) {
		error("external filter %s failed %d", cmd, -status);
		ret = 0;
	}

	if (ret) {
		strbuf_swap(dst, &nbuf);
	}
	strbuf_release(&nbuf);
	return ret;
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
		drv = xcalloc(1, sizeof(struct convert_driver));
		drv->name = xmemdupz(name, namelen);
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

static int ident_to_git(const char *path, const char *src, size_t len,
                        struct strbuf *buf, int ident)
{
	char *dst, *dollar;

	if (!ident || !count_ident(src, len))
		return 0;

	/* only grow if not in place */
	if (strbuf_avail(buf) + buf->len < len)
		strbuf_grow(buf, len - buf->len);
	dst = buf->buf;
	for (;;) {
		dollar = memchr(src, '$', len);
		if (!dollar)
			break;
		memcpy(dst, src, dollar + 1 - src);
		dst += dollar + 1 - src;
		len -= dollar + 1 - src;
		src  = dollar + 1;

		if (len > 3 && !memcmp(src, "Id:", 3)) {
			dollar = memchr(src + 3, '$', len - 3);
			if (!dollar)
				break;
			memcpy(dst, "Id$", 3);
			dst += 3;
			len -= dollar + 1 - src;
			src  = dollar + 1;
		}
	}
	memcpy(dst, src, len);
	strbuf_setlen(buf, dst + len - buf->buf);
	return 1;
}

static int ident_to_worktree(const char *path, const char *src, size_t len,
                             struct strbuf *buf, int ident)
{
	unsigned char sha1[20];
	char *to_free = NULL, *dollar;
	int cnt;

	if (!ident)
		return 0;

	cnt = count_ident(src, len);
	if (!cnt)
		return 0;

	/* are we "faking" in place editing ? */
	if (src == buf->buf)
		to_free = strbuf_detach(buf, NULL);
	hash_sha1_file(src, len, "blob", sha1);

	strbuf_grow(buf, len + cnt * 43);
	for (;;) {
		/* step 1: run to the next '$' */
		dollar = memchr(src, '$', len);
		if (!dollar)
			break;
		strbuf_add(buf, src, dollar + 1 - src);
		len -= dollar + 1 - src;
		src  = dollar + 1;

		/* step 2: does it looks like a bit like Id:xxx$ or Id$ ? */
		if (len < 3 || memcmp("Id", src, 2))
			continue;

		/* step 3: skip over Id$ or Id:xxxxx$ */
		if (src[2] == '$') {
			src += 3;
			len -= 3;
		} else if (src[2] == ':') {
			/*
			 * It's possible that an expanded Id has crept its way into the
			 * repository, we cope with that by stripping the expansion out
			 */
			dollar = memchr(src + 3, '$', len - 3);
			if (!dollar) {
				/* incomplete keyword, no more '$', so just quit the loop */
				break;
			}

			len -= dollar + 1 - src;
			src  = dollar + 1;
		} else {
			/* it wasn't a "Id$" or "Id:xxxx$" */
			continue;
		}

		/* step 4: substitute */
		strbuf_addstr(buf, "Id: ");
		strbuf_add(buf, sha1_to_hex(sha1), 40);
		strbuf_addstr(buf, " $");
	}
	strbuf_add(buf, src, len);

	free(to_free);
	return 1;
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

int convert_to_git(const char *path, const char *src, size_t len, struct strbuf *dst)
{
	struct git_attr_check check[3];
	int crlf = CRLF_GUESS;
	int ident = 0, ret = 0;
	char *filter = NULL;

	setup_convert_check(check);
	if (!git_checkattr(path, ARRAY_SIZE(check), check)) {
		struct convert_driver *drv;
		crlf = git_path_check_crlf(path, check + 0);
		ident = git_path_check_ident(path, check + 1);
		drv = git_path_check_convert(path, check + 2);
		if (drv && drv->clean)
			filter = drv->clean;
	}

	ret |= apply_filter(path, src, len, dst, filter);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	ret |= crlf_to_git(path, src, len, dst, crlf);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	return ret | ident_to_git(path, src, len, dst, ident);
}

int convert_to_working_tree(const char *path, const char *src, size_t len, struct strbuf *dst)
{
	struct git_attr_check check[3];
	int crlf = CRLF_GUESS;
	int ident = 0, ret = 0;
	char *filter = NULL;

	setup_convert_check(check);
	if (!git_checkattr(path, ARRAY_SIZE(check), check)) {
		struct convert_driver *drv;
		crlf = git_path_check_crlf(path, check + 0);
		ident = git_path_check_ident(path, check + 1);
		drv = git_path_check_convert(path, check + 2);
		if (drv && drv->smudge)
			filter = drv->smudge;
	}

	ret |= ident_to_worktree(path, src, len, dst, ident);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	ret |= crlf_to_worktree(path, src, len, dst, crlf);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	return ret | apply_filter(path, src, len, dst, filter);
}
