/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "git-compat-util.h"
#include "bulk-checkin.h"
#include "config.h"
#include "date.h"
#include "diff.h"
#include "diffcore.h"
#include "hex.h"
#include "tempfile.h"
#include "lockfile.h"
#include "cache-tree.h"
#include "refs.h"
#include "dir.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "tree.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "mem-pool.h"
#include "name-hash.h"
#include "object-name.h"
#include "path.h"
#include "preload-index.h"
#include "read-cache.h"
#include "resolve-undo.h"
#include "revision.h"
#include "strbuf.h"
#include "trace2.h"
#include "varint.h"
#include "split-index.h"
#include "symlinks.h"
#include "utf8.h"
#include "fsmonitor.h"
#include "thread-utils.h"
#include "progress.h"
#include "sparse-index.h"
#include "csum-file.h"
#include "promisor-remote.h"
#include "hook.h"

/* Mask for the name length in ce_flags in the on-disk index */

#define CE_NAMEMASK  (0x0fff)

/* Index extensions.
 *
 * The first letter should be 'A'..'Z' for extensions that are not
 * necessary for a correct operation (i.e. optimization data).
 * When new extensions are added that _needs_ to be understood in
 * order to correctly interpret the index file, pick character that
 * is outside the range, to cause the reader to abort.
 */

#define CACHE_EXT(s) ( (s[0]<<24)|(s[1]<<16)|(s[2]<<8)|(s[3]) )
#define CACHE_EXT_TREE 0x54524545	/* "TREE" */
#define CACHE_EXT_RESOLVE_UNDO 0x52455543 /* "REUC" */
#define CACHE_EXT_LINK 0x6c696e6b	  /* "link" */
#define CACHE_EXT_UNTRACKED 0x554E5452	  /* "UNTR" */
#define CACHE_EXT_FSMONITOR 0x46534D4E	  /* "FSMN" */
#define CACHE_EXT_ENDOFINDEXENTRIES 0x454F4945	/* "EOIE" */
#define CACHE_EXT_INDEXENTRYOFFSETTABLE 0x49454F54 /* "IEOT" */
#define CACHE_EXT_SPARSE_DIRECTORIES 0x73646972 /* "sdir" */

/* changes that can be kept in $GIT_DIR/index (basically all extensions) */
#define EXTMASK (RESOLVE_UNDO_CHANGED | CACHE_TREE_CHANGED | \
		 CE_ENTRY_ADDED | CE_ENTRY_REMOVED | CE_ENTRY_CHANGED | \
		 SPLIT_INDEX_ORDERED | UNTRACKED_CHANGED | FSMONITOR_CHANGED)


/*
 * This is an estimate of the pathname length in the index.  We use
 * this for V4 index files to guess the un-deltafied size of the index
 * in memory because of pathname deltafication.  This is not required
 * for V2/V3 index formats because their pathnames are not compressed.
 * If the initial amount of memory set aside is not sufficient, the
 * mem pool will allocate extra memory.
 */
#define CACHE_ENTRY_PATH_LENGTH 80

enum index_search_mode {
	NO_EXPAND_SPARSE = 0,
	EXPAND_SPARSE = 1
};

static inline struct cache_entry *mem_pool__ce_alloc(struct mem_pool *mem_pool, size_t len)
{
	struct cache_entry *ce;
	ce = mem_pool_alloc(mem_pool, cache_entry_size(len));
	ce->mem_pool_allocated = 1;
	return ce;
}

static inline struct cache_entry *mem_pool__ce_calloc(struct mem_pool *mem_pool, size_t len)
{
	struct cache_entry * ce;
	ce = mem_pool_calloc(mem_pool, 1, cache_entry_size(len));
	ce->mem_pool_allocated = 1;
	return ce;
}

static struct mem_pool *find_mem_pool(struct index_state *istate)
{
	struct mem_pool **pool_ptr;

	if (istate->split_index && istate->split_index->base)
		pool_ptr = &istate->split_index->base->ce_mem_pool;
	else
		pool_ptr = &istate->ce_mem_pool;

	if (!*pool_ptr) {
		*pool_ptr = xmalloc(sizeof(**pool_ptr));
		mem_pool_init(*pool_ptr, 0);
	}

	return *pool_ptr;
}

static const char *alternate_index_output;

static void set_index_entry(struct index_state *istate, int nr, struct cache_entry *ce)
{
	if (S_ISSPARSEDIR(ce->ce_mode))
		istate->sparse_index = INDEX_COLLAPSED;

	istate->cache[nr] = ce;
	add_name_hash(istate, ce);
}

static void replace_index_entry(struct index_state *istate, int nr, struct cache_entry *ce)
{
	struct cache_entry *old = istate->cache[nr];

	replace_index_entry_in_base(istate, old, ce);
	remove_name_hash(istate, old);
	discard_cache_entry(old);
	ce->ce_flags &= ~CE_HASHED;
	set_index_entry(istate, nr, ce);
	ce->ce_flags |= CE_UPDATE_IN_BASE;
	mark_fsmonitor_invalid(istate, ce);
	istate->cache_changed |= CE_ENTRY_CHANGED;
}

