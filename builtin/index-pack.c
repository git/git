#include "builtin.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "progress.h"
#include "fsck.h"
#include "exec_cmd.h"
#include "streaming.h"
#include "thread-utils.h"

static const char index_pack_usage[] =
"git index-pack [-v] [-o <index-file>] [--keep | --keep=<msg>] [--verify] [--strict] (<pack-file> | --stdin [--fix-thin] [<pack-file>])";

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
	struct base_data *base;
	struct base_data *child;
	struct object_entry *obj;
	void *data;
	unsigned long size;
	int ref_first, ref_last;
	int ofs_first, ofs_last;
};

struct thread_local {
#ifndef NO_PTHREADS
	pthread_t thread;
#endif
	struct base_data *base_cache;
	size_t base_cache_used;
	int pack_fd;
};

#define FLAG_LINK (1u<<20)
#define FLAG_CHECKED (1u<<21)

struct ofs_delta_entry {
	off_t offset;
	int obj_no;
};

struct ref_delta_entry {
	unsigned char sha1[20];
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
static struct fsck_options fsck_options = FSCK_OPTIONS_STRICT;
static int verbose;
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
static git_SHA_CTX input_ctx;
static uint32_t input_crc32;
static int input_fd, output_fd;
static const char *curr_pack;

#ifndef NO_PTHREADS

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

static pthread_mutex_t type_cas_mutex;
#define type_cas_lock()		lock_mutex(&type_cas_mutex)
#define type_cas_unlock()	unlock_mutex(&type_cas_mutex)

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
	pthread_mutex_init(&type_cas_mutex, NULL);
	if (show_stat)
		pthread_mutex_init(&deepest_delta_mutex, NULL);
	pthread_key_create(&key, NULL);
	thread_data = xcalloc(nr_threads, sizeof(*thread_data));
	for (i = 0; i < nr_threads; i++) {
		thread_data[i].pack_fd = open(curr_pack, O_RDONLY);
		if (thread_data[i].pack_fd == -1)
			die_errno(_("unable to open %s"), curr_pack);
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
	pthread_mutex_destroy(&type_cas_mutex);
	if (show_stat)
		pthread_mutex_destroy(&deepest_delta_mutex);
	for (i = 0; i < nr_threads; i++)
		close(thread_data[i].pack_fd);
	pthread_key_delete(key);
	free(thread_data);
}

#else

#define read_lock()
#define read_unlock()

#define counter_lock()
#define counter_unlock()

#define work_lock()
#define work_unlock()

#define deepest_delta_lock()
#define deepest_delta_unlock()

#define type_cas_lock()
#define type_cas_unlock()

#endif


static int mark_link(struct object *obj, int type, void *data, struct fsck_options *options)
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
		int type = sha1_object_info(obj->oid.hash, &size);
		if (type <= 0)
			die(_("did not receive expected object %s"),
			      oid_to_hex(&obj->oid));
		if (type != obj->type)
			die(_("object %s: expected type %s, found %s"),
			    oid_to_hex(&obj->oid),
			    typename(obj->type), typename(type));
		obj->flags |= FLAG_CHECKED;
		return 1;
	}

	return 0;
}

static unsigned check_objects(void)
{
	unsigned i, max, foreign_nr = 0;

	max = get_max_object_index();
	for (i = 0; i < max; i++)
		foreign_nr += check_object(get_indexed_object(i));
	return foreign_nr;
}


/* Discard current buffer used content. */
static void flush(void)
{
	if (input_offset) {
		if (output_fd >= 0)
			write_or_die(output_fd, input_buffer, input_offset);
		git_SHA1_Update(&input_ctx, input_buffer, input_offset);
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
	if (max_input_size && consumed_bytes > max_input_size)
		die(_("pack exceeds maximum allowed size"));
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
			output_fd = open(pack_name, O_CREAT|O_EXCL|O_RDWR, 0600);
			if (output_fd < 0)
				die_errno(_("unable to create '%s'"), pack_name);
		}
		nothread_data.pack_fd = output_fd;
	} else {
		input_fd = open(pack_name, O_RDONLY);
		if (input_fd < 0)
			die_errno(_("cannot open packfile '%s'"), pack_name);
		output_fd = -1;
		nothread_data.pack_fd = input_fd;
	}
	git_SHA1_Init(&input_ctx);
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

