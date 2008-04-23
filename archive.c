#include "cache.h"
#include "commit.h"
#include "attr.h"

static void format_subst(const struct commit *commit,
                         const char *src, size_t len,
                         struct strbuf *buf)
{
	char *to_free = NULL;
	struct strbuf fmt;

	if (src == buf->buf)
		to_free = strbuf_detach(buf, NULL);
	strbuf_init(&fmt, 0);
	for (;;) {
		const char *b, *c;

		b = memmem(src, len, "$Format:", 8);
		if (!b)
			break;
		c = memchr(b + 8, '$', (src + len) - b - 8);
		if (!c)
			break;

		strbuf_reset(&fmt);
		strbuf_add(&fmt, b + 8, c - b - 8);

		strbuf_add(buf, src, b - src);
		format_commit_message(commit, fmt.buf, buf);
		len -= c + 1 - src;
		src  = c + 1;
	}
	strbuf_add(buf, src, len);
	strbuf_release(&fmt);
	free(to_free);
}

static int convert_to_archive(const char *path,
                              const void *src, size_t len,
                              struct strbuf *buf,
                              const struct commit *commit)
{
	static struct git_attr *attr_export_subst;
	struct git_attr_check check[1];

	if (!commit)
		return 0;

	if (!attr_export_subst)
		attr_export_subst = git_attr("export-subst", 12);

	check[0].attr = attr_export_subst;
	if (git_checkattr(path, ARRAY_SIZE(check), check))
		return 0;
	if (!ATTR_TRUE(check[0].value))
		return 0;

	format_subst(commit, src, len, buf);
	return 1;
}

void *sha1_file_to_archive(const char *path, const unsigned char *sha1,
                           unsigned int mode, enum object_type *type,
                           unsigned long *sizep,
                           const struct commit *commit)
{
	void *buffer;

	buffer = read_sha1_file(sha1, type, sizep);
	if (buffer && S_ISREG(mode)) {
		struct strbuf buf;
		size_t size = 0;

		strbuf_init(&buf, 0);
		strbuf_attach(&buf, buffer, *sizep, *sizep + 1);
		convert_to_working_tree(path, buf.buf, buf.len, &buf);
		convert_to_archive(path, buf.buf, buf.len, &buf, commit);
		buffer = strbuf_detach(&buf, &size);
		*sizep = size;
	}

	return buffer;
}

