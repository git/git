#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "gettext.h"
#include "strbuf.h"
#include "hex.h"
#include "repository.h"
#include "hash.h"
#include "object.h"
#include "loose.h"
#include "commit.h"
#include "gpg-interface.h"
#include "object-file-convert.h"
#include "odb.h"
#include "odb/source-files.h"
#include "odb/source-packed.h"
#include "pack-compat-names.h"
#include "packfile-list.h"
#include "packfile.h"
#include "object-file.h"

static int derive_oid(struct repository *repo, const struct object_id *oid,
		      const struct git_hash_algo *from,
		      const struct git_hash_algo *to, struct object_id *dest);

/*
 * Find the compatibility object name of a packed object, or the packed object
 * with a given compatibility object name.
 */
static int packed_object_map_oid(struct repository *repo,
				 const struct object_id *src,
				 const struct git_hash_algo *to,
				 struct object_id *dest)
{
	struct odb_source *source;

	/*
	 * The packs of a source may not have been read yet; this can be the
	 * first time we are asked to name an object.
	 */
	odb_prepare(repo->objects, 0);

	for (source = repo->objects->sources; source; source = source->next) {
		struct odb_source_files *files = odb_source_files_downcast(source);
		struct packfile_list_entry *entry;

		for (entry = files->packed->packs.head; entry;
		     entry = entry->next) {
			struct packed_git *p = entry->pack;
			struct object_id compat_oid;
			uint32_t nr;

			if (open_pack_index(p))
				continue;

			if (to != repo->compat_hash_algo) {
				/* Look the object up by its compat name. */
				if (!packed_object_by_compat_oid(p, src, &nr) &&
				    !nth_packed_object_id(dest, p, nr)) {
					dest->algo = hash_algo_by_ptr(repo->hash_algo);
					return 0;
				}
				continue;
			}

			if (bsearch_pack(src, p, &nr) &&
			    !packed_object_compat_oid(p, nr, &compat_oid)) {
				oidcpy(dest, &compat_oid);
				dest->algo = hash_algo_by_ptr(to);
				return 0;
			}
		}
	}
	return -1;
}

