#include "cache.h"
#include "pack.h"
#include "pack-revindex.h"
#include "progress.h"
#include "varint.h"
#include "packv4-create.h"

extern int pack_compression_seen;
extern int pack_compression_level;
extern int min_tree_copy;

static struct pack_idx_entry *get_packed_object_list(struct packed_git *p)
{
	unsigned i, nr_objects = p->num_objects;
	struct pack_idx_entry *objects;

	objects = xmalloc((nr_objects + 1) * sizeof(*objects));
	objects[nr_objects].offset = p->pack_size - 20;
	for (i = 0; i < nr_objects; i++) {
		hashcpy(objects[i].sha1, nth_packed_object_sha1(p, i));
		objects[i].offset = nth_packed_object_offset(p, i);
	}

	return objects;
}

static int sort_by_offset(const void *e1, const void *e2)
{
	const struct pack_idx_entry * const *entry1 = e1;
	const struct pack_idx_entry * const *entry2 = e2;
	if ((*entry1)->offset < (*entry2)->offset)
		return -1;
	if ((*entry1)->offset > (*entry2)->offset)
		return 1;
	return 0;
}

static struct pack_idx_entry **sort_objs_by_offset(struct pack_idx_entry *list,
						    unsigned nr_objects)
{
	unsigned i;
	struct pack_idx_entry **sorted;

	sorted = xmalloc((nr_objects + 1) * sizeof(*sorted));
	for (i = 0; i < nr_objects + 1; i++)
		sorted[i] = &list[i];
	qsort(sorted, nr_objects + 1, sizeof(*sorted), sort_by_offset);

	return sorted;
}

static int create_pack_dictionaries(struct packv4_tables *v4,
				    struct packed_git *p,
				    struct pack_idx_entry **obj_list)
{
	struct progress *progress_state;
	unsigned int i;

	v4->commit_ident_table = create_dict_table();
	v4->tree_path_table = create_dict_table();

	progress_state = start_progress("Scanning objects", p->num_objects);
	for (i = 0; i < p->num_objects; i++) {
		struct pack_idx_entry *obj = obj_list[i];
		void *data;
		enum object_type type;
		unsigned long size;
		struct object_info oi = {};
		int (*add_dict_entries)(struct dict_table *, void *, unsigned long);
		struct dict_table *dict;

		display_progress(progress_state, i+1);

		oi.typep = &type;
		oi.sizep = &size;
		if (packed_object_info(p, obj->offset, &oi) < 0)
			die("cannot get type of %s from %s",
			    sha1_to_hex(obj->sha1), p->pack_name);

		switch (type) {
		case OBJ_COMMIT:
			add_dict_entries = add_commit_dict_entries;
			dict = v4->commit_ident_table;
			break;
		case OBJ_TREE:
			add_dict_entries = add_tree_dict_entries;
			dict = v4->tree_path_table;
			break;
		default:
			continue;
		}
		data = unpack_entry(p, obj->offset, &type, &size);
		if (!data)
			die("cannot unpack %s from %s",
			    sha1_to_hex(obj->sha1), p->pack_name);
		if (check_sha1_signature(obj->sha1, data, size, typename(type)))
			die("packed %s from %s is corrupt",
			    sha1_to_hex(obj->sha1), p->pack_name);
		if (add_dict_entries(dict, data, size) < 0)
			die("can't process %s object %s",
				typename(type), sha1_to_hex(obj->sha1));
		free(data);
	}

	stop_progress(&progress_state);
	return 0;
}

static struct sha1file * packv4_open(char *path)
{
	int fd;

	fd = open(path, O_CREAT|O_EXCL|O_WRONLY, 0600);
	if (fd < 0)
		die_errno("unable to create '%s'", path);
	return sha1fd(fd, path);
}

static unsigned int packv4_write_header(struct sha1file *f, unsigned nr_objects)
{
	struct pack_header hdr;

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(4);
	hdr.hdr_entries = htonl(nr_objects);
	sha1write(f, &hdr, sizeof(hdr));

	return sizeof(hdr);
}

static int write_object_header(struct sha1file *f, enum object_type type, unsigned long size)
{
	unsigned char buf[16];
	uint64_t val;
	int len;

	/*
	 * We really have only one kind of delta object.
	 */
	if (type == OBJ_OFS_DELTA)
		type = OBJ_REF_DELTA;

	/*
	 * We allocate 4 bits in the LSB for the object type which should
	 * be good for quite a while, given that we effectively encodes
	 * only 5 object types: commit, tree, blob, delta, tag.
	 */
	val = size;
	if (MSB(val, 4))
		die("fixme: the code doesn't currently cope with big sizes");
	val <<= 4;
	val |= type;
	len = encode_varint(val, buf);
	sha1write(f, buf, len);
	return len;
}

