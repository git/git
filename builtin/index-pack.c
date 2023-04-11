#include "builtin.h"
#include "alloc.h"
#include "config.h"
#include "delta.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "pack.h"
#include "csum-file.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "progress.h"
#include "fsck.h"
#include "exec-cmd.h"
#include "streaming.h"
#include "thread-utils.h"
#include "packfile.h"
#include "pack-revindex.h"
#include "object-store.h"
#include "oid-array.h"
#include "replace-object.h"
#include "promisor-remote.h"
#include "setup.h"
#include "wrapper.h"

static const char index_pack_usage[] =
"git index-pack [-v] [-o <index-file>] [--keep | --keep=<msg>] [--[no-]rev-index] [--verify] [--strict] (<pack-file> | --stdin [--fix-thin] [<pack-file>])";

struct object_entry {
	struct pack_idx_entry idx;
	unsigned long size;
	unsigned char hdr_size;
	signed char type;
	signed char real_type;
};

struct object_stat {
	unsigned delta_depth;
	int base_object_no;
};

struct base_data {
	/* Initialized by make_base(). */
	struct base_data *base;
	struct object_entry *obj;
	int ref_first, ref_last;
	int ofs_first, ofs_last;
	/*
	 * Threads should increment retain_data if they are about to call
	 * patch_delta() using this struct's data as a base, and decrement this
	 * when they are done. While retain_data is nonzero, this struct's data
	 * will not be freed even if the delta base cache limit is exceeded.
	 */
	int retain_data;
	/*
	 * The number of direct children that have not been fully processed
	 * (entered work_head, entered done_head, left done_head). When this
	 * number reaches zero, this struct base_data can be freed.
	 */
	int children_remaining;

	/* Not initialized by make_base(). */
	struct list_head list;
	void *data;
	unsigned long size;
};

/*
 * Stack of struct base_data that have unprocessed children.
 * threaded_second_pass() uses this as a source of work (the other being the
 * objects array).
 *
 * Guarded by work_mutex.
 */
static LIST_HEAD(work_head);

/*
 * Stack of struct base_data that have children, all of whom have been
 * processed or are being processed, and at least one child is being processed.
 * These struct base_data must be kept around until the last child is
 * processed.
 *
 * Guarded by work_mutex.
 */
static LIST_HEAD(done_head);

/*
 * All threads share one delta base cache.
 *
 * base_cache_used is guarded by work_mutex, and base_cache_limit is read-only
 * in a thread.
 */
static size_t base_cache_used;
static size_t base_cache_limit;

struct thread_local {
	pthread_t thread;
	int pack_fd;
};

/* Remember to update object flag allocation in object.h */
#define FLAG_LINK (1u<<20)
#define FLAG_CHECKED (1u<<21)

struct ofs_delta_entry {
	off_t offset;
	int obj_no;
};

struct ref_delta_entry {
	struct object_id oid;
	int obj_no;
};

static struct object_entry *objects;
static struct object_stat *obj_stat;
static struct ofs_delta_entry *ofs_deltas;
static struct ref_delta_entry *ref_deltas;
static struct thread_local nothread_data;
static int nr_objects;
static int nr_ofs_deltas;
static int nr_ref_deltas;
static int ref_deltas_alloc;
static int nr_resolved_deltas;
static int nr_threads;

static int from_stdin;
static int strict;
static int do_fsck_object;
static struct fsck_options fsck_options = FSCK_OPTIONS_MISSING_GITMODULES;
static int verbose;
static const char *progress_title;
static int show_resolving_progress;
static int show_stat;
static int check_self_contained_and_connected;

static struct progress *progress;

/* We always read in 4kB chunks. */
static unsigned char input_buffer[4096];
static unsigned int input_offset, input_len;
static off_t consumed_bytes;
static off_t max_input_size;
static unsigned deepest_delta;
static git_hash_ctx input_ctx;
static uint32_t input_crc32;
static int input_fd, output_fd;
static const char *curr_pack;

static struct thread_local *thread_data;
static int nr_dispatched;
static int threads_active;

static pthread_mutex_t read_mutex;
#define read_lock()		lock_mutex(&read_mutex)
#define read_unlock()		unlock_mutex(&read_mutex)

static pthread_mutex_t counter_mutex;
#define counter_lock()		lock_mutex(&counter_mutex)
#define counter_unlock()	unlock_mutex(&counter_mutex)

static pthread_mutex_t work_mutex;
#define work_lock()		lock_mutex(&work_mutex)
#define work_unlock()		unlock_mutex(&work_mutex)

static pthread_mutex_t deepest_delta_mutex;
#define deepest_delta_lock()	lock_mutex(&deepest_delta_mutex)
#define deepest_delta_unlock()	unlock_mutex(&deepest_delta_mutex)

static pthread_key_t key;

static inline void lock_mutex(pthread_mutex_t *mutex)
{
	if (threads_active)
		pthread_mutex_lock(mutex);
}

static inline void unlock_mutex(pthread_mutex_t *mutex)
{
	if (threads_active)
		pthread_mutex_unlock(mutex);
}

/*
 * Mutex and conditional variable can't be statically-initialized on Windows.
 */
static void init_thread(void)
{
	int i;
	init_recursive_mutex(&read_mutex);
	pthread_mutex_init(&counter_mutex, NULL);
	pthread_mutex_init(&work_mutex, NULL);
	if (show_stat)
		pthread_mutex_init(&deepest_delta_mutex, NULL);
	pthread_key_create(&key, NULL);
	CALLOC_ARRAY(thread_data, nr_threads);
	for (i = 0; i < nr_threads; i++) {
		thread_data[i].pack_fd = xopen(curr_pack, O_RDONLY);
	}

	threads_active = 1;
}

static void cleanup_thread(void)
{
	int i;
	if (!threads_active)
		return;
	threads_active = 0;
	pthread_mutex_destroy(&read_mutex);
	pthread_mutex_destroy(&counter_mutex);
	pthread_mutex_destroy(&work_mutex);
	if (show_stat)
		pthread_mutex_destroy(&deepest_delta_mutex);
	for (i = 0; i < nr_threads; i++)
		close(thread_data[i].pack_fd);
	pthread_key_delete(key);
	free(thread_data);
}

static int mark_link(struct object *obj, enum object_type type,
		     void *data, struct fsck_options *options)
{
	if (!obj)
		return -1;

	if (type != OBJ_ANY && obj->type != type)
		die(_("object type mismatch at %s"), oid_to_hex(&obj->oid));

	obj->flags |= FLAG_LINK;
	return 0;
}

/* The content of each linked object must have been checked
   or it must be already present in the object database */
static unsigned check_object(struct object *obj)
{
	if (!obj)
		return 0;

	if (!(obj->flags & FLAG_LINK))
		return 0;

	if (!(obj->flags & FLAG_CHECKED)) {
		unsigned long size;
		int type = oid_object_info(the_repository, &obj->oid, &size);
		if (type <= 0)
			die(_("did not receive expected object %s"),
			      oid_to_hex(&obj->oid));
		if (type != obj->type)
			die(_("object %s: expected type %s, found %s"),
			    oid_to_hex(&obj->oid),
			    type_name(obj->type), type_name(type));
		obj->flags |= FLAG_CHECKED;
		return 1;
	}

	return 0;
}

static unsigned check_objects(void)
{
	unsigned i, max, foreign_nr = 0;

	max = get_max_object_index();

	if (verbose)
		progress = start_delayed_progress(_("Checking objects"), max);

	for (i = 0; i < max; i++) {
		foreign_nr += check_object(get_indexed_object(i));
		display_progress(progress, i + 1);
	}

	stop_progress(&progress);
	return foreign_nr;
}


