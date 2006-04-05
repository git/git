#include "cache.h"
#include "xdiff-interface.h"

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
			priv->remainder = realloc(priv->remainder,
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
		priv->remainder = realloc(priv->remainder,
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