static NORETURN void bad_object(off_t offset, const char *format,
		       ...) __attribute__((format (printf, 2, 3)));

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
#ifndef NO_PTHREADS
	if (threads_active)
		return pthread_getspecific(key);
	assert(!threads_active &&
	       "This should only be reached when all threads are gone");
#endif
	return &nothread_data;
}

#ifndef NO_PTHREADS
static void set_thread_data(struct thread_local *data)
{
	if (threads_active)
		pthread_setspecific(key, data);
}
#endif

static struct base_data *alloc_base_data(void)
{
	struct base_data *base = xcalloc(1, sizeof(struct base_data));
	base->ref_last = -1;
	base->ofs_last = -1;
	return base;
}

static void free_base_data(struct base_data *c)
{
	if (c->data) {
		free(c->data);
		c->data = NULL;
		get_thread_data()->base_cache_used -= c->size;
	}
}

static void prune_base_data(struct base_data *retain)
{
	struct base_data *b;
	struct thread_local *data = get_thread_data();
	for (b = data->base_cache;
	     data->base_cache_used > delta_base_cache_limit && b;
	     b = b->child) {
		if (b->data && b != retain)
			free_base_data(b);
	}
}

static void link_base_data(struct base_data *base, struct base_data *c)
{
	if (base)
		base->child = c;
	else
		get_thread_data()->base_cache = c;

	c->base = base;
	c->child = NULL;
	if (c->data)
		get_thread_data()->base_cache_used += c->size;
	prune_base_data(c);
}

static void unlink_base_data(struct base_data *c)
{
	struct base_data *base = c->base;
	if (base)
		base->child = NULL;
	else
		get_thread_data()->base_cache = NULL;
	free_base_data(c);
}

static int is_delta_type(enum object_type type)
{
	return (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA);
}

static void *unpack_entry_data(off_t offset, unsigned long size,
			       enum object_type type, unsigned char *sha1)
{
	static char fixed_buf[8192];
	int status;
	git_zstream stream;
	void *buf;
	git_SHA_CTX c;
	char hdr[32];
	int hdrlen;

	if (!is_delta_type(type)) {
		hdrlen = xsnprintf(hdr, sizeof(hdr), "%s %lu", typename(type), size) + 1;
		git_SHA1_Init(&c);
		git_SHA1_Update(&c, hdr, hdrlen);
	} else
		sha1 = NULL;
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
		if (sha1)
			git_SHA1_Update(&c, last_out, stream.next_out - last_out);
		if (buf == fixed_buf) {
			stream.next_out = buf;
			stream.avail_out = sizeof(fixed_buf);
		}
	} while (status == Z_OK);
	if (stream.total_out != size || status != Z_STREAM_END)
		bad_object(offset, _("inflate returned %d"), status);
	git_inflate_end(&stream);
	if (sha1)
		git_SHA1_Final(sha1, &c);
	return buf == fixed_buf ? NULL : buf;
}

static void *unpack_raw_entry(struct object_entry *obj,
			      off_t *ofs_offset,
			      unsigned char *ref_sha1,
			      unsigned char *sha1)
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
		hashcpy(ref_sha1, fill(20));
		use(20);
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

	data = unpack_entry_data(obj->idx.offset, obj->size, obj->type, sha1);
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
			       (unsigned int)len),
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
		free(data);
		data = NULL;
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

