#include "git-compat-util.h"
#include "gettext.h"
#include "strbuf.h"
#include "repository.h"
#include "hash-ll.h"
#include "object.h"
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
	return -1;
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
	case OBJ_TREE:
	case OBJ_TAG:
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
