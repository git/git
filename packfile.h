#ifndef PACKFILE_H
#define PACKFILE_H

#include "list.h"
#include "object.h"
#include "odb.h"
#include "oidset.h"
#include "repository.h"
#include "strmap.h"

/* in odb.h */
struct object_info;
struct odb_read_stream;

struct packed_git {
	struct pack_window *windows;
	off_t pack_size;
	const void *index_data;
	size_t index_size;
	uint32_t num_objects;
	size_t crc_offset;
	struct oidset bad_objects;
	int index_version;
	time_t mtime;
	int pack_fd;
	int index;              /* for builtin/pack-objects.c */
	unsigned pack_local:1,
		 pack_keep:1,
		 pack_keep_in_core:1,
		 freshened:1,
		 do_not_close:1,
		 pack_promisor:1,
		 multi_pack_index:1,
		 is_cruft:1;
	unsigned char hash[GIT_MAX_RAWSZ];
	struct revindex_entry *revindex;
	const uint32_t *revindex_data;
	const uint32_t *revindex_map;
	size_t revindex_size;
	/*
	 * mtimes_map points at the beginning of the memory mapped region of
	 * this pack's corresponding .mtimes file, and mtimes_size is the size
	 * of that .mtimes file
	 */
	const uint32_t *mtimes_map;
	size_t mtimes_size;

	/* repo denotes the repository this packfile belongs to */
	struct repository *repo;

	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
};

struct packfile_list {
	struct packfile_list_entry *head, *tail;
};

struct packfile_list_entry {
	struct packfile_list_entry *next;
	struct packed_git *pack;
};

void packfile_list_clear(struct packfile_list *list);
void packfile_list_remove(struct packfile_list *list, struct packed_git *pack);
void packfile_list_prepend(struct packfile_list *list, struct packed_git *pack);
void packfile_list_append(struct packfile_list *list, struct packed_git *pack);

/*
 * Find the pack within the "packs" list whose index contains the object
 * "oid". For general object lookups, you probably don't want this; use
 * find_pack_entry() instead.
 */
struct packed_git *packfile_list_find_oid(struct packfile_list_entry *packs,
					  const struct object_id *oid);

/*
 * A store that manages packfiles for a given object database.
 */
struct packfile_store {
	struct odb_source *source;

	/*
	 * The list of packfiles in the order in which they have been most
	 * recently used.
	 */
	struct packfile_list packs;

	/*
	 * Cache of packfiles which are marked as "kept", either because there
	 * is an on-disk ".keep" file or because they are marked as "kept" in
	 * memory.
	 *
	 * Should not be accessed directly, but via
	 * `packfile_store_get_kept_pack_cache()`. The list of packs gets
	 * invalidated when the stored flags and the flags passed to
	 * `packfile_store_get_kept_pack_cache()` mismatch.
	 */
	struct {
		struct packed_git **packs;
		unsigned flags;
	} kept_cache;

	/* The multi-pack index that belongs to this specific packfile store. */
	struct multi_pack_index *midx;

	/*
	 * A map of packfile names to packed_git structs for tracking which
	 * packs have been loaded already.
	 */
	struct strmap packs_by_path;

	/*
	 * Whether packfiles have already been populated with this store's
	 * packs.
	 */
	bool initialized;

	/*
	 * Usually, packfiles will be reordered to the front of the `packs`
	 * list whenever an object is looked up via them. This has the effect
	 * that packs that contain a lot of accessed objects will be located
	 * towards the front.
	 *
	 * This is usually desireable, but there are exceptions. One exception
	 * is when the looking up multiple objects in a loop for each packfile.
	 * In that case, we may easily end up with an infinite loop as the
	 * packfiles get reordered to the front repeatedly.
	 *
	 * Setting this field to `true` thus disables these reorderings.
	 */
	bool skip_mru_updates;
};

/*
 * Allocate and initialize a new empty packfile store for the given object
 * database source.
 */
struct packfile_store *packfile_store_new(struct odb_source *source);

/*
 * Free the packfile store and all its associated state. All packfiles
 * tracked by the store will be closed.
 */
void packfile_store_free(struct packfile_store *store);

/*
 * Close all packfiles associated with this store. The packfiles won't be
 * free'd, so they can be re-opened at a later point in time.
 */
void packfile_store_close(struct packfile_store *store);

/*
 * Prepare the packfile store by loading packfiles and multi-pack indices for
 * all alternates. This becomes a no-op if the store is already prepared.
 *
 * It shouldn't typically be necessary to call this function directly, as
 * functions that access the store know to prepare it.
 */
void packfile_store_prepare(struct packfile_store *store);

/*
 * Clear the packfile caches and try to look up any new packfiles that have
 * appeared since last preparing the packfiles store.
 *
 * This function must be called under the `odb_read_lock()`.
 */
void packfile_store_reprepare(struct packfile_store *store);

/*
 * Add the pack to the store so that contained objects become accessible via
 * the store. This moves ownership into the store.
 */
void packfile_store_add_pack(struct packfile_store *store,
			     struct packed_git *pack);

/*
 * Get all packs managed by the given store, including packfiles that are
 * referenced by multi-pack indices.
 */
