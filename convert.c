#include "cache.h"
#include "attr.h"
#include "run-command.h"
#include "quote.h"
#include "sigchain.h"

/*
 * convert.c - convert a file when checking it out and checking it in.
 *
 * This should use the pathname to decide on whether it wants to do some
 * more interesting conversions (automatic gzip/unzip, general format
 * conversions etc etc), but by default it just does automatic CRLF<->LF
 * translation when the "text" attribute or "auto_crlf" option is set.
 */

/* Stat bits: When BIN is set, the txt bits are unset */
#define CONVERT_STAT_BITS_TXT_LF    0x1
#define CONVERT_STAT_BITS_TXT_CRLF  0x2
#define CONVERT_STAT_BITS_BIN       0x4

enum crlf_action {
	CRLF_UNDEFINED,
	CRLF_BINARY,
	CRLF_TEXT,
	CRLF_TEXT_INPUT,
	CRLF_TEXT_CRLF,
	CRLF_AUTO,
	CRLF_AUTO_INPUT,
	CRLF_AUTO_CRLF
};

struct text_stat {
	/* NUL, CR, LF and CRLF counts */
	unsigned nul, lonecr, lonelf, crlf;

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
			if (i+1 < size && buf[i+1] == '\n') {
				stats->crlf++;
				i++;
			} else
				stats->lonecr++;
			continue;
		}
		if (c == '\n') {
			stats->lonelf++;
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
			case 0:
				stats->nul++;
				/* fall through */
			default:
				stats->nonprintable++;
			}
		}
		else
			stats->printable++;
	}

	/* If file ends with EOF then don't count this EOF as non-printable. */
	if (size >= 1 && buf[size-1] == '\032')
		stats->nonprintable--;
}

/*
 * The same heuristics as diff.c::mmfile_is_binary()
 * We treat files with bare CR as binary
 */
static int convert_is_binary(unsigned long size, const struct text_stat *stats)
{
	if (stats->lonecr)
		return 1;
	if (stats->nul)
		return 1;
	if ((stats->printable >> 7) < stats->nonprintable)
		return 1;
	return 0;
}

static unsigned int gather_convert_stats(const char *data, unsigned long size)
{
	struct text_stat stats;
	int ret = 0;
	if (!data || !size)
		return 0;
	gather_stats(data, size, &stats);
	if (convert_is_binary(size, &stats))
		ret |= CONVERT_STAT_BITS_BIN;
	if (stats.crlf)
		ret |= CONVERT_STAT_BITS_TXT_CRLF;
	if (stats.lonelf)
		ret |=  CONVERT_STAT_BITS_TXT_LF;

	return ret;
}

static const char *gather_convert_stats_ascii(const char *data, unsigned long size)
{
	unsigned int convert_stats = gather_convert_stats(data, size);

	if (convert_stats & CONVERT_STAT_BITS_BIN)
		return "-text";
	switch (convert_stats) {
	case CONVERT_STAT_BITS_TXT_LF:
		return "lf";
	case CONVERT_STAT_BITS_TXT_CRLF:
		return "crlf";
	case CONVERT_STAT_BITS_TXT_LF | CONVERT_STAT_BITS_TXT_CRLF:
		return "mixed";
	default:
		return "none";
	}
}

const char *get_cached_convert_stats_ascii(const char *path)
{
	const char *ret;
	unsigned long sz;
	void *data = read_blob_data_from_cache(path, &sz);
	ret = gather_convert_stats_ascii(data, sz);
	free(data);
	return ret;
}

const char *get_wt_convert_stats_ascii(const char *path)
{
	const char *ret = "";
	struct strbuf sb = STRBUF_INIT;
	if (strbuf_read_file(&sb, path, 0) >= 0)
		ret = gather_convert_stats_ascii(sb.buf, sb.len);
	strbuf_release(&sb);
	return ret;
}

static int text_eol_is_crlf(void)
{
	if (auto_crlf == AUTO_CRLF_TRUE)
		return 1;
	else if (auto_crlf == AUTO_CRLF_INPUT)
		return 0;
	if (core_eol == EOL_CRLF)
		return 1;
	if (core_eol == EOL_UNSET && EOL_NATIVE == EOL_CRLF)
		return 1;
	return 0;
}

