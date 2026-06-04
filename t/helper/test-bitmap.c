#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "git-compat-util.h"
#include "hex.h"
#include "odb.h"
#include "pack-bitmap.h"
#include "pseudo-merge.h"
#include "setup.h"

static int bitmap_list_commits(void)
{
	return test_bitmap_commits(the_repository);
}

static int bitmap_list_commits_with_offset(void)
{
	return test_bitmap_commits_with_offset(the_repository);
}

static int bitmap_dump_hashes(void)
{
	return test_bitmap_hashes(the_repository);
}

static int bitmap_dump_pseudo_merges(void)
{
	return test_bitmap_pseudo_merges(the_repository);
}

static int bitmap_dump_pseudo_merge_commits(uint32_t n)
{
	return test_bitmap_pseudo_merge_commits(the_repository, n);
}

static int bitmap_dump_pseudo_merge_objects(uint32_t n)
{
	return test_bitmap_pseudo_merge_objects(the_repository, n);
}

static int add_packed_object(const struct object_id *oid,
			     struct packed_git *pack,
			     uint32_t pos,
			     void *_data)
{
	struct packing_data *packed = _data;
	struct object_entry *entry;
	struct object_info oi = OBJECT_INFO_INIT;
	enum object_type type;

	oi.typep = &type;

	entry = packlist_alloc(packed, oid);
	entry->idx.offset = nth_packed_object_offset(pack, pos);
	if (packed_object_info(pack, entry->idx.offset, &oi) < 0)
		die("could not get type of object %s",
		    oid_to_hex(oid));
	oe_set_type(entry, type);
	oe_set_in_pack(packed, entry, pack);

	return 0;
}

static int idx_oid_cmp(const void *va, const void *vb)
{
	const struct pack_idx_entry *a = *(const struct pack_idx_entry **)va;
	const struct pack_idx_entry *b = *(const struct pack_idx_entry **)vb;

	return oidcmp(&a->oid, &b->oid);
}

static int bitmap_write(const char *basename)
{
	struct packed_git *p = NULL;
	struct packing_data packed = { 0 };
	struct bitmap_writer writer;
	struct pack_idx_entry **index;
	struct strbuf buf = STRBUF_INIT;
	uint32_t i;

	prepare_repo_settings(the_repository);
	repo_for_each_pack(the_repository, p) {
		if (!strcmp(pack_basename(p), basename))
			break;
	}

	if (!p)
		die("could not find pack '%s'", basename);

	if (open_pack_index(p))
		die("cannot open pack index for '%s'", p->pack_name);

	prepare_packing_data(the_repository, &packed);

	for_each_object_in_pack(p, add_packed_object, &packed,
				ODB_FOR_EACH_OBJECT_PACK_ORDER);

	/*
	 * Build the index array now that data.packed.objects[] is
	 * fully allocated (packlist_alloc() may have reallocated it
	 * during the loop above).
	 */
	ALLOC_ARRAY(index, p->num_objects);
	for (i = 0; i < p->num_objects; i++)
		index[i] = &packed.objects[i].idx;

	bitmap_writer_init(&writer, the_repository, &packed, NULL);
	bitmap_writer_build_type_index(&writer, index);

	while (strbuf_getline_lf(&buf, stdin) != EOF) {
		struct object_id oid;
		struct commit *c;

		if (get_oid_hex(buf.buf, &oid))
			die("invalid OID: %s", buf.buf);

		c = lookup_commit(the_repository, &oid);
		if (!c || repo_parse_commit(the_repository, c))
			die("could not parse commit %s", buf.buf);

		bitmap_writer_push_commit(&writer, c, 0);
	}

	select_pseudo_merges(&writer);
	if (bitmap_writer_build(&writer) < 0)
		die("failed to build bitmaps");

	bitmap_writer_set_checksum(&writer, p->hash);

	QSORT(index, p->num_objects, idx_oid_cmp);

	strbuf_reset(&buf);
	strbuf_addstr(&buf, p->pack_name);
	strbuf_strip_suffix(&buf, ".pack");
	strbuf_addstr(&buf, ".bitmap");
	bitmap_writer_finish(&writer, index, buf.buf, 0);

	bitmap_writer_free(&writer);
	strbuf_release(&buf);
	free(index);
	clear_packing_data(&packed);

	return 0;
}

int cmd__bitmap(int argc, const char **argv)
{
	setup_git_directory(the_repository);

	if (argc == 2 && !strcmp(argv[1], "list-commits"))
		return bitmap_list_commits();
	if (argc == 2 && !strcmp(argv[1], "list-commits-with-offset"))
		return bitmap_list_commits_with_offset();
	if (argc == 2 && !strcmp(argv[1], "dump-hashes"))
		return bitmap_dump_hashes();
	if (argc == 2 && !strcmp(argv[1], "dump-pseudo-merges"))
		return bitmap_dump_pseudo_merges();
	if (argc == 3 && !strcmp(argv[1], "dump-pseudo-merge-commits"))
		return bitmap_dump_pseudo_merge_commits(atoi(argv[2]));
	if (argc == 3 && !strcmp(argv[1], "dump-pseudo-merge-objects"))
		return bitmap_dump_pseudo_merge_objects(atoi(argv[2]));
	if (argc == 3 && !strcmp(argv[1], "write"))
		return bitmap_write(argv[2]);

	usage("\ttest-tool bitmap list-commits\n"
	      "\ttest-tool bitmap list-commits-with-offset\n"
	      "\ttest-tool bitmap dump-hashes\n"
	      "\ttest-tool bitmap dump-pseudo-merges\n"
	      "\ttest-tool bitmap dump-pseudo-merge-commits <n>\n"
	      "\ttest-tool bitmap dump-pseudo-merge-objects <n>\n"
	      "\ttest-tool bitmap write <pack-basename> < <commit-list>");

	return -1;
}