static unsigned long copy_object_data(struct packv4_tables *v4,
				      struct sha1file *f, struct packed_git *p,
				      off_t offset)
{
	struct pack_window *w_curs = NULL;
	struct revindex_entry *revidx;
	enum object_type type;
	unsigned long avail, size, datalen, written;
	int hdrlen, reflen, idx_nr;
	unsigned char *src, buf[24];

	revidx = find_pack_revindex(p, offset);
	idx_nr = revidx->nr;
	datalen = revidx[1].offset - offset;

	src = use_pack(p, &w_curs, offset, &avail);
	hdrlen = unpack_object_header_buffer(src, avail, &type, &size);

	written = write_object_header(f, type, size);

	if (type == OBJ_OFS_DELTA) {
		const unsigned char *cp = src + hdrlen;
		off_t base_offset = decode_varint(&cp);
		hdrlen = cp - src;
		base_offset = offset - base_offset;
		if (base_offset <= 0 || base_offset >= offset)
			die("delta offset out of bound");
		revidx = find_pack_revindex(p, base_offset);
		reflen = encode_sha1ref(v4,
					nth_packed_object_sha1(p, revidx->nr),
					buf);
		sha1write(f, buf, reflen);
		written += reflen;
	} else if (type == OBJ_REF_DELTA) {
		reflen = encode_sha1ref(v4, src + hdrlen, buf);
		hdrlen += 20;
		sha1write(f, buf, reflen);
		written += reflen;
	}

	if (p->index_version > 1 &&
	    check_pack_crc(p, &w_curs, offset, datalen, idx_nr))
		die("bad CRC for object at offset %"PRIuMAX" in %s",
		    (uintmax_t)offset, p->pack_name);

	offset += hdrlen;
	datalen -= hdrlen;

	while (datalen) {
		src = use_pack(p, &w_curs, offset, &avail);
		if (avail > datalen)
			avail = datalen;
		sha1write(f, src, avail);
		written += avail;
		offset += avail;
		datalen -= avail;
	}
	unuse_pack(&w_curs);

	return written;
}

static unsigned char *get_delta_base(struct packed_git *p, off_t offset,
				     unsigned char *sha1_buf)
{
	struct pack_window *w_curs = NULL;
	enum object_type type;
	unsigned long avail, size;
	int hdrlen;
	unsigned char *src;
	const unsigned char *base_sha1 = NULL; ;

	src = use_pack(p, &w_curs, offset, &avail);
	hdrlen = unpack_object_header_buffer(src, avail, &type, &size);

	if (type == OBJ_OFS_DELTA) {
		const unsigned char *cp = src + hdrlen;
		off_t base_offset = decode_varint(&cp);
		base_offset = offset - base_offset;
		if (base_offset <= 0 || base_offset >= offset) {
			error("delta offset out of bound");
		} else {
			struct revindex_entry *revidx;
			revidx = find_pack_revindex(p, base_offset);
			base_sha1 = nth_packed_object_sha1(p, revidx->nr);
		}
	} else if (type == OBJ_REF_DELTA) {
		base_sha1 = src + hdrlen;
	} else
		error("expected to get a delta but got a %s", typename(type));

	unuse_pack(&w_curs);

	if (!base_sha1)
		return NULL;
	hashcpy(sha1_buf, base_sha1);
	return sha1_buf;
}

static off_t packv4_write_object(struct packv4_tables *v4,
				 struct sha1file *f, struct packed_git *p,
				 struct pack_idx_entry *obj)
{
	void *src, *result;
	struct object_info oi = {};
	enum object_type type, packed_type;
	unsigned long obj_size, buf_size;
	unsigned int hdrlen;

	oi.typep = &type;
	oi.sizep = &obj_size;
	packed_type = packed_object_info(p, obj->offset, &oi);
	if (packed_type < 0)
		die("cannot get type of %s from %s",
		    sha1_to_hex(obj->sha1), p->pack_name);

	/* Some objects are copied without decompression */
	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
		break;
	default:
		return copy_object_data(v4, f, p, obj->offset);
	}

	/* The rest is converted into their new format */
	src = unpack_entry(p, obj->offset, &type, &buf_size);
	if (!src || obj_size != buf_size)
		die("cannot unpack %s from %s",
		    sha1_to_hex(obj->sha1), p->pack_name);
	if (check_sha1_signature(obj->sha1, src, buf_size, typename(type)))
		die("packed %s from %s is corrupt",
		    sha1_to_hex(obj->sha1), p->pack_name);

	switch (type) {
	case OBJ_COMMIT:
		result = pv4_encode_commit(v4, src, &buf_size);
		break;
	case OBJ_TREE:
		if (packed_type != OBJ_TREE) {
			unsigned char sha1_buf[20], *ref_sha1;
			void *ref;
			enum object_type ref_type;
			unsigned long ref_size;

			ref_sha1 = get_delta_base(p, obj->offset, sha1_buf);
			if (!ref_sha1)
				die("unable to get delta base sha1 for %s",
						sha1_to_hex(obj->sha1));
			ref = read_sha1_file(ref_sha1, &ref_type, &ref_size);
			if (!ref || ref_type != OBJ_TREE)
				die("cannot obtain delta base for %s",
						sha1_to_hex(obj->sha1));
			result = pv4_encode_tree(v4, src, &buf_size,
						 ref, ref_size, ref_sha1);
			free(ref);
		} else {
			result = pv4_encode_tree(v4, src, &buf_size,
						 NULL, 0, NULL);
		}
		break;
	default:
		die("unexpected object type %d", type);
	}
	free(src);
	if (!result) {
		warning("can't convert %s object %s",
			typename(type), sha1_to_hex(obj->sha1));
		/* fall back to copy the object in its original form */
		return copy_object_data(v4, f, p, obj->offset);
	}

	/* Use bit 3 to indicate a special type encoding */
	type += 8;
	hdrlen = write_object_header(f, type, obj_size);
	sha1write(f, result, buf_size);
	free(result);
	return hdrlen + buf_size;
}

