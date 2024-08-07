#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "config.h"
#include "hex.h"
#include "object-store-ll.h"
#include "strbuf.h"
#include "xdiff-interface.h"
#include "xdiff/xtypes.h"
#include "xdiff/xdiffi.h"
#include "xdiff/xutils.h"

struct xdiff_emit_state {
	xdiff_emit_hunk_fn hunk_fn;
	xdiff_emit_line_fn line_fn;
	void *consume_callback_data;
	struct strbuf remainder;
};

static int xdiff_out_hunk(void *priv_,
			  long old_begin, long old_nr,
			  long new_begin, long new_nr,
			  const char *func, long funclen)
{
	struct xdiff_emit_state *priv = priv_;

	if (priv->remainder.len)
		BUG("xdiff emitted hunk in the middle of a line");

	priv->hunk_fn(priv->consume_callback_data,
		      old_begin, old_nr, new_begin, new_nr,
		      func, funclen);
	return 0;
}

static int consume_one(void *priv_, char *s, unsigned long size)
{
	struct xdiff_emit_state *priv = priv_;
	char *ep;
	while (size) {
		unsigned long this_size;
		int ret;
		ep = memchr(s, '\n', size);
		this_size = (ep == NULL) ? size : (ep - s + 1);
		ret = priv->line_fn(priv->consume_callback_data, s, this_size);
		if (ret)
			return ret;
		size -= this_size;
		s += this_size;
	}
	return 0;
}

static int xdiff_outf(void *priv_, mmbuffer_t *mb, int nbuf)
{
	struct xdiff_emit_state *priv = priv_;
	int i;
	int stop = 0;

	if (!priv->line_fn)
		return 0;

	for (i = 0; i < nbuf; i++) {
		if (stop)
			return 1;
		if (mb[i].ptr[mb[i].size-1] != '\n') {
			/* Incomplete line */
			strbuf_add(&priv->remainder, mb[i].ptr, mb[i].size);
			continue;
		}

		/* we have a complete line */
		if (!priv->remainder.len) {
			stop = consume_one(priv, mb[i].ptr, mb[i].size);
			continue;
		}
		strbuf_add(&priv->remainder, mb[i].ptr, mb[i].size);
		stop = consume_one(priv, priv->remainder.buf, priv->remainder.len);
		strbuf_reset(&priv->remainder);
	}
	if (stop)
		return -1;
	if (priv->remainder.len) {
		stop = consume_one(priv, priv->remainder.buf, priv->remainder.len);
		strbuf_reset(&priv->remainder);
	}
	if (stop)
		return -1;
	return 0;
}

/*
 * Trim down common substring at the end of the buffers,
 * but end on a complete line.
 */
static void trim_common_tail(mmfile_t *a, mmfile_t *b)
{
	const int blk = 1024;
	long trimmed = 0, recovered = 0;
	char *ap = a->size ? a->ptr + a->size : a->ptr;
	char *bp = b->size ? b->ptr + b->size : b->ptr;
	long smaller = (a->size < b->size) ? a->size : b->size;

	while (blk + trimmed <= smaller && !memcmp(ap - blk, bp - blk, blk)) {
		trimmed += blk;
		ap -= blk;
		bp -= blk;
	}

	while (recovered < trimmed)
		if (ap[recovered++] == '\n')
			break;
	a->size -= trimmed - recovered;
	b->size -= trimmed - recovered;
}

int xdi_diff(mmfile_t *mf1, mmfile_t *mf2, xpparam_t const *xpp, xdemitconf_t const *xecfg, xdemitcb_t *xecb)
{
	mmfile_t a = *mf1;
	mmfile_t b = *mf2;

	if (mf1->size > MAX_XDIFF_SIZE || mf2->size > MAX_XDIFF_SIZE)
		return -1;

	if (!xecfg->ctxlen && !(xecfg->flags & XDL_EMIT_FUNCCONTEXT))
		trim_common_tail(&a, &b);

	return xdl_diff(&a, &b, xpp, xecfg, xecb);
}

int xdi_diff_outf(mmfile_t *mf1, mmfile_t *mf2,
		  xdiff_emit_hunk_fn hunk_fn,
		  xdiff_emit_line_fn line_fn,
		  void *consume_callback_data,
		  xpparam_t const *xpp, xdemitconf_t const *xecfg)
{
	int ret;
	struct xdiff_emit_state state;
	xdemitcb_t ecb;

	memset(&state, 0, sizeof(state));
	state.hunk_fn = hunk_fn;
	state.line_fn = line_fn;
	state.consume_callback_data = consume_callback_data;
	memset(&ecb, 0, sizeof(ecb));
	if (hunk_fn)
		ecb.out_hunk = xdiff_out_hunk;
	ecb.out_line = xdiff_outf;
	ecb.priv = &state;
	strbuf_init(&state.remainder, 0);
	ret = xdi_diff(mf1, mf2, xpp, xecfg, &ecb);
	strbuf_release(&state.remainder);
	return ret;
}