static int find_ofs_delta(const off_t offset, enum object_type type)
{
	int first = 0, last = nr_ofs_deltas;

	while (first < last) {
		int next = (first + last) / 2;
		struct ofs_delta_entry *delta = &ofs_deltas[next];
		int cmp;

		cmp = compare_ofs_delta_bases(offset, delta->offset,
					      type, objects[delta->obj_no].type);
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
				    int *first_index, int *last_index,
				    enum object_type type)
{
	int first = find_ofs_delta(offset, type);
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

static int compare_ref_delta_bases(const unsigned char *sha1,
				   const unsigned char *sha2,
				   enum object_type type1,
				   enum object_type type2)
{
	int cmp = type1 - type2;
	if (cmp)
		return cmp;
	return hashcmp(sha1, sha2);
}

static int find_ref_delta(const unsigned char *sha1, enum object_type type)
{
	int first = 0, last = nr_ref_deltas;

	while (first < last) {
		int next = (first + last) / 2;
		struct ref_delta_entry *delta = &ref_deltas[next];
		int cmp;

		cmp = compare_ref_delta_bases(sha1, delta->sha1,
					      type, objects[delta->obj_no].type);
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

static void find_ref_delta_children(const unsigned char *sha1,
				    int *first_index, int *last_index,
				    enum object_type type)
{
	int first = find_ref_delta(sha1, type);
	int last = first;
	int end = nr_ref_deltas - 1;

	if (first < 0) {
		*first_index = 0;
		*last_index = -1;
		return;
	}
	while (first > 0 && !hashcmp(ref_deltas[first - 1].sha1, sha1))
		--first;
	while (last < end && !hashcmp(ref_deltas[last + 1].sha1, sha1))
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
			    sha1_to_hex(data->entry->idx.sha1));
		if (len < 0)
			die(_("unable to read %s"),
			    sha1_to_hex(data->entry->idx.sha1));
		if (memcmp(buf, data->buf, len))
			die(_("SHA1 COLLISION FOUND WITH %s !"),
			    sha1_to_hex(data->entry->idx.sha1));
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
	data.st = open_istream(entry->idx.sha1, &type, &size, NULL);
	if (!data.st)
		return -1;
	if (size != entry->size || type != entry->type)
		die(_("SHA1 COLLISION FOUND WITH %s !"),
		    sha1_to_hex(entry->idx.sha1));
	unpack_data(entry, compare_objects, &data);
	close_istream(data.st);
	free(data.buf);
	return 0;
}

static void sha1_object(const void *data, struct object_entry *obj_entry,
			unsigned long size, enum object_type type,
			const unsigned char *sha1)
{
	void *new_data = NULL;
	int collision_test_needed = 0;

	assert(data || obj_entry);

	if (startup_info->have_repository) {
		read_lock();
		collision_test_needed = has_sha1_file_with_flags(sha1, HAS_SHA1_QUICK);
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
		has_type = sha1_object_info(sha1, &has_size);
		if (has_type != type || has_size != size)
			die(_("SHA1 COLLISION FOUND WITH %s !"), sha1_to_hex(sha1));
		has_data = read_sha1_file(sha1, &has_type, &has_size);
		read_unlock();
		if (!data)
			data = new_data = get_data_from_pack(obj_entry);
		if (!has_data)
			die(_("cannot read existing object %s"), sha1_to_hex(sha1));
		if (size != has_size || type != has_type ||
		    memcmp(data, has_data, size) != 0)
			die(_("SHA1 COLLISION FOUND WITH %s !"), sha1_to_hex(sha1));
		free(has_data);
	}

	if (strict) {
		read_lock();
		if (type == OBJ_BLOB) {
			struct blob *blob = lookup_blob(sha1);
			if (blob)
				blob->object.flags |= FLAG_CHECKED;
			else
				die(_("invalid blob object %s"), sha1_to_hex(sha1));
		} else {
			struct object *obj;
			int eaten;
			void *buf = (void *) data;

			assert(data && "data can only be NULL for large _blobs_");

			/*
			 * we do not need to free the memory here, as the
			 * buf is deleted by the caller.
			 */
			obj = parse_object_buffer(sha1, type, size, buf, &eaten);
			if (!obj)
				die(_("invalid %s"), typename(type));
			if (do_fsck_object &&
			    fsck_object(obj, buf, size, &fsck_options))
				die(_("Error in object"));
			if (fsck_walk(obj, NULL, &fsck_options))
				die(_("Not all child objects of %s are reachable"), oid_to_hex(&obj->oid));

			if (obj->type == OBJ_TREE) {
				struct tree *item = (struct tree *) obj;
				item->buffer = NULL;
				obj->parsed = 0;
			}
			if (obj->type == OBJ_COMMIT) {
				struct commit *commit = (struct commit *) obj;
				if (detach_commit_buffer(commit, NULL) != data)
					die("BUG: parse_object_buffer transmogrified our buffer");
			}
			obj->flags |= FLAG_CHECKED;
		}
		read_unlock();
	}

	free(new_data);
}

/*
 * This function is part of find_unresolved_deltas(). There are two
 * walkers going in the opposite ways.
 *
 * The first one in find_unresolved_deltas() traverses down from
 * parent node to children, deflating nodes along the way. However,
 * memory for deflated nodes is limited by delta_base_cache_limit, so
 * at some point parent node's deflated content may be freed.
 *
 * The second walker is this function, which goes from current node up
 * to top parent if necessary to deflate the node. In normal
 * situation, its parent node would be already deflated, so it just
 * needs to apply delta.
 *
 * In the worst case scenario, parent node is no longer deflated because
 * we're running out of delta_base_cache_limit; we need to re-deflate
 * parents, possibly up to the top base.
 *
 * All deflated objects here are subject to be freed if we exceed
 * delta_base_cache_limit, just like in find_unresolved_deltas(), we
 * just need to make sure the last node is not freed.
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
			get_thread_data()->base_cache_used += c->size;
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
			get_thread_data()->base_cache_used += c->size;
			prune_base_data(c);
		}
		free(delta);
	}
	return c->data;
}

static void resolve_delta(struct object_entry *delta_obj,
			  struct base_data *base, struct base_data *result)
{
	void *base_data, *delta_data;

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
	base_data = get_base_data(base);
	result->obj = delta_obj;
	result->data = patch_delta(base_data, base->size,
				   delta_data, delta_obj->size, &result->size);
	free(delta_data);
	if (!result->data)
		bad_object(delta_obj->idx.offset, _("failed to apply delta"));
	hash_sha1_file(result->data, result->size,
		       typename(delta_obj->real_type), delta_obj->idx.sha1);
	sha1_object(result->data, NULL, result->size, delta_obj->real_type,
		    delta_obj->idx.sha1);
	counter_lock();
	nr_resolved_deltas++;
	counter_unlock();
}

/*
 * Standard boolean compare-and-swap: atomically check whether "*type" is
 * "want"; if so, swap in "set" and return true. Otherwise, leave it untouched
 * and return false.
 */
static int compare_and_swap_type(signed char *type,
				 enum object_type want,
				 enum object_type set)
{
	enum object_type old;

	type_cas_lock();
	old = *type;
	if (old == want)
		*type = set;
	type_cas_unlock();

	return old == want;
}

static struct base_data *find_unresolved_deltas_1(struct base_data *base,
						  struct base_data *prev_base)
{
	if (base->ref_last == -1 && base->ofs_last == -1) {
		find_ref_delta_children(base->obj->idx.sha1,
					&base->ref_first, &base->ref_last,
					OBJ_REF_DELTA);

		find_ofs_delta_children(base->obj->idx.offset,
					&base->ofs_first, &base->ofs_last,
					OBJ_OFS_DELTA);

		if (base->ref_last == -1 && base->ofs_last == -1) {
			free(base->data);
			return NULL;
		}

		link_base_data(prev_base, base);
	}

	if (base->ref_first <= base->ref_last) {
		struct object_entry *child = objects + ref_deltas[base->ref_first].obj_no;
		struct base_data *result = alloc_base_data();

		if (!compare_and_swap_type(&child->real_type, OBJ_REF_DELTA,
					   base->obj->real_type))
			die("BUG: child->real_type != OBJ_REF_DELTA");

		resolve_delta(child, base, result);
		if (base->ref_first == base->ref_last && base->ofs_last == -1)
			free_base_data(base);

		base->ref_first++;
		return result;
	}

	if (base->ofs_first <= base->ofs_last) {
		struct object_entry *child = objects + ofs_deltas[base->ofs_first].obj_no;
		struct base_data *result = alloc_base_data();

		assert(child->real_type == OBJ_OFS_DELTA);
		child->real_type = base->obj->real_type;
		resolve_delta(child, base, result);
		if (base->ofs_first == base->ofs_last)
			free_base_data(base);

		base->ofs_first++;
		return result;
	}

	unlink_base_data(base);
	return NULL;
}

static void find_unresolved_deltas(struct base_data *base)
{
	struct base_data *new_base, *prev_base = NULL;
	for (;;) {
		new_base = find_unresolved_deltas_1(base, prev_base);

		if (new_base) {
			prev_base = base;
			base = new_base;
		} else {
			free(base);
			base = prev_base;
			if (!base)
				return;
			prev_base = base->base;
		}
	}
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

	return hashcmp(delta_a->sha1, delta_b->sha1);
}

static void resolve_base(struct object_entry *obj)
{
	struct base_data *base_obj = alloc_base_data();
	base_obj->obj = obj;
	base_obj->data = NULL;
	find_unresolved_deltas(base_obj);
}

#ifndef NO_PTHREADS
static void *threaded_second_pass(void *data)
{
	set_thread_data(data);
	for (;;) {
		int i;
		counter_lock();
		display_progress(progress, nr_resolved_deltas);
		counter_unlock();
		work_lock();
		while (nr_dispatched < nr_objects &&
		       is_delta_type(objects[nr_dispatched].type))
			nr_dispatched++;
		if (nr_dispatched >= nr_objects) {
			work_unlock();
			break;
		}
		i = nr_dispatched++;
		work_unlock();

		resolve_base(&objects[i]);
	}
	return NULL;
}
#endif

/*
 * First pass:
 * - find locations of all objects;
 * - calculate SHA1 of all non-delta objects;
 * - remember base (SHA1 or offset) for all deltas.
 */
static void parse_pack_objects(unsigned char *sha1)
{
	int i, nr_delays = 0;
	struct ofs_delta_entry *ofs_delta = ofs_deltas;
	unsigned char ref_delta_sha1[20];
	struct stat st;

	if (verbose)
		progress = start_progress(
				from_stdin ? _("Receiving objects") : _("Indexing objects"),
				nr_objects);
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		void *data = unpack_raw_entry(obj, &ofs_delta->offset,
					      ref_delta_sha1, obj->idx.sha1);
		obj->real_type = obj->type;
		if (obj->type == OBJ_OFS_DELTA) {
			nr_ofs_deltas++;
			ofs_delta->obj_no = i;
			ofs_delta++;
		} else if (obj->type == OBJ_REF_DELTA) {
			ALLOC_GROW(ref_deltas, nr_ref_deltas + 1, ref_deltas_alloc);
			hashcpy(ref_deltas[nr_ref_deltas].sha1, ref_delta_sha1);
			ref_deltas[nr_ref_deltas].obj_no = i;
			nr_ref_deltas++;
		} else if (!data) {
			/* large blobs, check later */
			obj->real_type = OBJ_BAD;
			nr_delays++;
		} else
			sha1_object(data, NULL, obj->size, obj->type, obj->idx.sha1);
		free(data);
		display_progress(progress, i+1);
	}
	objects[i].idx.offset = consumed_bytes;
	stop_progress(&progress);

	/* Check pack integrity */
	flush();
	git_SHA1_Final(sha1, &input_ctx);
	if (hashcmp(fill(20), sha1))
		die(_("pack is corrupted (SHA1 mismatch)"));
	use(20);

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
		sha1_object(NULL, obj, obj->size, obj->type, obj->idx.sha1);
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

#ifndef NO_PTHREADS
	nr_dispatched = 0;
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
#endif

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];

		if (is_delta_type(obj->type))
			continue;
		resolve_base(obj);
		display_progress(progress, nr_resolved_deltas);
	}
}