static enum eol output_eol(enum crlf_action crlf_action)
{
	switch (crlf_action) {
	case CRLF_BINARY:
		return EOL_UNSET;
	case CRLF_TEXT_CRLF:
		return EOL_CRLF;
	case CRLF_TEXT_INPUT:
		return EOL_LF;
	case CRLF_UNDEFINED:
	case CRLF_AUTO_CRLF:
	case CRLF_AUTO_INPUT:
	case CRLF_TEXT:
	case CRLF_AUTO:
		/* fall through */
		return text_eol_is_crlf() ? EOL_CRLF : EOL_LF;
	}
	warning("Illegal crlf_action %d\n", (int)crlf_action);
	return core_eol;
}

static void check_safe_crlf(const char *path, enum crlf_action crlf_action,
                            struct text_stat *stats, enum safe_crlf checksafe)
{
	if (!checksafe)
		return;

	if (output_eol(crlf_action) == EOL_LF) {
		/*
		 * CRLFs would not be restored by checkout:
		 * check if we'd remove CRLFs
		 */
		if (stats->crlf) {
			if (checksafe == SAFE_CRLF_WARN)
				warning("CRLF will be replaced by LF in %s.\nThe file will have its original line endings in your working directory.", path);
			else /* i.e. SAFE_CRLF_FAIL */
				die("CRLF would be replaced by LF in %s.", path);
		}
	} else if (output_eol(crlf_action) == EOL_CRLF) {
		/*
		 * CRLFs would be added by checkout:
		 * check if we have "naked" LFs
		 */
		if (stats->lonelf) {
			if (checksafe == SAFE_CRLF_WARN)
				warning("LF will be replaced by CRLF in %s.\nThe file will have its original line endings in your working directory.", path);
			else /* i.e. SAFE_CRLF_FAIL */
				die("LF would be replaced by CRLF in %s", path);
		}
	}
}

static int has_cr_in_index(const char *path)
{
	unsigned long sz;
	void *data;
	int has_cr;

	data = read_blob_data_from_cache(path, &sz);
	if (!data)
		return 0;
	has_cr = memchr(data, '\r', sz) != NULL;
	free(data);
	return has_cr;
}

static int crlf_to_git(const char *path, const char *src, size_t len,
		       struct strbuf *buf,
		       enum crlf_action crlf_action, enum safe_crlf checksafe)
{
	struct text_stat stats;
	char *dst;

	if (crlf_action == CRLF_BINARY ||
	    (src && !len))
		return 0;

	/*
	 * If we are doing a dry-run and have no source buffer, there is
	 * nothing to analyze; we must assume we would convert.
	 */
	if (!buf && !src)
		return 1;

	gather_stats(src, len, &stats);

	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_AUTO_INPUT || crlf_action == CRLF_AUTO_CRLF) {
		if (convert_is_binary(len, &stats))
			return 0;

		if (crlf_action == CRLF_AUTO_INPUT || crlf_action == CRLF_AUTO_CRLF) {
			/*
			 * If the file in the index has any CR in it, do not convert.
			 * This is the new safer autocrlf handling.
			 */
			if (has_cr_in_index(path))
				return 0;
		}
	}

	check_safe_crlf(path, crlf_action, &stats, checksafe);

	/* Optimization: No CRLF? Nothing to convert, regardless. */
	if (!stats.crlf)
		return 0;

	/*
	 * At this point all of our source analysis is done, and we are sure we
	 * would convert. If we are in dry-run mode, we can give an answer.
	 */
	if (!buf)
		return 1;

	/* only grow if not in place */
	if (strbuf_avail(buf) + buf->len < len)
		strbuf_grow(buf, len - buf->len);
	dst = buf->buf;
	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_AUTO_INPUT || crlf_action == CRLF_AUTO_CRLF) {
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
			    struct strbuf *buf, enum crlf_action crlf_action)
{
	char *to_free = NULL;
	struct text_stat stats;

	if (!len || output_eol(crlf_action) != EOL_CRLF)
		return 0;

	gather_stats(src, len, &stats);

	/* No "naked" LF? Nothing to convert, regardless. */
	if (!stats.lonelf)
		return 0;

	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_AUTO_INPUT || crlf_action == CRLF_AUTO_CRLF) {
		if (crlf_action == CRLF_AUTO_INPUT || crlf_action == CRLF_AUTO_CRLF) {
			/* If we have any CR or CRLF line endings, we do not touch it */
			/* This is the new safer autocrlf-handling */
			if (stats.lonecr || stats.crlf )
				return 0;
		}

		if (convert_is_binary(len, &stats))
			return 0;
	}

	/* are we "faking" in place editing ? */
	if (src == buf->buf)
		to_free = strbuf_detach(buf, NULL);

	strbuf_grow(buf, len + stats.lonelf);
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

