#ifndef TRAILER_H
#define TRAILER_H

void process_trailers(const char *file, int trim_empty, struct string_list *trailers);

struct trailer_parse_context {
	struct strbuf **lines;
	int start;
	int end;

	/* These fields are private to the parser. */
	struct strbuf token;
	struct strbuf value;
};

/*
 * Parse the commit message found in "buf", looking for trailers. Any data in
 * ctx is overwritten, and should later be freed with trailer_parse_clear().
 *
 * The caller can iterate over all trailers using the "start" and "end" indices
 * into "lines".
 */
void trailer_parse_init(struct trailer_parse_context *ctx, const struct strbuf *buf);

/*
 * If the line contains a trailer with key "trailer", returns a pointer into
 * "line" for the value. Otherwise, returns NULL.
 */
const char *trailer_parse_match(struct trailer_parse_context *ctx, int line,
				const char *trailer);

/*
 * Free resources allocated by trailer_parse_init().
 */
void trailer_parse_clear(struct trailer_parse_context *ctx);

#endif /* TRAILER_H */
