#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "gettext.h"
#include "strbuf.h"
#include "hex.h"
#include "repository.h"
#include "hash.h"
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

	oidread(oid, (const unsigned char *)*path + *len, algo);
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
	struct strbuf payload = STRBUF_INIT, oursig = STRBUF_INIT, othersig = STRBUF_INIT;
	const int entry_len = from->hexsz + 7;
	size_t payload_size;
	struct object_id oid, mapped_oid;
	const char *p;

	/* Consume the object line */
	if ((entry_len >= size) ||
	    memcmp(buffer, "object ", 7) || buffer[entry_len] != '\n')
		return error("bogus tag object");
	if (parse_oid_hex_algop(buffer + 7, &oid, &p, from) < 0)
		return error("bad tag object ID");
	if (repo_oid_to_algop(the_repository, &oid, to, &mapped_oid))
		return error("unable to map tree %s in tag object",
			     oid_to_hex(&oid));
	size -= ((p + 1) - buffer);
	buffer = p + 1;

	/* Is there a signature for our algorithm? */
	payload_size = parse_signed_buffer(buffer, size);
	if (payload_size != size) {
		/* Yes, there is. */
		strbuf_add(&oursig, buffer + payload_size, size - payload_size);
	}

	/* Now, is there a signature for the other algorithm? */
	parse_buffer_signed_by_header(buffer, payload_size, &payload, &othersig, to);
	/*
	 * Our payload is now in payload and we may have up to two signatrures
	 * in oursig and othersig.
	 */

	/* Add some slop for longer signature header in the new algorithm. */
	strbuf_grow(out, (7 + to->hexsz + 1) + size + 7);
	strbuf_addf(out, "object %s\n", oid_to_hex(&mapped_oid));
	strbuf_addbuf(out, &payload);
	if (oursig.len)
		add_header_signature(out, &oursig, from);
	strbuf_addbuf(out, &othersig);

	strbuf_release(&payload);
	strbuf_release(&othersig);
	strbuf_release(&oursig);
	return 0;
}

static int convert_commit_object(struct strbuf *out,
				 const struct git_hash_algo *from,
				 const struct git_hash_algo *to,
				 const char *buffer, size_t size)
{
	const char *tail = buffer;
	const char *bufptr = buffer;
	const int tree_entry_len = from->hexsz + 5;
	const int parent_entry_len = from->hexsz + 7;
	struct object_id oid, mapped_oid;
	const char *p, *eol;

	tail += size;

	while ((bufptr < tail) && (*bufptr != '\n')) {
		eol = memchr(bufptr, '\n', tail - bufptr);
		if (!eol)
			return error(_("bad %s in commit"), "line");

		if (((bufptr + 5) < eol) && !memcmp(bufptr, "tree ", 5))
		{
			if (((bufptr + tree_entry_len) != eol) ||
			    parse_oid_hex_algop(bufptr + 5, &oid, &p, from) ||
			    (p != eol))
				return error(_("bad %s in commit"), "tree");

			if (repo_oid_to_algop(the_repository, &oid, to, &mapped_oid))
				return error(_("unable to map %s %s in commit object"),
					     "tree", oid_to_hex(&oid));
			strbuf_addf(out, "tree %s\n", oid_to_hex(&mapped_oid));
		}
		else if (((bufptr + 7) < eol) && !memcmp(bufptr, "parent ", 7))
		{
			if (((bufptr + parent_entry_len) != eol) ||
			    parse_oid_hex_algop(bufptr + 7, &oid, &p, from) ||
			    (p != eol))
				return error(_("bad %s in commit"), "parent");

			if (repo_oid_to_algop(the_repository, &oid, to, &mapped_oid))
				return error(_("unable to map %s %s in commit object"),
					     "parent", oid_to_hex(&oid));

			strbuf_addf(out, "parent %s\n", oid_to_hex(&mapped_oid));
		}
		else if (((bufptr + 9) < eol) && !memcmp(bufptr, "mergetag ", 9))
		{
			struct strbuf tag = STRBUF_INIT, new_tag = STRBUF_INIT;

			/* Recover the tag object from the mergetag */
			strbuf_add(&tag, bufptr + 9, (eol - (bufptr + 9)) + 1);

			bufptr = eol + 1;
			while ((bufptr < tail) && (*bufptr == ' ')) {
				eol = memchr(bufptr, '\n', tail - bufptr);
				if (!eol) {
					strbuf_release(&tag);
					return error(_("bad %s in commit"), "mergetag continuation");
				}
				strbuf_add(&tag, bufptr + 1, (eol - (bufptr + 1)) + 1);
				bufptr = eol + 1;
			}

			/* Compute the new tag object */
			if (convert_tag_object(&new_tag, from, to, tag.buf, tag.len)) {
				strbuf_release(&tag);
				strbuf_release(&new_tag);
				return -1;
			}

			/* Write the new mergetag */
			strbuf_addstr(out, "mergetag");
			strbuf_add_lines(out, " ", new_tag.buf, new_tag.len);
			strbuf_release(&tag);
			strbuf_release(&new_tag);
		}
		else if (((bufptr + 7) < tail) && !memcmp(bufptr, "author ", 7))
			strbuf_add(out, bufptr, (eol - bufptr) + 1);
		else if (((bufptr + 10) < tail) && !memcmp(bufptr, "committer ", 10))
			strbuf_add(out, bufptr, (eol - bufptr) + 1);
		else if (((bufptr + 9) < tail) && !memcmp(bufptr, "encoding ", 9))
			strbuf_add(out, bufptr, (eol - bufptr) + 1);
		else if (((bufptr + 6) < tail) && !memcmp(bufptr, "gpgsig", 6))
			strbuf_add(out, bufptr, (eol - bufptr) + 1);
		else {
			/* Unknown line fail it might embed an oid */
			return -1;
		}
		/* Consume any trailing continuation lines */
		bufptr = eol + 1;
		while ((bufptr < tail) && (*bufptr == ' ')) {
			eol = memchr(bufptr, '\n', tail - bufptr);
			if (!eol)
				return error(_("bad %s in commit"), "continuation");
			strbuf_add(out, bufptr, (eol - bufptr) + 1);
			bufptr = eol + 1;
		}
	}
	if (bufptr < tail)
		strbuf_add(out, bufptr, tail - bufptr);
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
	case OBJ_COMMIT:
		ret = convert_commit_object(outbuf, from, to, buf, len);
		break;
	case OBJ_TREE:
		ret = convert_tree_object(outbuf, from, to, buf, len);
		break;
	case OBJ_TAG:
		ret = convert_tag_object(outbuf, from, to, buf, len);
		break;
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
