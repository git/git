#include "git-compat-util.h"
#include "repack.h"
#include "hash.h"
#include "hex.h"
#include "odb.h"
#include "oidset.h"
#include "pack-bitmap.h"
#include "refs.h"
#include "tempfile.h"

struct midx_snapshot_ref_data {
	struct repository *repo;
	struct tempfile *f;
	struct oidset seen;
	int preferred;
};

static int midx_snapshot_ref_one(const char *refname UNUSED,
				 const char *referent UNUSED,
				 const struct object_id *oid,
				 int flag UNUSED, void *_data)
{
	struct midx_snapshot_ref_data *data = _data;
	struct object_id peeled;

	if (!peel_iterated_oid(data->repo, oid, &peeled))
		oid = &peeled;

	if (oidset_insert(&data->seen, oid))
		return 0; /* already seen */

	if (odb_read_object_info(data->repo->objects, oid, NULL) != OBJ_COMMIT)
		return 0;

	fprintf(data->f->fp, "%s%s\n", data->preferred ? "+" : "",
		oid_to_hex(oid));

	return 0;
}

void midx_snapshot_refs(struct repository *repo, struct tempfile *f)
{
	struct midx_snapshot_ref_data data;
	const struct string_list *preferred = bitmap_preferred_tips(repo);

	data.repo = repo;
	data.f = f;
	data.preferred = 0;
	oidset_init(&data.seen, 0);

	if (!fdopen_tempfile(f, "w"))
		 die(_("could not open tempfile %s for writing"),
		     get_tempfile_path(f));

	if (preferred) {
		struct string_list_item *item;

		data.preferred = 1;
		for_each_string_list_item(item, preferred)
			refs_for_each_ref_in(get_main_ref_store(repo),
					     item->string,
					     midx_snapshot_ref_one, &data);
		data.preferred = 0;
	}

	refs_for_each_ref(get_main_ref_store(repo),
			  midx_snapshot_ref_one, &data);

	if (close_tempfile_gently(f)) {
		int save_errno = errno;
		delete_tempfile(&f);
		errno = save_errno;
		die_errno(_("could not close refs snapshot tempfile"));
	}

	oidset_clear(&data.seen);
}