/* Discard current buffer used content. */
static void flush(void)
{
	if (input_offset) {
		if (output_fd >= 0)
			write_or_die(output_fd, input_buffer, input_offset);
		the_hash_algo->update_fn(&input_ctx, input_buffer, input_offset);
		memmove(input_buffer, input_buffer + input_offset, input_len);
		input_offset = 0;
	}
}

/*
 * Make sure at least "min" bytes are available in the buffer, and
 * return the pointer to the buffer.
 */
static void *fill(int min)
{
	if (min <= input_len)
		return input_buffer + input_offset;
	if (min > sizeof(input_buffer))
		die(Q_("cannot fill %d byte",
		       "cannot fill %d bytes",
		       min),
		    min);
	flush();
	do {
		ssize_t ret = xread(input_fd, input_buffer + input_len,
				sizeof(input_buffer) - input_len);
		if (ret <= 0) {
			if (!ret)
				die(_("early EOF"));
			die_errno(_("read error on input"));
		}
		input_len += ret;
		if (from_stdin)
			display_throughput(progress, consumed_bytes + input_len);
	} while (input_len < min);
	return input_buffer;
}

static void use(int bytes)
{
	if (bytes > input_len)
		die(_("used more bytes than were available"));
	input_crc32 = crc32(input_crc32, input_buffer + input_offset, bytes);
	input_len -= bytes;
	input_offset += bytes;

	/* make sure off_t is sufficiently large not to wrap */
	if (signed_add_overflows(consumed_bytes, bytes))
		die(_("pack too large for current definition of off_t"));
	consumed_bytes += bytes;
	if (max_input_size && consumed_bytes > max_input_size) {
		struct strbuf size_limit = STRBUF_INIT;
		strbuf_humanise_bytes(&size_limit, max_input_size);
		die(_("pack exceeds maximum allowed size (%s)"),
		    size_limit.buf);
	}
}

static const char *open_pack_file(const char *pack_name)
{
	if (from_stdin) {
		input_fd = 0;
		if (!pack_name) {
			struct strbuf tmp_file = STRBUF_INIT;
			output_fd = odb_mkstemp(&tmp_file,
						"pack/tmp_pack_XXXXXX");
			pack_name = strbuf_detach(&tmp_file, NULL);
		} else {
			output_fd = xopen(pack_name, O_CREAT|O_EXCL|O_RDWR, 0600);
		}
		nothread_data.pack_fd = output_fd;
	} else {
		input_fd = xopen(pack_name, O_RDONLY);
		output_fd = -1;
		nothread_data.pack_fd = input_fd;
	}
	the_hash_algo->init_fn(&input_ctx);
	return pack_name;
}

static void parse_pack_header(void)
{
	struct pack_header *hdr = fill(sizeof(struct pack_header));

	/* Header consistency check */
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		die(_("pack signature mismatch"));
	if (!pack_version_ok(hdr->hdr_version))
		die(_("pack version %"PRIu32" unsupported"),
			ntohl(hdr->hdr_version));

	nr_objects = ntohl(hdr->hdr_entries);
	use(sizeof(struct pack_header));
}

__attribute__((format (printf, 2, 3)))
static NORETURN void bad_object(off_t offset, const char *format, ...)
{
	va_list params;
	char buf[1024];

	va_start(params, format);
	vsnprintf(buf, sizeof(buf), format, params);
	va_end(params);
	die(_("pack has bad object at offset %"PRIuMAX": %s"),
	    (uintmax_t)offset, buf);
}

static inline struct thread_local *get_thread_data(void)
{
	if (HAVE_THREADS) {
		if (threads_active)
			return pthread_getspecific(key);
		assert(!threads_active &&
		       "This should only be reached when all threads are gone");
	}
	return &nothread_data;
}

static void set_thread_data(struct thread_local *data)
{
	if (threads_active)
		pthread_setspecific(key, data);
}

static void free_base_data(struct base_data *c)
{
	if (c->data) {
		FREE_AND_NULL(c->data);
		base_cache_used -= c->size;
	}
}

static void prune_base_data(struct base_data *retain)
{
	struct list_head *pos;

	if (base_cache_used <= base_cache_limit)
		return;

	list_for_each_prev(pos, &done_head) {
		struct base_data *b = list_entry(pos, struct base_data, list);
		if (b->retain_data || b == retain)
			continue;
		if (b->data) {
			free_base_data(b);
			if (base_cache_used <= base_cache_limit)
				return;
		}
	}

	list_for_each_prev(pos, &work_head) {
		struct base_data *b = list_entry(pos, struct base_data, list);
		if (b->retain_data || b == retain)
			continue;
		if (b->data) {
			free_base_data(b);
			if (base_cache_used <= base_cache_limit)
				return;
		}
	}
}

static int is_delta_type(enum object_type type)
{
	return (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA);
}

static void *unpack_entry_data(off_t offset, unsigned long size,
			       enum object_type type, struct object_id *oid)
{
	static char fixed_buf[8192];
	int status;
	git_zstream stream;
	void *buf;
	git_hash_ctx c;
	char hdr[32];
	int hdrlen;

	if (!is_delta_type(type)) {
		hdrlen = format_object_header(hdr, sizeof(hdr), type, size);
		the_hash_algo->init_fn(&c);
		the_hash_algo->update_fn(&c, hdr, hdrlen);
	} else
		oid = NULL;
	if (type == OBJ_BLOB && size > big_file_threshold)
		buf = fixed_buf;
	else
		buf = xmallocz(size);

	memset(&stream, 0, sizeof(stream));
	git_inflate_init(&stream);
	stream.next_out = buf;
	stream.avail_out = buf == fixed_buf ? sizeof(fixed_buf) : size;

	do {
		unsigned char *last_out = stream.next_out;
		stream.next_in = fill(1);
		stream.avail_in = input_len;
		status = git_inflate(&stream, 0);
		use(input_len - stream.avail_in);
		if (oid)
			the_hash_algo->update_fn(&c, last_out, stream.next_out - last_out);
		if (buf == fixed_buf) {
			stream.next_out = buf;
			stream.avail_out = sizeof(fixed_buf);
		}
	} while (status == Z_OK);
	if (stream.total_out != size || status != Z_STREAM_END)
		bad_object(offset, _("inflate returned %d"), status);
	git_inflate_end(&stream);
	if (oid)
		the_hash_algo->final_oid_fn(oid, &c);
	return buf == fixed_buf ? NULL : buf;
}

static void *unpack_raw_entry(struct object_entry *obj,
			      off_t *ofs_offset,
			      struct object_id *ref_oid,
			      struct object_id *oid)
{
	unsigned char *p;
	unsigned long size, c;
	off_t base_offset;
	unsigned shift;
	void *data;

	obj->idx.offset = consumed_bytes;
	input_crc32 = crc32(0, NULL, 0);

	p = fill(1);
	c = *p;
	use(1);
	obj->type = (c >> 4) & 7;
	size = (c & 15);
	shift = 4;
	while (c & 0x80) {
		p = fill(1);
		c = *p;
		use(1);
		size += (c & 0x7f) << shift;
		shift += 7;
	}
	obj->size = size;

	switch (obj->type) {
	case OBJ_REF_DELTA:
		oidread(ref_oid, fill(the_hash_algo->rawsz));
		use(the_hash_algo->rawsz);
		break;
	case OBJ_OFS_DELTA:
		p = fill(1);
		c = *p;
		use(1);
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || MSB(base_offset, 7))
				bad_object(obj->idx.offset, _("offset value overflow for delta base object"));
			p = fill(1);
			c = *p;
			use(1);
			base_offset = (base_offset << 7) + (c & 127);
		}
		*ofs_offset = obj->idx.offset - base_offset;
		if (*ofs_offset <= 0 || *ofs_offset >= obj->idx.offset)
			bad_object(obj->idx.offset, _("delta base offset is out of bound"));
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		break;
	default:
		bad_object(obj->idx.offset, _("unknown object type %d"), obj->type);
	}
	obj->hdr_size = consumed_bytes - obj->idx.offset;

	data = unpack_entry_data(obj->idx.offset, obj->size, obj->type, oid);
	obj->idx.crc32 = input_crc32;
	return data;
}