struct packfile_list_entry *packfile_store_get_packs(struct packfile_store *store);

struct repo_for_each_pack_data {
	struct odb_source *source;
	struct packfile_list_entry *entry;
};

static inline struct repo_for_each_pack_data repo_for_eack_pack_data_init(struct repository *repo)
{
	struct repo_for_each_pack_data data = { 0 };

	odb_prepare_alternates(repo->objects);

	for (struct odb_source *source = repo->objects->sources; source; source = source->next) {
		struct packfile_list_entry *entry = packfile_store_get_packs(source->packfiles);
		if (!entry)
			continue;
		data.source = source;
		data.entry = entry;
		break;
	}

	return data;
}

static inline void repo_for_each_pack_data_next(struct repo_for_each_pack_data *data)
{
	struct odb_source *source;

	data->entry = data->entry->next;
	if (data->entry)
		return;

	for (source = data->source->next; source; source = source->next) {
		struct packfile_list_entry *entry = packfile_store_get_packs(source->packfiles);
		if (!entry)
			continue;
		data->source = source;
		data->entry = entry;
		return;
	}

	data->source = NULL;
	data->entry = NULL;
}

/*
 * Load and iterate through all packs of the given repository. This helper
 * function will yield packfiles from all object sources connected to the
 * repository.
 */
#define repo_for_each_pack(repo, p) \
	for (struct repo_for_each_pack_data eack_pack_data = repo_for_eack_pack_data_init(repo); \
	     ((p) = (eack_pack_data.entry ? eack_pack_data.entry->pack : NULL)); \
	     repo_for_each_pack_data_next(&eack_pack_data))

int packfile_store_read_object_stream(struct odb_read_stream **out,
				      struct packfile_store *store,
				      const struct object_id *oid);

/*
 * Try to read the object identified by its ID from the object store and
 * populate the object info with its data. Returns 1 in case the object was
 * not found, 0 if it was and read successfully, and a negative error code in
 * case the object was corrupted.
 */
int packfile_store_read_object_info(struct packfile_store *store,
				    const struct object_id *oid,
				    struct object_info *oi,
				    unsigned flags);

/*
 * Open the packfile and add it to the store if it isn't yet known. Returns
 * either the newly opened packfile or the preexisting packfile. Returns a
 * `NULL` pointer in case the packfile could not be opened.
 */
struct packed_git *packfile_store_load_pack(struct packfile_store *store,
					    const char *idx_path, int local);

int packfile_store_freshen_object(struct packfile_store *store,
				  const struct object_id *oid);

enum kept_pack_type {
	KEPT_PACK_ON_DISK = (1 << 0),
	KEPT_PACK_IN_CORE = (1 << 1),
};

/*
 * Retrieve the cache of kept packs from the given packfile store. Accepts a
 * combination of `kept_pack_type` flags. The cache is computed on demand and
 * will be recomputed whenever the flags change.
 */
struct packed_git **packfile_store_get_kept_pack_cache(struct packfile_store *store,
						       unsigned flags);

struct pack_window {
	struct pack_window *next;
	unsigned char *base;
	off_t offset;
	size_t len;
	unsigned int last_used;
	unsigned int inuse_cnt;
};

struct pack_entry {
	off_t offset;
	struct packed_git *p;
};

/*
 * Generate the filename to be used for a pack file with checksum "sha1" and
 * extension "ext". The result is written into the strbuf "buf", overwriting
 * any existing contents. A pointer to buf->buf is returned as a convenience.
 *
 * Example: odb_pack_name(out, sha1, "idx") => ".git/objects/pack/pack-1234..idx"
 */
char *odb_pack_name(struct repository *r, struct strbuf *buf,
		    const unsigned char *hash, const char *ext);

/*
 * Return the basename of the packfile, omitting any containing directory
 * (e.g., "pack-1234abcd[...].pack").
 */
const char *pack_basename(struct packed_git *p);

/*
 * Parse the pack idx file found at idx_path and create a packed_git struct
 * which can be used with find_pack_entry_one().
 *
 * You probably don't want to use this function! It skips most of the normal
 * sanity checks (including whether we even have the matching .pack file),
 * and does not add the resulting packed_git struct to the internal list of
 * packs. You probably want add_packed_git() instead.
 */
struct packed_git *parse_pack_index(struct repository *r, unsigned char *sha1,
				    const char *idx_path);

typedef void each_file_in_pack_dir_fn(const char *full_path, size_t full_path_len,
				      const char *file_name, void *data);
void for_each_file_in_pack_subdir(const char *objdir,
				  const char *subdir,
				  each_file_in_pack_dir_fn fn,
				  void *data);
void for_each_file_in_pack_dir(const char *objdir,
			       each_file_in_pack_dir_fn fn,
			       void *data);

/*
 * Iterate over all accessible packed objects without respect to reachability.
 * By default, this includes both local and alternate packs.
 *
 * Note that some objects may appear twice if they are found in multiple packs.
 * Each pack is visited in an unspecified order. By default, objects within a
 * pack are visited in pack-idx order (i.e., sorted by oid).
 */