void rename_index_entry_at(struct index_state *istate, int nr, const char *new_name)
{
	struct cache_entry *old_entry = istate->cache[nr], *new_entry, *refreshed;
	int namelen = strlen(new_name);

	new_entry = make_empty_cache_entry(istate, namelen);
	copy_cache_entry(new_entry, old_entry);
	new_entry->ce_flags &= ~CE_HASHED;
	new_entry->ce_namelen = namelen;
	new_entry->index = 0;
	memcpy(new_entry->name, new_name, namelen + 1);

	cache_tree_invalidate_path(istate, old_entry->name);
	untracked_cache_remove_from_index(istate, old_entry->name);
	remove_index_entry_at(istate, nr);

	/*
	 * Refresh the new index entry. Using 'refresh_cache_entry' ensures
	 * we only update stat info if the entry is otherwise up-to-date (i.e.,
	 * the contents/mode haven't changed). This ensures that we reflect the
	 * 'ctime' of the rename in the index without (incorrectly) updating
	 * the cached stat info to reflect unstaged changes on disk.
	 */
	refreshed = refresh_cache_entry(istate, new_entry, CE_MATCH_REFRESH);
	if (refreshed && refreshed != new_entry) {
		add_index_entry(istate, refreshed, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
		discard_cache_entry(new_entry);
	} else
		add_index_entry(istate, new_entry, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
}

/*
 * This only updates the "non-critical" parts of the directory
 * cache, ie the parts that aren't tracked by GIT, and only used
 * to validate the cache.
 */
void fill_stat_cache_info(struct index_state *istate, struct cache_entry *ce, struct stat *st)
{
	fill_stat_data(&ce->ce_stat_data, st);

	if (assume_unchanged)
		ce->ce_flags |= CE_VALID;

	if (S_ISREG(st->st_mode)) {
		ce_mark_uptodate(ce);
		mark_fsmonitor_valid(istate, ce);
	}
}

static unsigned int st_mode_from_ce(const struct cache_entry *ce)
{
	extern int trust_executable_bit, has_symlinks;

	switch (ce->ce_mode & S_IFMT) {
	case S_IFLNK:
		return has_symlinks ? S_IFLNK : (S_IFREG | 0644);
	case S_IFREG:
		return (ce->ce_mode & (trust_executable_bit ? 0755 : 0644)) | S_IFREG;
	case S_IFGITLINK:
		return S_IFDIR | 0755;
	case S_IFDIR:
		return ce->ce_mode;
	default:
		BUG("unsupported ce_mode: %o", ce->ce_mode);
	}
}

int fake_lstat(const struct cache_entry *ce, struct stat *st)
{
	fake_lstat_data(&ce->ce_stat_data, st);
	st->st_mode = st_mode_from_ce(ce);

	/* always succeed as lstat() replacement */
	return 0;
}

static int ce_compare_data(struct index_state *istate,
			   const struct cache_entry *ce,
			   struct stat *st)
{
	int match = -1;
	int fd = git_open_cloexec(ce->name, O_RDONLY);

	if (fd >= 0) {
		struct object_id oid;
		if (!index_fd(istate, &oid, fd, st, OBJ_BLOB, ce->name, 0))
			match = !oideq(&oid, &ce->oid);
		/* index_fd() closed the file descriptor already */
	}
	return match;
}

static int ce_compare_link(const struct cache_entry *ce, size_t expected_size)
{
	int match = -1;
	void *buffer;
	unsigned long size;
	enum object_type type;
	struct strbuf sb = STRBUF_INIT;

	if (strbuf_readlink(&sb, ce->name, expected_size))
		return -1;

	buffer = repo_read_object_file(the_repository, &ce->oid, &type, &size);
	if (buffer) {
		if (size == sb.len)
			match = memcmp(buffer, sb.buf, size);
		free(buffer);
	}
	strbuf_release(&sb);
	return match;
}

static int ce_compare_gitlink(const struct cache_entry *ce)
{
	struct object_id oid;

	/*
	 * We don't actually require that the .git directory
	 * under GITLINK directory be a valid git directory. It
	 * might even be missing (in case nobody populated that
	 * sub-project).
	 *
	 * If so, we consider it always to match.
	 */
	if (resolve_gitlink_ref(ce->name, "HEAD", &oid) < 0)
		return 0;
	return !oideq(&oid, &ce->oid);
}

static int ce_modified_check_fs(struct index_state *istate,
				const struct cache_entry *ce,
				struct stat *st)
{
	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		if (ce_compare_data(istate, ce, st))
			return DATA_CHANGED;
		break;
	case S_IFLNK:
		if (ce_compare_link(ce, xsize_t(st->st_size)))
			return DATA_CHANGED;
		break;
	case S_IFDIR:
		if (S_ISGITLINK(ce->ce_mode))
			return ce_compare_gitlink(ce) ? DATA_CHANGED : 0;
		/* else fallthrough */
	default:
		return TYPE_CHANGED;
	}
	return 0;
}

static int ce_match_stat_basic(const struct cache_entry *ce, struct stat *st)
{
	unsigned int changed = 0;

	if (ce->ce_flags & CE_REMOVE)
		return MODE_CHANGED | DATA_CHANGED | TYPE_CHANGED;

	switch (ce->ce_mode & S_IFMT) {
	case S_IFREG:
		changed |= !S_ISREG(st->st_mode) ? TYPE_CHANGED : 0;
		/* We consider only the owner x bit to be relevant for
		 * "mode changes"
		 */
		if (trust_executable_bit &&
		    (0100 & (ce->ce_mode ^ st->st_mode)))
			changed |= MODE_CHANGED;
		break;
	case S_IFLNK:
		if (!S_ISLNK(st->st_mode) &&
		    (has_symlinks || !S_ISREG(st->st_mode)))
			changed |= TYPE_CHANGED;
		break;
	case S_IFGITLINK:
		/* We ignore most of the st_xxx fields for gitlinks */
		if (!S_ISDIR(st->st_mode))
			changed |= TYPE_CHANGED;
		else if (ce_compare_gitlink(ce))
			changed |= DATA_CHANGED;
		return changed;
	default:
		BUG("unsupported ce_mode: %o", ce->ce_mode);
	}

	changed |= match_stat_data(&ce->ce_stat_data, st);

	/* Racily smudged entry? */
	if (!ce->ce_stat_data.sd_size) {
		if (!is_empty_blob_sha1(ce->oid.hash))
			changed |= DATA_CHANGED;
	}

	return changed;
}

static int is_racy_stat(const struct index_state *istate,
			const struct stat_data *sd)
{
	return (istate->timestamp.sec &&
#ifdef USE_NSEC
		 /* nanosecond timestamped files can also be racy! */
		(istate->timestamp.sec < sd->sd_mtime.sec ||
		 (istate->timestamp.sec == sd->sd_mtime.sec &&
		  istate->timestamp.nsec <= sd->sd_mtime.nsec))
#else
		istate->timestamp.sec <= sd->sd_mtime.sec
#endif
		);
}

int is_racy_timestamp(const struct index_state *istate,
			     const struct cache_entry *ce)
{
	return (!S_ISGITLINK(ce->ce_mode) &&
		is_racy_stat(istate, &ce->ce_stat_data));
}

int match_stat_data_racy(const struct index_state *istate,
			 const struct stat_data *sd, struct stat *st)
{
	if (is_racy_stat(istate, sd))
		return MTIME_CHANGED;
	return match_stat_data(sd, st);
}

int ie_match_stat(struct index_state *istate,
		  const struct cache_entry *ce, struct stat *st,
		  unsigned int options)
{
	unsigned int changed;
	int ignore_valid = options & CE_MATCH_IGNORE_VALID;
	int ignore_skip_worktree = options & CE_MATCH_IGNORE_SKIP_WORKTREE;
	int assume_racy_is_modified = options & CE_MATCH_RACY_IS_DIRTY;
	int ignore_fsmonitor = options & CE_MATCH_IGNORE_FSMONITOR;

	if (!ignore_fsmonitor)
		refresh_fsmonitor(istate);
	/*
	 * If it's marked as always valid in the index, it's
	 * valid whatever the checked-out copy says.
	 *
	 * skip-worktree has the same effect with higher precedence
	 */
	if (!ignore_skip_worktree && ce_skip_worktree(ce))
		return 0;
	if (!ignore_valid && (ce->ce_flags & CE_VALID))
		return 0;
	if (!ignore_fsmonitor && (ce->ce_flags & CE_FSMONITOR_VALID))
		return 0;

	/*
	 * Intent-to-add entries have not been added, so the index entry
	 * by definition never matches what is in the work tree until it
	 * actually gets added.
	 */
	if (ce_intent_to_add(ce))
		return DATA_CHANGED | TYPE_CHANGED | MODE_CHANGED;

	changed = ce_match_stat_basic(ce, st);

	/*
	 * Within 1 second of this sequence:
	 * 	echo xyzzy >file && git-update-index --add file
	 * running this command:
	 * 	echo frotz >file
	 * would give a falsely clean cache entry.  The mtime and
	 * length match the cache, and other stat fields do not change.
	 *
	 * We could detect this at update-index time (the cache entry
	 * being registered/updated records the same time as "now")
	 * and delay the return from git-update-index, but that would
	 * effectively mean we can make at most one commit per second,
	 * which is not acceptable.  Instead, we check cache entries
	 * whose mtime are the same as the index file timestamp more
	 * carefully than others.
	 */
	if (!changed && is_racy_timestamp(istate, ce)) {
		if (assume_racy_is_modified)
			changed |= DATA_CHANGED;
		else
			changed |= ce_modified_check_fs(istate, ce, st);
	}

	return changed;
}

int ie_modified(struct index_state *istate,
		const struct cache_entry *ce,
		struct stat *st, unsigned int options)
{
	int changed, changed_fs;

	changed = ie_match_stat(istate, ce, st, options);
	if (!changed)
		return 0;
	/*
	 * If the mode or type has changed, there's no point in trying
	 * to refresh the entry - it's not going to match
	 */
	if (changed & (MODE_CHANGED | TYPE_CHANGED))
		return changed;

	/*
	 * Immediately after read-tree or update-index --cacheinfo,
	 * the length field is zero, as we have never even read the
	 * lstat(2) information once, and we cannot trust DATA_CHANGED
	 * returned by ie_match_stat() which in turn was returned by
	 * ce_match_stat_basic() to signal that the filesize of the
	 * blob changed.  We have to actually go to the filesystem to
	 * see if the contents match, and if so, should answer "unchanged".
	 *
	 * The logic does not apply to gitlinks, as ce_match_stat_basic()
	 * already has checked the actual HEAD from the filesystem in the
	 * subproject.  If ie_match_stat() already said it is different,
	 * then we know it is.
	 */
	if ((changed & DATA_CHANGED) &&
	    (S_ISGITLINK(ce->ce_mode) || ce->ce_stat_data.sd_size != 0))
		return changed;

	changed_fs = ce_modified_check_fs(istate, ce, st);
	if (changed_fs)
		return changed | changed_fs;
	return 0;
}

static int cache_name_stage_compare(const char *name1, int len1, int stage1,
				    const char *name2, int len2, int stage2)
{
	int cmp;

	cmp = name_compare(name1, len1, name2, len2);
	if (cmp)
		return cmp;

	if (stage1 < stage2)
		return -1;
	if (stage1 > stage2)
		return 1;
	return 0;
}

int cmp_cache_name_compare(const void *a_, const void *b_)
{
	const struct cache_entry *ce1, *ce2;

	ce1 = *((const struct cache_entry **)a_);
	ce2 = *((const struct cache_entry **)b_);
	return cache_name_stage_compare(ce1->name, ce1->ce_namelen, ce_stage(ce1),
				  ce2->name, ce2->ce_namelen, ce_stage(ce2));
}

static int index_name_stage_pos(struct index_state *istate,
				const char *name, int namelen,
				int stage,
				enum index_search_mode search_mode)
{
	int first, last;

	first = 0;
	last = istate->cache_nr;
	while (last > first) {
		int next = first + ((last - first) >> 1);
		struct cache_entry *ce = istate->cache[next];
		int cmp = cache_name_stage_compare(name, namelen, stage, ce->name, ce_namelen(ce), ce_stage(ce));
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}

	if (search_mode == EXPAND_SPARSE && istate->sparse_index &&
	    first > 0) {
		/* Note: first <= istate->cache_nr */
		struct cache_entry *ce = istate->cache[first - 1];

		/*
		 * If we are in a sparse-index _and_ the entry before the
		 * insertion position is a sparse-directory entry that is
		 * an ancestor of 'name', then we need to expand the index
		 * and search again. This will only trigger once, because
		 * thereafter the index is fully expanded.
		 */
		if (S_ISSPARSEDIR(ce->ce_mode) &&
		    ce_namelen(ce) < namelen &&
		    !strncmp(name, ce->name, ce_namelen(ce))) {
			ensure_full_index(istate);
			return index_name_stage_pos(istate, name, namelen, stage, search_mode);
		}
	}

	return -first-1;
}

int index_name_pos(struct index_state *istate, const char *name, int namelen)
{
	return index_name_stage_pos(istate, name, namelen, 0, EXPAND_SPARSE);
}

int index_name_pos_sparse(struct index_state *istate, const char *name, int namelen)
{
	return index_name_stage_pos(istate, name, namelen, 0, NO_EXPAND_SPARSE);
}

int index_entry_exists(struct index_state *istate, const char *name, int namelen)
{
	return index_name_stage_pos(istate, name, namelen, 0, NO_EXPAND_SPARSE) >= 0;
}

int remove_index_entry_at(struct index_state *istate, int pos)
{
	struct cache_entry *ce = istate->cache[pos];

	record_resolve_undo(istate, ce);
	remove_name_hash(istate, ce);
	save_or_free_index_entry(istate, ce);
	istate->cache_changed |= CE_ENTRY_REMOVED;
	istate->cache_nr--;
	if (pos >= istate->cache_nr)
		return 0;
	MOVE_ARRAY(istate->cache + pos, istate->cache + pos + 1,
		   istate->cache_nr - pos);
	return 1;
}

/*
 * Remove all cache entries marked for removal, that is where
 * CE_REMOVE is set in ce_flags.  This is much more effective than
 * calling remove_index_entry_at() for each entry to be removed.
 */
void remove_marked_cache_entries(struct index_state *istate, int invalidate)
{
	struct cache_entry **ce_array = istate->cache;
	unsigned int i, j;

	for (i = j = 0; i < istate->cache_nr; i++) {
		if (ce_array[i]->ce_flags & CE_REMOVE) {
			if (invalidate) {
				cache_tree_invalidate_path(istate,
							   ce_array[i]->name);
				untracked_cache_remove_from_index(istate,
								  ce_array[i]->name);
			}
			remove_name_hash(istate, ce_array[i]);
			save_or_free_index_entry(istate, ce_array[i]);
		}
		else
			ce_array[j++] = ce_array[i];
	}
	if (j == istate->cache_nr)
		return;
	istate->cache_changed |= CE_ENTRY_REMOVED;
	istate->cache_nr = j;
}

int remove_file_from_index(struct index_state *istate, const char *path)
{
	int pos = index_name_pos(istate, path, strlen(path));
	if (pos < 0)
		pos = -pos-1;
	cache_tree_invalidate_path(istate, path);
	untracked_cache_remove_from_index(istate, path);
	while (pos < istate->cache_nr && !strcmp(istate->cache[pos]->name, path))
		remove_index_entry_at(istate, pos);
	return 0;
}

static int compare_name(struct cache_entry *ce, const char *path, int namelen)
{
	return namelen != ce_namelen(ce) || memcmp(path, ce->name, namelen);
}

static int index_name_pos_also_unmerged(struct index_state *istate,
	const char *path, int namelen)
{
	int pos = index_name_pos(istate, path, namelen);
	struct cache_entry *ce;

	if (pos >= 0)
		return pos;

	/* maybe unmerged? */
	pos = -1 - pos;
	if (pos >= istate->cache_nr ||
			compare_name((ce = istate->cache[pos]), path, namelen))
		return -1;

	/* order of preference: stage 2, 1, 3 */
	if (ce_stage(ce) == 1 && pos + 1 < istate->cache_nr &&
			ce_stage((ce = istate->cache[pos + 1])) == 2 &&
			!compare_name(ce, path, namelen))
		pos++;
	return pos;
}

static int different_name(struct cache_entry *ce, struct cache_entry *alias)
{
	int len = ce_namelen(ce);
	return ce_namelen(alias) != len || memcmp(ce->name, alias->name, len);
}

/*
 * If we add a filename that aliases in the cache, we will use the
 * name that we already have - but we don't want to update the same
 * alias twice, because that implies that there were actually two
 * different files with aliasing names!
 *
 * So we use the CE_ADDED flag to verify that the alias was an old
 * one before we accept it as
 */
static struct cache_entry *create_alias_ce(struct index_state *istate,
					   struct cache_entry *ce,
					   struct cache_entry *alias)
{
	int len;
	struct cache_entry *new_entry;

	if (alias->ce_flags & CE_ADDED)
		die(_("will not add file alias '%s' ('%s' already exists in index)"),
		    ce->name, alias->name);

	/* Ok, create the new entry using the name of the existing alias */
	len = ce_namelen(alias);
	new_entry = make_empty_cache_entry(istate, len);
	memcpy(new_entry->name, alias->name, len);
	copy_cache_entry(new_entry, ce);
	save_or_free_index_entry(istate, ce);
	return new_entry;
}

void set_object_name_for_intent_to_add_entry(struct cache_entry *ce)
{
	struct object_id oid;
	if (write_object_file("", 0, OBJ_BLOB, &oid))
		die(_("cannot create an empty blob in the object database"));
	oidcpy(&ce->oid, &oid);
}

int add_to_index(struct index_state *istate, const char *path, struct stat *st, int flags)
{
	int namelen, was_same;
	mode_t st_mode = st->st_mode;
	struct cache_entry *ce, *alias = NULL;
	unsigned ce_option = CE_MATCH_IGNORE_VALID|CE_MATCH_IGNORE_SKIP_WORKTREE|CE_MATCH_RACY_IS_DIRTY;
	int verbose = flags & (ADD_CACHE_VERBOSE | ADD_CACHE_PRETEND);
	int pretend = flags & ADD_CACHE_PRETEND;
	int intent_only = flags & ADD_CACHE_INTENT;
	int add_option = (ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE|
			  (intent_only ? ADD_CACHE_NEW_ONLY : 0));
	unsigned hash_flags = pretend ? 0 : HASH_WRITE_OBJECT;
	struct object_id oid;

	if (flags & ADD_CACHE_RENORMALIZE)
		hash_flags |= HASH_RENORMALIZE;

	if (!S_ISREG(st_mode) && !S_ISLNK(st_mode) && !S_ISDIR(st_mode))
		return error(_("%s: can only add regular files, symbolic links or git-directories"), path);

	namelen = strlen(path);
	if (S_ISDIR(st_mode)) {
		if (resolve_gitlink_ref(path, "HEAD", &oid) < 0)
			return error(_("'%s' does not have a commit checked out"), path);
		while (namelen && path[namelen-1] == '/')
			namelen--;
	}
	ce = make_empty_cache_entry(istate, namelen);
	memcpy(ce->name, path, namelen);
	ce->ce_namelen = namelen;
	if (!intent_only)
		fill_stat_cache_info(istate, ce, st);
	else
		ce->ce_flags |= CE_INTENT_TO_ADD;


	if (trust_executable_bit && has_symlinks) {
		ce->ce_mode = create_ce_mode(st_mode);
	} else {
		/* If there is an existing entry, pick the mode bits and type
		 * from it, otherwise assume unexecutable regular file.
		 */
		struct cache_entry *ent;
		int pos = index_name_pos_also_unmerged(istate, path, namelen);

		ent = (0 <= pos) ? istate->cache[pos] : NULL;
		ce->ce_mode = ce_mode_from_stat(ent, st_mode);
	}

	/* When core.ignorecase=true, determine if a directory of the same name but differing
	 * case already exists within the Git repository.  If it does, ensure the directory
	 * case of the file being added to the repository matches (is folded into) the existing
	 * entry's directory case.
	 */
	if (ignore_case) {
		adjust_dirname_case(istate, ce->name);
	}
	if (!(flags & ADD_CACHE_RENORMALIZE)) {
		alias = index_file_exists(istate, ce->name,
					  ce_namelen(ce), ignore_case);
		if (alias &&
		    !ce_stage(alias) &&
		    !ie_match_stat(istate, alias, st, ce_option)) {
			/* Nothing changed, really */
			if (!S_ISGITLINK(alias->ce_mode))
				ce_mark_uptodate(alias);
			alias->ce_flags |= CE_ADDED;

			discard_cache_entry(ce);
			return 0;
		}
	}
	if (!intent_only) {
		if (index_path(istate, &ce->oid, path, st, hash_flags)) {
			discard_cache_entry(ce);
			return error(_("unable to index file '%s'"), path);
		}
	} else
		set_object_name_for_intent_to_add_entry(ce);

	if (ignore_case && alias && different_name(ce, alias))
		ce = create_alias_ce(istate, ce, alias);
	ce->ce_flags |= CE_ADDED;

	/* It was suspected to be racily clean, but it turns out to be Ok */
	was_same = (alias &&
		    !ce_stage(alias) &&
		    oideq(&alias->oid, &ce->oid) &&
		    ce->ce_mode == alias->ce_mode);

	if (pretend)
		discard_cache_entry(ce);
	else if (add_index_entry(istate, ce, add_option)) {
		discard_cache_entry(ce);
		return error(_("unable to add '%s' to index"), path);
	}
	if (verbose && !was_same)
		printf("add '%s'\n", path);
	return 0;
}

int add_file_to_index(struct index_state *istate, const char *path, int flags)
{
	struct stat st;
	if (lstat(path, &st))
		die_errno(_("unable to stat '%s'"), path);
	return add_to_index(istate, path, &st, flags);
}

struct cache_entry *make_empty_cache_entry(struct index_state *istate, size_t len)
{
	return mem_pool__ce_calloc(find_mem_pool(istate), len);
}

struct cache_entry *make_empty_transient_cache_entry(size_t len,
						     struct mem_pool *ce_mem_pool)
{
	if (ce_mem_pool)
		return mem_pool__ce_calloc(ce_mem_pool, len);
	return xcalloc(1, cache_entry_size(len));
}

enum verify_path_result {
	PATH_OK,
	PATH_INVALID,
	PATH_DIR_WITH_SEP,
};

static enum verify_path_result verify_path_internal(const char *, unsigned);

int verify_path(const char *path, unsigned mode)
{
	return verify_path_internal(path, mode) == PATH_OK;
}

struct cache_entry *make_cache_entry(struct index_state *istate,
				     unsigned int mode,
				     const struct object_id *oid,
				     const char *path,
				     int stage,
				     unsigned int refresh_options)
{
	struct cache_entry *ce, *ret;
	int len;

	if (verify_path_internal(path, mode) == PATH_INVALID) {
		error(_("invalid path '%s'"), path);
		return NULL;
	}

	len = strlen(path);
	ce = make_empty_cache_entry(istate, len);

	oidcpy(&ce->oid, oid);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(stage);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);

	ret = refresh_cache_entry(istate, ce, refresh_options);
	if (ret != ce)
		discard_cache_entry(ce);
	return ret;
}

struct cache_entry *make_transient_cache_entry(unsigned int mode,
					       const struct object_id *oid,
					       const char *path,
					       int stage,
					       struct mem_pool *ce_mem_pool)
{
	struct cache_entry *ce;
	int len;

	if (!verify_path(path, mode)) {
		error(_("invalid path '%s'"), path);
		return NULL;
	}

	len = strlen(path);
	ce = make_empty_transient_cache_entry(len, ce_mem_pool);

	oidcpy(&ce->oid, oid);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(stage);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);

	return ce;
}

/*
 * Chmod an index entry with either +x or -x.
 *
 * Returns -1 if the chmod for the particular cache entry failed (if it's
 * not a regular file), -2 if an invalid flip argument is passed in, 0
 * otherwise.
 */
int chmod_index_entry(struct index_state *istate, struct cache_entry *ce,
		      char flip)
{
	if (!S_ISREG(ce->ce_mode))
		return -1;
	switch (flip) {
	case '+':
		ce->ce_mode |= 0111;
		break;
	case '-':
		ce->ce_mode &= ~0111;
		break;
	default:
		return -2;
	}
	cache_tree_invalidate_path(istate, ce->name);
	ce->ce_flags |= CE_UPDATE_IN_BASE;
	mark_fsmonitor_invalid(istate, ce);
	istate->cache_changed |= CE_ENTRY_CHANGED;

	return 0;
}

int ce_same_name(const struct cache_entry *a, const struct cache_entry *b)
{
	int len = ce_namelen(a);
	return ce_namelen(b) == len && !memcmp(a->name, b->name, len);
}

/*
 * We fundamentally don't like some paths: we don't want
 * dot or dot-dot anywhere, and for obvious reasons don't
 * want to recurse into ".git" either.
 *
 * Also, we don't want double slashes or slashes at the
 * end that can make pathnames ambiguous.
 */
static int verify_dotfile(const char *rest, unsigned mode)
{
	/*
	 * The first character was '.', but that
	 * has already been discarded, we now test
	 * the rest.
	 */

	/* "." is not allowed */
	if (*rest == '\0' || is_dir_sep(*rest))
		return 0;

	switch (*rest) {
	/*
	 * ".git" followed by NUL or slash is bad. Note that we match
	 * case-insensitively here, even if ignore_case is not set.
	 * This outlaws ".GIT" everywhere out of an abundance of caution,
	 * since there's really no good reason to allow it.
	 *
	 * Once we've seen ".git", we can also find ".gitmodules", etc (also
	 * case-insensitively).
	 */
	case 'g':
	case 'G':
		if (rest[1] != 'i' && rest[1] != 'I')
			break;
		if (rest[2] != 't' && rest[2] != 'T')
			break;
		if (rest[3] == '\0' || is_dir_sep(rest[3]))
			return 0;
		if (S_ISLNK(mode)) {
			rest += 3;
			if (skip_iprefix(rest, "modules", &rest) &&
			    (*rest == '\0' || is_dir_sep(*rest)))
				return 0;
		}
		break;
	case '.':
		if (rest[1] == '\0' || is_dir_sep(rest[1]))
			return 0;
	}
	return 1;
}

