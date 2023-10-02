#include "git-compat-util.h"
#include "gettext.h"
#include "strbuf.h"
#include "hex.h"
#include "repository.h"
#include "hash-ll.h"
#include "hash.h"
#include "object.h"
#include "loose.h"
#include "commit.h"
#include "gpg-interface.h"
#include "object-file-convert.h"

int repo_oid_to_algop(struct repository *repo, const struct object_id *src,
		      const struct git_hash_algo *to, struct object_id *dest)
{
	/*
	 * If the source algorithm is not set, then we're using the
	 * default hash algorithm for that object.
	 */
	const struct git_hash_algo *from =
		src->algo ? &hash_algos[src->algo] : repo->hash_algo;

	if (from == to) {
		if (src != dest)
			oidcpy(dest, src);
		return 0;
	}
	if (repo_loose_object_map_oid(repo, src, to, dest)) {
		/*
		 * We may have loaded the object map at repo initialization but
		 * another process (perhaps upstream of a pipe from us) may have
		 * written a new object into the map.  If the object is missing,
		 * let's reload the map to see if the object has appeared.
		 */
		repo_read_loose_object_map(repo);
		if (repo_loose_object_map_oid(repo, src, to, dest))
			return -1;
	}
	return 0;
}

static int decode_tree_entry_raw(struct object_id *oid, const char **path,
				 size_t *len, const struct git_hash_algo *algo,
				 const char *buf, unsigned long size)
{
	uint16_t mode;
	const unsigned hashsz = algo->rawsz;

	if (size < hashsz + 3 || buf[size - (hashsz + 1)]) {
		return -1;
	}

	*path = parse_mode(buf, &mode);
	if (!*path || !**path)
		return -1;
	*len = strlen(*path) + 1;

	oidread_algop(oid, (const unsigned char *)*path + *len, algo);
	return 0;
}

static int convert_tree_object(struct strbuf *out,
			       const struct git_hash_algo *from,
			       const struct git_hash_algo *to,
			       const char *buffer, size_t size)
{
	const char *p = buffer, *end = buffer + size;

	while (p < end) {
		struct object_id entry_oid, mapped_oid;
		const char *path = NULL;
		size_t pathlen;

		if (decode_tree_entry_raw(&entry_oid, &path, &pathlen, from, p,
					  end - p))
			return error(_("failed to decode tree entry"));
		if (repo_oid_to_algop(the_repository, &entry_oid, to, &mapped_oid))
			return error(_("failed to map tree entry for %s"), oid_to_hex(&entry_oid));
		strbuf_add(out, p, path - p);
		strbuf_add(out, path, pathlen);
		strbuf_add(out, mapped_oid.hash, to->rawsz);
		p = path + pathlen + from->rawsz;
	}
	return 0;
}

static int convert_tag_object(struct strbuf *out,
			      const struct git_hash_algo *from,
			      const struct git_hash_algo *to,
			      const char *buffer, size_t size)
{
	struct strbuf payload = STRBUF_INIT, temp = STRBUF_INIT, oursig = STRBUF_INIT, othersig = STRBUF_INIT;
	size_t payload_size;
	struct object_id oid, mapped_oid;
	const char *p;

	/* Add some slop for longer signature header in the new algorithm. */
	strbuf_grow(out, size + 7);

	/* Is there a signature for our algorithm? */
	payload_size = parse_signed_buffer(buffer, size);
	strbuf_add(&payload, buffer, payload_size);
	if (payload_size != size) {
		/* Yes, there is. */
		strbuf_add(&oursig, buffer + payload_size, size - payload_size);
	}
	/* Now, is there a signature for the other algorithm? */
	if (parse_buffer_signed_by_header(payload.buf, payload.len, &temp, &othersig, to)) {
		/* Yes, there is. */
		strbuf_swap(&payload, &temp);
		strbuf_release(&temp);
	}

	/*
	 * Our payload is now in payload and we may have up to two signatrures
	 * in oursig and othersig.
	 */
	if (strncmp(payload.buf, "object ", 7) || payload.buf[from->hexsz + 7] != '\n')
		return error("bogus tag object");
	if (parse_oid_hex_algop(payload.buf + 7, &oid, &p, from) < 0)
		return error("bad tag object ID");
	if (repo_oid_to_algop(the_repository, &oid, to, &mapped_oid))
		return error("unable to map tree %s in tag object",
			     oid_to_hex(&oid));
	strbuf_addf(out, "object %s", oid_to_hex(&mapped_oid));
	strbuf_add(out, p, payload.len - (p - payload.buf));
	strbuf_addbuf(out, &othersig);
	if (oursig.len)
		add_header_signature(out, &oursig, from);
	return 0;
}

int convert_object_file(struct strbuf *outbuf,
			const struct git_hash_algo *from,
			const struct git_hash_algo *to,
			const void *buf, size_t len,
			enum object_type type,
			int gentle)
{
	int ret;

	/* Don't call this function when no conversion is necessary */
	if ((from == to) || (type == OBJ_BLOB))
		BUG("Refusing noop object file conversion");

	switch (type) {
	case OBJ_TREE:
		ret = convert_tree_object(outbuf, from, to, buf, len);
		break;
	case OBJ_TAG:
		ret = convert_tag_object(outbuf, from, to, buf, len);
		break;
	case OBJ_COMMIT:
	default:
		/* Not implemented yet, so fail. */
		ret = -1;
		break;
	}
	if (!ret)
		return 0;
	if (gentle) {
		strbuf_release(outbuf);
		return ret;
	}
	die(_("Failed to convert object from %s to %s"),
		from->name, to->name);
}
