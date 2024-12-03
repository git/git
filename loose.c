#include "git-compat-util.h"
#include "hash.h"
#include "path.h"
#include "object-store.h"
#include "hex.h"
#include "repository.h"
#include "wrapper.h"
#include "gettext.h"
#include "loose.h"
#include "lockfile.h"
#include "oidtree.h"

static const char *loose_object_header = "# loose-object-idx\n";

static inline int should_use_loose_object_map(struct repository *repo)
{
	return repo->compat_hash_algo && repo->gitdir;
}

void loose_object_map_init(struct loose_object_map **map)
{
	struct loose_object_map *m;
	m = xmalloc(sizeof(**map));
	m->to_compat = kh_init_oid_map();
	m->to_storage = kh_init_oid_map();
	*map = m;
}

static int insert_oid_pair(kh_oid_map_t *map, const struct object_id *key, const struct object_id *value)
{
	khiter_t pos;
	int ret;
	struct object_id *stored;

	pos = kh_put_oid_map(map, *key, &ret);

	/* This item already exists in the map. */
	if (ret == 0)
		return 0;

	stored = xmalloc(sizeof(*stored));
	oidcpy(stored, value);
	kh_value(map, pos) = stored;
	return 1;
}

static int insert_loose_map(struct object_directory *odb,
			    const struct object_id *oid,
			    const struct object_id *compat_oid)
{
	struct loose_object_map *map = odb->loose_map;
	int inserted = 0;

	inserted |= insert_oid_pair(map->to_compat, oid, compat_oid);
	inserted |= insert_oid_pair(map->to_storage, compat_oid, oid);
	if (inserted)
		oidtree_insert(odb->loose_objects_cache, compat_oid);

	return inserted;
}

static int load_one_loose_object_map(struct repository *repo, struct object_directory *dir)
{
	struct strbuf buf = STRBUF_INIT, path = STRBUF_INIT;
	FILE *fp;

	if (!dir->loose_map)
		loose_object_map_init(&dir->loose_map);
	if (!dir->loose_objects_cache) {
		ALLOC_ARRAY(dir->loose_objects_cache, 1);
		oidtree_init(dir->loose_objects_cache);
	}

	insert_loose_map(dir, repo->hash_algo->empty_tree, repo->compat_hash_algo->empty_tree);
	insert_loose_map(dir, repo->hash_algo->empty_blob, repo->compat_hash_algo->empty_blob);
	insert_loose_map(dir, repo->hash_algo->null_oid, repo->compat_hash_algo->null_oid);

	strbuf_git_common_path(&path, repo, "objects/loose-object-idx");
	fp = fopen(path.buf, "rb");
	if (!fp) {
		strbuf_release(&path);
		return 0;
	}

	errno = 0;
	if (strbuf_getwholeline(&buf, fp, '\n') || strcmp(buf.buf, loose_object_header))
		goto err;
	while (!strbuf_getline_lf(&buf, fp)) {
		const char *p;
		struct object_id oid, compat_oid;
		if (parse_oid_hex_algop(buf.buf, &oid, &p, repo->hash_algo) ||
		    *p++ != ' ' ||
		    parse_oid_hex_algop(p, &compat_oid, &p, repo->compat_hash_algo) ||
		    p != buf.buf + buf.len)
			goto err;
		insert_loose_map(dir, &oid, &compat_oid);
	}

	strbuf_release(&buf);
	strbuf_release(&path);
	return errno ? -1 : 0;
err:
	strbuf_release(&buf);
	strbuf_release(&path);
	return -1;
}

int repo_read_loose_object_map(struct repository *repo)
{
	struct object_directory *dir;

	if (!should_use_loose_object_map(repo))
		return 0;

	prepare_alt_odb(repo);

	for (dir = repo->objects->odb; dir; dir = dir->next) {
		if (load_one_loose_object_map(repo, dir) < 0) {
			return -1;
		}
	}
	return 0;
}