static enum verify_path_result verify_path_internal(const char *path,
						    unsigned mode)
{
	char c = 0;

	if (has_dos_drive_prefix(path))
		return PATH_INVALID;

	if (!is_valid_path(path))
		return PATH_INVALID;

	goto inside;
	for (;;) {
		if (!c)
			return PATH_OK;
		if (is_dir_sep(c)) {
inside:
			if (protect_hfs) {

				if (is_hfs_dotgit(path))
					return PATH_INVALID;
				if (S_ISLNK(mode)) {
					if (is_hfs_dotgitmodules(path))
						return PATH_INVALID;
				}
			}
			if (protect_ntfs) {
#if defined GIT_WINDOWS_NATIVE || defined __CYGWIN__
				if (c == '\\')
					return PATH_INVALID;
#endif
				if (is_ntfs_dotgit(path))
					return PATH_INVALID;
				if (S_ISLNK(mode)) {
					if (is_ntfs_dotgitmodules(path))
						return PATH_INVALID;
				}
			}

			c = *path++;
			if ((c == '.' && !verify_dotfile(path, mode)) ||
			    is_dir_sep(c))
				return PATH_INVALID;
			/*
			 * allow terminating directory separators for
			 * sparse directory entries.
			 */
			if (c == '\0')
				return S_ISDIR(mode) ? PATH_DIR_WITH_SEP :
						       PATH_INVALID;
		} else if (c == '\\' && protect_ntfs) {
			if (is_ntfs_dotgit(path))
				return PATH_INVALID;
			if (S_ISLNK(mode)) {
				if (is_ntfs_dotgitmodules(path))
					return PATH_INVALID;
			}
		}

		c = *path++;
	}
}

/*
 * Do we have another file that has the beginning components being a
 * proper superset of the name we're trying to add?
 */
static int has_file_name(struct index_state *istate,
			 const struct cache_entry *ce, int pos, int ok_to_replace)
{
	int retval = 0;
	int len = ce_namelen(ce);
	int stage = ce_stage(ce);
	const char *name = ce->name;

	while (pos < istate->cache_nr) {
		struct cache_entry *p = istate->cache[pos++];

		if (len >= ce_namelen(p))
			break;
		if (memcmp(name, p->name, len))
			break;
		if (ce_stage(p) != stage)
			continue;
		if (p->name[len] != '/')
			continue;
		if (p->ce_flags & CE_REMOVE)
			continue;
		retval = -1;
		if (!ok_to_replace)
			break;
		remove_index_entry_at(istate, --pos);
	}
	return retval;
}


/*
 * Like strcmp(), but also return the offset of the first change.
 * If strings are equal, return the length.
 */
int strcmp_offset(const char *s1, const char *s2, size_t *first_change)
{
	size_t k;

	if (!first_change)
		return strcmp(s1, s2);

	for (k = 0; s1[k] == s2[k]; k++)
		if (s1[k] == '\0')
			break;

	*first_change = k;
	return (unsigned char)s1[k] - (unsigned char)s2[k];
}

/*
 * Do we have another file with a pathname that is a proper
 * subset of the name we're trying to add?
 *
 * That is, is there another file in the index with a path
 * that matches a sub-directory in the given entry?
 */
static int has_dir_name(struct index_state *istate,
			const struct cache_entry *ce, int pos, int ok_to_replace)
{
	int retval = 0;
	int stage = ce_stage(ce);
	const char *name = ce->name;
	const char *slash = name + ce_namelen(ce);
	size_t len_eq_last;
	int cmp_last = 0;

	/*
	 * We are frequently called during an iteration on a sorted
	 * list of pathnames and while building a new index.  Therefore,
	 * there is a high probability that this entry will eventually
	 * be appended to the index, rather than inserted in the middle.
	 * If we can confirm that, we can avoid binary searches on the
	 * components of the pathname.
	 *
	 * Compare the entry's full path with the last path in the index.
	 */
	if (istate->cache_nr > 0) {
		cmp_last = strcmp_offset(name,
			istate->cache[istate->cache_nr - 1]->name,
			&len_eq_last);
		if (cmp_last > 0) {
			if (len_eq_last == 0) {
				/*
				 * The entry sorts AFTER the last one in the
				 * index and their paths have no common prefix,
				 * so there cannot be a F/D conflict.
				 */
				return retval;
			} else {
				/*
				 * The entry sorts AFTER the last one in the
				 * index, but has a common prefix.  Fall through
				 * to the loop below to disect the entry's path
				 * and see where the difference is.
				 */
			}
		} else if (cmp_last == 0) {
			/*
			 * The entry exactly matches the last one in the
			 * index, but because of multiple stage and CE_REMOVE
			 * items, we fall through and let the regular search
			 * code handle it.
			 */
		}
	}

	for (;;) {
		size_t len;

		for (;;) {
			if (*--slash == '/')
				break;
			if (slash <= ce->name)
				return retval;
		}
		len = slash - name;

		if (cmp_last > 0) {
			/*
			 * (len + 1) is a directory boundary (including
			 * the trailing slash).  And since the loop is
			 * decrementing "slash", the first iteration is
			 * the longest directory prefix; subsequent
			 * iterations consider parent directories.
			 */

			if (len + 1 <= len_eq_last) {
				/*
				 * The directory prefix (including the trailing
				 * slash) also appears as a prefix in the last
				 * entry, so the remainder cannot collide (because
				 * strcmp said the whole path was greater).
				 *
				 * EQ: last: xxx/A
				 *     this: xxx/B
				 *
				 * LT: last: xxx/file_A
				 *     this: xxx/file_B
				 */
				return retval;
			}

			if (len > len_eq_last) {
				/*
				 * This part of the directory prefix (excluding
				 * the trailing slash) is longer than the known
				 * equal portions, so this sub-directory cannot
				 * collide with a file.
				 *
				 * GT: last: xxxA
				 *     this: xxxB/file
				 */
				return retval;
			}

			/*
			 * This is a possible collision. Fall through and
			 * let the regular search code handle it.
			 *
			 * last: xxx
			 * this: xxx/file
			 */
		}

		pos = index_name_stage_pos(istate, name, len, stage, EXPAND_SPARSE);
		if (pos >= 0) {
			/*
			 * Found one, but not so fast.  This could
			 * be a marker that says "I was here, but
			 * I am being removed".  Such an entry is
			 * not a part of the resulting tree, and
			 * it is Ok to have a directory at the same
			 * path.
			 */
			if (!(istate->cache[pos]->ce_flags & CE_REMOVE)) {
				retval = -1;
				if (!ok_to_replace)
					break;
				remove_index_entry_at(istate, pos);
				continue;
			}
		}
		else
			pos = -pos-1;

		/*
		 * Trivial optimization: if we find an entry that
		 * already matches the sub-directory, then we know
		 * we're ok, and we can exit.
		 */
		while (pos < istate->cache_nr) {
			struct cache_entry *p = istate->cache[pos];
			if ((ce_namelen(p) <= len) ||
			    (p->name[len] != '/') ||
			    memcmp(p->name, name, len))
				break; /* not our subdirectory */
			if (ce_stage(p) == stage && !(p->ce_flags & CE_REMOVE))
				/*
				 * p is at the same stage as our entry, and
				 * is a subdirectory of what we are looking
				 * at, so we cannot have conflicts at our
				 * level or anything shorter.
				 */
				return retval;
			pos++;
		}
	}
	return retval;
}

/* We may be in a situation where we already have path/file and path
 * is being added, or we already have path and path/file is being
 * added.  Either one would result in a nonsense tree that has path
 * twice when git-write-tree tries to write it out.  Prevent it.
 *
 * If ok-to-replace is specified, we remove the conflicting entries
 * from the cache so the caller should recompute the insert position.
 * When this happens, we return non-zero.
 */
static int check_file_directory_conflict(struct index_state *istate,
					 const struct cache_entry *ce,
					 int pos, int ok_to_replace)
{
	int retval;

	/*
	 * When ce is an "I am going away" entry, we allow it to be added
	 */
	if (ce->ce_flags & CE_REMOVE)
		return 0;

	/*
	 * We check if the path is a sub-path of a subsequent pathname
	 * first, since removing those will not change the position
	 * in the array.
	 */
	retval = has_file_name(istate, ce, pos, ok_to_replace);

	/*
	 * Then check if the path might have a clashing sub-directory
	 * before it.
	 */
	return retval + has_dir_name(istate, ce, pos, ok_to_replace);
}

static int add_index_entry_with_check(struct index_state *istate, struct cache_entry *ce, int option)
{
	int pos;
	int ok_to_add = option & ADD_CACHE_OK_TO_ADD;
	int ok_to_replace = option & ADD_CACHE_OK_TO_REPLACE;
	int skip_df_check = option & ADD_CACHE_SKIP_DFCHECK;
	int new_only = option & ADD_CACHE_NEW_ONLY;

	/*
	 * If this entry's path sorts after the last entry in the index,
	 * we can avoid searching for it.
	 */
	if (istate->cache_nr > 0 &&
		strcmp(ce->name, istate->cache[istate->cache_nr - 1]->name) > 0)
		pos = index_pos_to_insert_pos(istate->cache_nr);
	else
		pos = index_name_stage_pos(istate, ce->name, ce_namelen(ce), ce_stage(ce), EXPAND_SPARSE);

	/*
	 * Cache tree path should be invalidated only after index_name_stage_pos,
	 * in case it expands a sparse index.
	 */
	if (!(option & ADD_CACHE_KEEP_CACHE_TREE))
		cache_tree_invalidate_path(istate, ce->name);

	/* existing match? Just replace it. */
	if (pos >= 0) {
		if (!new_only)
			replace_index_entry(istate, pos, ce);
		return 0;
	}
	pos = -pos-1;

	if (!(option & ADD_CACHE_KEEP_CACHE_TREE))
		untracked_cache_add_to_index(istate, ce->name);

	/*
	 * Inserting a merged entry ("stage 0") into the index
	 * will always replace all non-merged entries..
	 */
	if (pos < istate->cache_nr && ce_stage(ce) == 0) {
		while (ce_same_name(istate->cache[pos], ce)) {
			ok_to_add = 1;
			if (!remove_index_entry_at(istate, pos))
				break;
		}
	}

	if (!ok_to_add)
		return -1;
	if (verify_path_internal(ce->name, ce->ce_mode) == PATH_INVALID)
		return error(_("invalid path '%s'"), ce->name);

	if (!skip_df_check &&
	    check_file_directory_conflict(istate, ce, pos, ok_to_replace)) {
		if (!ok_to_replace)
			return error(_("'%s' appears as both a file and as a directory"),
				     ce->name);
		pos = index_name_stage_pos(istate, ce->name, ce_namelen(ce), ce_stage(ce), EXPAND_SPARSE);
		pos = -pos-1;
	}
	return pos + 1;
}

int add_index_entry(struct index_state *istate, struct cache_entry *ce, int option)
{
	int pos;

	if (option & ADD_CACHE_JUST_APPEND)
		pos = istate->cache_nr;
	else {
		int ret;
		ret = add_index_entry_with_check(istate, ce, option);
		if (ret <= 0)
			return ret;
		pos = ret - 1;
	}

	/* Make sure the array is big enough .. */
	ALLOC_GROW(istate->cache, istate->cache_nr + 1, istate->cache_alloc);

	/* Add it in.. */
	istate->cache_nr++;
	if (istate->cache_nr > pos + 1)
		MOVE_ARRAY(istate->cache + pos + 1, istate->cache + pos,
			   istate->cache_nr - pos - 1);
	set_index_entry(istate, pos, ce);
	istate->cache_changed |= CE_ENTRY_ADDED;
	return 0;
}

/*
 * "refresh" does not calculate a new sha1 file or bring the
 * cache up-to-date for mode/content changes. But what it
 * _does_ do is to "re-match" the stat information of a file
 * with the cache, so that you can refresh the cache for a
 * file that hasn't been changed but where the stat entry is
 * out of date.
 *
 * For example, you'd want to do this after doing a "git-read-tree",
 * to link up the stat cache details with the proper files.
 */
static struct cache_entry *refresh_cache_ent(struct index_state *istate,
					     struct cache_entry *ce,
					     unsigned int options, int *err,
					     int *changed_ret,
					     int *t2_did_lstat,
					     int *t2_did_scan)
{
	struct stat st;
	struct cache_entry *updated;
	int changed;
	int refresh = options & CE_MATCH_REFRESH;
	int ignore_valid = options & CE_MATCH_IGNORE_VALID;
	int ignore_skip_worktree = options & CE_MATCH_IGNORE_SKIP_WORKTREE;
	int ignore_missing = options & CE_MATCH_IGNORE_MISSING;
	int ignore_fsmonitor = options & CE_MATCH_IGNORE_FSMONITOR;

	if (!refresh || ce_uptodate(ce))
		return ce;

	if (!ignore_fsmonitor)
		refresh_fsmonitor(istate);
	/*
	 * CE_VALID or CE_SKIP_WORKTREE means the user promised us
	 * that the change to the work tree does not matter and told
	 * us not to worry.
	 */
	if (!ignore_skip_worktree && ce_skip_worktree(ce)) {
		ce_mark_uptodate(ce);
		return ce;
	}
	if (!ignore_valid && (ce->ce_flags & CE_VALID)) {
		ce_mark_uptodate(ce);
		return ce;
	}
	if (!ignore_fsmonitor && (ce->ce_flags & CE_FSMONITOR_VALID)) {
		ce_mark_uptodate(ce);
		return ce;
	}

	if (has_symlink_leading_path(ce->name, ce_namelen(ce))) {
		if (ignore_missing)
			return ce;
		if (err)
			*err = ENOENT;
		return NULL;
	}

	if (t2_did_lstat)
		*t2_did_lstat = 1;
	if (lstat(ce->name, &st) < 0) {
		if (ignore_missing && errno == ENOENT)
			return ce;
		if (err)
			*err = errno;
		return NULL;
	}

	changed = ie_match_stat(istate, ce, &st, options);
	if (changed_ret)
		*changed_ret = changed;
	if (!changed) {
		/*
		 * The path is unchanged.  If we were told to ignore
		 * valid bit, then we did the actual stat check and
		 * found that the entry is unmodified.  If the entry
		 * is not marked VALID, this is the place to mark it
		 * valid again, under "assume unchanged" mode.
		 */
		if (ignore_valid && assume_unchanged &&
		    !(ce->ce_flags & CE_VALID))
			; /* mark this one VALID again */
		else {
			/*
			 * We do not mark the index itself "modified"
			 * because CE_UPTODATE flag is in-core only;
			 * we are not going to write this change out.
			 */
			if (!S_ISGITLINK(ce->ce_mode)) {
				ce_mark_uptodate(ce);
				mark_fsmonitor_valid(istate, ce);
			}
			return ce;
		}
	}

	if (t2_did_scan)
		*t2_did_scan = 1;
	if (ie_modified(istate, ce, &st, options)) {
		if (err)
			*err = EINVAL;
		return NULL;
	}

	updated = make_empty_cache_entry(istate, ce_namelen(ce));
	copy_cache_entry(updated, ce);
	memcpy(updated->name, ce->name, ce->ce_namelen + 1);
	fill_stat_cache_info(istate, updated, &st);
	/*
	 * If ignore_valid is not set, we should leave CE_VALID bit
	 * alone.  Otherwise, paths marked with --no-assume-unchanged
	 * (i.e. things to be edited) will reacquire CE_VALID bit
	 * automatically, which is not really what we want.
	 */
	if (!ignore_valid && assume_unchanged &&
	    !(ce->ce_flags & CE_VALID))
		updated->ce_flags &= ~CE_VALID;

	/* istate->cache_changed is updated in the caller */
	return updated;
}

static void show_file(const char * fmt, const char * name, int in_porcelain,
		      int * first, const char *header_msg)
{
	if (in_porcelain && *first && header_msg) {
		printf("%s\n", header_msg);
		*first = 0;
	}
	printf(fmt, name);
}

int repo_refresh_and_write_index(struct repository *repo,
				 unsigned int refresh_flags,
				 unsigned int write_flags,
				 int gentle,
				 const struct pathspec *pathspec,
				 char *seen, const char *header_msg)
{
	struct lock_file lock_file = LOCK_INIT;
	int fd, ret = 0;

	fd = repo_hold_locked_index(repo, &lock_file, 0);
	if (!gentle && fd < 0)
		return -1;
	if (refresh_index(repo->index, refresh_flags, pathspec, seen, header_msg))
		ret = 1;
	if (0 <= fd && write_locked_index(repo->index, &lock_file, COMMIT_LOCK | write_flags))
		ret = -1;
	return ret;
}


