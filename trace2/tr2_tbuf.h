#ifndef TR2_TBUF_H
#define TR2_TBUF_H

/*
 * A simple wrapper around a fixed buffer to avoid C syntax
 * quirks and the need to pass around an additional size_t
 * argument.
 */
struct tr2_tbuf {
	char buf[32];
};

/*
 * Fill buffer with formatted local time string.
 */
void tr2_tbuf_local_time(struct tr2_tbuf *tb);

/*
 * Fill buffer with formatted UTC time string.
 */
void tr2_tbuf_utc_time(struct tr2_tbuf *tb);

#endif /* TR2_TBUF_H */