static char *normalize_pack_name(const char *path)
{
	char buf[PATH_MAX];
	int len;

	len = strlcpy(buf, path, PATH_MAX);
	if (len >= PATH_MAX - 6)
		die("name too long: %s", path);

	/*
	 * In addition to "foo.idx" we accept "foo.pack" and "foo";
	 * normalize these forms to "foo.pack".
	 */
	if (has_extension(buf, ".idx")) {
		strcpy(buf + len - 4, ".pack");
		len++;
	} else if (!has_extension(buf, ".pack")) {
		strcpy(buf + len, ".pack");
		len += 5;
	}

	return xstrdup(buf);
}

static struct packed_git *open_pack(const char *path)
{
	char *packname = normalize_pack_name(path);
	int len = strlen(packname);
	struct packed_git *p;

	strcpy(packname + len - 5, ".idx");
	p = add_packed_git(packname, len - 1, 1);
	if (!p)
		die("packfile %s not found.", packname);

	install_packed_git(p);
	if (open_pack_index(p))
		die("packfile %s index not opened", p->pack_name);

	free(packname);
	return p;
}

void process_one_pack(struct packv4_tables *v4, char *src_pack, char *dst_pack)
{
	struct packed_git *p;
	struct sha1file *f;
	struct pack_idx_entry *objs, **p_objs;
	struct pack_idx_option idx_opts;
	unsigned i, nr_objects;
	off_t written = 0;
	char *packname;
	unsigned char pack_sha1[20];
	struct progress *progress_state;

	p = open_pack(src_pack);
	if (!p)
		die("unable to open source pack");

	nr_objects = p->num_objects;
	objs = get_packed_object_list(p);
	p_objs = sort_objs_by_offset(objs, nr_objects);

	v4->all_objs = objs;
	v4->all_objs_nr = nr_objects;

	create_pack_dictionaries(v4, p, p_objs);
	sort_dict_entries_by_hits(v4->commit_ident_table);
	sort_dict_entries_by_hits(v4->tree_path_table);

	packname = normalize_pack_name(dst_pack);
	f = packv4_open(packname);
	if (!f)
		die("unable to open destination pack");
	written += packv4_write_header(f, nr_objects);
	written += packv4_write_tables(f, v4);

	/* Let's write objects out, updating the object index list in place */
	progress_state = start_progress("Writing objects", nr_objects);
	for (i = 0; i < nr_objects; i++) {
		off_t obj_pos = written;
		struct pack_idx_entry *obj = p_objs[i];
		crc32_begin(f);
		written += packv4_write_object(v4, f, p, obj);
		obj->offset = obj_pos;
		obj->crc32 = crc32_end(f);
		display_progress(progress_state, i+1);
	}
	stop_progress(&progress_state);

	sha1close(f, pack_sha1, CSUM_CLOSE | CSUM_FSYNC);

	reset_pack_idx_option(&idx_opts);
	idx_opts.version = 3;
	strcpy(packname + strlen(packname) - 5, ".idx");
	write_idx_file(packname, p_objs, nr_objects, &idx_opts, pack_sha1);

	free(packname);
}

static int git_pack_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "pack.compression")) {
		int level = git_config_int(k, v);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad pack compression level %d", level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}
	return git_default_config(k, v, cb);
}

int main(int argc, char *argv[])
{
	struct packv4_tables v4;
	char *src_pack, *dst_pack;

	if (argc == 3) {
		src_pack = argv[1];
		dst_pack = argv[2];
	} else if (argc == 4 && !prefixcmp(argv[1], "--min-tree-copy=")) {
		min_tree_copy = atoi(argv[1] + strlen("--min-tree-copy="));
		src_pack = argv[2];
		dst_pack = argv[3];
	} else {
		fprintf(stderr, "Usage: %s [--min-tree-copy=<n>] <src_packfile> <dst_packfile>\n", argv[0]);
		exit(1);
	}

	git_config(git_pack_config, NULL);
	if (!pack_compression_seen && core_compression_seen)
		pack_compression_level = core_compression_level;
	process_one_pack(&v4, src_pack, dst_pack);
	if (0)
		dict_dump(&v4);
	return 0;
}