int refresh_index(struct index_state *istate, unsigned int flags,
		  const struct pathspec *pathspec,
		  char *seen, const char *header_msg)
{
	int i;
	int has_errors = 0;
	int really = (flags & REFRESH_REALLY) != 0;
	int allow_unmerged = (flags & REFRESH_UNMERGED) != 0;
	int quiet = (flags & REFRESH_QUIET) != 0;
	int not_new = (flags & REFRESH_IGNORE_MISSING) != 0;
	int ignore_submodules = (flags & REFRESH_IGNORE_SUBMODULES) != 0;
	int ignore_skip_worktree = (flags & REFRESH_IGNORE_SKIP_WORKTREE) != 0;
	int first = 1;
	int in_porcelain = (flags & REFRESH_IN_PORCELAIN);
	unsigned int options = (CE_MATCH_REFRESH |
				(really ? CE_MATCH_IGNORE_VALID : 0) |
				(not_new ? CE_MATCH_IGNORE_MISSING : 0));
	const char *modified_fmt;
	const char *deleted_fmt;
	const char *typechange_fmt;
	const char *added_fmt;
	const char *unmerged_fmt;
	struct progress *progress = NULL;
	int t2_sum_lstat = 0;
	int t2_sum_scan = 0;

	if (flags & REFRESH_PROGRESS && isatty(2))
		progress = start_delayed_progress(_("Refresh index"),
						  istate->cache_nr);

	trace_performance_enter();
	modified_fmt   = in_porcelain ? "M\t%s\n" : "%s: needs update\n";
	deleted_fmt    = in_porcelain ? "D\t%s\n" : "%s: needs update\n";
	typechange_fmt = in_porcelain ? "T\t%s\n" : "%s: needs update\n";
	added_fmt      = in_porcelain ? "A\t%s\n" : "%s: needs update\n";
	unmerged_fmt   = in_porcelain ? "U\t%s\n" : "%s: needs merge\n";
	enable_fscache(0);
	/*
	 * Use the multi-threaded preload_index() to refresh most of the
	 * cache entries quickly then in the single threaded loop below,
	 * we only have to do the special cases that are left.
	 */
	preload_index(istate, pathspec, 0);
	trace2_region_enter("index", "refresh", NULL);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce, *new_entry;
		int cache_errno = 0;
		int changed = 0;
		int filtered = 0;
		int t2_did_lstat = 0;
		int t2_did_scan = 0;

		ce = istate->cache[i];
		if (ignore_submodules && S_ISGITLINK(ce->ce_mode))
			continue;
		if (ignore_skip_worktree && ce_skip_worktree(ce))
			continue;

		/*
		 * If this entry is a sparse directory, then there isn't
		 * any stat() information to update. Ignore the entry.
		 */
		if (S_ISSPARSEDIR(ce->ce_mode))
			continue;

		if (pathspec && !ce_path_match(istate, ce, pathspec, seen))
			filtered = 1;

		if (ce_stage(ce)) {
			while ((i < istate->cache_nr) &&
			       ! strcmp(istate->cache[i]->name, ce->name))
				i++;
			i--;
			if (allow_unmerged)
				continue;
			if (!filtered)
				show_file(unmerged_fmt, ce->name, in_porcelain,
					  &first, header_msg);
			has_errors = 1;
			continue;
		}

		if (filtered)
			continue;

		new_entry = refresh_cache_ent(istate, ce, options,
					      &cache_errno, &changed,
					      &t2_did_lstat, &t2_did_scan);
		t2_sum_lstat += t2_did_lstat;
		t2_sum_scan += t2_did_scan;
		if (new_entry == ce)
			continue;
		display_progress(progress, i);
		if (!new_entry) {
			const char *fmt;

			if (really && cache_errno == EINVAL) {
				/* If we are doing --really-refresh that
				 * means the index is not valid anymore.
				 */
				ce->ce_flags &= ~CE_VALID;
				ce->ce_flags |= CE_UPDATE_IN_BASE;
				mark_fsmonitor_invalid(istate, ce);
				istate->cache_changed |= CE_ENTRY_CHANGED;
			}
			if (quiet)
				continue;

			if (cache_errno == ENOENT)
				fmt = deleted_fmt;
			else if (ce_intent_to_add(ce))
				fmt = added_fmt; /* must be before other checks */
			else if (changed & TYPE_CHANGED)
				fmt = typechange_fmt;
			else
				fmt = modified_fmt;
			show_file(fmt,
				  ce->name, in_porcelain, &first, header_msg);
			has_errors = 1;
			continue;
		}

		replace_index_entry(istate, i, new_entry);
	}
	trace2_data_intmax("index", NULL, "refresh/sum_lstat", t2_sum_lstat);
	trace2_data_intmax("index", NULL, "refresh/sum_scan", t2_sum_scan);
	trace2_region_leave("index", "refresh", NULL);
	display_progress(progress, istate->cache_nr);
	stop_progress(&progress);
	trace_performance_leave("refresh index");
	disable_fscache();
	return has_errors;
}

struct cache_entry *refresh_cache_entry(struct index_state *istate,
					struct cache_entry *ce,
					unsigned int options)
{
	return refresh_cache_ent(istate, ce, options, NULL, NULL, NULL, NULL);
}


/*****************************************************************
 * Index File I/O
 *****************************************************************/

#define INDEX_FORMAT_DEFAULT 3

static unsigned int get_index_format_default(struct repository *r)
{
	char *envversion = getenv("GIT_INDEX_VERSION");
	char *endp;
	unsigned int version = INDEX_FORMAT_DEFAULT;

	if (!envversion) {
		prepare_repo_settings(r);

		if (r->settings.index_version >= 0)
			version = r->settings.index_version;
		if (version < INDEX_FORMAT_LB || INDEX_FORMAT_UB < version) {
			warning(_("index.version set, but the value is invalid.\n"
				  "Using version %i"), INDEX_FORMAT_DEFAULT);
			return INDEX_FORMAT_DEFAULT;
		}
		return version;
	}

	version = strtoul(envversion, &endp, 10);
	if (*endp ||
	    version < INDEX_FORMAT_LB || INDEX_FORMAT_UB < version) {
		warning(_("GIT_INDEX_VERSION set, but the value is invalid.\n"
			  "Using version %i"), INDEX_FORMAT_DEFAULT);
		version = INDEX_FORMAT_DEFAULT;
	}
	return version;
}

/*
 * dev/ino/uid/gid/size are also just tracked to the low 32 bits
 * Again - this is just a (very strong in practice) heuristic that
 * the inode hasn't changed.
 *
 * We save the fields in big-endian order to allow using the
 * index file over NFS transparently.
 */
struct ondisk_cache_entry {
	struct cache_time ctime;
	struct cache_time mtime;
	uint32_t dev;
	uint32_t ino;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint32_t size;
	/*
	 * unsigned char hash[hashsz];
	 * uint16_t flags;
	 * if (flags & CE_EXTENDED)
	 *	uint16_t flags2;
	 */
	unsigned char data[GIT_MAX_RAWSZ + 2 * sizeof(uint16_t)];
	char name[FLEX_ARRAY];
};

/* These are only used for v3 or lower */
#define align_padding_size(size, len) ((size + (len) + 8) & ~7) - (size + len)
#define align_flex_name(STRUCT,len) ((offsetof(struct STRUCT,data) + (len) + 8) & ~7)
#define ondisk_cache_entry_size(len) align_flex_name(ondisk_cache_entry,len)
#define ondisk_data_size(flags, len) (the_hash_algo->rawsz + \
				     ((flags & CE_EXTENDED) ? 2 : 1) * sizeof(uint16_t) + len)
#define ondisk_data_size_max(len) (ondisk_data_size(CE_EXTENDED, len))
#define ondisk_ce_size(ce) (ondisk_cache_entry_size(ondisk_data_size((ce)->ce_flags, ce_namelen(ce))))

/* Allow fsck to force verification of the index checksum. */
int verify_index_checksum;

/* Allow fsck to force verification of the cache entry order. */
int verify_ce_order;

static int verify_hdr(const struct cache_header *hdr, unsigned long size)
{
	git_hash_ctx c;
	unsigned char hash[GIT_MAX_RAWSZ];
	int hdr_version;
	unsigned char *start, *end;
	struct object_id oid;

	if (hdr->hdr_signature != htonl(CACHE_SIGNATURE))
		return error(_("bad signature 0x%08x"), hdr->hdr_signature);
	hdr_version = ntohl(hdr->hdr_version);
	if (hdr_version < INDEX_FORMAT_LB || INDEX_FORMAT_UB < hdr_version)
		return error(_("bad index version %d"), hdr_version);

	if (!verify_index_checksum)
		return 0;

	end = (unsigned char *)hdr + size;
	start = end - the_hash_algo->rawsz;
	oidread(&oid, start);
	if (oideq(&oid, null_oid()))
		return 0;

	the_hash_algo->init_fn(&c);
	the_hash_algo->update_fn(&c, hdr, size - the_hash_algo->rawsz);
	the_hash_algo->final_fn(hash, &c);
	if (!hasheq(hash, start))
		return error(_("bad index file sha1 signature"));
	return 0;
}

static int read_index_extension(struct index_state *istate,
				const char *ext, const char *data, unsigned long sz)
{
	switch (CACHE_EXT(ext)) {
	case CACHE_EXT_TREE:
		istate->cache_tree = cache_tree_read(data, sz);
		break;
	case CACHE_EXT_RESOLVE_UNDO:
		istate->resolve_undo = resolve_undo_read(data, sz);
		break;
	case CACHE_EXT_LINK:
		if (read_link_extension(istate, data, sz))
			return -1;
		break;
	case CACHE_EXT_UNTRACKED:
		istate->untracked = read_untracked_extension(data, sz);
		break;
	case CACHE_EXT_FSMONITOR:
		read_fsmonitor_extension(istate, data, sz);
		break;
	case CACHE_EXT_ENDOFINDEXENTRIES:
	case CACHE_EXT_INDEXENTRYOFFSETTABLE:
		/* already handled in do_read_index() */
		break;
	case CACHE_EXT_SPARSE_DIRECTORIES:
		/* no content, only an indicator */
		istate->sparse_index = INDEX_COLLAPSED;
		break;
	default:
		if (*ext < 'A' || 'Z' < *ext)
			return error(_("index uses %.4s extension, which we do not understand"),
				     ext);
		fprintf_ln(stderr, _("ignoring %.4s extension"), ext);
		break;
	}
	return 0;
}

/*
 * Parses the contents of the cache entry contained within the 'ondisk' buffer
 * into a new incore 'cache_entry'.
 *
 * Note that 'char *ondisk' may not be aligned to a 4-byte address interval in
 * index v4, so we cannot cast it to 'struct ondisk_cache_entry *' and access
 * its members. Instead, we use the byte offsets of members within the struct to
 * identify where 'get_be16()', 'get_be32()', and 'oidread()' (which can all
 * read from an unaligned memory buffer) should read from the 'ondisk' buffer
 * into the corresponding incore 'cache_entry' members.
 */
static struct cache_entry *create_from_disk(struct mem_pool *ce_mem_pool,
					    unsigned int version,
					    const char *ondisk,
					    unsigned long *ent_size,
					    const struct cache_entry *previous_ce)
{
	struct cache_entry *ce;
	size_t len;
	const char *name;
	const unsigned hashsz = the_hash_algo->rawsz;
	const char *flagsp = ondisk + offsetof(struct ondisk_cache_entry, data) + hashsz;
	unsigned int flags;
	size_t copy_len = 0;
	/*
	 * Adjacent cache entries tend to share the leading paths, so it makes
	 * sense to only store the differences in later entries.  In the v4
	 * on-disk format of the index, each on-disk cache entry stores the
	 * number of bytes to be stripped from the end of the previous name,
	 * and the bytes to append to the result, to come up with its name.
	 */
	int expand_name_field = version == 4;

	/* On-disk flags are just 16 bits */
	flags = get_be16(flagsp);
	len = flags & CE_NAMEMASK;

	if (flags & CE_EXTENDED) {
		int extended_flags;
		extended_flags = get_be16(flagsp + sizeof(uint16_t)) << 16;
		/* We do not yet understand any bit out of CE_EXTENDED_FLAGS */
		if (extended_flags & ~CE_EXTENDED_FLAGS)
			die(_("unknown index entry format 0x%08x"), extended_flags);
		flags |= extended_flags;
		name = (const char *)(flagsp + 2 * sizeof(uint16_t));
	}
	else
		name = (const char *)(flagsp + sizeof(uint16_t));

	if (expand_name_field) {
		const unsigned char *cp = (const unsigned char *)name;
		size_t strip_len, previous_len;

		/* If we're at the beginning of a block, ignore the previous name */
		strip_len = decode_varint(&cp);
		if (previous_ce) {
			previous_len = previous_ce->ce_namelen;
			if (previous_len < strip_len)
				die(_("malformed name field in the index, near path '%s'"),
					previous_ce->name);
			copy_len = previous_len - strip_len;
		}
		name = (const char *)cp;
	}

	if (len == CE_NAMEMASK) {
		len = strlen(name);
		if (expand_name_field)
			len += copy_len;
	}

	ce = mem_pool__ce_alloc(ce_mem_pool, len);

	/*
	 * NEEDSWORK: using 'offsetof()' is cumbersome and should be replaced
	 * with something more akin to 'load_bitmap_entries_v1()'s use of
	 * 'read_be16'/'read_be32'. For consistency with the corresponding
	 * ondisk entry write function ('copy_cache_entry_to_ondisk()'), this
	 * should be done at the same time as removing references to
	 * 'ondisk_cache_entry' there.
	 */
	ce->ce_stat_data.sd_ctime.sec = get_be32(ondisk + offsetof(struct ondisk_cache_entry, ctime)
							+ offsetof(struct cache_time, sec));
	ce->ce_stat_data.sd_mtime.sec = get_be32(ondisk + offsetof(struct ondisk_cache_entry, mtime)
							+ offsetof(struct cache_time, sec));
	ce->ce_stat_data.sd_ctime.nsec = get_be32(ondisk + offsetof(struct ondisk_cache_entry, ctime)
							 + offsetof(struct cache_time, nsec));
	ce->ce_stat_data.sd_mtime.nsec = get_be32(ondisk + offsetof(struct ondisk_cache_entry, mtime)
							 + offsetof(struct cache_time, nsec));
	ce->ce_stat_data.sd_dev   = get_be32(ondisk + offsetof(struct ondisk_cache_entry, dev));
	ce->ce_stat_data.sd_ino   = get_be32(ondisk + offsetof(struct ondisk_cache_entry, ino));
	ce->ce_mode  = get_be32(ondisk + offsetof(struct ondisk_cache_entry, mode));
	ce->ce_stat_data.sd_uid   = get_be32(ondisk + offsetof(struct ondisk_cache_entry, uid));
	ce->ce_stat_data.sd_gid   = get_be32(ondisk + offsetof(struct ondisk_cache_entry, gid));
	ce->ce_stat_data.sd_size  = get_be32(ondisk + offsetof(struct ondisk_cache_entry, size));
	ce->ce_flags = flags & ~CE_NAMEMASK;
	ce->ce_namelen = len;
	ce->index = 0;
	oidread(&ce->oid, (const unsigned char *)ondisk + offsetof(struct ondisk_cache_entry, data));

	if (expand_name_field) {
		if (copy_len)
			memcpy(ce->name, previous_ce->name, copy_len);
		memcpy(ce->name + copy_len, name, len + 1 - copy_len);
		*ent_size = (name - ((char *)ondisk)) + len + 1 - copy_len;
	} else {
		memcpy(ce->name, name, len + 1);
		*ent_size = ondisk_ce_size(ce);
	}
	return ce;
}

