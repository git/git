#include "cache.h"
#include "attr.h"
#include "run-command.h"
#include "quote.h"

/*
 * convert.c - convert a file when checking it out and checking it in.
 *
 * This should use the pathname to decide on whether it wants to do some
 * more interesting conversions (automatic gzip/unzip, general format
 * conversions etc etc), but by default it just does automatic CRLF<->LF
 * translation when the "text" attribute or "auto_crlf" option is set.
 */

enum crlf_action {
	CRLF_GUESS = -1,
	CRLF_BINARY = 0,
	CRLF_TEXT,
	CRLF_INPUT,
	CRLF_CRLF,
	CRLF_AUTO
};

struct text_stat {
	/* NUL, CR, LF and CRLF counts */
	unsigned nul, cr, lf, crlf;

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
 */
static int is_binary(unsigned long size, struct text_stat *stats)
{

	if (stats->nul)
		return 1;
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

static enum eol output_eol(enum crlf_action crlf_action)
{
	switch (crlf_action) {
	case CRLF_BINARY:
		return EOL_UNSET;
	case CRLF_CRLF:
		return EOL_CRLF;
	case CRLF_INPUT:
		return EOL_LF;
	case CRLF_GUESS:
		if (!auto_crlf)
			return EOL_UNSET;
		/* fall through */
	case CRLF_TEXT:
	case CRLF_AUTO:
		if (auto_crlf == AUTO_CRLF_TRUE)
			return EOL_CRLF;
		else if (auto_crlf == AUTO_CRLF_INPUT)
			return EOL_LF;
		else if (core_eol == EOL_UNSET)
			return EOL_NATIVE;
	}
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
		if (stats->lf != stats->crlf) {
			if (checksafe == SAFE_CRLF_WARN)
				warning("LF will be replaced by CRLF in %s.\nThe file will have its original line endings in your working directory.", path);
			else /* i.e. SAFE_CRLF_FAIL */
				die("LF would be replaced by CRLF in %s", path);
		}
	}
}

static int has_cr_in_index(const char *path)
{
	int pos, len;
	unsigned long sz;
	enum object_type type;
	void *data;
	int has_cr;
	struct index_state *istate = &the_index;

	len = strlen(path);
	pos = index_name_pos(istate, path, len);
	if (pos < 0) {
		/*
		 * We might be in the middle of a merge, in which
		 * case we would read stage #2 (ours).
		 */
		int i;
		for (i = -pos - 1;
		     (pos < 0 && i < istate->cache_nr &&
		      !strcmp(istate->cache[i]->name, path));
		     i++)
			if (ce_stage(istate->cache[i]) == 2)
				pos = i;
	}
	if (pos < 0)
		return 0;
	data = read_sha1_file(istate->cache[pos]->sha1, &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return 0;
	}

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
	    (crlf_action == CRLF_GUESS && auto_crlf == AUTO_CRLF_FALSE) || !len)
		return 0;

