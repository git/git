#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "git-compat-util.h"
#include "delta.h"
#include "git-zlib.h"
#include "hash.h"
#include "hex.h"
#include "pack.h"
#include "pack-objects.h"
#include "parse-options.h"
#include "setup.h"
#include "strbuf.h"
#include "string-list.h"

static const char *usage_str[] = {
	"test-tool pack-deltas --num-objects <num-objects>",
	NULL
};

static unsigned long do_compress(void **pptr, unsigned long size)
{
	git_zstream stream;
	void *in, *out;
	unsigned long maxsize;

	git_deflate_init(&stream, 1);
	maxsize = git_deflate_bound(&stream, size);

	in = *pptr;
	out = xmalloc(maxsize);
	*pptr = out;

	stream.next_in = in;
	stream.avail_in = size;
	stream.next_out = out;
	stream.avail_out = maxsize;
	while (git_deflate(&stream, Z_FINISH) == Z_OK)
		; /* nothing */
	git_deflate_end(&stream);

	free(in);
	return stream.total_out;
}

static void write_ref_delta(struct hashfile *f,
			    struct object_id *oid,
			    struct object_id *base)
{
	unsigned char header[MAX_PACK_OBJECT_HEADER];
	unsigned long size, base_size, delta_size, compressed_size, hdrlen;
	enum object_type type;
	void *base_buf, *delta_buf;
	void *buf = repo_read_object_file(the_repository,
					  oid, &type,
					  &size);

	if (!buf)
		die("unable to read %s", oid_to_hex(oid));

	base_buf = repo_read_object_file(the_repository,
					 base, &type,
					 &base_size);

	if (!base_buf)
		die("unable to read %s", oid_to_hex(base));

	delta_buf = diff_delta(base_buf, base_size,
			       buf, size, &delta_size, 0);

	compressed_size = do_compress(&delta_buf, delta_size);

	hdrlen = encode_in_pack_object_header(header, sizeof(header),
					      OBJ_REF_DELTA, delta_size);
	hashwrite(f, header, hdrlen);
	hashwrite(f, base->hash, the_repository->hash_algo->rawsz);
	hashwrite(f, delta_buf, compressed_size);

	free(buf);
	free(base_buf);
	free(delta_buf);
}

int cmd__pack_deltas(int argc, const char **argv)
{
	int num_objects = -1;
	struct hashfile *f;
	struct strbuf line = STRBUF_INIT;
	struct option options[] = {
		OPT_INTEGER('n', "num-objects", &num_objects, N_("the number of objects to write")),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL,
			     options, usage_str, 0);

	if (argc || num_objects < 0)
		usage_with_options(usage_str, options);

	setup_git_directory();

	f = hashfd(the_repository->hash_algo, 1, "<stdout>");
	write_pack_header(f, num_objects);

	/* Read each line from stdin into 'line' */
	while (strbuf_getline_lf(&line, stdin) != EOF) {
		const char *type_str, *content_oid_str, *base_oid_str = NULL;
		struct object_id content_oid, base_oid;
		struct string_list items = STRING_LIST_INIT_NODUP;
		/*
		 * Tokenize into two or three parts:
		 * 1. REF_DELTA, OFS_DELTA, or FULL.
		 * 2. The object ID for the content object.
		 * 3. The object ID for the base object (optional).
		 */
		if (string_list_split_in_place(&items, line.buf, " ", 3) < 0)
			die("invalid input format: %s", line.buf);

		if (items.nr < 2)
			die("invalid input format: %s", line.buf);

		type_str = items.items[0].string;
		content_oid_str = items.items[1].string;

		if (get_oid_hex(content_oid_str, &content_oid))
			die("invalid object: %s", content_oid_str);
		if (items.nr >= 3) {
			base_oid_str = items.items[2].string;
			if (get_oid_hex(base_oid_str, &base_oid))
				die("invalid object: %s", base_oid_str);
		}
		string_list_clear(&items, 0);

		if (!strcmp(type_str, "REF_DELTA"))
			write_ref_delta(f, &content_oid, &base_oid);
		else if (!strcmp(type_str, "OFS_DELTA"))
			die("OFS_DELTA not implemented");
		else if (!strcmp(type_str, "FULL"))
			die("FULL not implemented");
		else
			die("unknown pack type: %s", type_str);
	}

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);
	strbuf_release(&line);
	return 0;
}