static void *unpack_data(struct object_entry *obj,
			 int (*consume)(const unsigned char *, unsigned long, void *),
			 void *cb_data)
{
	off_t from = obj[0].idx.offset + obj[0].hdr_size;
	off_t len = obj[1].idx.offset - from;
	unsigned char *data, *inbuf;
	git_zstream stream;
	int status;

	data = xmallocz(consume ? 64*1024 : obj->size);
	inbuf = xmalloc((len < 64*1024) ? (int)len : 64*1024);

	memset(&stream, 0, sizeof(stream));
	git_inflate_init(&stream);
	stream.next_out = data;
	stream.avail_out = consume ? 64*1024 : obj->size;

	do {
		ssize_t n = (len < 64*1024) ? (ssize_t)len : 64*1024;
		n = xpread(get_thread_data()->pack_fd, inbuf, n, from);
		if (n < 0)
			die_errno(_("cannot pread pack file"));
		if (!n)
			die(Q_("premature end of pack file, %"PRIuMAX" byte missing",
			       "premature end of pack file, %"PRIuMAX" bytes missing",
			       len),
			    (uintmax_t)len);
		from += n;
		len -= n;
		stream.next_in = inbuf;
		stream.avail_in = n;
		if (!consume)
			status = git_inflate(&stream, 0);
		else {
			do {
				status = git_inflate(&stream, 0);
				if (consume(data, stream.next_out - data, cb_data)) {
					free(inbuf);
					free(data);
					return NULL;
				}
				stream.next_out = data;
				stream.avail_out = 64*1024;
			} while (status == Z_OK && stream.avail_in);
		}
	} while (len && status == Z_OK && !stream.avail_in);

	/* This has been inflated OK when first encountered, so... */
	if (status != Z_STREAM_END || stream.total_out != obj->size)
		die(_("serious inflate inconsistency"));

	git_inflate_end(&stream);
	free(inbuf);
	if (consume) {
		FREE_AND_NULL(data);
	}
	return data;
}

static void *get_data_from_pack(struct object_entry *obj)
{
	return unpack_data(obj, NULL, NULL);
}

static int compare_ofs_delta_bases(off_t offset1, off_t offset2,
				   enum object_type type1,
				   enum object_type type2)
{
	int cmp = type1 - type2;
	if (cmp)
		return cmp;
	return offset1 < offset2 ? -1 :
	       offset1 > offset2 ?  1 :
	       0;
}

