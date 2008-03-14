#include "cache.h"
#include "xdiff-interface.h"

static int parse_num(char **cp_p, int *num_p)
{
	char *cp = *cp_p;
	int num = 0;
	int read_some;

	while ('0' <= *cp && *cp <= '9')
		num = num * 10 + *cp++ - '0';
	if (!(read_some = cp - *cp_p))
		return -1;
	*cp_p = cp;
	*num_p = num;
	return 0;
}

int parse_hunk_header(char *line, int len,
		      int *ob, int *on,
		      int *nb, int *nn)
{
	char *cp;
	cp = line + 4;
	if (parse_num(&cp, ob)) {
	bad_line:
		return error("malformed diff output: %s", line);
	}
	if (*cp == ',') {
		cp++;
		if (parse_num(&cp, on))
			goto bad_line;
	}
	else
		*on = 1;
	if (*cp++ != ' ' || *cp++ != '+')
		goto bad_line;
	if (parse_num(&cp, nb))
		goto bad_line;
	if (*cp == ',') {
		cp++;
		if (parse_num(&cp, nn))
			goto bad_line;
	}
	else
		*nn = 1;
	return -!!memcmp(cp, " @@", 3);
}

static void consume_one(void *priv_, char *s, unsigned long size)
{
	struct xdiff_emit_state *priv = priv_;
	char *ep;
	while (size) {
		unsigned long this_size;
		ep = memchr(s, '\n', size);
		this_size = (ep == NULL) ? size : (ep - s + 1);
		priv->consume(priv, s, this_size);
		size -= this_size;
		s += this_size;
	}
}

int xdiff_outf(void *priv_, mmbuffer_t *mb, int nbuf)
{
	struct xdiff_emit_state *priv = priv_;
	int i;

	for (i = 0; i < nbuf; i++) {
		if (mb[i].ptr[mb[i].size-1] != '\n') {
			/* Incomplete line */
			priv->remainder = xrealloc(priv->remainder,
						   priv->remainder_size +
						   mb[i].size);
			memcpy(priv->remainder + priv->remainder_size,
			       mb[i].ptr, mb[i].size);
			priv->remainder_size += mb[i].size;
			continue;
		}

		/* we have a complete line */
		if (!priv->remainder) {
			consume_one(priv, mb[i].ptr, mb[i].size);
			continue;
		}
		priv->remainder = xrealloc(priv->remainder,
					   priv->remainder_size +
					   mb[i].size);
		memcpy(priv->remainder + priv->remainder_size,
		       mb[i].ptr, mb[i].size);
		consume_one(priv, priv->remainder,
			    priv->remainder_size + mb[i].size);
		free(priv->remainder);
		priv->remainder = NULL;
		priv->remainder_size = 0;
	}
	if (priv->remainder) {
		consume_one(priv, priv->remainder, priv->remainder_size);
		free(priv->remainder);
		priv->remainder = NULL;
		priv->remainder_size = 0;
	}
	return 0;
}

/*
 * Trim down common substring at the end of the buffers,
 * but leave at least ctx lines at the end.
 */
static void trim_common_tail(mmfile_t *a, mmfile_t *b, long ctx)
{
	const int blk = 1024;
	long trimmed = 0, recovered = 0;
	char *ap = a->ptr + a->size;
	char *bp = b->ptr + b->size;
	long smaller = (a->size < b->size) ? a->size : b->size;

	if (ctx)
		return;

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

	trim_common_tail(&a, &b, xecfg->ctxlen);

	return xdl_diff(&a, &b, xpp, xecfg, xecb);
}

int read_mmfile(mmfile_t *ptr, const char *filename)
{
	struct stat st;
	FILE *f;
	size_t sz;

	if (stat(filename, &st))
		return error("Could not stat %s", filename);
	if ((f = fopen(filename, "rb")) == NULL)
		return error("Could not open %s", filename);
	sz = xsize_t(st.st_size);
	ptr->ptr = xmalloc(sz ? sz : 1);
	if (sz && fread(ptr->ptr, sz, 1, f) != 1)
		return error("Could not read %s", filename);
	fclose(f);
	ptr->size = sz;
	return 0;
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
	char *line_buffer = xstrndup(line, len); /* make NUL terminated */
	struct ff_regs *regs = priv;
	regmatch_t pmatch[2];
	int result = 0, i;

	for (i = 0; i < regs->nr; i++) {
		struct ff_reg *reg = regs->array + i;
		if (reg->negate ^ !!regexec(&reg->re,
					line_buffer, 2, pmatch, 0)) {
			free(line_buffer);
			return -1;
		}
	}
	i = pmatch[1].rm_so >= 0 ? 1 : 0;
	line += pmatch[i].rm_so;
	result = pmatch[i].rm_eo - pmatch[i].rm_so;
	if (result > buffer_size)
		result = buffer_size;
	else
		while (result > 0 && (isspace(line[result - 1]) ||
					line[result - 1] == '\n'))
			result--;
	memcpy(buffer, line, result);
	free(line_buffer);
	return result;
}

void xdiff_set_find_func(xdemitconf_t *xecfg, const char *value)
{
	int i;
	struct ff_regs *regs;

	xecfg->find_func = ff_regexp;
	regs = xecfg->find_func_priv = xmalloc(sizeof(struct ff_regs));
	for (i = 0, regs->nr = 1; value[i]; i++)
		if (value[i] == '\n')
			regs->nr++;
	regs->array = xmalloc(regs->nr * sizeof(struct ff_reg));
	for (i = 0; i < regs->nr; i++) {
		struct ff_reg *reg = regs->array + i;
		const char *ep = strchr(value, '\n'), *expression;
		char *buffer = NULL;

		reg->negate = (*value == '!');
		if (reg->negate && i == regs->nr - 1)
			die("Last expression must not be negated: %s", value);
		if (*value == '!')
			value++;
		if (ep)
			expression = buffer = xstrndup(value, ep - value);
		else
			expression = value;
		if (regcomp(&reg->re, expression, 0))
			die("Invalid regexp to look for hunk header: %s", expression);
		free(buffer);
		value = ep + 1;
	}
}