/*
 * Third pass:
 * - append objects to convert thin pack to full pack if required
 * - write the final 20-byte SHA-1
 */
static void fix_unresolved_deltas(struct sha1file *f);
static void conclude_pack(int fix_thin_pack, const char *curr_pack, unsigned char *pack_sha1)
{
	if (nr_ref_deltas + nr_ofs_deltas == nr_resolved_deltas) {
		stop_progress(&progress);
		/* Flush remaining pack final 20-byte SHA1. */
		flush();
		return;
	}

	if (fix_thin_pack) {
		struct sha1file *f;
		unsigned char read_sha1[20], tail_sha1[20];
		struct strbuf msg = STRBUF_INIT;
		int nr_unresolved = nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas;
		int nr_objects_initial = nr_objects;
		if (nr_unresolved <= 0)
			die(_("confusion beyond insanity"));
		REALLOC_ARRAY(objects, nr_objects + nr_unresolved + 1);
		memset(objects + nr_objects + 1, 0,
		       nr_unresolved * sizeof(*objects));
		f = sha1fd(output_fd, curr_pack);
		fix_unresolved_deltas(f);
		strbuf_addf(&msg, Q_("completed with %d local object",
				     "completed with %d local objects",
				     nr_objects - nr_objects_initial),
			    nr_objects - nr_objects_initial);
		stop_progress_msg(&progress, msg.buf);
		strbuf_release(&msg);
		sha1close(f, tail_sha1, 0);
		hashcpy(read_sha1, pack_sha1);
		fixup_pack_header_footer(output_fd, pack_sha1,
					 curr_pack, nr_objects,
					 read_sha1, consumed_bytes-20);
		if (hashcmp(read_sha1, tail_sha1) != 0)
			die(_("Unexpected tail checksum for %s "
			      "(disk corruption?)"), curr_pack);
	}
	if (nr_ofs_deltas + nr_ref_deltas != nr_resolved_deltas)
		die(Q_("pack has %d unresolved delta",
		       "pack has %d unresolved deltas",
		       nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas),
		    nr_ofs_deltas + nr_ref_deltas - nr_resolved_deltas);
}