int read_mmfile(mmfile_t *ptr, const char *filename)
{
	struct stat st;
	FILE *f;
	size_t sz;

	if (stat(filename, &st))
		return error_errno("Could not stat %s", filename);
	if (!(f = fopen(filename, "rb")))
		return error_errno("Could not open %s", filename);
	sz = xsize_t(st.st_size);
	ptr->ptr = xmalloc(sz ? sz : 1);
	if (sz && fread(ptr->ptr, sz, 1, f) != 1) {
		fclose(f);
		return error("Could not read %s", filename);
	}
	fclose(f);
	ptr->size = sz;
	return 0;
}

void read_mmblob(mmfile_t *ptr, const struct object_id *oid)
{
	unsigned long size;
	enum object_type type;

	if (oideq(oid, null_oid())) {
		ptr->ptr = xstrdup("");
		ptr->size = 0;
		return;
	}

	ptr->ptr = repo_read_object_file(the_repository, oid, &type, &size);
	if (!ptr->ptr || type != OBJ_BLOB)
		die("unable to read blob object %s", oid_to_hex(oid));
	ptr->size = size;
}

#define FIRST_FEW_BYTES 8000
int buffer_is_binary(const char *ptr, unsigned long size)
{
	if (FIRST_FEW_BYTES < size)
		size = FIRST_FEW_BYTES;
	return !!memchr(ptr, 0, size);
}

struct ff_regs {
	int nr;
	struct ff_reg {
		regex_t re;
		int negate;
	} *array;
};

static long ff_regexp(const char *line, long len,
		char *buffer, long buffer_size, void *priv)
{
	struct ff_regs *regs = priv;
	regmatch_t pmatch[2];
	int i;
	int result;

	/* Exclude terminating newline (and cr) from matching */
	if (len > 0 && line[len-1] == '\n') {
		if (len > 1 && line[len-2] == '\r')
			len -= 2;
		else
			len--;
	}

	for (i = 0; i < regs->nr; i++) {
		struct ff_reg *reg = regs->array + i;
		if (!regexec_buf(&reg->re, line, len, 2, pmatch, 0)) {
			if (reg->negate)
				return -1;
			break;
		}
	}
	if (regs->nr <= i)
		return -1;
	i = pmatch[1].rm_so >= 0 ? 1 : 0;
	line += pmatch[i].rm_so;
	result = pmatch[i].rm_eo - pmatch[i].rm_so;
	if (result > buffer_size)
		result = buffer_size;
	while (result > 0 && (isspace(line[result - 1])))
		result--;
	memcpy(buffer, line, result);
	return result;
}

void xdiff_set_find_func(xdemitconf_t *xecfg, const char *value, int cflags)
{
	int i;
	struct ff_regs *regs;

	xecfg->find_func = ff_regexp;
	regs = xecfg->find_func_priv = xmalloc(sizeof(struct ff_regs));
	for (i = 0, regs->nr = 1; value[i]; i++)
		if (value[i] == '\n')
			regs->nr++;
	ALLOC_ARRAY(regs->array, regs->nr);
	for (i = 0; i < regs->nr; i++) {
		struct ff_reg *reg = regs->array + i;
		const char *ep, *expression;
		char *buffer = NULL;

		if (!value)
			BUG("mismatch between line count and parsing");
		ep = strchr(value, '\n');

		reg->negate = (*value == '!');
		if (reg->negate && i == regs->nr - 1)
			die("Last expression must not be negated: %s", value);
		if (*value == '!')
			value++;
		if (ep)
			expression = buffer = xstrndup(value, ep - value);
		else
			expression = value;
		if (regcomp(&reg->re, expression, cflags))
			die("Invalid regexp to look for hunk header: %s", expression);
		free(buffer);
		value = ep ? ep + 1 : NULL;
	}
}

void xdiff_clear_find_func(xdemitconf_t *xecfg)
{
	if (xecfg->find_func) {
		int i;
		struct ff_regs *regs = xecfg->find_func_priv;

		for (i = 0; i < regs->nr; i++)
			regfree(&regs->array[i].re);
		free(regs->array);
		free(regs);
		xecfg->find_func = NULL;
		xecfg->find_func_priv = NULL;
	}
}

unsigned long xdiff_hash_string(const char *s, size_t len, long flags)
{
	return xdl_hash_record(&s, s + len, flags);
}

int xdiff_compare_lines(const char *l1, long s1,
			const char *l2, long s2, long flags)
{
	return xdl_recmatch(l1, s1, l2, s2, flags);
}

int parse_conflict_style_name(const char *value)
{
	if (!strcmp(value, "diff3"))
		return XDL_MERGE_DIFF3;
	else if (!strcmp(value, "zdiff3"))
		return XDL_MERGE_ZEALOUS_DIFF3;
	else if (!strcmp(value, "merge"))
		return 0;
	/*
	 * Please update _git_checkout() in git-completion.bash when
	 * you add new merge config
	 */
	else
		return -1;
}

int git_xmerge_style = -1;

int git_xmerge_config(const char *var, const char *value,
		      const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "merge.conflictstyle")) {
		if (!value)
			return config_error_nonbool(var);
		git_xmerge_style = parse_conflict_style_name(value);
		if (git_xmerge_style == -1)
			return error(_("unknown style '%s' given for '%s'"),
				     value, var);
		return 0;
	}
	return git_default_config(var, value, ctx, cb);
}