static void check_ce_order(struct index_state *istate)
{
	unsigned int i;

	if (!verify_ce_order)
		return;

	for (i = 1; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i - 1];
		struct cache_entry *next_ce = istate->cache[i];
		int name_compare = strcmp(ce->name, next_ce->name);

		if (0 < name_compare)
			die(_("unordered stage entries in index"));
		if (!name_compare) {
			if (!ce_stage(ce))
				die(_("multiple stage entries for merged file '%s'"),
				    ce->name);
			if (ce_stage(ce) > ce_stage(next_ce))
				die(_("unordered stage entries for '%s'"),
				    ce->name);
		}
	}
}

static void tweak_untracked_cache(struct index_state *istate)
{
	struct repository *r = the_repository;

	prepare_repo_settings(r);

	switch (r->settings.core_untracked_cache) {
	case UNTRACKED_CACHE_REMOVE:
		remove_untracked_cache(istate);
		break;
	case UNTRACKED_CACHE_WRITE:
		add_untracked_cache(istate);
		break;
	case UNTRACKED_CACHE_KEEP:
		/*
		 * Either an explicit "core.untrackedCache=keep", the
		 * default if "core.untrackedCache" isn't configured,
		 * or a fallback on an unknown "core.untrackedCache"
		 * value.
		 */
		break;
	}
}

static void tweak_split_index(struct index_state *istate)
{
	switch (git_config_get_split_index()) {
	case -1: /* unset: do nothing */
		break;
	case 0: /* false */
		remove_split_index(istate);
		break;
	case 1: /* true */
		add_split_index(istate);
		break;
	default: /* unknown value: do nothing */
		break;
	}
}

static void post_read_index_from(struct index_state *istate)
{
	check_ce_order(istate);
	tweak_untracked_cache(istate);
	tweak_split_index(istate);
	tweak_fsmonitor(istate);
}

static size_t estimate_cache_size_from_compressed(unsigned int entries)
{
	return entries * (sizeof(struct cache_entry) + CACHE_ENTRY_PATH_LENGTH);
}

static size_t estimate_cache_size(size_t ondisk_size, unsigned int entries)
{
	long per_entry = sizeof(struct cache_entry) - sizeof(struct ondisk_cache_entry);

	/*
	 * Account for potential alignment differences.
	 */
	per_entry += align_padding_size(per_entry, 0);
	return ondisk_size + entries * per_entry;
}

struct index_entry_offset
{
	/* starting byte offset into index file, count of index entries in this block */
	int offset, nr;
};

struct index_entry_offset_table
{
	int nr;
	struct index_entry_offset entries[FLEX_ARRAY];
};

static struct index_entry_offset_table *read_ieot_extension(const char *mmap, size_t mmap_size, size_t offset);
static void write_ieot_extension(struct strbuf *sb, struct index_entry_offset_table *ieot);

static size_t read_eoie_extension(const char *mmap, size_t mmap_size);
static void write_eoie_extension(struct strbuf *sb, git_hash_ctx *eoie_context, size_t offset);

struct load_index_extensions
{
	pthread_t pthread;
	struct index_state *istate;
	const char *mmap;
	size_t mmap_size;
	unsigned long src_offset;
};

static void *load_index_extensions(void *_data)
{
	struct load_index_extensions *p = _data;
	unsigned long src_offset = p->src_offset;

	while (src_offset <= p->mmap_size - the_hash_algo->rawsz - 8) {
		/* After an array of active_nr index entries,
		 * there can be arbitrary number of extended
		 * sections, each of which is prefixed with
		 * extension name (4-byte) and section length
		 * in 4-byte network byte order.
		 */
		uint32_t extsize = get_be32(p->mmap + src_offset + 4);
		if (read_index_extension(p->istate,
					 p->mmap + src_offset,
					 p->mmap + src_offset + 8,
					 extsize) < 0) {
			munmap((void *)p->mmap, p->mmap_size);
			die(_("index file corrupt"));
		}
		src_offset += 8;
		src_offset += extsize;
	}

	return NULL;
}

/*
 * A helper function that will load the specified range of cache entries
 * from the memory mapped file and add them to the given index.
 */
static unsigned long load_cache_entry_block(struct index_state *istate,
			struct mem_pool *ce_mem_pool, int offset, int nr, const char *mmap,
			unsigned long start_offset, const struct cache_entry *previous_ce)
{
	int i;
	unsigned long src_offset = start_offset;

	for (i = offset; i < offset + nr; i++) {
		struct cache_entry *ce;
		unsigned long consumed;

		ce = create_from_disk(ce_mem_pool, istate->version,
				      mmap + src_offset,
				      &consumed, previous_ce);
		set_index_entry(istate, i, ce);

		src_offset += consumed;
		previous_ce = ce;
	}
	return src_offset - start_offset;
}

static unsigned long load_all_cache_entries(struct index_state *istate,
			const char *mmap, size_t mmap_size, unsigned long src_offset)
{
	unsigned long consumed;

	istate->ce_mem_pool = xmalloc(sizeof(*istate->ce_mem_pool));
	if (istate->version == 4) {
		mem_pool_init(istate->ce_mem_pool,
				estimate_cache_size_from_compressed(istate->cache_nr));
	} else {
		mem_pool_init(istate->ce_mem_pool,
				estimate_cache_size(mmap_size, istate->cache_nr));
	}

	consumed = load_cache_entry_block(istate, istate->ce_mem_pool,
					0, istate->cache_nr, mmap, src_offset, NULL);
	return consumed;
}

/*
 * Mostly randomly chosen maximum thread counts: we
 * cap the parallelism to online_cpus() threads, and we want
 * to have at least 10000 cache entries per thread for it to
 * be worth starting a thread.
 */

#define THREAD_COST		(10000)

struct load_cache_entries_thread_data
{
	pthread_t pthread;
	struct index_state *istate;
	struct mem_pool *ce_mem_pool;
	int offset;
	const char *mmap;
	struct index_entry_offset_table *ieot;
	int ieot_start;		/* starting index into the ieot array */
	int ieot_blocks;	/* count of ieot entries to process */
	unsigned long consumed;	/* return # of bytes in index file processed */
};

/*
 * A thread proc to run the load_cache_entries() computation
 * across multiple background threads.
 */
static void *load_cache_entries_thread(void *_data)
{
	struct load_cache_entries_thread_data *p = _data;
	int i;

	/* iterate across all ieot blocks assigned to this thread */
	for (i = p->ieot_start; i < p->ieot_start + p->ieot_blocks; i++) {
		p->consumed += load_cache_entry_block(p->istate, p->ce_mem_pool,
			p->offset, p->ieot->entries[i].nr, p->mmap, p->ieot->entries[i].offset, NULL);
		p->offset += p->ieot->entries[i].nr;
	}
	return NULL;
}

static unsigned long load_cache_entries_threaded(struct index_state *istate, const char *mmap, size_t mmap_size,
						 int nr_threads, struct index_entry_offset_table *ieot)
{
	int i, offset, ieot_blocks, ieot_start, err;
	struct load_cache_entries_thread_data *data;
	unsigned long consumed = 0;

	/* a little sanity checking */
	if (istate->name_hash_initialized)
		BUG("the name hash isn't thread safe");

	istate->ce_mem_pool = xmalloc(sizeof(*istate->ce_mem_pool));
	mem_pool_init(istate->ce_mem_pool, 0);

	/* ensure we have no more threads than we have blocks to process */
	if (nr_threads > ieot->nr)
		nr_threads = ieot->nr;
	CALLOC_ARRAY(data, nr_threads);

	offset = ieot_start = 0;
	ieot_blocks = DIV_ROUND_UP(ieot->nr, nr_threads);
	for (i = 0; i < nr_threads; i++) {
		struct load_cache_entries_thread_data *p = &data[i];
		int nr, j;

		if (ieot_start + ieot_blocks > ieot->nr)
			ieot_blocks = ieot->nr - ieot_start;

		p->istate = istate;
		p->offset = offset;
		p->mmap = mmap;
		p->ieot = ieot;
		p->ieot_start = ieot_start;
		p->ieot_blocks = ieot_blocks;

		/* create a mem_pool for each thread */
		nr = 0;
		for (j = p->ieot_start; j < p->ieot_start + p->ieot_blocks; j++)
			nr += p->ieot->entries[j].nr;
		p->ce_mem_pool = xmalloc(sizeof(*istate->ce_mem_pool));
		if (istate->version == 4) {
			mem_pool_init(p->ce_mem_pool,
				estimate_cache_size_from_compressed(nr));
		} else {
			mem_pool_init(p->ce_mem_pool,
				estimate_cache_size(mmap_size, nr));
		}

		err = pthread_create(&p->pthread, NULL, load_cache_entries_thread, p);
		if (err)
			die(_("unable to create load_cache_entries thread: %s"), strerror(err));

		/* increment by the number of cache entries in the ieot block being processed */
		for (j = 0; j < ieot_blocks; j++)
			offset += ieot->entries[ieot_start + j].nr;
		ieot_start += ieot_blocks;
	}

	for (i = 0; i < nr_threads; i++) {
		struct load_cache_entries_thread_data *p = &data[i];

		err = pthread_join(p->pthread, NULL);
		if (err)
			die(_("unable to join load_cache_entries thread: %s"), strerror(err));
		mem_pool_combine(istate->ce_mem_pool, p->ce_mem_pool);
		consumed += p->consumed;
	}

	free(data);

	return consumed;
}

static void set_new_index_sparsity(struct index_state *istate)
{
	/*
	 * If the index's repo exists, mark it sparse according to
	 * repo settings.
	 */
	prepare_repo_settings(istate->repo);
	if (!istate->repo->settings.command_requires_full_index &&
	    is_sparse_index_allowed(istate, 0))
		istate->sparse_index = 1;
}

/* remember to discard_cache() before reading a different cache! */
int do_read_index(struct index_state *istate, const char *path, int must_exist)
{
	int fd;
	struct stat st;
	unsigned long src_offset;
	const struct cache_header *hdr;
	const char *mmap;
	size_t mmap_size;
	struct load_index_extensions p;
	size_t extension_offset = 0;
	int nr_threads, cpus;
	struct index_entry_offset_table *ieot = NULL;

	if (istate->initialized)
		return istate->cache_nr;

	istate->timestamp.sec = 0;
	istate->timestamp.nsec = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (!must_exist && errno == ENOENT) {
			set_new_index_sparsity(istate);
			istate->initialized = 1;
			return 0;
		}
		die_errno(_("%s: index file open failed"), path);
	}

	if (fstat(fd, &st))
		die_errno(_("%s: cannot stat the open index"), path);

	mmap_size = xsize_t(st.st_size);
	if (mmap_size < sizeof(struct cache_header) + the_hash_algo->rawsz)
		die(_("%s: index file smaller than expected"), path);

	mmap = xmmap_gently(NULL, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mmap == MAP_FAILED)
		die_errno(_("%s: unable to map index file%s"), path,
			mmap_os_err());
	close(fd);

	hdr = (const struct cache_header *)mmap;
	if (verify_hdr(hdr, mmap_size) < 0)
		goto unmap;

	oidread(&istate->oid, (const unsigned char *)hdr + mmap_size - the_hash_algo->rawsz);
	istate->version = ntohl(hdr->hdr_version);
	istate->cache_nr = ntohl(hdr->hdr_entries);
	istate->cache_alloc = alloc_nr(istate->cache_nr);
	CALLOC_ARRAY(istate->cache, istate->cache_alloc);
	istate->initialized = 1;

	p.istate = istate;
	p.mmap = mmap;
	p.mmap_size = mmap_size;

	src_offset = sizeof(*hdr);

	if (git_config_get_index_threads(&nr_threads))
		nr_threads = 1;

	/* TODO: does creating more threads than cores help? */
	if (!nr_threads) {
		nr_threads = istate->cache_nr / THREAD_COST;
		cpus = online_cpus();
		if (nr_threads > cpus)
			nr_threads = cpus;
	}

	if (!HAVE_THREADS)
		nr_threads = 1;

	if (nr_threads > 1) {
		extension_offset = read_eoie_extension(mmap, mmap_size);
		if (extension_offset) {
			int err;

			p.src_offset = extension_offset;
			err = pthread_create(&p.pthread, NULL, load_index_extensions, &p);
			if (err)
				die(_("unable to create load_index_extensions thread: %s"), strerror(err));

			nr_threads--;
		}
	}

	/*
	 * Locate and read the index entry offset table so that we can use it
	 * to multi-thread the reading of the cache entries.
	 */
	if (extension_offset && nr_threads > 1)
		ieot = read_ieot_extension(mmap, mmap_size, extension_offset);

	if (ieot) {
		src_offset += load_cache_entries_threaded(istate, mmap, mmap_size, nr_threads, ieot);
		free(ieot);
	} else {
		src_offset += load_all_cache_entries(istate, mmap, mmap_size, src_offset);
	}

	istate->timestamp.sec = st.st_mtime;
	istate->timestamp.nsec = ST_MTIME_NSEC(st);

	/* if we created a thread, join it otherwise load the extensions on the primary thread */
	if (extension_offset) {
		int ret = pthread_join(p.pthread, NULL);
		if (ret)
			die(_("unable to join load_index_extensions thread: %s"), strerror(ret));
	} else {
		p.src_offset = src_offset;
		load_index_extensions(&p);
	}
	munmap((void *)mmap, mmap_size);

	/*
	 * TODO trace2: replace "the_repository" with the actual repo instance
	 * that is associated with the given "istate".
	 */
	trace2_data_intmax("index", the_repository, "read/version",
			   istate->version);
	trace2_data_intmax("index", the_repository, "read/cache_nr",
			   istate->cache_nr);

	/*
	 * If the command explicitly requires a full index, force it
	 * to be full. Otherwise, correct the sparsity based on repository
	 * settings and other properties of the index (if necessary).
	 */
	prepare_repo_settings(istate->repo);
	if (istate->repo->settings.command_requires_full_index)
		ensure_full_index(istate);
	else
		ensure_correct_sparsity(istate);

	return istate->cache_nr;

unmap:
	munmap((void *)mmap, mmap_size);
	die(_("index file corrupt"));
}

/*
 * Signal that the shared index is used by updating its mtime.
 *
 * This way, shared index can be removed if they have not been used
 * for some time.
 */
static void freshen_shared_index(const char *shared_index, int warn)
{
	if (!check_and_freshen_file(shared_index, 1) && warn)
		warning(_("could not freshen shared index '%s'"), shared_index);
}

int read_index_from(struct index_state *istate, const char *path,
		    const char *gitdir)
{
	struct split_index *split_index;
	int ret;
	char *base_oid_hex;
	char *base_path;

	/* istate->initialized covers both .git/index and .git/sharedindex.xxx */
	if (istate->initialized)
		return istate->cache_nr;

	/*
	 * TODO trace2: replace "the_repository" with the actual repo instance
	 * that is associated with the given "istate".
	 */
	trace2_region_enter_printf("index", "do_read_index", the_repository,
				   "%s", path);
	trace_performance_enter();
	ret = do_read_index(istate, path, 0);
	trace_performance_leave("read cache %s", path);
	trace2_region_leave_printf("index", "do_read_index", the_repository,
				   "%s", path);

	split_index = istate->split_index;
	if (!split_index || is_null_oid(&split_index->base_oid)) {
		post_read_index_from(istate);
		return ret;
	}

	trace_performance_enter();
	if (split_index->base)
		release_index(split_index->base);
	else
		ALLOC_ARRAY(split_index->base, 1);
	index_state_init(split_index->base, istate->repo);

	base_oid_hex = oid_to_hex(&split_index->base_oid);
	base_path = xstrfmt("%s/sharedindex.%s", gitdir, base_oid_hex);
	if (file_exists(base_path)) {
		trace2_region_enter_printf("index", "shared/do_read_index",
					the_repository, "%s", base_path);

		ret = do_read_index(split_index->base, base_path, 0);
		trace2_region_leave_printf("index", "shared/do_read_index",
					the_repository, "%s", base_path);
	} else {
		char *path_copy = xstrdup(path);
		char *base_path2 = xstrfmt("%s/sharedindex.%s",
					   dirname(path_copy), base_oid_hex);
		free(path_copy);
		trace2_region_enter_printf("index", "shared/do_read_index",
					   the_repository, "%s", base_path2);
		ret = do_read_index(split_index->base, base_path2, 1);
		trace2_region_leave_printf("index", "shared/do_read_index",
					   the_repository, "%s", base_path2);
		free(base_path2);
	}
	if (!oideq(&split_index->base_oid, &split_index->base->oid))
		die(_("broken index, expect %s in %s, got %s"),
		    base_oid_hex, base_path,
		    oid_to_hex(&split_index->base->oid));

	freshen_shared_index(base_path, 0);
	merge_base_index(istate);
	post_read_index_from(istate);
	trace_performance_leave("read cache %s", base_path);
	free(base_path);
	return ret;
}