struct filter_params {
	const char *src;
	unsigned long size;
	int fd;
	const char *cmd;
	const char *path;
};

static int filter_buffer_or_fd(int in, int out, void *data)
{
	/*
	 * Spawn cmd and feed the buffer contents through its stdin.
	 */
	struct child_process child_process = CHILD_PROCESS_INIT;
	struct filter_params *params = (struct filter_params *)data;
	int write_err, status;
	const char *argv[] = { NULL, NULL };

	/* apply % substitution to cmd */
	struct strbuf cmd = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf_expand_dict_entry dict[] = {
		{ "f", NULL, },
		{ NULL, NULL, },
	};

	/* quote the path to preserve spaces, etc. */
	sq_quote_buf(&path, params->path);
	dict[0].value = path.buf;

	/* expand all %f with the quoted path */
	strbuf_expand(&cmd, params->cmd, strbuf_expand_dict_cb, &dict);
	strbuf_release(&path);

	argv[0] = cmd.buf;

	child_process.argv = argv;
	child_process.use_shell = 1;
	child_process.in = -1;
	child_process.out = out;

	if (start_command(&child_process))
		return error("cannot fork to run external filter %s", params->cmd);

	sigchain_push(SIGPIPE, SIG_IGN);

	if (params->src) {
		write_err = (write_in_full(child_process.in,
					   params->src, params->size) < 0);
		if (errno == EPIPE)
			write_err = 0;
	} else {
		write_err = copy_fd(params->fd, child_process.in);
		if (write_err == COPY_WRITE_ERROR && errno == EPIPE)
			write_err = 0;
	}

	if (close(child_process.in))
		write_err = 1;
	if (write_err)
		error("cannot feed the input to external filter %s", params->cmd);

	sigchain_pop(SIGPIPE);

	status = finish_command(&child_process);
	if (status)
		error("external filter %s failed %d", params->cmd, status);

	strbuf_release(&cmd);
	return (write_err || status);
}

static int apply_filter(const char *path, const char *src, size_t len, int fd,
                        struct strbuf *dst, const char *cmd)
{
	/*
	 * Create a pipeline to have the command filter the buffer's
	 * contents.
	 *
	 * (child --> cmd) --> us
	 */
	int ret = 1;
	struct strbuf nbuf = STRBUF_INIT;
	struct async async;
	struct filter_params params;

	if (!cmd)
		return 0;

	if (!dst)
		return 1;

	memset(&async, 0, sizeof(async));
	async.proc = filter_buffer_or_fd;
	async.data = &params;
	async.out = -1;
	params.src = src;
	params.size = len;
	params.fd = fd;
	params.cmd = cmd;
	params.path = path;

	fflush(NULL);
	if (start_async(&async))
		return 0;	/* error was already reported */

	if (strbuf_read(&nbuf, async.out, len) < 0) {
		error("read from external filter %s failed", cmd);
		ret = 0;
	}
	if (close(async.out)) {
		error("read from external filter %s failed", cmd);
		ret = 0;
	}
	if (finish_async(&async)) {
		error("external filter %s failed", cmd);
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
	const char *smudge;
	const char *clean;
	int required;
} *user_convert, **user_convert_tail;

static int read_convert_config(const char *var, const char *value, void *cb)
{
	const char *key, *name;
	int namelen;
	struct convert_driver *drv;

	/*
	 * External conversion drivers are configured using
	 * "filter.<name>.variable".
	 */
	if (parse_config_key(var, "filter", &name, &namelen, &key) < 0 || !name)
		return 0;
	for (drv = user_convert; drv; drv = drv->next)
		if (!strncmp(drv->name, name, namelen) && !drv->name[namelen])
			break;
	if (!drv) {
		drv = xcalloc(1, sizeof(struct convert_driver));
		drv->name = xmemdupz(name, namelen);
		*user_convert_tail = drv;
		user_convert_tail = &(drv->next);
	}

	/*
	 * filter.<name>.smudge and filter.<name>.clean specifies
	 * the command line:
	 *
	 *	command-line
	 *
	 * The command-line will not be interpolated in any way.
	 */

	if (!strcmp("smudge", key))
		return git_config_string(&drv->smudge, var, value);

	if (!strcmp("clean", key))
		return git_config_string(&drv->clean, var, value);

	if (!strcmp("required", key)) {
		drv->required = git_config_bool(var, value);
		return 0;
	}

	return 0;
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
			if (ch == '\n')
				break;
		}
	}
	return cnt;
}

