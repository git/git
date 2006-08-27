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