typedef int each_packed_object_fn(const struct object_id *oid,
				  struct packed_git *pack,
				  uint32_t pos,
				  void *data);
int for_each_object_in_pack(struct packed_git *p,
			    each_packed_object_fn, void *data,
			    enum for_each_object_flags flags);
int for_each_packed_object(struct repository *repo, each_packed_object_fn cb,
			   void *data, enum for_each_object_flags flags);

/* A hook to report invalid files in pack directory */
#define PACKDIR_FILE_PACK 1
#define PACKDIR_FILE_IDX 2
#define PACKDIR_FILE_GARBAGE 4
extern void (*report_garbage)(unsigned seen_bits, const char *path);

/*
 * Give a rough count of objects in the repository. This sacrifices accuracy
 * for speed.
 */
unsigned long repo_approximate_object_count(struct repository *r);

void pack_report(struct repository *repo);

/*
 * mmap the index file for the specified packfile (if it is not
 * already mmapped).  Return 0 on success.
 */
int open_pack_index(struct packed_git *);

/*
 * munmap the index file for the specified packfile (if it is
 * currently mmapped).
 */
void close_pack_index(struct packed_git *);

int close_pack_fd(struct packed_git *p);

uint32_t get_pack_fanout(struct packed_git *p, uint32_t value);

struct object_database;

unsigned char *use_pack(struct packed_git *, struct pack_window **, off_t, unsigned long *);
void close_pack_windows(struct packed_git *);
void close_pack(struct packed_git *);
void unuse_pack(struct pack_window **);
void clear_delta_base_cache(void);
struct packed_git *add_packed_git(struct repository *r, const char *path,
				  size_t path_len, int local);

/*
 * Unlink the .pack and associated extension files.
 * Does not unlink if 'force_delete' is false and the pack-file is
 * marked as ".keep".
 */
void unlink_pack_path(const char *pack_name, int force_delete);

/*
 * Make sure that a pointer access into an mmap'd index file is within bounds,
 * and can provide at least 8 bytes of data.
 *
 * Note that this is only necessary for variable-length segments of the file
 * (like the 64-bit extended offset table), as we compare the size to the
 * fixed-length parts when we open the file.
 */
void check_pack_index_ptr(const struct packed_git *p, const void *ptr);

/*
 * Perform binary search on a pack-index for a given oid. Packfile is expected to
 * have a valid pack-index.
 *
 * See 'bsearch_hash' for more information.
 */
int bsearch_pack(const struct object_id *oid, const struct packed_git *p, uint32_t *result);

/*
 * Write the oid of the nth object within the specified packfile into the first
 * parameter. Open the index if it is not already open.  Returns 0 on success,
 * negative otherwise.
 */
int nth_packed_object_id(struct object_id *, struct packed_git *, uint32_t n);

/*
 * Return the offset of the nth object within the specified packfile.
 * The index must already be opened.
 */
off_t nth_packed_object_offset(const struct packed_git *, uint32_t n);

/*
 * If the object named by oid is present in the specified packfile,
 * return its offset within the packfile; otherwise, return 0.
 */
off_t find_pack_entry_one(const struct object_id *oid, struct packed_git *);

int is_pack_valid(struct packed_git *);
void *unpack_entry(struct repository *r, struct packed_git *, off_t, enum object_type *, unsigned long *);
unsigned long unpack_object_header_buffer(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
unsigned long get_size_from_delta(struct packed_git *, struct pack_window **, off_t);
int unpack_object_header(struct packed_git *, struct pack_window **, off_t *, unsigned long *);
off_t get_delta_base(struct packed_git *p, struct pack_window **w_curs,
		     off_t *curpos, enum object_type type,
		     off_t delta_obj_offset);

void release_pack_memory(size_t);

/* global flag to enable extra checks when accessing packed objects */
extern int do_check_packed_object_crc;

int packed_object_info(struct repository *r,
		       struct packed_git *pack,
		       off_t offset, struct object_info *);

void mark_bad_packed_object(struct packed_git *, const struct object_id *);
const struct packed_git *has_packed_and_bad(struct repository *, const struct object_id *);

int has_object_pack(struct repository *r, const struct object_id *oid);
int has_object_kept_pack(struct repository *r, const struct object_id *oid,
			 unsigned flags);

/*
 * Return 1 if an object in a promisor packfile is or refers to the given
 * object, 0 otherwise.
 */
int is_promisor_object(struct repository *r, const struct object_id *oid);

/*
 * Expose a function for fuzz testing.
 *
 * load_idx() parses a block of memory as a packfile index and puts the results
 * into a struct packed_git.
 *
 * This function should not be used directly. It is exposed here only so that we
 * have a convenient entry-point for fuzz testing. For real uses, you should
 * probably use open_pack_index() instead.
 */
int load_idx(const char *path, const unsigned int hashsz, void *idx_map,
	     size_t idx_size, struct packed_git *p);

/*
 * Parse a --pack_header option as accepted by index-pack and unpack-objects,
 * turning it into the matching bytes we'd find in a pack.
 */
int parse_pack_header_option(const char *in, unsigned char *out, unsigned int *len);

#endif