static int ident_to_git(const char *path, const char *src, size_t len,
                        struct strbuf *buf, int ident)
{
	char *dst, *dollar;

	if (!ident || (src && !count_ident(src, len)))
		return 0;

	if (!buf)
		return 1;

	/* only grow if not in place */
	if (strbuf_avail(buf) + buf->len < len)
		strbuf_grow(buf, len - buf->len);
	dst = buf->buf;
	for (;;) {
		dollar = memchr(src, '$', len);
		if (!dollar)
			break;
		memmove(dst, src, dollar + 1 - src);
		dst += dollar + 1 - src;
		len -= dollar + 1 - src;
		src  = dollar + 1;

		if (len > 3 && !memcmp(src, "Id:", 3)) {
			dollar = memchr(src + 3, '$', len - 3);
			if (!dollar)
				break;
			if (memchr(src + 3, '\n', dollar - src - 3)) {
				/* Line break before the next dollar. */
				continue;
			}

			memcpy(dst, "Id$", 3);
			dst += 3;
			len -= dollar + 1 - src;
			src  = dollar + 1;
		}
	}
	memmove(dst, src, len);
	strbuf_setlen(buf, dst + len - buf->buf);
	return 1;
}

static int ident_to_worktree(const char *path, const char *src, size_t len,
                             struct strbuf *buf, int ident)
{
	unsigned char sha1[20];
	char *to_free = NULL, *dollar, *spc;
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
			 * repository, we cope with that by stripping the expansion out.
			 * This is probably not a good idea, since it will cause changes
			 * on checkout, which won't go away by stash, but let's keep it
			 * for git-style ids.
			 */
			dollar = memchr(src + 3, '$', len - 3);
			if (!dollar) {
				/* incomplete keyword, no more '$', so just quit the loop */
				break;
			}

			if (memchr(src + 3, '\n', dollar - src - 3)) {
				/* Line break before the next dollar. */
				continue;
			}

