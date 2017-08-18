#include "cache.h"

char *odb_pack_name(struct strbuf *buf,
		    const unsigned char *sha1,
		    const char *ext)
{
	strbuf_reset(buf);
	strbuf_addf(buf, "%s/pack/pack-%s.%s", get_object_directory(),
		    sha1_to_hex(sha1), ext);
	return buf->buf;
}

char *sha1_pack_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "pack");
}

char *sha1_pack_index_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "idx");
}