int repo_oid_to_algop(struct repository *repo, const struct object_id *srcoid,
		      const struct git_hash_algo *to, struct object_id *dest)
{
	/*
	 * If the source algorithm is not set, then we're using the
	 * default hash algorithm for that object.
	 */
	const struct git_hash_algo *from =
		srcoid->algo ? &hash_algos[srcoid->algo] : repo->hash_algo;
	struct object_id temp;
	const struct object_id *src = srcoid;

	if (!srcoid->algo) {
		oidcpy(&temp, srcoid);
		temp.algo = hash_algo_by_ptr(repo->hash_algo);
		src = &temp;
	}

	if (from == to || !to) {
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
		if (!repo_loose_object_map_oid(repo, src, to, dest))
			return 0;
		if (!packed_object_map_oid(repo, src, to, dest))
			return 0;
		/*
		 * Objects that never made it into the map can still be named:
		 * deriving the name from the content is cheap compared to the
		 * trouble it causes to lose track of an object entirely.  We
		 * only learn it now, so we also remember it for later.
		 */
		return derive_oid(repo, src, from, to, dest);
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

static int convert_tree_object(struct repository *repo,
			       struct strbuf *out,
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
		if (repo_oid_to_algop(repo, &entry_oid, to, &mapped_oid))
			return error(_("failed to map tree entry for %s"), oid_to_hex(&entry_oid));
		strbuf_add(out, p, path - p);
		strbuf_add(out, path, pathlen);
		strbuf_add(out, mapped_oid.hash, to->rawsz);
		p = path + pathlen + from->rawsz;
	}
	return 0;
}

static int convert_tag_object(struct repository *repo,
			      struct strbuf *out,
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
	if (repo_oid_to_algop(repo, &oid, to, &mapped_oid))
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

static int convert_commit_object(struct repository *repo,
				 struct strbuf *out,
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

			if (repo_oid_to_algop(repo, &oid, to, &mapped_oid))
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

			if (repo_oid_to_algop(repo, &oid, to, &mapped_oid))
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
			if (convert_tag_object(repo, &new_tag, from, to, tag.buf, tag.len)) {
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

int convert_object_file(struct repository *repo,
			struct strbuf *outbuf,
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
		ret = convert_commit_object(repo, outbuf, from, to, buf, len);
		break;
	case OBJ_TREE:
		ret = convert_tree_object(repo, outbuf, from, to, buf, len);
		break;
	case OBJ_TAG:
		ret = convert_tag_object(repo, outbuf, from, to, buf, len);
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

/*
 * An object that is being named in one hash algorithm while it is stored in
 * another.  Only objects of a single type are kept on the stack at a time.
 */
struct derive_frame {
	struct object_id oid;
	enum object_type type;
	void *content;
	size_t size;
	struct object_id *deps;
	size_t nr_deps;
	size_t deps_alloc;
	size_t dep_next;
};

static void derive_frame_clear(struct derive_frame *frame)
{
	free(frame->content);
	free(frame->deps);
	memset(frame, 0, sizeof(*frame));
}

static void derive_push(struct derive_frame **stack, size_t *nr,
			size_t *alloc, const struct object_id *oid)
{
	if (*nr >= *alloc) {
		*alloc = *alloc ? *alloc * 2 : 8;
		REALLOC_ARRAY(*stack, *alloc);
	}
	memset(&(*stack)[*nr], 0, sizeof((*stack)[*nr]));
	oidcpy(&(*stack)[(*nr)++].oid, oid);
}

static int derive_frame_add_dep(struct derive_frame *frame,
				const struct object_id *oid)
{
	if (frame->nr_deps >= frame->deps_alloc) {
		frame->deps_alloc = frame->deps_alloc ?
				     frame->deps_alloc * 2 : 8;
		REALLOC_ARRAY(frame->deps, frame->deps_alloc);
	}
	oidcpy(&frame->deps[frame->nr_deps++], oid);
	return 0;
}

/*
 * Collect the objects referenced by `frame`, whose content is in the `from`
 * algorithm.  These have to be named in the `to` algorithm before we can
 * translate the content of `frame` itself.
 */
static int derive_frame_collect_deps(struct derive_frame *frame,
				     const struct git_hash_algo *from)
{
	const char *buf = frame->content, *end = buf + frame->size;
	const char *p = buf;

	if (frame->type == OBJ_TREE) {
		while (p < end) {
			struct object_id entry;
			const char *path;
			size_t pathlen;

			if (decode_tree_entry_raw(&entry, &path, &pathlen,
						  from, p, end - p))
				return error(_("failed to decode tree entry"));
			derive_frame_add_dep(frame, &entry);
			p = path + pathlen + from->rawsz;
		}
		return 0;
	}

	if (frame->type == OBJ_TAG) {
		struct object_id tagged;
		const char *eol;

		if (end - p < 7 || memcmp(p, "object ", 7))
			return error("bogus tag object");
		eol = memchr(p, '\n', end - p);
		if (!eol)
			return error("bad tag object ID");
		if (parse_oid_hex_algop(p + 7, &tagged, &p, from) || p != eol)
			return error("bad tag object ID");
		return derive_frame_add_dep(frame, &tagged);
	}

	if (frame->type == OBJ_COMMIT) {
		const size_t tree_entry_len = from->hexsz + 5;
		const size_t parent_entry_len = from->hexsz + 7;

		while (p < end) {
			const char *eol = memchr(p, '\n', end - p);

			if (!eol)
				return error(_("bad %s in commit"), "line");
			if (eol - p >= tree_entry_len &&
			    !memcmp(p, "tree ", 5)) {
				struct object_id tree;
				if ((eol - p) != tree_entry_len ||
				    parse_oid_hex_algop(p + 5, &tree, &p, from) ||
				    p != eol)
					return error(_("bad %s in commit"), "tree");
				derive_frame_add_dep(frame, &tree);
			} else if (eol - p >= parent_entry_len &&
				   !memcmp(p, "parent ", 7)) {
				struct object_id parent;
				if ((eol - p) != parent_entry_len ||
				    parse_oid_hex_algop(p + 7, &parent, &p, from) ||
				    p != eol)
					return error(_("bad %s in commit"), "parent");
				derive_frame_add_dep(frame, &parent);
			}
			p = eol + 1;
		}
		return 0;
	}

	/* Blobs reference nothing. */
	return 0;
}

/*
 * Derive the name that `oid` has under the `to` algorithm by translating the
 * content of the object, and cache the mapping in memory so that translating
 * other objects that refer to it can succeed.
 *
 * Only the forward direction can be derived: the object name is a hash of the
 * content, so we cannot work backwards from a compatibility name to the name
 * the object is stored under.
 *
 * Returns 0 on success.
 */
static int derive_oid(struct repository *repo, const struct object_id *oid,
		      const struct git_hash_algo *from,
		      const struct git_hash_algo *to, struct object_id *dest)
{
	struct derive_frame *stack = NULL;
	size_t nr_stack = 0, stack_alloc = 0;
	int ret = -1;

	/*
	 * Only the forward direction can be derived: the name of an object is
	 * a hash of its content, so there is no way to work backwards from a
	 * compatibility name to the name the object is stored under.
	 */
	if (from != repo->hash_algo || to != repo->compat_hash_algo)
		return -1;

	derive_push(&stack, &nr_stack, &stack_alloc, oid);

	while (nr_stack) {
		struct derive_frame *frame = &stack[nr_stack - 1];
		struct object_id name;
		struct strbuf out = STRBUF_INIT;
		const void *content;
		size_t size;

		if (!frame->content) {
			frame->content = odb_read_object(repo->objects,
							 &frame->oid,
							 &frame->type,
							 &frame->size);
			if (!frame->content)
				goto out;
			if (derive_frame_collect_deps(frame, from))
				goto out;
		}

		/*
		 * Wait until every object we refer to can be named in the
		 * target algorithm.  Objects we have already derived are
		 * in the map by now, so this makes progress every round.
		 */
		if (frame->dep_next < frame->nr_deps) {
			struct object_id *dep = &frame->deps[frame->dep_next];
			struct object_id mapped;

			if (!repo_oid_to_algop(repo, dep, to, &mapped)) {
				frame->dep_next++;
				continue;
			}
			if (!odb_has_object(repo->objects, dep, 0)) {
				error(_("cannot name %s in %s"),
				      oid_to_hex(dep), to->name);
				goto out;
			}
			derive_push(&stack, &nr_stack, &stack_alloc, dep);
			continue;
		}

		if (frame->type != OBJ_BLOB) {
			if (convert_object_file(repo, &out, from, to,
						frame->content, frame->size,
						frame->type, 1))
				goto out;
			content = out.buf;
			size = out.len;
		} else {
			content = frame->content;
			size = frame->size;
		}
		hash_object_file(to, content, size, frame->type, &name);
		strbuf_release(&out);

		repo_insert_compat_object_map(repo, &frame->oid, &name);
		if (nr_stack == 1)
			oidcpy(dest, &name);

		derive_frame_clear(frame);
		nr_stack--;
	}
	ret = 0;
out:
	for (size_t i = 0; i < nr_stack; i++)
		derive_frame_clear(&stack[i]);
	free(stack);
	return ret;
}