int repo_write_loose_object_map(struct repository *repo)
{
	kh_oid_map_t *map = repo->objects->odb->loose_map->to_compat;
	struct lock_file lock;
	int fd;
	khiter_t iter;
	struct strbuf buf = STRBUF_INIT, path = STRBUF_INIT;

	if (!should_use_loose_object_map(repo))
		return 0;

	strbuf_git_common_path(&path, repo, "objects/loose-object-idx");
	fd = hold_lock_file_for_update_timeout(&lock, path.buf, LOCK_DIE_ON_ERROR, -1);
	iter = kh_begin(map);
	if (write_in_full(fd, loose_object_header, strlen(loose_object_header)) < 0)
		goto errout;

	for (; iter != kh_end(map); iter++) {
		if (kh_exist(map, iter)) {
			if (oideq(&kh_key(map, iter), repo->hash_algo->empty_tree) ||
			    oideq(&kh_key(map, iter), repo->hash_algo->empty_blob))
				continue;
			strbuf_addf(&buf, "%s %s\n", oid_to_hex(&kh_key(map, iter)), oid_to_hex(kh_value(map, iter)));
			if (write_in_full(fd, buf.buf, buf.len) < 0)
				goto errout;
			strbuf_reset(&buf);
		}
	}
	strbuf_release(&buf);
	if (commit_lock_file(&lock) < 0) {
		error_errno(_("could not write loose object index %s"), path.buf);
		strbuf_release(&path);
		return -1;
	}
	strbuf_release(&path);
	return 0;
errout:
	rollback_lock_file(&lock);
	strbuf_release(&buf);
	error_errno(_("failed to write loose object index %s"), path.buf);
	strbuf_release(&path);
	return -1;
}

static int write_one_object(struct repository *repo, const struct object_id *oid,
			    const struct object_id *compat_oid)
{
	struct lock_file lock;
	int fd;
	struct stat st;
	struct strbuf buf = STRBUF_INIT, path = STRBUF_INIT;

	strbuf_git_common_path(&path, repo, "objects/loose-object-idx");
	hold_lock_file_for_update_timeout(&lock, path.buf, LOCK_DIE_ON_ERROR, -1);

	fd = open(path.buf, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0)
		goto errout;
	if (fstat(fd, &st) < 0)
		goto errout;
	if (!st.st_size && write_in_full(fd, loose_object_header, strlen(loose_object_header)) < 0)
		goto errout;

	strbuf_addf(&buf, "%s %s\n", oid_to_hex(oid), oid_to_hex(compat_oid));
	if (write_in_full(fd, buf.buf, buf.len) < 0)
		goto errout;
	if (close(fd))
		goto errout;
	adjust_shared_perm(path.buf);
	rollback_lock_file(&lock);
	strbuf_release(&buf);
	strbuf_release(&path);
	return 0;
errout:
	error_errno(_("failed to write loose object index %s"), path.buf);
	close(fd);
	rollback_lock_file(&lock);
	strbuf_release(&buf);
	strbuf_release(&path);
	return -1;
}

int repo_add_loose_object_map(struct repository *repo, const struct object_id *oid,
			      const struct object_id *compat_oid)
{
	int inserted = 0;

	if (!should_use_loose_object_map(repo))
		return 0;

	inserted = insert_loose_map(repo->objects->odb, oid, compat_oid);
	if (inserted)
		return write_one_object(repo, oid, compat_oid);
	return 0;
}

int repo_loose_object_map_oid(struct repository *repo,
			      const struct object_id *src,
			      const struct git_hash_algo *to,
			      struct object_id *dest)
{
	struct object_directory *dir;
	kh_oid_map_t *map;
	khiter_t pos;

	for (dir = repo->objects->odb; dir; dir = dir->next) {
		struct loose_object_map *loose_map = dir->loose_map;
		if (!loose_map)
			continue;
		map = (to == repo->compat_hash_algo) ?
			loose_map->to_compat :
			loose_map->to_storage;
		pos = kh_get_oid_map(map, *src);
		if (pos < kh_end(map)) {
			oidcpy(dest, kh_value(map, pos));
			return 0;
		}
	}
	return -1;
}

void loose_object_map_clear(struct loose_object_map **map)
{
	struct loose_object_map *m = *map;
	struct object_id *oid;

	if (!m)
		return;

	kh_foreach_value(m->to_compat, oid, free(oid));
	kh_foreach_value(m->to_storage, oid, free(oid));
	kh_destroy_oid_map(m->to_compat);
	kh_destroy_oid_map(m->to_storage);
	free(m);
	*map = NULL;
}