static int find_ofs_delta(const off_t offset)
{
	int first = 0, last = nr_ofs_deltas;

	while (first < last) {
		int next = first + (last - first) / 2;
		struct ofs_delta_entry *delta = &ofs_deltas[next];
		int cmp;

		cmp = compare_ofs_delta_bases(offset, delta->offset,
					      OBJ_OFS_DELTA,
					      objects[delta->obj_no].type);
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first-1;
}

static void find_ofs_delta_children(off_t offset,
				    int *first_index, int *last_index)
{
	int first = find_ofs_delta(offset);
	int last = first;
	int end = nr_ofs_deltas - 1;

	if (first < 0) {
		*first_index = 0;
		*last_index = -1;
		return;
	}
	while (first > 0 && ofs_deltas[first - 1].offset == offset)
		--first;
	while (last < end && ofs_deltas[last + 1].offset == offset)
		++last;
	*first_index = first;
	*last_index = last;
}

static int compare_ref_delta_bases(const struct object_id *oid1,
				   const struct object_id *oid2,
				   enum object_type type1,
				   enum object_type type2)
{
	int cmp = type1 - type2;
	if (cmp)
		return cmp;
	return oidcmp(oid1, oid2);
}

static int find_ref_delta(const struct object_id *oid)
{
	int first = 0, last = nr_ref_deltas;

	while (first < last) {
		int next = first + (last - first) / 2;
		struct ref_delta_entry *delta = &ref_deltas[next];
		int cmp;

		cmp = compare_ref_delta_bases(oid, &delta->oid,
					      OBJ_REF_DELTA,
					      objects[delta->obj_no].type);
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first-1;
}

static void find_ref_delta_children(const struct object_id *oid,
				    int *first_index, int *last_index)
{
	int first = find_ref_delta(oid);
	int last = first;
	int end = nr_ref_deltas - 1;

	if (first < 0) {
		*first_index = 0;
		*last_index = -1;
		return;
	}
	while (first > 0 && oideq(&ref_deltas[first - 1].oid, oid))
		--first;
	while (last < end && oideq(&ref_deltas[last + 1].oid, oid))
		++last;
	*first_index = first;
	*last_index = last;
}

struct compare_data {
	struct object_entry *entry;
	struct git_istream *st;
	unsigned char *buf;
	unsigned long buf_size;
};

static int compare_objects(const unsigned char *buf, unsigned long size,
			   void *cb_data)
{
	struct compare_data *data = cb_data;

	if (data->buf_size < size) {
		free(data->buf);
		data->buf = xmalloc(size);
		data->buf_size = size;
	}

	while (size) {
		ssize_t len = read_istream(data->st, data->buf, size);
		if (len == 0)
			die(_("SHA1 COLLISION FOUND WITH %s !"),
			    oid_to_hex(&data->entry->idx.oid));
		if (len < 0)
			die(_("unable to read %s"),
			    oid_to_hex(&data->entry->idx.oid));
		if (memcmp(buf, data->buf, len))
			die(_("SHA1 COLLISION FOUND WITH %s !"),
			    oid_to_hex(&data->entry->idx.oid));
		size -= len;
		buf += len;
	}
	return 0;
}

static int check_collison(struct object_entry *entry)
{
	struct compare_data data;
	enum object_type type;
	unsigned long size;

	if (entry->size <= big_file_threshold || entry->type != OBJ_BLOB)
		return -1;

	memset(&data, 0, sizeof(data));
	data.entry = entry;
	data.st = open_istream(the_repository, &entry->idx.oid, &type, &size,
			       NULL);
	if (!data.st)
		return -1;
	if (size != entry->size || type != entry->type)
		die(_("SHA1 COLLISION FOUND WITH %s !"),
		    oid_to_hex(&entry->idx.oid));
	unpack_data(entry, compare_objects, &data);
	close_istream(data.st);
	free(data.buf);
	return 0;
}

static void sha1_object(const void *data, struct object_entry *obj_entry,
			unsigned long size, enum object_type type,
			const struct object_id *oid)
{
	void *new_data = NULL;
	int collision_test_needed = 0;

	assert(data || obj_entry);

	if (startup_info->have_repository) {
		read_lock();
		collision_test_needed =
			repo_has_object_file_with_flags(the_repository, oid,
							OBJECT_INFO_QUICK);
		read_unlock();
	}

	if (collision_test_needed && !data) {
		read_lock();
		if (!check_collison(obj_entry))
			collision_test_needed = 0;
		read_unlock();
	}
	if (collision_test_needed) {
		void *has_data;
		enum object_type has_type;
		unsigned long has_size;
		read_lock();
		has_type = oid_object_info(the_repository, oid, &has_size);
		if (has_type < 0)
			die(_("cannot read existing object info %s"), oid_to_hex(oid));
		if (has_type != type || has_size != size)
			die(_("SHA1 COLLISION FOUND WITH %s !"), oid_to_hex(oid));
		has_data = repo_read_object_file(the_repository, oid,
						 &has_type, &has_size);
		read_unlock();
		if (!data)
			data = new_data = get_data_from_pack(obj_entry);
		if (!has_data)
			die(_("cannot read existing object %s"), oid_to_hex(oid));
		if (size != has_size || type != has_type ||
		    memcmp(data, has_data, size) != 0)
			die(_("SHA1 COLLISION FOUND WITH %s !"), oid_to_hex(oid));
		free(has_data);
	}

	if (strict || do_fsck_object) {
		read_lock();
		if (type == OBJ_BLOB) {
			struct blob *blob = lookup_blob(the_repository, oid);
			if (blob)
				blob->object.flags |= FLAG_CHECKED;
			else
				die(_("invalid blob object %s"), oid_to_hex(oid));
			if (do_fsck_object &&
			    fsck_object(&blob->object, (void *)data, size, &fsck_options))
				die(_("fsck error in packed object"));
		} else {
			struct object *obj;
			int eaten;
			void *buf = (void *) data;

			assert(data && "data can only be NULL for large _blobs_");

			/*
			 * we do not need to free the memory here, as the
			 * buf is deleted by the caller.
			 */
			obj = parse_object_buffer(the_repository, oid, type,
						  size, buf,
						  &eaten);
			if (!obj)
				die(_("invalid %s"), type_name(type));
			if (do_fsck_object &&
			    fsck_object(obj, buf, size, &fsck_options))
				die(_("fsck error in packed object"));
			if (strict && fsck_walk(obj, NULL, &fsck_options))
				die(_("Not all child objects of %s are reachable"), oid_to_hex(&obj->oid));

			if (obj->type == OBJ_TREE) {
				struct tree *item = (struct tree *) obj;
				item->buffer = NULL;
				obj->parsed = 0;
			}
			if (obj->type == OBJ_COMMIT) {
				struct commit *commit = (struct commit *) obj;
				if (detach_commit_buffer(commit, NULL) != data)
					BUG("parse_object_buffer transmogrified our buffer");
			}
			obj->flags |= FLAG_CHECKED;
		}
		read_unlock();
	}

	free(new_data);
}

/*
 * Ensure that this node has been reconstructed and return its contents.
 *
 * In the typical and best case, this node would already be reconstructed
 * (through the invocation to resolve_delta() in threaded_second_pass()) and it
 * would not be pruned. However, if pruning of this node was necessary due to
 * reaching delta_base_cache_limit, this function will find the closest
 * ancestor with reconstructed data that has not been pruned (or if there is
 * none, the ultimate base object), and reconstruct each node in the delta
 * chain in order to generate the reconstructed data for this node.
 */
static void *get_base_data(struct base_data *c)
{
	if (!c->data) {
		struct object_entry *obj = c->obj;
		struct base_data **delta = NULL;
		int delta_nr = 0, delta_alloc = 0;

		while (is_delta_type(c->obj->type) && !c->data) {
			ALLOC_GROW(delta, delta_nr + 1, delta_alloc);
			delta[delta_nr++] = c;
			c = c->base;
		}
		if (!delta_nr) {
			c->data = get_data_from_pack(obj);
			c->size = obj->size;
			base_cache_used += c->size;
			prune_base_data(c);
		}
		for (; delta_nr > 0; delta_nr--) {
			void *base, *raw;
			c = delta[delta_nr - 1];
			obj = c->obj;
			base = get_base_data(c->base);
			raw = get_data_from_pack(obj);
			c->data = patch_delta(
				base, c->base->size,
				raw, obj->size,
				&c->size);
			free(raw);
			if (!c->data)
				bad_object(obj->idx.offset, _("failed to apply delta"));
			base_cache_used += c->size;
			prune_base_data(c);
		}
		free(delta);
	}
	return c->data;
}

static struct base_data *make_base(struct object_entry *obj,
				   struct base_data *parent)
{
	struct base_data *base = xcalloc(1, sizeof(struct base_data));
	base->base = parent;
	base->obj = obj;
	find_ref_delta_children(&obj->idx.oid,
				&base->ref_first, &base->ref_last);
	find_ofs_delta_children(obj->idx.offset,
				&base->ofs_first, &base->ofs_last);
	base->children_remaining = base->ref_last - base->ref_first +
		base->ofs_last - base->ofs_first + 2;
	return base;
}

static struct base_data *resolve_delta(struct object_entry *delta_obj,
				       struct base_data *base)
{
	void *delta_data, *result_data;
	struct base_data *result;
	unsigned long result_size;

	if (show_stat) {
		int i = delta_obj - objects;
		int j = base->obj - objects;
		obj_stat[i].delta_depth = obj_stat[j].delta_depth + 1;
		deepest_delta_lock();
		if (deepest_delta < obj_stat[i].delta_depth)
			deepest_delta = obj_stat[i].delta_depth;
		deepest_delta_unlock();
		obj_stat[i].base_object_no = j;
	}
	delta_data = get_data_from_pack(delta_obj);
	assert(base->data);
	result_data = patch_delta(base->data, base->size,
				  delta_data, delta_obj->size, &result_size);
	free(delta_data);
	if (!result_data)
		bad_object(delta_obj->idx.offset, _("failed to apply delta"));
	hash_object_file(the_hash_algo, result_data, result_size,
			 delta_obj->real_type, &delta_obj->idx.oid);
	sha1_object(result_data, NULL, result_size, delta_obj->real_type,
		    &delta_obj->idx.oid);

	result = make_base(delta_obj, base);
	result->data = result_data;
	result->size = result_size;

	counter_lock();
	nr_resolved_deltas++;
	counter_unlock();

	return result;
}

static int compare_ofs_delta_entry(const void *a, const void *b)
{
	const struct ofs_delta_entry *delta_a = a;
	const struct ofs_delta_entry *delta_b = b;

	return delta_a->offset < delta_b->offset ? -1 :
	       delta_a->offset > delta_b->offset ?  1 :
	       0;
}

static int compare_ref_delta_entry(const void *a, const void *b)
{
	const struct ref_delta_entry *delta_a = a;
	const struct ref_delta_entry *delta_b = b;

	return oidcmp(&delta_a->oid, &delta_b->oid);
}

static void *threaded_second_pass(void *data)
{
	if (data)
		set_thread_data(data);
	for (;;) {
		struct base_data *parent = NULL;
		struct object_entry *child_obj;
		struct base_data *child;

		counter_lock();
		display_progress(progress, nr_resolved_deltas);
		counter_unlock();

		work_lock();
		if (list_empty(&work_head)) {
			/*
			 * Take an object from the object array.
			 */
			while (nr_dispatched < nr_objects &&
			       is_delta_type(objects[nr_dispatched].type))
				nr_dispatched++;
			if (nr_dispatched >= nr_objects) {
				work_unlock();
				break;
			}
			child_obj = &objects[nr_dispatched++];
		} else {
			/*
			 * Peek at the top of the stack, and take a child from
			 * it.
			 */
			parent = list_first_entry(&work_head, struct base_data,
						  list);

			if (parent->ref_first <= parent->ref_last) {
				int offset = ref_deltas[parent->ref_first++].obj_no;
				child_obj = objects + offset;
				if (child_obj->real_type != OBJ_REF_DELTA)
					die("REF_DELTA at offset %"PRIuMAX" already resolved (duplicate base %s?)",
					    (uintmax_t) child_obj->idx.offset,
					    oid_to_hex(&parent->obj->idx.oid));
				child_obj->real_type = parent->obj->real_type;
			} else {
				child_obj = objects +
					ofs_deltas[parent->ofs_first++].obj_no;
				assert(child_obj->real_type == OBJ_OFS_DELTA);
				child_obj->real_type = parent->obj->real_type;
			}

			if (parent->ref_first > parent->ref_last &&
			    parent->ofs_first > parent->ofs_last) {
				/*
				 * This parent has run out of children, so move
				 * it to done_head.
				 */
				list_del(&parent->list);
				list_add(&parent->list, &done_head);
			}

			/*
			 * Ensure that the parent has data, since we will need
			 * it later.
			 *
			 * NEEDSWORK: If parent data needs to be reloaded, this
			 * prolongs the time that the current thread spends in
			 * the mutex. A mitigating factor is that parent data
			 * needs to be reloaded only if the delta base cache
			 * limit is exceeded, so in the typical case, this does
			 * not happen.
			 */
			get_base_data(parent);
			parent->retain_data++;
		}
		work_unlock();

		if (parent) {
			child = resolve_delta(child_obj, parent);
			if (!child->children_remaining)
				FREE_AND_NULL(child->data);
		} else {
			child = make_base(child_obj, NULL);
			if (child->children_remaining) {
				/*
				 * Since this child has its own delta children,
				 * we will need this data in the future.
				 * Inflate now so that future iterations will
				 * have access to this object's data while
				 * outside the work mutex.
				 */
				child->data = get_data_from_pack(child_obj);
				child->size = child_obj->size;
			}
		}

		work_lock();
		if (parent)
			parent->retain_data--;
		if (child->data) {
			/*
			 * This child has its own children, so add it to
			 * work_head.
			 */
			list_add(&child->list, &work_head);
			base_cache_used += child->size;
			prune_base_data(NULL);
			free_base_data(child);
		} else {
			/*
			 * This child does not have its own children. It may be
			 * the last descendant of its ancestors; free those
			 * that we can.
			 */
			struct base_data *p = parent;

			while (p) {
				struct base_data *next_p;

				p->children_remaining--;
				if (p->children_remaining)
					break;

				next_p = p->base;
				free_base_data(p);
				list_del(&p->list);
				free(p);

				p = next_p;
			}
			FREE_AND_NULL(child);
		}
		work_unlock();
	}
	return NULL;
}

/*
 * First pass:
 * - find locations of all objects;
 * - calculate SHA1 of all non-delta objects;
 * - remember base (SHA1 or offset) for all deltas.
 */
static void parse_pack_objects(unsigned char *hash)
{
	int i, nr_delays = 0;
	struct ofs_delta_entry *ofs_delta = ofs_deltas;
	struct object_id ref_delta_oid;
	struct stat st;

	if (verbose)
		progress = start_progress(
				progress_title ? progress_title :
				from_stdin ? _("Receiving objects") : _("Indexing objects"),
				nr_objects);
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		void *data = unpack_raw_entry(obj, &ofs_delta->offset,
					      &ref_delta_oid,
					      &obj->idx.oid);
		obj->real_type = obj->type;
		if (obj->type == OBJ_OFS_DELTA) {
			nr_ofs_deltas++;
			ofs_delta->obj_no = i;
			ofs_delta++;
		} else if (obj->type == OBJ_REF_DELTA) {
			ALLOC_GROW(ref_deltas, nr_ref_deltas + 1, ref_deltas_alloc);
			oidcpy(&ref_deltas[nr_ref_deltas].oid, &ref_delta_oid);
			ref_deltas[nr_ref_deltas].obj_no = i;
			nr_ref_deltas++;
		} else if (!data) {
			/* large blobs, check later */
			obj->real_type = OBJ_BAD;
			nr_delays++;
		} else
			sha1_object(data, NULL, obj->size, obj->type,
				    &obj->idx.oid);
		free(data);
		display_progress(progress, i+1);
	}
	objects[i].idx.offset = consumed_bytes;
	stop_progress(&progress);

	/* Check pack integrity */
	flush();
	the_hash_algo->final_fn(hash, &input_ctx);
	if (!hasheq(fill(the_hash_algo->rawsz), hash))
		die(_("pack is corrupted (SHA1 mismatch)"));
	use(the_hash_algo->rawsz);

	/* If input_fd is a file, we should have reached its end now. */
	if (fstat(input_fd, &st))
		die_errno(_("cannot fstat packfile"));
	if (S_ISREG(st.st_mode) &&
			lseek(input_fd, 0, SEEK_CUR) - input_len != st.st_size)
		die(_("pack has junk at the end"));

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		if (obj->real_type != OBJ_BAD)
			continue;
		obj->real_type = obj->type;
		sha1_object(NULL, obj, obj->size, obj->type,
			    &obj->idx.oid);
		nr_delays--;
	}
	if (nr_delays)
		die(_("confusion beyond insanity in parse_pack_objects()"));
}