static int write_compressed(struct sha1file *f, void *in, unsigned int size)
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
		sha1write(f, outbuf, sizeof(outbuf) - stream.avail_out);
	} while (status == Z_OK);

	if (status != Z_STREAM_END)
		die(_("unable to deflate appended object (%d)"), status);
	size = stream.total_out;
	git_deflate_end(&stream);
	return size;
}

static struct object_entry *append_obj_to_pack(struct sha1file *f,
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
	sha1write(f, header, n);
	obj[0].size = size;
	obj[0].hdr_size = n;
	obj[0].type = type;
	obj[0].real_type = type;
	obj[1].idx.offset = obj[0].idx.offset + n;
	obj[1].idx.offset += write_compressed(f, buf, size);
	obj[0].idx.crc32 = crc32_end(f);
	sha1flush(f);
	hashcpy(obj->idx.sha1, sha1);
	return obj;
}

static int delta_pos_compare(const void *_a, const void *_b)
{
	struct ref_delta_entry *a = *(struct ref_delta_entry **)_a;
	struct ref_delta_entry *b = *(struct ref_delta_entry **)_b;
	return a->obj_no - b->obj_no;
}

static void fix_unresolved_deltas(struct sha1file *f)
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

	for (i = 0; i < nr_ref_deltas; i++) {
		struct ref_delta_entry *d = sorted_by_pos[i];
		enum object_type type;
		struct base_data *base_obj = alloc_base_data();

		if (objects[d->obj_no].real_type != OBJ_REF_DELTA)
			continue;
		base_obj->data = read_sha1_file(d->sha1, &type, &base_obj->size);
		if (!base_obj->data)
			continue;

		if (check_sha1_signature(d->sha1, base_obj->data,
				base_obj->size, typename(type)))
			die(_("local object %s is corrupt"), sha1_to_hex(d->sha1));
		base_obj->obj = append_obj_to_pack(f, d->sha1,
					base_obj->data, base_obj->size, type);
		find_unresolved_deltas(base_obj);
		display_progress(progress, nr_resolved_deltas);
	}
	free(sorted_by_pos);
}