	gather_stats(src, len, &stats);

	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_GUESS) {
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

		if (crlf_action == CRLF_GUESS) {
			/*
			 * If the file in the index has any CR in it, do not convert.
			 * This is the new safer autocrlf handling.
			 */
			if (has_cr_in_index(path))
				return 0;
		}
	}

	check_safe_crlf(path, crlf_action, &stats, checksafe);

	/* Optimization: No CR? Nothing to convert, regardless. */
	if (!stats.cr)
		return 0;

	/* only grow if not in place */
	if (strbuf_avail(buf) + buf->len < len)
		strbuf_grow(buf, len - buf->len);
	dst = buf->buf;
	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_GUESS) {
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

	/* No LF? Nothing to convert, regardless. */
	if (!stats.lf)
		return 0;

	/* Was it already in CRLF format? */
	if (stats.lf == stats.crlf)
		return 0;

	if (crlf_action == CRLF_AUTO || crlf_action == CRLF_GUESS) {
		if (crlf_action == CRLF_GUESS) {
			/* If we have any CR or CRLF line endings, we do not touch it */
			/* This is the new safer autocrlf-handling */
			if (stats.cr > 0 || stats.crlf > 0)
				return 0;
		}

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

struct filter_params {
	const char *src;
	unsigned long size;
	const char *cmd;
	const char *path;
};

static int filter_buffer(int in, int out, void *data)
{
	/*
	 * Spawn cmd and feed the buffer contents through its stdin.
	 */
	struct child_process child_process;
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

	memset(&child_process, 0, sizeof(child_process));
	child_process.argv = argv;
	child_process.use_shell = 1;
	child_process.in = -1;
	child_process.out = out;

	if (start_command(&child_process))
		return error("cannot fork to run external filter %s", params->cmd);

	write_err = (write_in_full(child_process.in, params->src, params->size) < 0);
	if (close(child_process.in))
		write_err = 1;
	if (write_err)
		error("cannot feed the input to external filter %s", params->cmd);

	status = finish_command(&child_process);
	if (status)
		error("external filter %s failed %d", params->cmd, status);

	strbuf_release(&cmd);
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
	int ret = 1;
	struct strbuf nbuf = STRBUF_INIT;
	struct async async;
	struct filter_params params;

	if (!cmd)
		return 0;

	memset(&async, 0, sizeof(async));
	async.proc = filter_buffer;
	async.data = &params;
	async.out = -1;
	params.src = src;
	params.size = len;
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
} *user_convert, **user_convert_tail;

static int read_convert_config(const char *var, const char *value, void *cb)
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

	if (!strcmp("smudge", ep))
		return git_config_string(&drv->smudge, var, value);

	if (!strcmp("clean", ep))
		return git_config_string(&drv->clean, var, value);

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
	memcpy(dst, src, len);
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
	else if (!strcmp(value, "auto"))
		return CRLF_AUTO;
	return CRLF_GUESS;
}

static int git_path_check_eol(const char *path, struct git_attr_check *check)
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

static enum crlf_action input_crlf_action(enum crlf_action text_attr, enum eol eol_attr)
{
	if (text_attr == CRLF_BINARY)
		return CRLF_BINARY;
	if (eol_attr == EOL_LF)
		return CRLF_INPUT;
	if (eol_attr == EOL_CRLF)
		return CRLF_CRLF;
	return text_attr;
}

struct conv_attrs {
	struct convert_driver *drv;
	enum crlf_action crlf_action;
	enum eol eol_attr;
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

	if (!git_checkattr(path, NUM_CONV_ATTRS, ccheck)) {
		ca->crlf_action = git_path_check_crlf(path, ccheck + 4);
		if (ca->crlf_action == CRLF_GUESS)
			ca->crlf_action = git_path_check_crlf(path, ccheck + 0);
		ca->ident = git_path_check_ident(path, ccheck + 1);
		ca->drv = git_path_check_convert(path, ccheck + 2);
		ca->eol_attr = git_path_check_eol(path, ccheck + 3);
	} else {
		ca->drv = NULL;
		ca->crlf_action = CRLF_GUESS;
		ca->eol_attr = EOL_UNSET;
		ca->ident = 0;
	}
}

int convert_to_git(const char *path, const char *src, size_t len,
                   struct strbuf *dst, enum safe_crlf checksafe)
{
	int ret = 0;
	const char *filter = NULL;
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	if (ca.drv)
		filter = ca.drv->clean;

	ret |= apply_filter(path, src, len, dst, filter);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	ca.crlf_action = input_crlf_action(ca.crlf_action, ca.eol_attr);
	ret |= crlf_to_git(path, src, len, dst, ca.crlf_action, checksafe);
	if (ret) {
		src = dst->buf;
		len = dst->len;
	}
	return ret | ident_to_git(path, src, len, dst, ca.ident);
}

static int convert_to_working_tree_internal(const char *path, const char *src,
					    size_t len, struct strbuf *dst,
					    int normalizing)
{
	int ret = 0;
	const char *filter = NULL;
	struct conv_attrs ca;

	convert_attrs(&ca, path);
	if (ca.drv)
		filter = ca.drv->smudge;

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
		ca.crlf_action = input_crlf_action(ca.crlf_action, ca.eol_attr);
		ret |= crlf_to_worktree(path, src, len, dst, ca.crlf_action);
		if (ret) {
			src = dst->buf;
			len = dst->len;
		}
	}
	return ret | apply_filter(path, src, len, dst, filter);
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
	return ret | convert_to_git(path, src, len, dst, 0);
}

/*****************************************************************
 *
 * Streaming converison support
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
static int lf_to_crlf_filter_fn(struct stream_filter *filter,
				const char *input, size_t *isize_p,
				char *output, size_t *osize_p)
{
	size_t count;

	if (!input)
		return 0; /* we do not keep any states */
	count = *isize_p;
	if (count) {
		size_t i, o;
		for (i = o = 0; o < *osize_p && i < count; i++) {
			char ch = input[i];
			if (ch == '\n') {
				if (o + 1 < *osize_p)
					output[o++] = '\r';
				else
					break;
			}
			output[o++] = ch;
		}

		*osize_p -= o;
		*isize_p -= i;
	}
	return 0;
}

static struct stream_filter_vtbl lf_to_crlf_vtbl = {
	lf_to_crlf_filter_fn,
	null_free_fn,
};

static struct stream_filter lf_to_crlf_filter_singleton = {
	&lf_to_crlf_vtbl,
};


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

	if (prefixcmp(str, "$Id: "))
		return 0;
	for (i = 5; str[i]; i++) {
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

	sprintf(ident->ident, ": %s $", sha1_to_hex(sha1));
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

	crlf_action = input_crlf_action(ca.crlf_action, ca.eol_attr);

	if ((crlf_action == CRLF_BINARY) || (crlf_action == CRLF_INPUT) ||
	    (crlf_action == CRLF_GUESS && auto_crlf == AUTO_CRLF_FALSE))
		filter = cascade_filter(filter, &null_filter_singleton);

	else if (output_eol(crlf_action) == EOL_CRLF &&
		 !(crlf_action == CRLF_AUTO || crlf_action == CRLF_GUESS))
		filter = cascade_filter(filter, &lf_to_crlf_filter_singleton);

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