/*
 * Second pass:
 * - for all non-delta objects, look if it is used as a base for
 *   deltas;
 * - if used as a base, uncompress the object and apply all deltas,
 *   recursively checking if the resulting object is used as a base
 *   for some more deltas.
 */
static void resolve_deltas(void)
{
	int i;

	if (!nr_ofs_deltas && !nr_ref_deltas)
		return;

	/* Sort deltas by base SHA1/offset for fast searching */
	QSORT(ofs_deltas, nr_ofs_deltas, compare_ofs_delta_entry);
	QSORT(ref_deltas, nr_ref_deltas, compare_ref_delta_entry);

	if (verbose || show_resolving_progress)
		progress = start_progress(_("Resolving deltas"),
					  nr_ref_deltas + nr_ofs_deltas);

	nr_dispatched = 0;
	base_cache_limit = delta_base_cache_limit * nr_threads;
	if (nr_threads > 1 || getenv("GIT_FORCE_THREADS")) {
		init_thread();
		for (i = 0; i < nr_threads; i++) {
			int ret = pthread_create(&thread_data[i].thread, NULL,
						 threaded_second_pass, thread_data + i);
			if (ret)
				die(_("unable to create thread: %s"),
				    strerror(ret));
		}
		for (i = 0; i < nr_threads; i++)
			pthread_join(thread_data[i].thread, NULL);
		cleanup_thread();
		return;
	}
	threaded_second_pass(&nothread_data);
}

/*
 * Third pass:
 * - append objects to convert thin pack to full pack if required
 * - write the final pack hash
 */
static void fix_unresolved_deltas(struct hashfile *f);
static void conclude_pack(int fix_thin_pack, const char *curr_pack, unsigned char *pack_hash)
{
	if (nr_ref_deltas + nr_ofs_deltas == nr_resolved_deltas) {
		stop_progress(&progress);
		/* Flush remaining pack final hash. */
		flush();
		return;
	}

	if (fix_thin_pack) {
		struct hashfile *f;
		unsigned char read_hash[GIT_MAX_RAWSZ], tail_hash[GIT_MAX_RAWSZ];
		struct strbuf msg = STRBUF_INIT;
		int nr_unresolved = nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas;
		int nr_objects_initial = nr_objects;
		if (nr_unresolved <= 0)
			die(_("confusion beyond insanity"));
		REALLOC_ARRAY(objects, nr_objects + nr_unresolved + 1);
		memset(objects + nr_objects + 1, 0,
		       nr_unresolved * sizeof(*objects));
		f = hashfd(output_fd, curr_pack);
		fix_unresolved_deltas(f);
		strbuf_addf(&msg, Q_("completed with %d local object",
				     "completed with %d local objects",
				     nr_objects - nr_objects_initial),
			    nr_objects - nr_objects_initial);
		stop_progress_msg(&progress, msg.buf);
		strbuf_release(&msg);
		finalize_hashfile(f, tail_hash, FSYNC_COMPONENT_PACK, 0);
		hashcpy(read_hash, pack_hash);
		fixup_pack_header_footer(output_fd, pack_hash,
					 curr_pack, nr_objects,
					 read_hash, consumed_bytes-the_hash_algo->rawsz);
		if (!hasheq(read_hash, tail_hash))
			die(_("Unexpected tail checksum for %s "
			      "(disk corruption?)"), curr_pack);
	}
	if (nr_ofs_deltas + nr_ref_deltas != nr_resolved_deltas)
		die(Q_("pack has %d unresolved delta",
		       "pack has %d unresolved deltas",
		       nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas),
		    nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas);
}

