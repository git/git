#ifndef REPACK_H
#define REPACK_H

#include "list-objects-filter-options.h"
#include "string-list.h"

struct pack_objects_args {
	char *window;
	char *window_memory;
	char *depth;
	char *threads;
	unsigned long max_pack_size;
	int no_reuse_delta;
	int no_reuse_object;
	int quiet;
	int local;
	int name_hash_version;
	int path_walk;
	int delta_base_offset;
	int pack_kept_objects;
	struct list_objects_filter_options filter_options;
};

#define PACK_OBJECTS_ARGS_INIT { \
	.delta_base_offset = 1, \
	.pack_kept_objects = -1, \
}

struct child_process;

void prepare_pack_objects(struct child_process *cmd,
			  const struct pack_objects_args *args,
			  const char *out);
void pack_objects_args_release(struct pack_objects_args *args);

void repack_remove_redundant_pack(struct repository *repo, const char *dir_name,
				  const char *base_name);

struct write_pack_opts {
	struct pack_objects_args *po_args;
	const char *destination;
	const char *packdir;
	const char *packtmp;
};

const char *write_pack_opts_pack_prefix(const struct write_pack_opts *opts);
bool write_pack_opts_is_local(const struct write_pack_opts *opts);

int finish_pack_objects_cmd(const struct git_hash_algo *algop,
			    const struct write_pack_opts *opts,
			    struct child_process *cmd,
			    struct string_list *names);

struct repository;
struct packed_git;

struct existing_packs {
	struct repository *repo;
	struct string_list kept_packs;
	struct string_list non_kept_packs;
	struct string_list cruft_packs;
	struct string_list midx_packs;
};

#define EXISTING_PACKS_INIT { \
	.kept_packs = STRING_LIST_INIT_DUP, \
	.non_kept_packs = STRING_LIST_INIT_DUP, \
	.cruft_packs = STRING_LIST_INIT_DUP, \
}

/*
 * Adds all packs hex strings (pack-$HASH) to either packs->non_kept
 * or packs->kept based on whether each pack has a corresponding
 * .keep file or not.  Packs without a .keep file are not to be kept
 * if we are going to pack everything into one file.
 */
void existing_packs_collect(struct existing_packs *existing,
			    const struct string_list *extra_keep);
int existing_packs_has_non_kept(const struct existing_packs *existing);
int existing_pack_is_marked_for_deletion(struct string_list_item *item);
void existing_packs_retain_cruft(struct existing_packs *existing,
				 struct packed_git *cruft);
void existing_packs_mark_for_deletion(struct existing_packs *existing,
				      struct string_list *names);
void existing_packs_remove_redundant(struct existing_packs *existing,
				     const char *packdir);
void existing_packs_release(struct existing_packs *existing);

struct generated_pack;

struct generated_pack *generated_pack_populate(const char *name,
					       const char *packtmp);
int generated_pack_has_ext(const struct generated_pack *pack, const char *ext);
void generated_pack_install(struct generated_pack *pack, const char *name,
			    const char *packdir, const char *packtmp);

void repack_promisor_objects(struct repository *repo,
			     const struct pack_objects_args *args,
			     struct string_list *names, const char *packtmp);

struct pack_geometry {
	struct packed_git **pack;
	uint32_t pack_nr, pack_alloc;
	uint32_t split;

	int split_factor;
};

void pack_geometry_init(struct pack_geometry *geometry,
			struct existing_packs *existing,
			const struct pack_objects_args *args);
void pack_geometry_split(struct pack_geometry *geometry);
struct packed_git *pack_geometry_preferred_pack(struct pack_geometry *geometry);
void pack_geometry_remove_redundant(struct pack_geometry *geometry,
				    struct string_list *names,
				    struct existing_packs *existing,
				    const char *packdir);
void pack_geometry_release(struct pack_geometry *geometry);

struct tempfile;

struct repack_write_midx_opts {
	struct existing_packs *existing;
	struct pack_geometry *geometry;
	struct string_list *names;
	const char *refs_snapshot;
	const char *packdir;
	int show_progress;
	int write_bitmaps;
	int midx_must_contain_cruft;
};

void midx_snapshot_refs(struct repository *repo, struct tempfile *f);
int write_midx_included_packs(struct repack_write_midx_opts *opts);

int write_filtered_pack(const struct write_pack_opts *opts,
			struct existing_packs *existing,
			struct string_list *names);

int write_cruft_pack(const struct write_pack_opts *opts,
		     const char *cruft_expiration,
		     unsigned long combine_cruft_below_size,
		     struct string_list *names,
		     struct existing_packs *existing);

#endif /* REPACK_H */