			spc = memchr(src + 4, ' ', dollar - src - 4);
			if (spc && spc < dollar-1) {
				/* There are spaces in unexpected places.
				 * This is probably an id from some other
				 * versioning system. Keep it for now.
				 */
				continue;
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

static enum crlf_action git_path_check_crlf(struct git_attr_check *check)
{
	const char *value = check->value;

	if (ATTR_TRUE(value))
		return CRLF_TEXT;
	else if (ATTR_FALSE(value))
		return CRLF_BINARY;
	else if (ATTR_UNSET(value))
		;
	else if (!strcmp(value, "input"))
		return CRLF_TEXT_INPUT;
	else if (!strcmp(value, "auto"))
		return CRLF_AUTO;
	return CRLF_UNDEFINED;
}

static enum eol git_path_check_eol(struct git_attr_check *check)
{
	const char *value = check->value;

	if (ATTR_UNSET(value))
		;
	else if (!strcmp(value, "lf"))
		return EOL_LF;
	else if (!strcmp(value, "crlf"))
		return EOL_CRLF;
	return EOL_UNSET;
}

static struct convert_driver *git_path_check_convert(struct git_attr_check *check)
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

static int git_path_check_ident(struct git_attr_check *check)
{
	const char *value = check->value;

	return !!ATTR_TRUE(value);
}

struct conv_attrs {
	struct convert_driver *drv;
	enum crlf_action attr_action; /* What attr says */
	enum crlf_action crlf_action; /* When no attr is set, use core.autocrlf */
	int ident;
};

static const char *conv_attr_name[] = {
	"crlf", "ident", "filter", "eol", "text",
};
#define NUM_CONV_ATTRS ARRAY_SIZE(conv_attr_name)

static void convert_attrs(struct conv_attrs *ca, const char *path)
{
	int i;
	static struct git_attr_check ccheck[NUM_CONV_ATTRS];

	if (!ccheck[0].attr) {
		for (i = 0; i < NUM_CONV_ATTRS; i++)
			ccheck[i].attr = git_attr(conv_attr_name[i]);
		user_convert_tail = &user_convert;
		git_config(read_convert_config, NULL);
	}

	if (!git_check_attr(path, NUM_CONV_ATTRS, ccheck)) {
		ca->crlf_action = git_path_check_crlf(ccheck + 4);
		if (ca->crlf_action == CRLF_UNDEFINED)
			ca->crlf_action = git_path_check_crlf(ccheck + 0);
		ca->attr_action = ca->crlf_action;
		ca->ident = git_path_check_ident(ccheck + 1);
		ca->drv = git_path_check_convert(ccheck + 2);
		if (ca->crlf_action != CRLF_BINARY) {
			enum eol eol_attr = git_path_check_eol(ccheck + 3);
			if (eol_attr == EOL_LF)
				ca->crlf_action = CRLF_TEXT_INPUT;
			else if (eol_attr == EOL_CRLF)
				ca->crlf_action = CRLF_TEXT_CRLF;
		}
		ca->attr_action = ca->crlf_action;
	} else {
		ca->drv = NULL;
		ca->crlf_action = CRLF_UNDEFINED;
		ca->ident = 0;
	}
	if (ca->crlf_action == CRLF_TEXT)
		ca->crlf_action = text_eol_is_crlf() ? CRLF_TEXT_CRLF : CRLF_TEXT_INPUT;
	if (ca->crlf_action == CRLF_UNDEFINED && auto_crlf == AUTO_CRLF_FALSE)
		ca->crlf_action = CRLF_BINARY;
	if (ca->crlf_action == CRLF_UNDEFINED && auto_crlf == AUTO_CRLF_TRUE)
		ca->crlf_action = CRLF_AUTO_CRLF;
	if (ca->crlf_action == CRLF_UNDEFINED && auto_crlf == AUTO_CRLF_INPUT)
		ca->crlf_action = CRLF_AUTO_INPUT;
}

int would_convert_to_git_filter_fd(const char *path)
{
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	if (!ca.drv)
		return 0;

	/*
	 * Apply a filter to an fd only if the filter is required to succeed.
	 * We must die if the filter fails, because the original data before
	 * filtering is not available.
	 */
	if (!ca.drv->required)
		return 0;

	return apply_filter(path, NULL, 0, -1, NULL, ca.drv->clean);
}

const char *get_convert_attr_ascii(const char *path)
{
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	switch (ca.attr_action) {
	case CRLF_UNDEFINED:
		return "";
	case CRLF_BINARY:
		return "-text";
	case CRLF_TEXT:
		return "text";
	case CRLF_TEXT_INPUT:
		return "text eol=lf";
	case CRLF_TEXT_CRLF:
		return "text eol=crlf";
	case CRLF_AUTO:
		return "text=auto";
	case CRLF_AUTO_CRLF:
		return "text=auto eol=crlf"; /* This is not supported yet */
	case CRLF_AUTO_INPUT:
		return "text=auto eol=lf"; /* This is not supported yet */
	}
	return "";
}

int convert_to_git(const char *path, const char *src, size_t len,
                   struct strbuf *dst, enum safe_crlf checksafe)
{
	int ret = 0;
	const char *filter = NULL;
	int required = 0;
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	if (ca.drv) {
		filter = ca.drv->clean;
		required = ca.drv->required;
	}

	ret |= apply_filter(path, src, len, -1, dst, filter);
	if (!ret && required)
		die("%s: clean filter '%s' failed", path, ca.drv->name);

	if (ret && dst) {
		src = dst->buf;
		len = dst->len;
	}
	ret |= crlf_to_git(path, src, len, dst, ca.crlf_action, checksafe);
	if (ret && dst) {
		src = dst->buf;
		len = dst->len;
	}
	return ret | ident_to_git(path, src, len, dst, ca.ident);
}

void convert_to_git_filter_fd(const char *path, int fd, struct strbuf *dst,
			      enum safe_crlf checksafe)
{
	struct conv_attrs ca;
	convert_attrs(&ca, path);

	assert(ca.drv);
	assert(ca.drv->clean);

	if (!apply_filter(path, NULL, 0, fd, dst, ca.drv->clean))
		die("%s: clean filter '%s' failed", path, ca.drv->name);

	crlf_to_git(path, dst->buf, dst->len, dst, ca.crlf_action, checksafe);
	ident_to_git(path, dst->buf, dst->len, dst, ca.ident);
}

static int convert_to_working_tree_internal(const char *path, const char *src,
					    size_t len, struct strbuf *dst,
					    int normalizing)
{
	int ret = 0, ret_filter = 0;
	const char *filter = NULL;
	int required = 0;
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	if (ca.drv) {
		filter = ca.drv->smudge;
		required = ca.drv->required;
	}

	ret |= ident_to_worktree(path, src, len, dst, ca.ident);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	/*
	 * CRLF conversion can be skipped if normalizing, unless there
	 * is a smudge filter.  The filter might expect CRLFs.
	 */
	if (filter || !normalizing) {
		ret |= crlf_to_worktree(path, src, len, dst, ca.crlf_action);
		if (ret) {
			src = dst->buf;
			len = dst->len;
		}
	}

	ret_filter = apply_filter(path, src, len, -1, dst, filter);
	if (!ret_filter && required)
		die("%s: smudge filter %s failed", path, ca.drv->name);

	return ret | ret_filter;
}

int convert_to_working_tree(const char *path, const char *src, size_t len, struct strbuf *dst)
{
	return convert_to_working_tree_internal(path, src, len, dst, 0);
}

int renormalize_buffer(const char *path, const char *src, size_t len, struct strbuf *dst)
{
	int ret = convert_to_working_tree_internal(path, src, len, dst, 1);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	return ret | convert_to_git(path, src, len, dst, SAFE_CRLF_FALSE);
}

/*****************************************************************
 *
 * Streaming conversion support
 *
 *****************************************************************/

typedef int (*filter_fn)(struct stream_filter *,
			 const char *input, size_t *isize_p,
			 char *output, size_t *osize_p);
typedef void (*free_fn)(struct stream_filter *);

struct stream_filter_vtbl {
	filter_fn filter;
	free_fn free;
};

struct stream_filter {
	struct stream_filter_vtbl *vtbl;
};

static int null_filter_fn(struct stream_filter *filter,
			  const char *input, size_t *isize_p,
			  char *output, size_t *osize_p)
{
	size_t count;

	if (!input)
		return 0; /* we do not keep any states */
	count = *isize_p;
	if (*osize_p < count)
		count = *osize_p;
	if (count) {
		memmove(output, input, count);
		*isize_p -= count;
		*osize_p -= count;
	}
	return 0;
}

static void null_free_fn(struct stream_filter *filter)
{
	; /* nothing -- null instances are shared */
}

static struct stream_filter_vtbl null_vtbl = {
	null_filter_fn,
	null_free_fn,
};

static struct stream_filter null_filter_singleton = {
	&null_vtbl,
};

int is_null_stream_filter(struct stream_filter *filter)
{
	return filter == &null_filter_singleton;
}


/*
 * LF-to-CRLF filter
 */

struct lf_to_crlf_filter {
	struct stream_filter filter;
	unsigned has_held:1;
	char held;
};

static int lf_to_crlf_filter_fn(struct stream_filter *filter,
				const char *input, size_t *isize_p,
				char *output, size_t *osize_p)
{
	size_t count, o = 0;
	struct lf_to_crlf_filter *lf_to_crlf = (struct lf_to_crlf_filter *)filter;

	/*
	 * We may be holding onto the CR to see if it is followed by a
	 * LF, in which case we would need to go to the main loop.
	 * Otherwise, just emit it to the output stream.
	 */
	if (lf_to_crlf->has_held && (lf_to_crlf->held != '\r' || !input)) {
		output[o++] = lf_to_crlf->held;
		lf_to_crlf->has_held = 0;
	}

	/* We are told to drain */
	if (!input) {
		*osize_p -= o;
		return 0;
	}

	count = *isize_p;
	if (count || lf_to_crlf->has_held) {
		size_t i;
		int was_cr = 0;

		if (lf_to_crlf->has_held) {
			was_cr = 1;
			lf_to_crlf->has_held = 0;
		}

		for (i = 0; o < *osize_p && i < count; i++) {
			char ch = input[i];

			if (ch == '\n') {
				output[o++] = '\r';
			} else if (was_cr) {
				/*
				 * Previous round saw CR and it is not followed
				 * by a LF; emit the CR before processing the
				 * current character.
				 */
				output[o++] = '\r';
			}

			/*
			 * We may have consumed the last output slot,
			 * in which case we need to break out of this
			 * loop; hold the current character before
			 * returning.
			 */
			if (*osize_p <= o) {
				lf_to_crlf->has_held = 1;
				lf_to_crlf->held = ch;
				continue; /* break but increment i */
			}

			if (ch == '\r') {
				was_cr = 1;
				continue;
			}

			was_cr = 0;
			output[o++] = ch;
		}

		*osize_p -= o;
		*isize_p -= i;

		if (!lf_to_crlf->has_held && was_cr) {
			lf_to_crlf->has_held = 1;
			lf_to_crlf->held = '\r';
		}
	}
	return 0;
}

static void lf_to_crlf_free_fn(struct stream_filter *filter)
{
	free(filter);
}

static struct stream_filter_vtbl lf_to_crlf_vtbl = {
	lf_to_crlf_filter_fn,
	lf_to_crlf_free_fn,
};

static struct stream_filter *lf_to_crlf_filter(void)
{
	struct lf_to_crlf_filter *lf_to_crlf = xcalloc(1, sizeof(*lf_to_crlf));

	lf_to_crlf->filter.vtbl = &lf_to_crlf_vtbl;
	return (struct stream_filter *)lf_to_crlf;
}

/*
 * Cascade filter
 */
#define FILTER_BUFFER 1024
struct cascade_filter {
	struct stream_filter filter;
	struct stream_filter *one;
	struct stream_filter *two;
	char buf[FILTER_BUFFER];
	int end, ptr;
};

static int cascade_filter_fn(struct stream_filter *filter,
			     const char *input, size_t *isize_p,
			     char *output, size_t *osize_p)
{
	struct cascade_filter *cas = (struct cascade_filter *) filter;
	size_t filled = 0;
	size_t sz = *osize_p;
	size_t to_feed, remaining;

	/*
	 * input -- (one) --> buf -- (two) --> output
	 */
	while (filled < sz) {
		remaining = sz - filled;

		/* do we already have something to feed two with? */
		if (cas->ptr < cas->end) {
			to_feed = cas->end - cas->ptr;
			if (stream_filter(cas->two,
					  cas->buf + cas->ptr, &to_feed,
					  output + filled, &remaining))
				return -1;
			cas->ptr += (cas->end - cas->ptr) - to_feed;
			filled = sz - remaining;
			continue;
		}

		/* feed one from upstream and have it emit into our buffer */
		to_feed = input ? *isize_p : 0;
		if (input && !to_feed)
			break;
		remaining = sizeof(cas->buf);
		if (stream_filter(cas->one,
				  input, &to_feed,
				  cas->buf, &remaining))
			return -1;
		cas->end = sizeof(cas->buf) - remaining;
		cas->ptr = 0;
		if (input) {
			size_t fed = *isize_p - to_feed;
			*isize_p -= fed;
			input += fed;
		}

		/* do we know that we drained one completely? */
		if (input || cas->end)
			continue;

		/* tell two to drain; we have nothing more to give it */
		to_feed = 0;
		remaining = sz - filled;
		if (stream_filter(cas->two,
				  NULL, &to_feed,
				  output + filled, &remaining))
			return -1;
		if (remaining == (sz - filled))
			break; /* completely drained two */
		filled = sz - remaining;
	}
	*osize_p -= filled;
	return 0;
}

static void cascade_free_fn(struct stream_filter *filter)
{
	struct cascade_filter *cas = (struct cascade_filter *)filter;
	free_stream_filter(cas->one);
	free_stream_filter(cas->two);
	free(filter);
}

static struct stream_filter_vtbl cascade_vtbl = {
	cascade_filter_fn,
	cascade_free_fn,
};

static struct stream_filter *cascade_filter(struct stream_filter *one,
					    struct stream_filter *two)
{
	struct cascade_filter *cascade;

	if (!one || is_null_stream_filter(one))
		return two;
	if (!two || is_null_stream_filter(two))
		return one;

	cascade = xmalloc(sizeof(*cascade));
	cascade->one = one;
	cascade->two = two;
	cascade->end = cascade->ptr = 0;
	cascade->filter.vtbl = &cascade_vtbl;
	return (struct stream_filter *)cascade;
}

/*
 * ident filter
 */
#define IDENT_DRAINING (-1)
#define IDENT_SKIPPING (-2)
struct ident_filter {
	struct stream_filter filter;
	struct strbuf left;
	int state;
	char ident[45]; /* ": x40 $" */
};

static int is_foreign_ident(const char *str)
{
	int i;

	if (!skip_prefix(str, "$Id: ", &str))
		return 0;
	for (i = 0; str[i]; i++) {
		if (isspace(str[i]) && str[i+1] != '$')
			return 1;
	}
	return 0;
}

static void ident_drain(struct ident_filter *ident, char **output_p, size_t *osize_p)
{
	size_t to_drain = ident->left.len;

	if (*osize_p < to_drain)
		to_drain = *osize_p;
	if (to_drain) {
		memcpy(*output_p, ident->left.buf, to_drain);
		strbuf_remove(&ident->left, 0, to_drain);
		*output_p += to_drain;
		*osize_p -= to_drain;
	}
	if (!ident->left.len)
		ident->state = 0;
}

static int ident_filter_fn(struct stream_filter *filter,
			   const char *input, size_t *isize_p,
			   char *output, size_t *osize_p)
{
	struct ident_filter *ident = (struct ident_filter *)filter;
	static const char head[] = "$Id";

	if (!input) {
		/* drain upon eof */
		switch (ident->state) {
		default:
			strbuf_add(&ident->left, head, ident->state);
		case IDENT_SKIPPING:
			/* fallthru */
		case IDENT_DRAINING:
			ident_drain(ident, &output, osize_p);
		}
		return 0;
	}

	while (*isize_p || (ident->state == IDENT_DRAINING)) {
		int ch;

		if (ident->state == IDENT_DRAINING) {
			ident_drain(ident, &output, osize_p);
			if (!*osize_p)
				break;
			continue;
		}

		ch = *(input++);
		(*isize_p)--;

		if (ident->state == IDENT_SKIPPING) {
			/*
			 * Skipping until '$' or LF, but keeping them
			 * in case it is a foreign ident.
			 */
			strbuf_addch(&ident->left, ch);
			if (ch != '\n' && ch != '$')
				continue;
			if (ch == '$' && !is_foreign_ident(ident->left.buf)) {
				strbuf_setlen(&ident->left, sizeof(head) - 1);
				strbuf_addstr(&ident->left, ident->ident);
			}
			ident->state = IDENT_DRAINING;
			continue;
		}

		if (ident->state < sizeof(head) &&
		    head[ident->state] == ch) {
			ident->state++;
			continue;
		}

		if (ident->state)
			strbuf_add(&ident->left, head, ident->state);
		if (ident->state == sizeof(head) - 1) {
			if (ch != ':' && ch != '$') {
				strbuf_addch(&ident->left, ch);
				ident->state = 0;
				continue;
			}

			if (ch == ':') {
				strbuf_addch(&ident->left, ch);
				ident->state = IDENT_SKIPPING;
			} else {
				strbuf_addstr(&ident->left, ident->ident);
				ident->state = IDENT_DRAINING;
			}
			continue;
		}

		strbuf_addch(&ident->left, ch);
		ident->state = IDENT_DRAINING;
	}
	return 0;
}

static void ident_free_fn(struct stream_filter *filter)
{
	struct ident_filter *ident = (struct ident_filter *)filter;
	strbuf_release(&ident->left);
	free(filter);
}

static struct stream_filter_vtbl ident_vtbl = {
	ident_filter_fn,
	ident_free_fn,
};

static struct stream_filter *ident_filter(const unsigned char *sha1)
{
	struct ident_filter *ident = xmalloc(sizeof(*ident));

	xsnprintf(ident->ident, sizeof(ident->ident),
		  ": %s $", sha1_to_hex(sha1));
	strbuf_init(&ident->left, 0);
	ident->filter.vtbl = &ident_vtbl;
	ident->state = 0;
	return (struct stream_filter *)ident;
}

/*
 * Return an appropriately constructed filter for the path, or NULL if
 * the contents cannot be filtered without reading the whole thing
 * in-core.
 *
 * Note that you would be crazy to set CRLF, smuge/clean or ident to a
 * large binary blob you would want us not to slurp into the memory!
 */
struct stream_filter *get_stream_filter(const char *path, const unsigned char *sha1)
{
	struct conv_attrs ca;
	enum crlf_action crlf_action;
	struct stream_filter *filter = NULL;

	convert_attrs(&ca, path);

	if (ca.drv && (ca.drv->smudge || ca.drv->clean))
		return filter;

	if (ca.ident)
		filter = ident_filter(sha1);

	crlf_action = ca.crlf_action;

	if ((crlf_action == CRLF_BINARY) ||
			crlf_action == CRLF_AUTO_INPUT ||
			(crlf_action == CRLF_TEXT_INPUT))
		filter = cascade_filter(filter, &null_filter_singleton);

	else if (output_eol(crlf_action) == EOL_CRLF &&
		 !(crlf_action == CRLF_AUTO || crlf_action == CRLF_AUTO_CRLF))
		filter = cascade_filter(filter, lf_to_crlf_filter());

	return filter;
}

void free_stream_filter(struct stream_filter *filter)
{
	filter->vtbl->free(filter);
}

int stream_filter(struct stream_filter *filter,
		  const char *input, size_t *isize_p,
		  char *output, size_t *osize_p)
{
	return filter->vtbl->filter(filter, input, isize_p, output, osize_p);
}