static int write_compressed(struct hashfile *f, void *in, unsigned int size)
{
	git_zstream stream;
	int status;
	unsigned char outbuf[4096];

	git_deflate_init(&stream, zlib_compression_level);
	stream.next_in = in;
	stream.avail_in = size;

	do {
		stream.next_out = outbuf;
		stream.avail_out = sizeof(outbuf);
		status = git_deflate(&stream, Z_FINISH);
		hashwrite(f, outbuf, sizeof(outbuf) - stream.avail_out);
	} while (status == Z_OK);

	if (status != Z_STREAM_END)
		die(_("unable to deflate appended object (%d)"), status);
	size = stream.total_out;
	git_deflate_end(&stream);
	return size;
}

static struct object_entry *append_obj_to_pack(struct hashfile *f,
			       const unsigned char *sha1, void *buf,
			       unsigned long size, enum object_type type)
{
	struct object_entry *obj = &objects[nr_objects++];
	unsigned char header[10];
	unsigned long s = size;
	int n = 0;
	unsigned char c = (type << 4) | (s & 15);
	s >>= 4;
	while (s) {
		header[n++] = c | 0x80;
		c = s & 0x7f;
		s >>= 7;
	}
	header[n++] = c;
	crc32_begin(f);
	hashwrite(f, header, n);
	obj[0].size = size;
	obj[0].hdr_size = n;
	obj[0].type = type;
	obj[0].real_type = type;
	obj[1].idx.offset = obj[0].idx.offset + n;
	obj[1].idx.offset += write_compressed(f, buf, size);
	obj[0].idx.crc32 = crc32_end(f);
	hashflush(f);
	oidread(&obj->idx.oid, sha1);
	return obj;
}

static int delta_pos_compare(const void *_a, const void *_b)
{
	struct ref_delta_entry *a = *(struct ref_delta_entry **)_a;
	struct ref_delta_entry *b = *(struct ref_delta_entry **)_b;
	return a->obj_no - b->obj_no;
}

static void fix_unresolved_deltas(struct hashfile *f)
{
	struct ref_delta_entry **sorted_by_pos;
	int i;

	/*
	 * Since many unresolved deltas may well be themselves base objects
	 * for more unresolved deltas, we really want to include the
	 * smallest number of base objects that would cover as much delta
	 * as possible by picking the
	 * trunc deltas first, allowing for other deltas to resolve without
	 * additional base objects.  Since most base objects are to be found
	 * before deltas depending on them, a good heuristic is to start
	 * resolving deltas in the same order as their position in the pack.
	 */
	ALLOC_ARRAY(sorted_by_pos, nr_ref_deltas);
	for (i = 0; i < nr_ref_deltas; i++)
		sorted_by_pos[i] = &ref_deltas[i];
	QSORT(sorted_by_pos, nr_ref_deltas, delta_pos_compare);

	if (repo_has_promisor_remote(the_repository)) {
		/*
		 * Prefetch the delta bases.
		 */
		struct oid_array to_fetch = OID_ARRAY_INIT;
		for (i = 0; i < nr_ref_deltas; i++) {
			struct ref_delta_entry *d = sorted_by_pos[i];
			if (!oid_object_info_extended(the_repository, &d->oid,
						      NULL,
						      OBJECT_INFO_FOR_PREFETCH))
				continue;
			oid_array_append(&to_fetch, &d->oid);
		}
		promisor_remote_get_direct(the_repository,
					   to_fetch.oid, to_fetch.nr);
		oid_array_clear(&to_fetch);
	}

	for (i = 0; i < nr_ref_deltas; i++) {
		struct ref_delta_entry *d = sorted_by_pos[i];
		enum object_type type;
		void *data;
		unsigned long size;

		if (objects[d->obj_no].real_type != OBJ_REF_DELTA)
			continue;
		data = repo_read_object_file(the_repository, &d->oid, &type,
					     &size);
		if (!data)
			continue;

		if (check_object_signature(the_repository, &d->oid, data, size,
					   type) < 0)
			die(_("local object %s is corrupt"), oid_to_hex(&d->oid));

		/*
		 * Add this as an object to the objects array and call
		 * threaded_second_pass() (which will pick up the added
		 * object).
		 */
		append_obj_to_pack(f, d->oid.hash, data, size, type);
		free(data);
		threaded_second_pass(NULL);

		display_progress(progress, nr_resolved_deltas);
	}
	free(sorted_by_pos);
}

static const char *derive_filename(const char *pack_name, const char *strip,
				   const char *suffix, struct strbuf *buf)
{
	size_t len;
	if (!strip_suffix(pack_name, strip, &len) || !len ||
	    pack_name[len - 1] != '.')
		die(_("packfile name '%s' does not end with '.%s'"),
		    pack_name, strip);
	strbuf_add(buf, pack_name, len);
	strbuf_addstr(buf, suffix);
	return buf->buf;
}

static void write_special_file(const char *suffix, const char *msg,
			       const char *pack_name, const unsigned char *hash,
			       const char **report)
{
	struct strbuf name_buf = STRBUF_INIT;
	const char *filename;
	int fd;
	int msg_len = strlen(msg);

	if (pack_name)
		filename = derive_filename(pack_name, "pack", suffix, &name_buf);
	else
		filename = odb_pack_name(&name_buf, hash, suffix);

	fd = odb_pack_keep(filename);
	if (fd < 0) {
		if (errno != EEXIST)
			die_errno(_("cannot write %s file '%s'"),
				  suffix, filename);
	} else {
		if (msg_len > 0) {
			write_or_die(fd, msg, msg_len);
			write_or_die(fd, "\n", 1);
		}
		if (close(fd) != 0)
			die_errno(_("cannot close written %s file '%s'"),
				  suffix, filename);
		if (report)
			*report = suffix;
	}
	strbuf_release(&name_buf);
}

static void rename_tmp_packfile(const char **final_name,
				const char *curr_name,
				struct strbuf *name, unsigned char *hash,
				const char *ext, int make_read_only_if_same)
{
	if (*final_name != curr_name) {
		if (!*final_name)
			*final_name = odb_pack_name(name, hash, ext);
		if (finalize_object_file(curr_name, *final_name))
			die(_("unable to rename temporary '*.%s' file to '%s'"),
			    ext, *final_name);
	} else if (make_read_only_if_same) {
		chmod(*final_name, 0444);
	}
}