int is_index_unborn(struct index_state *istate)
{
	return (!istate->cache_nr && !istate->timestamp.sec);
}

void index_state_init(struct index_state *istate, struct repository *r)
{
	struct index_state blank = INDEX_STATE_INIT(r);
	memcpy(istate, &blank, sizeof(*istate));
}

void release_index(struct index_state *istate)
{
	/*
	 * Cache entries in istate->cache[] should have been allocated
	 * from the memory pool associated with this index, or from an
	 * associated split_index. There is no need to free individual
	 * cache entries. validate_cache_entries can detect when this
	 * assertion does not hold.
	 */
	validate_cache_entries(istate);

	resolve_undo_clear_index(istate);
	free_name_hash(istate);
	cache_tree_free(&(istate->cache_tree));
	free(istate->fsmonitor_last_update);
	free(istate->cache);
	discard_split_index(istate);
	free_untracked_cache(istate->untracked);

	if (istate->sparse_checkout_patterns) {
		clear_pattern_list(istate->sparse_checkout_patterns);
		FREE_AND_NULL(istate->sparse_checkout_patterns);
	}

	if (istate->ce_mem_pool) {
		mem_pool_discard(istate->ce_mem_pool, should_validate_cache_entries());
		FREE_AND_NULL(istate->ce_mem_pool);
	}
}

void discard_index(struct index_state *istate)
{
	release_index(istate);
	index_state_init(istate, istate->repo);
}

/*
 * Validate the cache entries of this index.
 * All cache entries associated with this index
 * should have been allocated by the memory pool
 * associated with this index, or by a referenced
 * split index.
 */
void validate_cache_entries(const struct index_state *istate)
{
	int i;

	if (!should_validate_cache_entries() ||!istate || !istate->initialized)
		return;

	for (i = 0; i < istate->cache_nr; i++) {
		if (!istate) {
			BUG("cache entry is not allocated from expected memory pool");
		} else if (!istate->ce_mem_pool ||
			!mem_pool_contains(istate->ce_mem_pool, istate->cache[i])) {
			if (!istate->split_index ||
				!istate->split_index->base ||
				!istate->split_index->base->ce_mem_pool ||
				!mem_pool_contains(istate->split_index->base->ce_mem_pool, istate->cache[i])) {
				BUG("cache entry is not allocated from expected memory pool");
			}
		}
	}

	if (istate->split_index)
		validate_cache_entries(istate->split_index->base);
}

int unmerged_index(const struct index_state *istate)
{
	int i;
	for (i = 0; i < istate->cache_nr; i++) {
		if (ce_stage(istate->cache[i]))
			return 1;
	}
	return 0;
}

int repo_index_has_changes(struct repository *repo,
			   struct tree *tree,
			   struct strbuf *sb)
{
	struct index_state *istate = repo->index;
	struct object_id cmp;
	int i;

	if (tree)
		cmp = tree->object.oid;
	if (tree || !repo_get_oid_tree(repo, "HEAD", &cmp)) {
		struct diff_options opt;

		repo_diff_setup(repo, &opt);
		opt.flags.exit_with_status = 1;
		if (!sb)
			opt.flags.quick = 1;
		diff_setup_done(&opt);
		do_diff_cache(&cmp, &opt);
		diffcore_std(&opt);
		for (i = 0; sb && i < diff_queued_diff.nr; i++) {
			if (i)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, diff_queued_diff.queue[i]->two->path);
		}
		diff_flush(&opt);
		return opt.flags.has_changes != 0;
	} else {
		/* TODO: audit for interaction with sparse-index. */
		ensure_full_index(istate);
		for (i = 0; sb && i < istate->cache_nr; i++) {
			if (i)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, istate->cache[i]->name);
		}
		return !!istate->cache_nr;
	}
}

static int write_index_ext_header(struct hashfile *f,
				  git_hash_ctx *eoie_f,
				  unsigned int ext,
				  unsigned int sz)
{
	hashwrite_be32(f, ext);
	hashwrite_be32(f, sz);

	if (eoie_f) {
		ext = htonl(ext);
		sz = htonl(sz);
		the_hash_algo->update_fn(eoie_f, &ext, sizeof(ext));
		the_hash_algo->update_fn(eoie_f, &sz, sizeof(sz));
	}
	return 0;
}

static void ce_smudge_racily_clean_entry(struct index_state *istate,
					 struct cache_entry *ce)
{
	/*
	 * The only thing we care about in this function is to smudge the
	 * falsely clean entry due to touch-update-touch race, so we leave
	 * everything else as they are.  We are called for entries whose
	 * ce_stat_data.sd_mtime match the index file mtime.
	 *
	 * Note that this actually does not do much for gitlinks, for
	 * which ce_match_stat_basic() always goes to the actual
	 * contents.  The caller checks with is_racy_timestamp() which
	 * always says "no" for gitlinks, so we are not called for them ;-)
	 */
	struct stat st;

	if (lstat(ce->name, &st) < 0)
		return;
	if (ce_match_stat_basic(ce, &st))
		return;
	if (ce_modified_check_fs(istate, ce, &st)) {
		/* This is "racily clean"; smudge it.  Note that this
		 * is a tricky code.  At first glance, it may appear
		 * that it can break with this sequence:
		 *
		 * $ echo xyzzy >frotz
		 * $ git-update-index --add frotz
		 * $ : >frotz
		 * $ sleep 3
		 * $ echo filfre >nitfol
		 * $ git-update-index --add nitfol
		 *
		 * but it does not.  When the second update-index runs,
		 * it notices that the entry "frotz" has the same timestamp
		 * as index, and if we were to smudge it by resetting its
		 * size to zero here, then the object name recorded
		 * in index is the 6-byte file but the cached stat information
		 * becomes zero --- which would then match what we would
		 * obtain from the filesystem next time we stat("frotz").
		 *
		 * However, the second update-index, before calling
		 * this function, notices that the cached size is 6
		 * bytes and what is on the filesystem is an empty
		 * file, and never calls us, so the cached size information
		 * for "frotz" stays 6 which does not match the filesystem.
		 */
		ce->ce_stat_data.sd_size = 0;
	}
}

/* Copy miscellaneous fields but not the name */
static void copy_cache_entry_to_ondisk(struct ondisk_cache_entry *ondisk,
				       struct cache_entry *ce)
{
	short flags;
	const unsigned hashsz = the_hash_algo->rawsz;
	uint16_t *flagsp = (uint16_t *)(ondisk->data + hashsz);

	ondisk->ctime.sec = htonl(ce->ce_stat_data.sd_ctime.sec);
	ondisk->mtime.sec = htonl(ce->ce_stat_data.sd_mtime.sec);
	ondisk->ctime.nsec = htonl(ce->ce_stat_data.sd_ctime.nsec);
	ondisk->mtime.nsec = htonl(ce->ce_stat_data.sd_mtime.nsec);
	ondisk->dev  = htonl(ce->ce_stat_data.sd_dev);
	ondisk->ino  = htonl(ce->ce_stat_data.sd_ino);
	ondisk->mode = htonl(ce->ce_mode);
	ondisk->uid  = htonl(ce->ce_stat_data.sd_uid);
	ondisk->gid  = htonl(ce->ce_stat_data.sd_gid);
	ondisk->size = htonl(ce->ce_stat_data.sd_size);
	hashcpy(ondisk->data, ce->oid.hash);

	flags = ce->ce_flags & ~CE_NAMEMASK;
	flags |= (ce_namelen(ce) >= CE_NAMEMASK ? CE_NAMEMASK : ce_namelen(ce));
	flagsp[0] = htons(flags);
	if (ce->ce_flags & CE_EXTENDED) {
		flagsp[1] = htons((ce->ce_flags & CE_EXTENDED_FLAGS) >> 16);
	}
}

static int ce_write_entry(struct hashfile *f, struct cache_entry *ce,
			  struct strbuf *previous_name, struct ondisk_cache_entry *ondisk)
{
	int size;
	unsigned int saved_namelen;
	int stripped_name = 0;
	static unsigned char padding[8] = { 0x00 };

	if (ce->ce_flags & CE_STRIP_NAME) {
		saved_namelen = ce_namelen(ce);
		ce->ce_namelen = 0;
		stripped_name = 1;
	}

	size = offsetof(struct ondisk_cache_entry,data) + ondisk_data_size(ce->ce_flags, 0);

	if (!previous_name) {
		int len = ce_namelen(ce);
		copy_cache_entry_to_ondisk(ondisk, ce);
		hashwrite(f, ondisk, size);
		hashwrite(f, ce->name, len);
		hashwrite(f, padding, align_padding_size(size, len));
	} else {
		int common, to_remove, prefix_size;
		unsigned char to_remove_vi[16];
		for (common = 0;
		     (ce->name[common] &&
		      common < previous_name->len &&
		      ce->name[common] == previous_name->buf[common]);
		     common++)
			; /* still matching */
		to_remove = previous_name->len - common;
		prefix_size = encode_varint(to_remove, to_remove_vi);

		copy_cache_entry_to_ondisk(ondisk, ce);
		hashwrite(f, ondisk, size);
		hashwrite(f, to_remove_vi, prefix_size);
		hashwrite(f, ce->name + common, ce_namelen(ce) - common);
		hashwrite(f, padding, 1);

		strbuf_splice(previous_name, common, to_remove,
			      ce->name + common, ce_namelen(ce) - common);
	}
	if (stripped_name) {
		ce->ce_namelen = saved_namelen;
		ce->ce_flags &= ~CE_STRIP_NAME;
	}

	return 0;
}

/*
 * This function verifies if index_state has the correct sha1 of the
 * index file.  Don't die if we have any other failure, just return 0.
 */
static int verify_index_from(const struct index_state *istate, const char *path)
{
	int fd;
	ssize_t n;
	struct stat st;
	unsigned char hash[GIT_MAX_RAWSZ];

	if (!istate->initialized)
		return 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;

	if (fstat(fd, &st))
		goto out;

	if (st.st_size < sizeof(struct cache_header) + the_hash_algo->rawsz)
		goto out;

	n = pread_in_full(fd, hash, the_hash_algo->rawsz, st.st_size - the_hash_algo->rawsz);
	if (n != the_hash_algo->rawsz)
		goto out;

	if (!hasheq(istate->oid.hash, hash))
		goto out;

	close(fd);
	return 1;

out:
	close(fd);
	return 0;
}

static int repo_verify_index(struct repository *repo)
{
	return verify_index_from(repo->index, repo->index_file);
}

int has_racy_timestamp(struct index_state *istate)
{
	int entries = istate->cache_nr;
	int i;

	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = istate->cache[i];
		if (is_racy_timestamp(istate, ce))
			return 1;
	}
	return 0;
}

void repo_update_index_if_able(struct repository *repo,
			       struct lock_file *lockfile)
{
	if ((repo->index->cache_changed ||
	     has_racy_timestamp(repo->index)) &&
	    repo_verify_index(repo))
		write_locked_index(repo->index, lockfile, COMMIT_LOCK);
	else
		rollback_lock_file(lockfile);
}

static int record_eoie(void)
{
	int val;

	if (!git_config_get_bool("index.recordendofindexentries", &val))
		return val;

	/*
	 * As a convenience, the end of index entries extension
	 * used for threading is written by default if the user
	 * explicitly requested threaded index reads.
	 */
	return !git_config_get_index_threads(&val) && val != 1;
}

static int record_ieot(void)
{
	int val;

	if (!git_config_get_bool("index.recordoffsettable", &val))
		return val;

	/*
	 * As a convenience, the offset table used for threading is
	 * written by default if the user explicitly requested
	 * threaded index reads.
	 */
	return !git_config_get_index_threads(&val) && val != 1;
}

enum write_extensions {
	WRITE_NO_EXTENSION =              0,
	WRITE_SPLIT_INDEX_EXTENSION =     1<<0,
	WRITE_CACHE_TREE_EXTENSION =      1<<1,
	WRITE_RESOLVE_UNDO_EXTENSION =    1<<2,
	WRITE_UNTRACKED_CACHE_EXTENSION = 1<<3,
	WRITE_FSMONITOR_EXTENSION =       1<<4,
};
#define WRITE_ALL_EXTENSIONS ((enum write_extensions)-1)

/*
 * On success, `tempfile` is closed. If it is the temporary file
 * of a `struct lock_file`, we will therefore effectively perform
 * a 'close_lock_file_gently()`. Since that is an implementation
 * detail of lockfiles, callers of `do_write_index()` should not
 * rely on it.
 */
static int do_write_index(struct index_state *istate, struct tempfile *tempfile,
			  enum write_extensions write_extensions, unsigned flags)
{
	uint64_t start = getnanotime();
	struct hashfile *f;
	git_hash_ctx *eoie_c = NULL;
	struct cache_header hdr;
	int i, err = 0, removed, extended, hdr_version;
	struct cache_entry **cache = istate->cache;
	int entries = istate->cache_nr;
	struct stat st;
	struct ondisk_cache_entry ondisk;
	struct strbuf previous_name_buf = STRBUF_INIT, *previous_name;
	int drop_cache_tree = istate->drop_cache_tree;
	off_t offset;
	int csum_fsync_flag;
	int ieot_entries = 1;
	struct index_entry_offset_table *ieot = NULL;
	int nr, nr_threads;
	struct repository *r = istate->repo;

	f = hashfd(tempfile->fd, tempfile->filename.buf);

	prepare_repo_settings(r);
	f->skip_hash = r->settings.index_skip_hash;

	for (i = removed = extended = 0; i < entries; i++) {
		if (cache[i]->ce_flags & CE_REMOVE)
			removed++;

		/* reduce extended entries if possible */
		cache[i]->ce_flags &= ~CE_EXTENDED;
		if (cache[i]->ce_flags & CE_EXTENDED_FLAGS) {
			extended++;
			cache[i]->ce_flags |= CE_EXTENDED;
		}
	}

	if (!istate->version)
		istate->version = get_index_format_default(r);

	/* demote version 3 to version 2 when the latter suffices */
	if (istate->version == 3 || istate->version == 2)
		istate->version = extended ? 3 : 2;

	hdr_version = istate->version;

	hdr.hdr_signature = htonl(CACHE_SIGNATURE);
	hdr.hdr_version = htonl(hdr_version);
	hdr.hdr_entries = htonl(entries - removed);

	hashwrite(f, &hdr, sizeof(hdr));

	if (!HAVE_THREADS || git_config_get_index_threads(&nr_threads))
		nr_threads = 1;

	if (nr_threads != 1 && record_ieot()) {
		int ieot_blocks, cpus;

		/*
		 * ensure default number of ieot blocks maps evenly to the
		 * default number of threads that will process them leaving
		 * room for the thread to load the index extensions.
		 */
		if (!nr_threads) {
			ieot_blocks = istate->cache_nr / THREAD_COST;
			cpus = online_cpus();
			if (ieot_blocks > cpus - 1)
				ieot_blocks = cpus - 1;
		} else {
			ieot_blocks = nr_threads;
			if (ieot_blocks > istate->cache_nr)
				ieot_blocks = istate->cache_nr;
		}

		/*
		 * no reason to write out the IEOT extension if we don't
		 * have enough blocks to utilize multi-threading
		 */
		if (ieot_blocks > 1) {
			ieot = xcalloc(1, sizeof(struct index_entry_offset_table)
				+ (ieot_blocks * sizeof(struct index_entry_offset)));
			ieot_entries = DIV_ROUND_UP(entries, ieot_blocks);
		}
	}

