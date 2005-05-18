#ifndef STRBUF_H
#define STRBUF_H
struct strbuf {
	int alloc;
	int len;
	int eof;
	char *buf;
};

extern void strbuf_init(struct strbuf *);
extern void read_line(struct strbuf *, FILE *, int);

#endif /* STRBUF_H */