static void final(const char *final_pack_name, const char *curr_pack_name,
		  const char *final_index_name, const char *curr_index_name,
		  const char *final_rev_index_name, const char *curr_rev_index_name,
		  const char *keep_msg, const char *promisor_msg,
		  unsigned char *hash)
{
	const char *report = "pack";
	struct strbuf pack_name = STRBUF_INIT;
	struct strbuf index_name = STRBUF_INIT;
	struct strbuf rev_index_name = STRBUF_INIT;
	int err;

	if (!from_stdin) {
		close(input_fd);
	} else {
		fsync_component_or_die(FSYNC_COMPONENT_PACK, output_fd, curr_pack_name);
		err = close(output_fd);
		if (err)
			die_errno(_("error while closing pack file"));
	}

	if (keep_msg)
		write_special_file("keep", keep_msg, final_pack_name, hash,
				   &report);
	if (promisor_msg)
		write_special_file("promisor", promisor_msg, final_pack_name,
				   hash, NULL);

	rename_tmp_packfile(&final_pack_name, curr_pack_name, &pack_name,
			    hash, "pack", from_stdin);
	if (curr_rev_index_name)
		rename_tmp_packfile(&final_rev_index_name, curr_rev_index_name,
				    &rev_index_name, hash, "rev", 1);
	rename_tmp_packfile(&final_index_name, curr_index_name, &index_name,
			    hash, "idx", 1);

	if (do_fsck_object) {
		struct packed_git *p;
		p = add_packed_git(final_index_name, strlen(final_index_name), 0);
		if (p)
			install_packed_git(the_repository, p);
	}

	if (!from_stdin) {
		printf("%s\n", hash_to_hex(hash));
	} else {
		struct strbuf buf = STRBUF_INIT;

		strbuf_addf(&buf, "%s\t%s\n", report, hash_to_hex(hash));
		write_or_die(1, buf.buf, buf.len);
		strbuf_release(&buf);

		/*
		 * Let's just mimic git-unpack-objects here and write
		 * the last part of the input buffer to stdout.
		 */
		while (input_len) {
			err = xwrite(1, input_buffer + input_offset, input_len);
			if (err <= 0)
				break;
			input_len -= err;
			input_offset += err;
		}
	}

	strbuf_release(&rev_index_name);
	strbuf_release(&index_name);
	strbuf_release(&pack_name);
}

static int git_index_pack_config(const char *k, const char *v, void *cb)
{
	struct pack_idx_option *opts = cb;

	if (!strcmp(k, "pack.indexversion")) {
		opts->version = git_config_int(k, v);
		if (opts->version > 2)
			die(_("bad pack.indexVersion=%"PRIu32), opts->version);
		return 0;
	}
	if (!strcmp(k, "pack.threads")) {
		nr_threads = git_config_int(k, v);
		if (nr_threads < 0)
			die(_("invalid number of threads specified (%d)"),
			    nr_threads);
		if (!HAVE_THREADS && nr_threads != 1) {
			warning(_("no threads support, ignoring %s"), k);
			nr_threads = 1;
		}
		return 0;
	}
	if (!strcmp(k, "pack.writereverseindex")) {
		if (git_config_bool(k, v))
			opts->flags |= WRITE_REV;
		else
			opts->flags &= ~WRITE_REV;
	}
	return git_default_config(k, v, cb);
}

static int cmp_uint32(const void *a_, const void *b_)
{
	uint32_t a = *((uint32_t *)a_);
	uint32_t b = *((uint32_t *)b_);

	return (a < b) ? -1 : (a != b);
}

static void read_v2_anomalous_offsets(struct packed_git *p,
				      struct pack_idx_option *opts)
{
	const uint32_t *idx1, *idx2;
	uint32_t i;

	/* The address of the 4-byte offset table */
	idx1 = (((const uint32_t *)((const uint8_t *)p->index_data + p->crc_offset))
		+ (size_t)p->num_objects /* CRC32 table */
		);

	/* The address of the 8-byte offset table */
	idx2 = idx1 + p->num_objects;

	for (i = 0; i < p->num_objects; i++) {
		uint32_t off = ntohl(idx1[i]);
		if (!(off & 0x80000000))
			continue;
		off = off & 0x7fffffff;
		check_pack_index_ptr(p, &idx2[off * 2]);
		if (idx2[off * 2])
			continue;
		/*
		 * The real offset is ntohl(idx2[off * 2]) in high 4
		 * octets, and ntohl(idx2[off * 2 + 1]) in low 4
		 * octets.  But idx2[off * 2] is Zero!!!
		 */
		ALLOC_GROW(opts->anomaly, opts->anomaly_nr + 1, opts->anomaly_alloc);
		opts->anomaly[opts->anomaly_nr++] = ntohl(idx2[off * 2 + 1]);
	}

	QSORT(opts->anomaly, opts->anomaly_nr, cmp_uint32);
}

static void read_idx_option(struct pack_idx_option *opts, const char *pack_name)
{
	struct packed_git *p = add_packed_git(pack_name, strlen(pack_name), 1);

	if (!p)
		die(_("Cannot open existing pack file '%s'"), pack_name);
	if (open_pack_index(p))
		die(_("Cannot open existing pack idx file for '%s'"), pack_name);

	/* Read the attributes from the existing idx file */
	opts->version = p->index_version;

	if (opts->version == 2)
		read_v2_anomalous_offsets(p, opts);

	/*
	 * Get rid of the idx file as we do not need it anymore.
	 * NEEDSWORK: extract this bit from free_pack_by_name() in
	 * object-file.c, perhaps?  It shouldn't matter very much as we
	 * know we haven't installed this pack (hence we never have
	 * read anything from it).
	 */
	close_pack_index(p);
	free(p);
}

static void show_pack_info(int stat_only)
{
	int i, baseobjects = nr_objects - nr_ref_deltas - nr_ofs_deltas;
	unsigned long *chain_histogram = NULL;

	if (deepest_delta)
		CALLOC_ARRAY(chain_histogram, deepest_delta);

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];

		if (is_delta_type(obj->type))
			chain_histogram[obj_stat[i].delta_depth - 1]++;
		if (stat_only)
			continue;
		printf("%s %-6s %"PRIuMAX" %"PRIuMAX" %"PRIuMAX,
		       oid_to_hex(&obj->idx.oid),
		       type_name(obj->real_type), (uintmax_t)obj->size,
		       (uintmax_t)(obj[1].idx.offset - obj->idx.offset),
		       (uintmax_t)obj->idx.offset);
		if (is_delta_type(obj->type)) {
			struct object_entry *bobj = &objects[obj_stat[i].base_object_no];
			printf(" %u %s", obj_stat[i].delta_depth,
			       oid_to_hex(&bobj->idx.oid));
		}
		putchar('\n');
	}

	if (baseobjects)
		printf_ln(Q_("non delta: %d object",
			     "non delta: %d objects",
			     baseobjects),
			  baseobjects);
	for (i = 0; i < deepest_delta; i++) {
		if (!chain_histogram[i])
			continue;
		printf_ln(Q_("chain length = %d: %lu object",
			     "chain length = %d: %lu objects",
			     chain_histogram[i]),
			  i + 1,
			  chain_histogram[i]);
	}
	free(chain_histogram);
}