	offset = hashfile_total(f);

	nr = 0;
	previous_name = (hdr_version == 4) ? &previous_name_buf : NULL;

	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		if (ce->ce_flags & CE_REMOVE)
			continue;
		if (!ce_uptodate(ce) && is_racy_timestamp(istate, ce))
			ce_smudge_racily_clean_entry(istate, ce);
		if (is_null_oid(&ce->oid)) {
			static const char msg[] = "cache entry has null sha1: %s";
			static int allow = -1;

			if (allow < 0)
				allow = git_env_bool("GIT_ALLOW_NULL_SHA1", 0);
			if (allow)
				warning(msg, ce->name);
			else
				err = error(msg, ce->name);

			drop_cache_tree = 1;
		}
		if (ieot && i && (i % ieot_entries == 0)) {
			ieot->entries[ieot->nr].nr = nr;
			ieot->entries[ieot->nr].offset = offset;
			ieot->nr++;
			/*
			 * If we have a V4 index, set the first byte to an invalid
			 * character to ensure there is nothing common with the previous
			 * entry
			 */
			if (previous_name)
				previous_name->buf[0] = 0;
			nr = 0;

			offset = hashfile_total(f);
		}
		if (ce_write_entry(f, ce, previous_name, (struct ondisk_cache_entry *)&ondisk) < 0)
			err = -1;

		if (err)
			break;
		nr++;
	}
	if (ieot && nr) {
		ieot->entries[ieot->nr].nr = nr;
		ieot->entries[ieot->nr].offset = offset;
		ieot->nr++;
	}
	strbuf_release(&previous_name_buf);

	if (err) {
		free(ieot);
		return err;
	}

	offset = hashfile_total(f);

	/*
	 * The extension headers must be hashed on their own for the
	 * EOIE extension. Create a hashfile here to compute that hash.
	 */
	if (offset && record_eoie()) {
		CALLOC_ARRAY(eoie_c, 1);
		the_hash_algo->init_fn(eoie_c);
	}

	/*
	 * Lets write out CACHE_EXT_INDEXENTRYOFFSETTABLE first so that we
	 * can minimize the number of extensions we have to scan through to
	 * find it during load.  Write it out regardless of the
	 * strip_extensions parameter as we need it when loading the shared
	 * index.
	 */
	if (ieot) {
		struct strbuf sb = STRBUF_INIT;

		write_ieot_extension(&sb, ieot);
		err = write_index_ext_header(f, eoie_c, CACHE_EXT_INDEXENTRYOFFSETTABLE, sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		free(ieot);
		if (err)
			return -1;
	}

	if (write_extensions & WRITE_SPLIT_INDEX_EXTENSION &&
	    istate->split_index) {
		struct strbuf sb = STRBUF_INIT;

		if (istate->sparse_index)
			die(_("cannot write split index for a sparse index"));

		err = write_link_extension(&sb, istate) < 0 ||
			write_index_ext_header(f, eoie_c, CACHE_EXT_LINK,
					       sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}
	if (write_extensions & WRITE_CACHE_TREE_EXTENSION &&
	    !drop_cache_tree && istate->cache_tree) {
		struct strbuf sb = STRBUF_INIT;

		cache_tree_write(&sb, istate->cache_tree);
		err = write_index_ext_header(f, eoie_c, CACHE_EXT_TREE, sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}
	if (write_extensions & WRITE_RESOLVE_UNDO_EXTENSION &&
	    istate->resolve_undo) {
		struct strbuf sb = STRBUF_INIT;

		resolve_undo_write(&sb, istate->resolve_undo);
		err = write_index_ext_header(f, eoie_c, CACHE_EXT_RESOLVE_UNDO,
					     sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}
	if (write_extensions & WRITE_UNTRACKED_CACHE_EXTENSION &&
	    istate->untracked) {
		struct strbuf sb = STRBUF_INIT;

		write_untracked_extension(&sb, istate->untracked);
		err = write_index_ext_header(f, eoie_c, CACHE_EXT_UNTRACKED,
					     sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}
	if (write_extensions & WRITE_FSMONITOR_EXTENSION &&
	    istate->fsmonitor_last_update) {
		struct strbuf sb = STRBUF_INIT;

		write_fsmonitor_extension(&sb, istate);
		err = write_index_ext_header(f, eoie_c, CACHE_EXT_FSMONITOR, sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}
	if (istate->sparse_index) {
		if (write_index_ext_header(f, eoie_c, CACHE_EXT_SPARSE_DIRECTORIES, 0) < 0)
			return -1;
	}

	/*
	 * CACHE_EXT_ENDOFINDEXENTRIES must be written as the last entry before the SHA1
	 * so that it can be found and processed before all the index entries are
	 * read.  Write it out regardless of the strip_extensions parameter as we need it
	 * when loading the shared index.
	 */
	if (eoie_c) {
		struct strbuf sb = STRBUF_INIT;

		write_eoie_extension(&sb, eoie_c, offset);
		err = write_index_ext_header(f, NULL, CACHE_EXT_ENDOFINDEXENTRIES, sb.len) < 0;
		hashwrite(f, sb.buf, sb.len);
		strbuf_release(&sb);
		if (err)
			return -1;
	}

	csum_fsync_flag = 0;
	if (!alternate_index_output && (flags & COMMIT_LOCK))
		csum_fsync_flag = CSUM_FSYNC;

	finalize_hashfile(f, istate->oid.hash, FSYNC_COMPONENT_INDEX,
			  CSUM_HASH_IN_STREAM | csum_fsync_flag);

	if (close_tempfile_gently(tempfile)) {
		error(_("could not close '%s'"), get_tempfile_path(tempfile));
		return -1;
	}
	if (stat(get_tempfile_path(tempfile), &st))
		return -1;
	istate->timestamp.sec = (unsigned int)st.st_mtime;
	istate->timestamp.nsec = ST_MTIME_NSEC(st);
	trace_performance_since(start, "write index, changed mask = %x", istate->cache_changed);

	/*
	 * TODO trace2: replace "the_repository" with the actual repo instance
	 * that is associated with the given "istate".
	 */
	trace2_data_intmax("index", the_repository, "write/version",
			   istate->version);
	trace2_data_intmax("index", the_repository, "write/cache_nr",
			   istate->cache_nr);

	return 0;
}

void set_alternate_index_output(const char *name)
{
	alternate_index_output = name;
}

static int commit_locked_index(struct lock_file *lk)
{
	if (alternate_index_output)
		return commit_lock_file_to(lk, alternate_index_output);
	else
		return commit_lock_file(lk);
}

static int do_write_locked_index(struct index_state *istate,
				 struct lock_file *lock,
				 unsigned flags,
				 enum write_extensions write_extensions)
{
	int ret;
	int was_full = istate->sparse_index == INDEX_EXPANDED;

	ret = convert_to_sparse(istate, 0);

	if (ret) {
		warning(_("failed to convert to a sparse-index"));
		return ret;
	}

	/*
	 * TODO trace2: replace "the_repository" with the actual repo instance
	 * that is associated with the given "istate".
	 */
	trace2_region_enter_printf("index", "do_write_index", the_repository,
				   "%s", get_lock_file_path(lock));
	ret = do_write_index(istate, lock->tempfile, write_extensions, flags);
	trace2_region_leave_printf("index", "do_write_index", the_repository,
				   "%s", get_lock_file_path(lock));

	if (was_full)
		ensure_full_index(istate);

	if (ret)
		return ret;
	if (flags & COMMIT_LOCK)
		ret = commit_locked_index(lock);
	else
		ret = close_lock_file_gently(lock);

	run_hooks_l("post-index-change",
			istate->updated_workdir ? "1" : "0",
			istate->updated_skipworktree ? "1" : "0", NULL);
	istate->updated_workdir = 0;
	istate->updated_skipworktree = 0;

	return ret;
}

static int write_split_index(struct index_state *istate,
			     struct lock_file *lock,
			     unsigned flags)
{
	int ret;
	prepare_to_write_split_index(istate);
	ret = do_write_locked_index(istate, lock, flags, WRITE_ALL_EXTENSIONS);
	finish_writing_split_index(istate);
	return ret;
}

static const char *shared_index_expire = "2.weeks.ago";

static unsigned long get_shared_index_expire_date(void)
{
	static unsigned long shared_index_expire_date;
	static int shared_index_expire_date_prepared;

	if (!shared_index_expire_date_prepared) {
		git_config_get_expiry("splitindex.sharedindexexpire",
				      &shared_index_expire);
		shared_index_expire_date = approxidate(shared_index_expire);
		shared_index_expire_date_prepared = 1;
	}

	return shared_index_expire_date;
}

static int should_delete_shared_index(const char *shared_index_path)
{
	struct stat st;
	unsigned long expiration;

	/* Check timestamp */
	expiration = get_shared_index_expire_date();
	if (!expiration)
		return 0;
	if (stat(shared_index_path, &st))
		return error_errno(_("could not stat '%s'"), shared_index_path);
	if (st.st_mtime > expiration)
		return 0;

	return 1;
}

static int clean_shared_index_files(const char *current_hex)
{
	struct dirent *de;
	DIR *dir = opendir(get_git_dir());

	if (!dir)
		return error_errno(_("unable to open git dir: %s"), get_git_dir());

	while ((de = readdir(dir)) != NULL) {
		const char *sha1_hex;
		const char *shared_index_path;
		if (!skip_prefix(de->d_name, "sharedindex.", &sha1_hex))
			continue;
		if (!strcmp(sha1_hex, current_hex))
			continue;
		shared_index_path = git_path("%s", de->d_name);
		if (should_delete_shared_index(shared_index_path) > 0 &&
		    unlink(shared_index_path))
			warning_errno(_("unable to unlink: %s"), shared_index_path);
	}
	closedir(dir);

	return 0;
}

static int write_shared_index(struct index_state *istate,
			      struct tempfile **temp, unsigned flags)
{
	struct split_index *si = istate->split_index;
	int ret, was_full = !istate->sparse_index;

	move_cache_to_base_index(istate);
	convert_to_sparse(istate, 0);

	trace2_region_enter_printf("index", "shared/do_write_index",
				   the_repository, "%s", get_tempfile_path(*temp));
	ret = do_write_index(si->base, *temp, WRITE_NO_EXTENSION, flags);
	trace2_region_leave_printf("index", "shared/do_write_index",
				   the_repository, "%s", get_tempfile_path(*temp));

	if (was_full)
		ensure_full_index(istate);

	if (ret)
		return ret;
	ret = adjust_shared_perm(get_tempfile_path(*temp));
	if (ret) {
		error(_("cannot fix permission bits on '%s'"), get_tempfile_path(*temp));
		return ret;
	}
	ret = rename_tempfile(temp,
			      git_path("sharedindex.%s", oid_to_hex(&si->base->oid)));
	if (!ret) {
		oidcpy(&si->base_oid, &si->base->oid);
		clean_shared_index_files(oid_to_hex(&si->base->oid));
	}

	return ret;
}

static const int default_max_percent_split_change = 20;

static int too_many_not_shared_entries(struct index_state *istate)
{
	int i, not_shared = 0;
	int max_split = git_config_get_max_percent_split_change();

	switch (max_split) {
	case -1:
		/* not or badly configured: use the default value */
		max_split = default_max_percent_split_change;
		break;
	case 0:
		return 1; /* 0% means always write a new shared index */
	case 100:
		return 0; /* 100% means never write a new shared index */
	default:
		break; /* just use the configured value */
	}

	/* Count not shared entries */
	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		if (!ce->index)
			not_shared++;
	}

	return (int64_t)istate->cache_nr * max_split < (int64_t)not_shared * 100;
}

int write_locked_index(struct index_state *istate, struct lock_file *lock,
		       unsigned flags)
{
	int new_shared_index, ret, test_split_index_env;
	struct split_index *si = istate->split_index;

	if (git_env_bool("GIT_TEST_CHECK_CACHE_TREE", 0))
		cache_tree_verify(the_repository, istate);

	if ((flags & SKIP_IF_UNCHANGED) && !istate->cache_changed) {
		if (flags & COMMIT_LOCK)
			rollback_lock_file(lock);
		return 0;
	}

	if (istate->fsmonitor_last_update)
		fill_fsmonitor_bitmap(istate);

	test_split_index_env = git_env_bool("GIT_TEST_SPLIT_INDEX", 0);

	if ((!si && !test_split_index_env) ||
	    alternate_index_output ||
	    (istate->cache_changed & ~EXTMASK)) {
		ret = do_write_locked_index(istate, lock, flags,
					    ~WRITE_SPLIT_INDEX_EXTENSION);
		goto out;
	}

	if (test_split_index_env) {
		if (!si) {
			si = init_split_index(istate);
			istate->cache_changed |= SPLIT_INDEX_ORDERED;
		} else {
			int v = si->base_oid.hash[0];
			if ((v & 15) < 6)
				istate->cache_changed |= SPLIT_INDEX_ORDERED;
		}
	}
	if (too_many_not_shared_entries(istate))
		istate->cache_changed |= SPLIT_INDEX_ORDERED;

	new_shared_index = istate->cache_changed & SPLIT_INDEX_ORDERED;

	if (new_shared_index) {
		struct tempfile *temp;
		int saved_errno;

		/* Same initial permissions as the main .git/index file */
		temp = mks_tempfile_sm(git_path("sharedindex_XXXXXX"), 0, 0666);
		if (!temp) {
			ret = do_write_locked_index(istate, lock, flags,
						    ~WRITE_SPLIT_INDEX_EXTENSION);
			goto out;
		}
		ret = write_shared_index(istate, &temp, flags);

		saved_errno = errno;
		if (is_tempfile_active(temp))
			delete_tempfile(&temp);
		errno = saved_errno;

		if (ret)
			goto out;
	}

	ret = write_split_index(istate, lock, flags);

	/* Freshen the shared index only if the split-index was written */
	if (!ret && !new_shared_index && !is_null_oid(&si->base_oid)) {
		const char *shared_index = git_path("sharedindex.%s",
						    oid_to_hex(&si->base_oid));
		freshen_shared_index(shared_index, 1);
	}

out:
	if (flags & COMMIT_LOCK)
		rollback_lock_file(lock);
	return ret;
}

/*
 * Read the index file that is potentially unmerged into given
 * index_state, dropping any unmerged entries to stage #0 (potentially
 * resulting in a path appearing as both a file and a directory in the
 * index; the caller is responsible to clear out the extra entries
 * before writing the index to a tree).  Returns true if the index is
 * unmerged.  Callers who want to refuse to work from an unmerged
 * state can call this and check its return value, instead of calling
 * read_cache().
 */
int repo_read_index_unmerged(struct repository *repo)
{
	struct index_state *istate;
	int i;
	int unmerged = 0;

	repo_read_index(repo);
	istate = repo->index;
	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		struct cache_entry *new_ce;
		int len;

		if (!ce_stage(ce))
			continue;
		unmerged = 1;
		len = ce_namelen(ce);
		new_ce = make_empty_cache_entry(istate, len);
		memcpy(new_ce->name, ce->name, len);
		new_ce->ce_flags = create_ce_flags(0) | CE_CONFLICTED;
		new_ce->ce_namelen = len;
		new_ce->ce_mode = ce->ce_mode;
		if (add_index_entry(istate, new_ce, ADD_CACHE_SKIP_DFCHECK))
			return error(_("%s: cannot drop to stage #0"),
				     new_ce->name);
	}
	return unmerged;
}

/*
 * Returns 1 if the path is an "other" path with respect to
 * the index; that is, the path is not mentioned in the index at all,
 * either as a file, a directory with some files in the index,
 * or as an unmerged entry.
 *
 * We helpfully remove a trailing "/" from directories so that
 * the output of read_directory can be used as-is.
 */
int index_name_is_other(struct index_state *istate, const char *name,
			int namelen)
{
	int pos;
	if (namelen && name[namelen - 1] == '/')
		namelen--;
	pos = index_name_pos(istate, name, namelen);
	if (0 <= pos)
		return 0;	/* exact match */
	pos = -pos - 1;
	if (pos < istate->cache_nr) {
		struct cache_entry *ce = istate->cache[pos];
		if (ce_namelen(ce) == namelen &&
		    !memcmp(ce->name, name, namelen))
			return 0; /* Yup, this one exists unmerged */
	}
	return 1;
}

void *read_blob_data_from_index(struct index_state *istate,
				const char *path, unsigned long *size)
{
	int pos, len;
	unsigned long sz;
	enum object_type type;
	void *data;

	len = strlen(path);
	pos = index_name_pos(istate, path, len);
	if (pos < 0) {
		/*
		 * We might be in the middle of a merge, in which
		 * case we would read stage #2 (ours).
		 */
		int i;
		for (i = -pos - 1;
		     (pos < 0 && i < istate->cache_nr &&
		      !strcmp(istate->cache[i]->name, path));
		     i++)
			if (ce_stage(istate->cache[i]) == 2)
				pos = i;
	}
	if (pos < 0)
		return NULL;
	data = repo_read_object_file(the_repository, &istate->cache[pos]->oid,
				     &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return NULL;
	}
	if (size)
		*size = sz;
	return data;
}

void move_index_extensions(struct index_state *dst, struct index_state *src)
{
	dst->untracked = src->untracked;
	src->untracked = NULL;
	dst->cache_tree = src->cache_tree;
	src->cache_tree = NULL;
}

struct cache_entry *dup_cache_entry(const struct cache_entry *ce,
				    struct index_state *istate)
{
	unsigned int size = ce_size(ce);
	int mem_pool_allocated;
	struct cache_entry *new_entry = make_empty_cache_entry(istate, ce_namelen(ce));
	mem_pool_allocated = new_entry->mem_pool_allocated;

	memcpy(new_entry, ce, size);
	new_entry->mem_pool_allocated = mem_pool_allocated;
	return new_entry;
}

void discard_cache_entry(struct cache_entry *ce)
{
	if (ce && should_validate_cache_entries())
		memset(ce, 0xCD, cache_entry_size(ce->ce_namelen));

	if (ce && ce->mem_pool_allocated)
		return;

	free(ce);
}

int should_validate_cache_entries(void)
{
	static int validate_index_cache_entries = -1;

	if (validate_index_cache_entries < 0) {
		if (getenv("GIT_TEST_VALIDATE_INDEX_CACHE_ENTRIES"))
			validate_index_cache_entries = 1;
		else
			validate_index_cache_entries = 0;
	}

	return validate_index_cache_entries;
}

#define EOIE_SIZE (4 + GIT_SHA1_RAWSZ) /* <4-byte offset> + <20-byte hash> */
#define EOIE_SIZE_WITH_HEADER (4 + 4 + EOIE_SIZE) /* <4-byte signature> + <4-byte length> + EOIE_SIZE */

static size_t read_eoie_extension(const char *mmap, size_t mmap_size)
{
	/*
	 * The end of index entries (EOIE) extension is guaranteed to be last
	 * so that it can be found by scanning backwards from the EOF.
	 *
	 * "EOIE"
	 * <4-byte length>
	 * <4-byte offset>
	 * <20-byte hash>
	 */
	const char *index, *eoie;
	uint32_t extsize;
	size_t offset, src_offset;
	unsigned char hash[GIT_MAX_RAWSZ];
	git_hash_ctx c;

	/* ensure we have an index big enough to contain an EOIE extension */
	if (mmap_size < sizeof(struct cache_header) + EOIE_SIZE_WITH_HEADER + the_hash_algo->rawsz)
		return 0;

	/* validate the extension signature */
	index = eoie = mmap + mmap_size - EOIE_SIZE_WITH_HEADER - the_hash_algo->rawsz;
	if (CACHE_EXT(index) != CACHE_EXT_ENDOFINDEXENTRIES)
		return 0;
	index += sizeof(uint32_t);

	/* validate the extension size */
	extsize = get_be32(index);
	if (extsize != EOIE_SIZE)
		return 0;
	index += sizeof(uint32_t);

	/*
	 * Validate the offset we're going to look for the first extension
	 * signature is after the index header and before the eoie extension.
	 */
	offset = get_be32(index);
	if (mmap + offset < mmap + sizeof(struct cache_header))
		return 0;
	if (mmap + offset >= eoie)
		return 0;
	index += sizeof(uint32_t);

	/*
	 * The hash is computed over extension types and their sizes (but not
	 * their contents).  E.g. if we have "TREE" extension that is N-bytes
	 * long, "REUC" extension that is M-bytes long, followed by "EOIE",
	 * then the hash would be:
	 *
	 * SHA-1("TREE" + <binary representation of N> +
	 *	 "REUC" + <binary representation of M>)
	 */
	src_offset = offset;
	the_hash_algo->init_fn(&c);
	while (src_offset < mmap_size - the_hash_algo->rawsz - EOIE_SIZE_WITH_HEADER) {
		/* After an array of active_nr index entries,
		 * there can be arbitrary number of extended
		 * sections, each of which is prefixed with
		 * extension name (4-byte) and section length
		 * in 4-byte network byte order.
		 */
		uint32_t extsize;
		memcpy(&extsize, mmap + src_offset + 4, 4);
		extsize = ntohl(extsize);

		/* verify the extension size isn't so large it will wrap around */
		if (src_offset + 8 + extsize < src_offset)
			return 0;

		the_hash_algo->update_fn(&c, mmap + src_offset, 8);

		src_offset += 8;
		src_offset += extsize;
	}
	the_hash_algo->final_fn(hash, &c);
	if (!hasheq(hash, (const unsigned char *)index))
		return 0;

	/* Validate that the extension offsets returned us back to the eoie extension. */
	if (src_offset != mmap_size - the_hash_algo->rawsz - EOIE_SIZE_WITH_HEADER)
		return 0;

	return offset;
}

static void write_eoie_extension(struct strbuf *sb, git_hash_ctx *eoie_context, size_t offset)
{
	uint32_t buffer;
	unsigned char hash[GIT_MAX_RAWSZ];

	/* offset */
	put_be32(&buffer, offset);
	strbuf_add(sb, &buffer, sizeof(uint32_t));

	/* hash */
	the_hash_algo->final_fn(hash, eoie_context);
	strbuf_add(sb, hash, the_hash_algo->rawsz);
}

#define IEOT_VERSION	(1)

static struct index_entry_offset_table *read_ieot_extension(const char *mmap, size_t mmap_size, size_t offset)
{
	const char *index = NULL;
	uint32_t extsize, ext_version;
	struct index_entry_offset_table *ieot;
	int i, nr;

	/* find the IEOT extension */
	if (!offset)
		return NULL;
	while (offset <= mmap_size - the_hash_algo->rawsz - 8) {
		extsize = get_be32(mmap + offset + 4);
		if (CACHE_EXT((mmap + offset)) == CACHE_EXT_INDEXENTRYOFFSETTABLE) {
			index = mmap + offset + 4 + 4;
			break;
		}
		offset += 8;
		offset += extsize;
	}
	if (!index)
		return NULL;

	/* validate the version is IEOT_VERSION */
	ext_version = get_be32(index);
	if (ext_version != IEOT_VERSION) {
		error("invalid IEOT version %d", ext_version);
		return NULL;
	}
	index += sizeof(uint32_t);

	/* extension size - version bytes / bytes per entry */
	nr = (extsize - sizeof(uint32_t)) / (sizeof(uint32_t) + sizeof(uint32_t));
	if (!nr) {
		error("invalid number of IEOT entries %d", nr);
		return NULL;
	}
	ieot = xmalloc(sizeof(struct index_entry_offset_table)
		       + (nr * sizeof(struct index_entry_offset)));
	ieot->nr = nr;
	for (i = 0; i < nr; i++) {
		ieot->entries[i].offset = get_be32(index);
		index += sizeof(uint32_t);
		ieot->entries[i].nr = get_be32(index);
		index += sizeof(uint32_t);
	}

	return ieot;
}

static void write_ieot_extension(struct strbuf *sb, struct index_entry_offset_table *ieot)
{
	uint32_t buffer;
	int i;

	/* version */
	put_be32(&buffer, IEOT_VERSION);
	strbuf_add(sb, &buffer, sizeof(uint32_t));

	/* ieot */
	for (i = 0; i < ieot->nr; i++) {

		/* offset */
		put_be32(&buffer, ieot->entries[i].offset);
		strbuf_add(sb, &buffer, sizeof(uint32_t));

		/* count */
		put_be32(&buffer, ieot->entries[i].nr);
		strbuf_add(sb, &buffer, sizeof(uint32_t));
	}
}

void prefetch_cache_entries(const struct index_state *istate,
			    must_prefetch_predicate must_prefetch)
{
	int i;
	struct oid_array to_fetch = OID_ARRAY_INIT;

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];

		if (S_ISGITLINK(ce->ce_mode) || !must_prefetch(ce))
			continue;
		if (!oid_object_info_extended(the_repository, &ce->oid,
					      NULL,
					      OBJECT_INFO_FOR_PREFETCH))
			continue;
		oid_array_append(&to_fetch, &ce->oid);
	}
	promisor_remote_get_direct(the_repository,
				   to_fetch.oid, to_fetch.nr);
	oid_array_clear(&to_fetch);
}

static int read_one_entry_opt(struct index_state *istate,
			      const struct object_id *oid,
			      struct strbuf *base,
			      const char *pathname,
			      unsigned mode, int opt)
{
	int len;
	struct cache_entry *ce;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	len = strlen(pathname);
	ce = make_empty_cache_entry(istate, base->len + len);

	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = create_ce_flags(1);
	ce->ce_namelen = base->len + len;
	memcpy(ce->name, base->buf, base->len);
	memcpy(ce->name + base->len, pathname, len+1);
	oidcpy(&ce->oid, oid);
	return add_index_entry(istate, ce, opt);
}

static int read_one_entry(const struct object_id *oid, struct strbuf *base,
			  const char *pathname, unsigned mode,
			  void *context)
{
	struct index_state *istate = context;
	return read_one_entry_opt(istate, oid, base, pathname,
				  mode,
				  ADD_CACHE_OK_TO_ADD|ADD_CACHE_SKIP_DFCHECK);
}

/*
 * This is used when the caller knows there is no existing entries at
 * the stage that will conflict with the entry being added.
 */
static int read_one_entry_quick(const struct object_id *oid, struct strbuf *base,
				const char *pathname, unsigned mode,
				void *context)
{
	struct index_state *istate = context;
	return read_one_entry_opt(istate, oid, base, pathname,
				  mode, ADD_CACHE_JUST_APPEND);
}

/*
 * Read the tree specified with --with-tree option
 * (typically, HEAD) into stage #1 and then
 * squash them down to stage #0.  This is used for
 * --error-unmatch to list and check the path patterns
 * that were given from the command line.  We are not
 * going to write this index out.
 */
void overlay_tree_on_index(struct index_state *istate,
			   const char *tree_name, const char *prefix)
{
	struct tree *tree;
	struct object_id oid;
	struct pathspec pathspec;
	struct cache_entry *last_stage0 = NULL;
	int i;
	read_tree_fn_t fn = NULL;
	int err;

	if (repo_get_oid(the_repository, tree_name, &oid))
		die("tree-ish %s not found.", tree_name);
	tree = parse_tree_indirect(&oid);
	if (!tree)
		die("bad tree-ish %s", tree_name);

	/* Hoist the unmerged entries up to stage #3 to make room */
	/* TODO: audit for interaction with sparse-index. */
	ensure_full_index(istate);
	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		if (!ce_stage(ce))
			continue;
		ce->ce_flags |= CE_STAGEMASK;
	}

	if (prefix) {
		static const char *(matchbuf[1]);
		matchbuf[0] = NULL;
		parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC,
			       PATHSPEC_PREFER_CWD, prefix, matchbuf);
	} else
		memset(&pathspec, 0, sizeof(pathspec));

	/*
	 * See if we have cache entry at the stage.  If so,
	 * do it the original slow way, otherwise, append and then
	 * sort at the end.
	 */
	for (i = 0; !fn && i < istate->cache_nr; i++) {
		const struct cache_entry *ce = istate->cache[i];
		if (ce_stage(ce) == 1)
			fn = read_one_entry;
	}

	if (!fn)
		fn = read_one_entry_quick;
	err = read_tree(the_repository, tree, &pathspec, fn, istate);
	clear_pathspec(&pathspec);
	if (err)
		die("unable to read tree entries %s", tree_name);

	/*
	 * Sort the cache entry -- we need to nuke the cache tree, though.
	 */
	if (fn == read_one_entry_quick) {
		cache_tree_free(&istate->cache_tree);
		QSORT(istate->cache, istate->cache_nr, cmp_cache_name_compare);
	}

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		switch (ce_stage(ce)) {
		case 0:
			last_stage0 = ce;
			/* fallthru */
		default:
			continue;
		case 1:
			/*
			 * If there is stage #0 entry for this, we do not
			 * need to show it.  We use CE_UPDATE bit to mark
			 * such an entry.
			 */
			if (last_stage0 &&
			    !strcmp(last_stage0->name, ce->name))
				ce->ce_flags |= CE_UPDATE;
		}
	}
}