static void final(const char *final_pack_name, const char *curr_pack_name,
		  const char *final_index_name, const char *curr_index_name,
		  const char *keep_name, const char *keep_msg,
		  unsigned char *sha1)
{
	const char *report = "pack";
	struct strbuf pack_name = STRBUF_INIT;
	struct strbuf index_name = STRBUF_INIT;
	struct strbuf keep_name_buf = STRBUF_INIT;
	int err;

	if (!from_stdin) {
		close(input_fd);
	} else {
		fsync_or_die(output_fd, curr_pack_name);
		err = close(output_fd);
		if (err)
			die_errno(_("error while closing pack file"));
	}

	if (keep_msg) {
		int keep_fd, keep_msg_len = strlen(keep_msg);

		if (!keep_name)
			keep_name = odb_pack_name(&keep_name_buf, sha1, "keep");

		keep_fd = odb_pack_keep(keep_name);
		if (keep_fd < 0) {
			if (errno != EEXIST)
				die_errno(_("cannot write keep file '%s'"),
					  keep_name);
		} else {
			if (keep_msg_len > 0) {
				write_or_die(keep_fd, keep_msg, keep_msg_len);
				write_or_die(keep_fd, "\n", 1);
			}
			if (close(keep_fd) != 0)
				die_errno(_("cannot close written keep file '%s'"),
					  keep_name);
			report = "keep";
		}
	}

	if (final_pack_name != curr_pack_name) {
		if (!final_pack_name)
			final_pack_name = odb_pack_name(&pack_name, sha1, "pack");
		if (finalize_object_file(curr_pack_name, final_pack_name))
			die(_("cannot store pack file"));
	} else if (from_stdin)
		chmod(final_pack_name, 0444);

	if (final_index_name != curr_index_name) {
		if (!final_index_name)
			final_index_name = odb_pack_name(&index_name, sha1, "idx");
		if (finalize_object_file(curr_index_name, final_index_name))
			die(_("cannot store index file"));
	} else
		chmod(final_index_name, 0444);

	if (!from_stdin) {
		printf("%s\n", sha1_to_hex(sha1));
	} else {
		struct strbuf buf = STRBUF_INIT;

		strbuf_addf(&buf, "%s\t%s\n", report, sha1_to_hex(sha1));
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

	strbuf_release(&index_name);
	strbuf_release(&pack_name);
	strbuf_release(&keep_name_buf);
}

static int git_index_pack_config(const char *k, const char *v, void *cb)
{
	struct pack_idx_option *opts = cb;

	if (!strcmp(k, "pack.indexversion")) {
		opts->version = git_config_int(k, v);
		if (opts->version > 2)
			die(_("bad pack.indexversion=%"PRIu32), opts->version);
		return 0;
	}
	if (!strcmp(k, "pack.threads")) {
		nr_threads = git_config_int(k, v);
		if (nr_threads < 0)
			die(_("invalid number of threads specified (%d)"),
			    nr_threads);
#ifdef NO_PTHREADS
		if (nr_threads != 1)
			warning(_("no threads support, ignoring %s"), k);
		nr_threads = 1;
#endif
		return 0;
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
	idx1 = (((const uint32_t *)p->index_data)
		+ 2 /* 8-byte header */
		+ 256 /* fan out */
		+ 5 * p->num_objects /* 20-byte SHA-1 table */
		+ p->num_objects /* CRC32 table */
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
	 * sha1_file.c, perhaps?  It shouldn't matter very much as we
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
		chain_histogram = xcalloc(deepest_delta, sizeof(unsigned long));

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];

		if (is_delta_type(obj->type))
			chain_histogram[obj_stat[i].delta_depth - 1]++;
		if (stat_only)
			continue;
		printf("%s %-6s %lu %lu %"PRIuMAX,
		       sha1_to_hex(obj->idx.sha1),
		       typename(obj->real_type), obj->size,
		       (unsigned long)(obj[1].idx.offset - obj->idx.offset),
		       (uintmax_t)obj->idx.offset);
		if (is_delta_type(obj->type)) {
			struct object_entry *bobj = &objects[obj_stat[i].base_object_no];
			printf(" %u %s", obj_stat[i].delta_depth, sha1_to_hex(bobj->idx.sha1));
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
}

static const char *derive_filename(const char *pack_name, const char *suffix,
				   struct strbuf *buf)
{
	size_t len;
	if (!strip_suffix(pack_name, ".pack", &len))
		die(_("packfile name '%s' does not end with '.pack'"),
		    pack_name);
	strbuf_add(buf, pack_name, len);
	strbuf_addstr(buf, suffix);
	return buf->buf;
}

int cmd_index_pack(int argc, const char **argv, const char *prefix)
{
	int i, fix_thin_pack = 0, verify = 0, stat_only = 0;
	const char *curr_index;
	const char *index_name = NULL, *pack_name = NULL;
	const char *keep_name = NULL, *keep_msg = NULL;
	struct strbuf index_name_buf = STRBUF_INIT,
		      keep_name_buf = STRBUF_INIT;
	struct pack_idx_entry **idx_objects;
	struct pack_idx_option opts;
	unsigned char pack_sha1[20];
	unsigned foreign_nr = 1;	/* zero is a "good" value, assume bad */
	int report_end_of_input = 0;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(index_pack_usage);

	check_replace_refs = 0;
	fsck_options.walk = mark_link;

	reset_pack_idx_option(&opts);
	git_config(git_index_pack_config, &opts);
	if (prefix && chdir(prefix))
		die(_("Cannot come back to cwd"));

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "--stdin")) {
				from_stdin = 1;
			} else if (!strcmp(arg, "--fix-thin")) {
				fix_thin_pack = 1;
			} else if (!strcmp(arg, "--strict")) {
				strict = 1;
				do_fsck_object = 1;
			} else if (skip_prefix(arg, "--strict=", &arg)) {
				strict = 1;
				do_fsck_object = 1;
				fsck_set_msg_types(&fsck_options, arg);
			} else if (!strcmp(arg, "--check-self-contained-and-connected")) {
				strict = 1;
				check_self_contained_and_connected = 1;
			} else if (!strcmp(arg, "--verify")) {
				verify = 1;
			} else if (!strcmp(arg, "--verify-stat")) {
				verify = 1;
				show_stat = 1;
			} else if (!strcmp(arg, "--verify-stat-only")) {
				verify = 1;
				show_stat = 1;
				stat_only = 1;
			} else if (!strcmp(arg, "--keep")) {
				keep_msg = "";
			} else if (starts_with(arg, "--keep=")) {
				keep_msg = arg + 7;
			} else if (starts_with(arg, "--threads=")) {
				char *end;
				nr_threads = strtoul(arg+10, &end, 0);
				if (!arg[10] || *end || nr_threads < 0)
					usage(index_pack_usage);
#ifdef NO_PTHREADS
				if (nr_threads != 1)
					warning(_("no threads support, "
						  "ignoring %s"), arg);
				nr_threads = 1;
#endif
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
		die(_("--fix-thin cannot be used without --stdin"));
	if (from_stdin && !startup_info->have_repository)
		die(_("--stdin requires a git repository"));
	if (!index_name && pack_name)
		index_name = derive_filename(pack_name, ".idx", &index_name_buf);
	if (keep_msg && !keep_name && pack_name)
		keep_name = derive_filename(pack_name, ".keep", &keep_name_buf);

	if (verify) {
		if (!index_name)
			die(_("--verify with no packfile name given"));
		read_idx_option(&opts, index_name);
		opts.flags |= WRITE_IDX_VERIFY | WRITE_IDX_STRICT;
	}
	if (strict)
		opts.flags |= WRITE_IDX_STRICT;

#ifndef NO_PTHREADS
	if (!nr_threads) {
		nr_threads = online_cpus();
		/* An experiment showed that more threads does not mean faster */
		if (nr_threads > 3)
			nr_threads = 3;
	}
#endif

	curr_pack = open_pack_file(pack_name);
	parse_pack_header();
	objects = xcalloc(st_add(nr_objects, 1), sizeof(struct object_entry));
	if (show_stat)
		obj_stat = xcalloc(st_add(nr_objects, 1), sizeof(struct object_stat));
	ofs_deltas = xcalloc(nr_objects, sizeof(struct ofs_delta_entry));
	parse_pack_objects(pack_sha1);
	if (report_end_of_input)
		write_in_full(2, "\0", 1);
	resolve_deltas();
	conclude_pack(fix_thin_pack, curr_pack, pack_sha1);
	free(ofs_deltas);
	free(ref_deltas);
	if (strict)
		foreign_nr = check_objects();

	if (show_stat)
		show_pack_info(stat_only);

	ALLOC_ARRAY(idx_objects, nr_objects);
	for (i = 0; i < nr_objects; i++)
		idx_objects[i] = &objects[i].idx;
	curr_index = write_idx_file(index_name, idx_objects, nr_objects, &opts, pack_sha1);
	free(idx_objects);

	if (!verify)
		final(pack_name, curr_pack,
		      index_name, curr_index,
		      keep_name, keep_msg,
		      pack_sha1);
	else
		close(input_fd);
	free(objects);
	strbuf_release(&index_name_buf);
	strbuf_release(&keep_name_buf);
	if (pack_name == NULL)
		free((void *) curr_pack);
	if (index_name == NULL)
		free((void *) curr_index);

	/*
	 * Let the caller know this pack is not self contained
	 */
	if (check_self_contained_and_connected && foreign_nr)
		return 1;

	return 0;
}