int cmd_index_pack(int argc, const char **argv, const char *prefix)
{
	int i, fix_thin_pack = 0, verify = 0, stat_only = 0, rev_index;
	const char *curr_index;
	const char *curr_rev_index = NULL;
	const char *index_name = NULL, *pack_name = NULL, *rev_index_name = NULL;
	const char *keep_msg = NULL;
	const char *promisor_msg = NULL;
	struct strbuf index_name_buf = STRBUF_INIT;
	struct strbuf rev_index_name_buf = STRBUF_INIT;
	struct pack_idx_entry **idx_objects;
	struct pack_idx_option opts;
	unsigned char pack_hash[GIT_MAX_RAWSZ];
	unsigned foreign_nr = 1;	/* zero is a "good" value, assume bad */
	int report_end_of_input = 0;
	int hash_algo = 0;

	/*
	 * index-pack never needs to fetch missing objects except when
	 * REF_DELTA bases are missing (which are explicitly handled). It only
	 * accesses the repo to do hash collision checks and to check which
	 * REF_DELTA bases need to be fetched.
	 */
	fetch_if_missing = 0;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(index_pack_usage);

	read_replace_refs = 0;
	fsck_options.walk = mark_link;

	reset_pack_idx_option(&opts);
	git_config(git_index_pack_config, &opts);
	if (prefix && chdir(prefix))
		die(_("Cannot come back to cwd"));

	if (git_env_bool(GIT_TEST_WRITE_REV_INDEX, 0))
		rev_index = 1;
	else
		rev_index = !!(opts.flags & (WRITE_REV_VERIFY | WRITE_REV));

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "--stdin")) {
				from_stdin = 1;
			} else if (!strcmp(arg, "--fix-thin")) {
				fix_thin_pack = 1;
			} else if (skip_to_optional_arg(arg, "--strict", &arg)) {
				strict = 1;
				do_fsck_object = 1;
				fsck_set_msg_types(&fsck_options, arg);
			} else if (!strcmp(arg, "--check-self-contained-and-connected")) {
				strict = 1;
				check_self_contained_and_connected = 1;
			} else if (!strcmp(arg, "--fsck-objects")) {
				do_fsck_object = 1;
			} else if (!strcmp(arg, "--verify")) {
				verify = 1;
			} else if (!strcmp(arg, "--verify-stat")) {
				verify = 1;
				show_stat = 1;
			} else if (!strcmp(arg, "--verify-stat-only")) {
				verify = 1;
				show_stat = 1;
				stat_only = 1;
			} else if (skip_to_optional_arg(arg, "--keep", &keep_msg)) {
				; /* nothing to do */
			} else if (skip_to_optional_arg(arg, "--promisor", &promisor_msg)) {
				; /* already parsed */
			} else if (starts_with(arg, "--threads=")) {
				char *end;
				nr_threads = strtoul(arg+10, &end, 0);
				if (!arg[10] || *end || nr_threads < 0)
					usage(index_pack_usage);
				if (!HAVE_THREADS && nr_threads != 1) {
					warning(_("no threads support, ignoring %s"), arg);
					nr_threads = 1;
				}
			} else if (starts_with(arg, "--pack_header=")) {
				struct pack_header *hdr;
				char *c;

				hdr = (struct pack_header *)input_buffer;
				hdr->hdr_signature = htonl(PACK_SIGNATURE);
				hdr->hdr_version = htonl(strtoul(arg + 14, &c, 10));
				if (*c != ',')
					die(_("bad %s"), arg);
				hdr->hdr_entries = htonl(strtoul(c + 1, &c, 10));
				if (*c)
					die(_("bad %s"), arg);
				input_len = sizeof(*hdr);
			} else if (!strcmp(arg, "-v")) {
				verbose = 1;
			} else if (!strcmp(arg, "--progress-title")) {
				if (progress_title || (i+1) >= argc)
					usage(index_pack_usage);
				progress_title = argv[++i];
			} else if (!strcmp(arg, "--show-resolving-progress")) {
				show_resolving_progress = 1;
			} else if (!strcmp(arg, "--report-end-of-input")) {
				report_end_of_input = 1;
			} else if (!strcmp(arg, "-o")) {
				if (index_name || (i+1) >= argc)
					usage(index_pack_usage);
				index_name = argv[++i];
			} else if (starts_with(arg, "--index-version=")) {
				char *c;
				opts.version = strtoul(arg + 16, &c, 10);
				if (opts.version > 2)
					die(_("bad %s"), arg);
				if (*c == ',')
					opts.off32_limit = strtoul(c+1, &c, 0);
				if (*c || opts.off32_limit & 0x80000000)
					die(_("bad %s"), arg);
			} else if (skip_prefix(arg, "--max-input-size=", &arg)) {
				max_input_size = strtoumax(arg, NULL, 10);
			} else if (skip_prefix(arg, "--object-format=", &arg)) {
				hash_algo = hash_algo_by_name(arg);
				if (hash_algo == GIT_HASH_UNKNOWN)
					die(_("unknown hash algorithm '%s'"), arg);
				repo_set_hash_algo(the_repository, hash_algo);
			} else if (!strcmp(arg, "--rev-index")) {
				rev_index = 1;
			} else if (!strcmp(arg, "--no-rev-index")) {
				rev_index = 0;
			} else
				usage(index_pack_usage);
			continue;
		}

		if (pack_name)
			usage(index_pack_usage);
		pack_name = arg;
	}

	if (!pack_name && !from_stdin)
		usage(index_pack_usage);
	if (fix_thin_pack && !from_stdin)
		die(_("the option '%s' requires '%s'"), "--fix-thin", "--stdin");
	if (from_stdin && !startup_info->have_repository)
		die(_("--stdin requires a git repository"));
	if (from_stdin && hash_algo)
		die(_("options '%s' and '%s' cannot be used together"), "--object-format", "--stdin");
	if (!index_name && pack_name)
		index_name = derive_filename(pack_name, "pack", "idx", &index_name_buf);

	opts.flags &= ~(WRITE_REV | WRITE_REV_VERIFY);
	if (rev_index) {
		opts.flags |= verify ? WRITE_REV_VERIFY : WRITE_REV;
		if (index_name)
			rev_index_name = derive_filename(index_name,
							 "idx", "rev",
							 &rev_index_name_buf);
	}

	if (verify) {
		if (!index_name)
			die(_("--verify with no packfile name given"));
		read_idx_option(&opts, index_name);
		opts.flags |= WRITE_IDX_VERIFY | WRITE_IDX_STRICT;
	}
	if (strict)
		opts.flags |= WRITE_IDX_STRICT;

	if (HAVE_THREADS && !nr_threads) {
		nr_threads = online_cpus();
		/*
		 * Experiments show that going above 20 threads doesn't help,
		 * no matter how many cores you have. Below that, we tend to
		 * max at half the number of online_cpus(), presumably because
		 * half of those are hyperthreads rather than full cores. We'll
		 * never reduce the level below "3", though, to match a
		 * historical value that nobody complained about.
		 */
		if (nr_threads < 4)
			; /* too few cores to consider capping */
		else if (nr_threads < 6)
			nr_threads = 3; /* historic cap */
		else if (nr_threads < 40)
			nr_threads /= 2;
		else
			nr_threads = 20; /* hard cap */
	}

	curr_pack = open_pack_file(pack_name);
	parse_pack_header();
	CALLOC_ARRAY(objects, st_add(nr_objects, 1));
	if (show_stat)
		CALLOC_ARRAY(obj_stat, st_add(nr_objects, 1));
	CALLOC_ARRAY(ofs_deltas, nr_objects);
	parse_pack_objects(pack_hash);
	if (report_end_of_input)
		write_in_full(2, "\0", 1);
	resolve_deltas();
	conclude_pack(fix_thin_pack, curr_pack, pack_hash);
	free(ofs_deltas);
	free(ref_deltas);
	if (strict)
		foreign_nr = check_objects();

	if (show_stat)
		show_pack_info(stat_only);

	ALLOC_ARRAY(idx_objects, nr_objects);
	for (i = 0; i < nr_objects; i++)
		idx_objects[i] = &objects[i].idx;
	curr_index = write_idx_file(index_name, idx_objects, nr_objects, &opts, pack_hash);
	if (rev_index)
		curr_rev_index = write_rev_file(rev_index_name, idx_objects,
						nr_objects, pack_hash,
						opts.flags);
	free(idx_objects);

	if (!verify)
		final(pack_name, curr_pack,
		      index_name, curr_index,
		      rev_index_name, curr_rev_index,
		      keep_msg, promisor_msg,
		      pack_hash);
	else
		close(input_fd);

	if (do_fsck_object && fsck_finish(&fsck_options))
		die(_("fsck error in pack objects"));

	free(opts.anomaly);
	free(objects);
	strbuf_release(&index_name_buf);
	strbuf_release(&rev_index_name_buf);
	if (!pack_name)
		free((void *) curr_pack);
	if (!index_name)
		free((void *) curr_index);
	if (!rev_index_name)
		free((void *) curr_rev_index);

	/*
	 * Let the caller know this pack is not self contained
	 */
	if (check_self_contained_and_connected && foreign_nr)
		return 1;

	return 0;
}