struct update_callback_data {
	struct index_state *index;
	int include_sparse;
	int flags;
	int add_errors;
};

static int fix_unmerged_status(struct diff_filepair *p,
			       struct update_callback_data *data)
{
	if (p->status != DIFF_STATUS_UNMERGED)
		return p->status;
	if (!(data->flags & ADD_CACHE_IGNORE_REMOVAL) && !p->two->mode)
		/*
		 * This is not an explicit add request, and the
		 * path is missing from the working tree (deleted)
		 */
		return DIFF_STATUS_DELETED;
	else
		/*
		 * Either an explicit add request, or path exists
		 * in the working tree.  An attempt to explicitly
		 * add a path that does not exist in the working tree
		 * will be caught as an error by the caller immediately.
		 */
		return DIFF_STATUS_MODIFIED;
}

static void update_callback(struct diff_queue_struct *q,
			    struct diff_options *opt UNUSED, void *cbdata)
{
	int i;
	struct update_callback_data *data = cbdata;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *path = p->one->path;

		if (!data->include_sparse &&
		    !path_in_sparse_checkout(path, data->index))
			continue;

		switch (fix_unmerged_status(p, data)) {
		default:
			die(_("unexpected diff status %c"), p->status);
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			if (add_file_to_index(data->index, path, data->flags)) {
				if (!(data->flags & ADD_CACHE_IGNORE_ERRORS))
					die(_("updating files failed"));
				data->add_errors++;
			}
			break;
		case DIFF_STATUS_DELETED:
			if (data->flags & ADD_CACHE_IGNORE_REMOVAL)
				break;
			if (!(data->flags & ADD_CACHE_PRETEND))
				remove_file_from_index(data->index, path);
			if (data->flags & (ADD_CACHE_PRETEND|ADD_CACHE_VERBOSE))
				printf(_("remove '%s'\n"), path);
			break;
		}
	}
}

int add_files_to_cache(struct repository *repo, const char *prefix,
		       const struct pathspec *pathspec, char *ps_matched,
		       int include_sparse, int flags)
{
	struct update_callback_data data;
	struct rev_info rev;

	memset(&data, 0, sizeof(data));
	data.index = repo->index;
	data.include_sparse = include_sparse;
	data.flags = flags;

	repo_init_revisions(repo, &rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);
	if (pathspec) {
		copy_pathspec(&rev.prune_data, pathspec);
		rev.ps_matched = ps_matched;
	}
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = update_callback;
	rev.diffopt.format_callback_data = &data;
	rev.diffopt.flags.override_submodule_config = 1;
	rev.max_count = 0; /* do not compare unmerged paths with stage #2 */

	/*
	 * Use an ODB transaction to optimize adding multiple objects.
	 * This function is invoked from commands other than 'add', which
	 * may not have their own transaction active.
	 */
	begin_odb_transaction();
	run_diff_files(&rev, DIFF_RACY_IS_MODIFIED);
	end_odb_transaction();

	release_revisions(&rev);
	return !!data.add_errors;
}
