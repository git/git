#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "abspath.h"
#include "commit-graph.h"
#include "config.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "khash.h"
#include "lockfile.h"
#include "loose.h"
#include "object-file-convert.h"
#include "object-file.h"
#include "object-store.h"
#include "packfile.h"
#include "path.h"
#include "promisor-remote.h"
#include "quote.h"
#include "replace-object.h"
#include "run-command.h"
#include "setup.h"
#include "strbuf.h"
#include "strvec.h"
#include "submodule.h"
#include "write-or-die.h"

KHASH_INIT(odb_path_map, const char * /* key: odb_path */,
	struct object_directory *, 1, fspathhash, fspatheq)

/*
 * This is meant to hold a *small* number of objects that you would
 * want repo_read_object_file() to be able to return, but yet you do not want
 * to write them into the object store (e.g. a browse-only
 * application).
 */
struct cached_object_entry {
	struct object_id oid;
	struct cached_object {
		enum object_type type;
		const void *buf;
		unsigned long size;
	} value;
};

static const struct cached_object *find_cached_object(struct raw_object_store *object_store,
						      const struct object_id *oid)
{
	static const struct cached_object empty_tree = {
		.type = OBJ_TREE,
		.buf = "",
	};
	const struct cached_object_entry *co = object_store->cached_objects;

	for (size_t i = 0; i < object_store->cached_object_nr; i++, co++)
		if (oideq(&co->oid, oid))
			return &co->value;

	if (oid->algo && oideq(oid, hash_algos[oid->algo].empty_tree))
		return &empty_tree;

	return NULL;
}

int odb_mkstemp(struct strbuf *temp_filename, const char *pattern)
{
	int fd;
	/*
	 * we let the umask do its job, don't try to be more
	 * restrictive except to remove write permission.
	 */
	int mode = 0444;
	repo_git_path_replace(the_repository, temp_filename, "objects/%s", pattern);
	fd = git_mkstemp_mode(temp_filename->buf, mode);
	if (0 <= fd)
		return fd;

	/* slow path */
	/* some mkstemp implementations erase temp_filename on failure */
	repo_git_path_replace(the_repository, temp_filename, "objects/%s", pattern);
	safe_create_leading_directories(the_repository, temp_filename->buf);
	return xmkstemp_mode(temp_filename->buf, mode);
}

/*
 * Return non-zero iff the path is usable as an alternate object database.
 */
static int alt_odb_usable(struct raw_object_store *o,
			  struct strbuf *path,
			  const char *normalized_objdir, khiter_t *pos)
{
	int r;

	/* Detect cases where alternate disappeared */
	if (!is_directory(path->buf)) {
		error(_("object directory %s does not exist; "
			"check .git/objects/info/alternates"),
		      path->buf);
		return 0;
	}

	/*
	 * Prevent the common mistake of listing the same
	 * thing twice, or object directory itself.
	 */
	if (!o->odb_by_path) {
		khiter_t p;

		o->odb_by_path = kh_init_odb_path_map();
		assert(!o->odb->next);
		p = kh_put_odb_path_map(o->odb_by_path, o->odb->path, &r);
		assert(r == 1); /* never used */
		kh_value(o->odb_by_path, p) = o->odb;
	}
	if (fspatheq(path->buf, normalized_objdir))
		return 0;
	*pos = kh_put_odb_path_map(o->odb_by_path, path->buf, &r);
	/* r: 0 = exists, 1 = never used, 2 = deleted */
	return r == 0 ? 0 : 1;
}

/*
 * Prepare alternate object database registry.
 *
 * The variable alt_odb_list points at the list of struct
 * object_directory.  The elements on this list come from
 * non-empty elements from colon separated ALTERNATE_DB_ENVIRONMENT
 * environment variable, and $GIT_OBJECT_DIRECTORY/info/alternates,
 * whose contents is similar to that environment variable but can be
 * LF separated.  Its base points at a statically allocated buffer that
 * contains "/the/directory/corresponding/to/.git/objects/...", while
 * its name points just after the slash at the end of ".git/objects/"
 * in the example above, and has enough space to hold all hex characters
 * of the object ID, an extra slash for the first level indirection, and
 * the terminating NUL.
 */
static void read_info_alternates(struct repository *r,
				 const char *relative_base,
				 int depth);
static int link_alt_odb_entry(struct repository *r, const struct strbuf *entry,
	const char *relative_base, int depth, const char *normalized_objdir)
{
	struct object_directory *ent;
	struct strbuf pathbuf = STRBUF_INIT;
	struct strbuf tmp = STRBUF_INIT;
	khiter_t pos;
	int ret = -1;

	if (!is_absolute_path(entry->buf) && relative_base) {
		strbuf_realpath(&pathbuf, relative_base, 1);
		strbuf_addch(&pathbuf, '/');
	}
	strbuf_addbuf(&pathbuf, entry);

	if (!strbuf_realpath(&tmp, pathbuf.buf, 0)) {
		error(_("unable to normalize alternate object path: %s"),
		      pathbuf.buf);
		goto error;
	}
	strbuf_swap(&pathbuf, &tmp);

	/*
	 * The trailing slash after the directory name is given by
	 * this function at the end. Remove duplicates.
	 */
	while (pathbuf.len && pathbuf.buf[pathbuf.len - 1] == '/')
		strbuf_setlen(&pathbuf, pathbuf.len - 1);

	if (!alt_odb_usable(r->objects, &pathbuf, normalized_objdir, &pos))
		goto error;

	CALLOC_ARRAY(ent, 1);
	/* pathbuf.buf is already in r->objects->odb_by_path */
	ent->path = strbuf_detach(&pathbuf, NULL);

	/* add the alternate entry */
	*r->objects->odb_tail = ent;
	r->objects->odb_tail = &(ent->next);
	ent->next = NULL;
	assert(r->objects->odb_by_path);
	kh_value(r->objects->odb_by_path, pos) = ent;

	/* recursively add alternates */
	read_info_alternates(r, ent->path, depth + 1);
	ret = 0;
 error:
	strbuf_release(&tmp);
	strbuf_release(&pathbuf);
	return ret;
}

static const char *parse_alt_odb_entry(const char *string,
				       int sep,
				       struct strbuf *out)
{
	const char *end;

	strbuf_reset(out);

	if (*string == '#') {
		/* comment; consume up to next separator */
		end = strchrnul(string, sep);
	} else if (*string == '"' && !unquote_c_style(out, string, &end)) {
		/*
		 * quoted path; unquote_c_style has copied the
		 * data for us and set "end". Broken quoting (e.g.,
		 * an entry that doesn't end with a quote) falls
		 * back to the unquoted case below.
		 */
	} else {
		/* normal, unquoted path */
		end = strchrnul(string, sep);
		strbuf_add(out, string, end - string);
	}

	if (*end)
		end++;
	return end;
}

static void link_alt_odb_entries(struct repository *r, const char *alt,
				 int sep, const char *relative_base, int depth)
{
	struct strbuf objdirbuf = STRBUF_INIT;
	struct strbuf entry = STRBUF_INIT;

	if (!alt || !*alt)
		return;

	if (depth > 5) {
		error(_("%s: ignoring alternate object stores, nesting too deep"),
				relative_base);
		return;
	}

	strbuf_realpath(&objdirbuf, r->objects->odb->path, 1);

	while (*alt) {
		alt = parse_alt_odb_entry(alt, sep, &entry);
		if (!entry.len)
			continue;
		link_alt_odb_entry(r, &entry,
				   relative_base, depth, objdirbuf.buf);
	}
	strbuf_release(&entry);
	strbuf_release(&objdirbuf);
}

static void read_info_alternates(struct repository *r,
				 const char *relative_base,
				 int depth)
{
	char *path;
	struct strbuf buf = STRBUF_INIT;

	path = xstrfmt("%s/info/alternates", relative_base);
	if (strbuf_read_file(&buf, path, 1024) < 0) {
		warn_on_fopen_errors(path);
		free(path);
		return;
	}

	link_alt_odb_entries(r, buf.buf, '\n', relative_base, depth);
	strbuf_release(&buf);
	free(path);
}

void add_to_alternates_file(const char *reference)
{
	struct lock_file lock = LOCK_INIT;
	char *alts = repo_git_path(the_repository, "objects/info/alternates");
	FILE *in, *out;
	int found = 0;

	hold_lock_file_for_update(&lock, alts, LOCK_DIE_ON_ERROR);
	out = fdopen_lock_file(&lock, "w");
	if (!out)
		die_errno(_("unable to fdopen alternates lockfile"));

	in = fopen(alts, "r");
	if (in) {
		struct strbuf line = STRBUF_INIT;

		while (strbuf_getline(&line, in) != EOF) {
			if (!strcmp(reference, line.buf)) {
				found = 1;
				break;
			}
			fprintf_or_die(out, "%s\n", line.buf);
		}

		strbuf_release(&line);
		fclose(in);
	}
	else if (errno != ENOENT)
		die_errno(_("unable to read alternates file"));

	if (found) {
		rollback_lock_file(&lock);
	} else {
		fprintf_or_die(out, "%s\n", reference);
		if (commit_lock_file(&lock))
			die_errno(_("unable to move new alternates file into place"));
		if (the_repository->objects->loaded_alternates)
			link_alt_odb_entries(the_repository, reference,
					     '\n', NULL, 0);
	}
	free(alts);
}

void add_to_alternates_memory(const char *reference)
{
	/*
	 * Make sure alternates are initialized, or else our entry may be
	 * overwritten when they are.
	 */
	prepare_alt_odb(the_repository);

	link_alt_odb_entries(the_repository, reference,
			     '\n', NULL, 0);
}

struct object_directory *set_temporary_primary_odb(const char *dir, int will_destroy)
{
	struct object_directory *new_odb;

	/*
	 * Make sure alternates are initialized, or else our entry may be
	 * overwritten when they are.
	 */
	prepare_alt_odb(the_repository);

	/*
	 * Make a new primary odb and link the old primary ODB in as an
	 * alternate
	 */
	new_odb = xcalloc(1, sizeof(*new_odb));
	new_odb->path = xstrdup(dir);

	/*
	 * Disable ref updates while a temporary odb is active, since
	 * the objects in the database may roll back.
	 */
	new_odb->disable_ref_updates = 1;
	new_odb->will_destroy = will_destroy;
	new_odb->next = the_repository->objects->odb;
	the_repository->objects->odb = new_odb;
	return new_odb->next;
}

static void free_object_directory(struct object_directory *odb)
{
	free(odb->path);
	odb_clear_loose_cache(odb);
	loose_object_map_clear(&odb->loose_map);
	free(odb);
}

void restore_primary_odb(struct object_directory *restore_odb, const char *old_path)
{
	struct object_directory *cur_odb = the_repository->objects->odb;

	if (strcmp(old_path, cur_odb->path))
		BUG("expected %s as primary object store; found %s",
		    old_path, cur_odb->path);

	if (cur_odb->next != restore_odb)
		BUG("we expect the old primary object store to be the first alternate");

	the_repository->objects->odb = restore_odb;
	free_object_directory(cur_odb);
}

/*
 * Compute the exact path an alternate is at and returns it. In case of
 * error NULL is returned and the human readable error is added to `err`
 * `path` may be relative and should point to $GIT_DIR.
 * `err` must not be null.
 */
char *compute_alternate_path(const char *path, struct strbuf *err)
{
	char *ref_git = NULL;
	const char *repo;
	int seen_error = 0;

	ref_git = real_pathdup(path, 0);
	if (!ref_git) {
		seen_error = 1;
		strbuf_addf(err, _("path '%s' does not exist"), path);
		goto out;
	}

	repo = read_gitfile(ref_git);
	if (!repo)
		repo = read_gitfile(mkpath("%s/.git", ref_git));
	if (repo) {
		free(ref_git);
		ref_git = xstrdup(repo);
	}

	if (!repo && is_directory(mkpath("%s/.git/objects", ref_git))) {
		char *ref_git_git = mkpathdup("%s/.git", ref_git);
		free(ref_git);
		ref_git = ref_git_git;
	} else if (!is_directory(mkpath("%s/objects", ref_git))) {
		struct strbuf sb = STRBUF_INIT;
		seen_error = 1;
		if (get_common_dir(&sb, ref_git)) {
			strbuf_addf(err,
				    _("reference repository '%s' as a linked "
				      "checkout is not supported yet."),
				    path);
			goto out;
		}

		strbuf_addf(err, _("reference repository '%s' is not a "
					"local repository."), path);
		goto out;
	}

	if (!access(mkpath("%s/shallow", ref_git), F_OK)) {
		strbuf_addf(err, _("reference repository '%s' is shallow"),
			    path);
		seen_error = 1;
		goto out;
	}

	if (!access(mkpath("%s/info/grafts", ref_git), F_OK)) {
		strbuf_addf(err,
			    _("reference repository '%s' is grafted"),
			    path);
		seen_error = 1;
		goto out;
	}

out:
	if (seen_error) {
		FREE_AND_NULL(ref_git);
	}

	return ref_git;
}

struct object_directory *find_odb(struct repository *r, const char *obj_dir)
{
	struct object_directory *odb;
	char *obj_dir_real = real_pathdup(obj_dir, 1);
	struct strbuf odb_path_real = STRBUF_INIT;

	prepare_alt_odb(r);
	for (odb = r->objects->odb; odb; odb = odb->next) {
		strbuf_realpath(&odb_path_real, odb->path, 1);
		if (!strcmp(obj_dir_real, odb_path_real.buf))
			break;
	}

	free(obj_dir_real);
	strbuf_release(&odb_path_real);

	if (!odb)
		die(_("could not find object directory matching %s"), obj_dir);
	return odb;
}

static void fill_alternate_refs_command(struct child_process *cmd,
					const char *repo_path)
{
	const char *value;

	if (!git_config_get_value("core.alternateRefsCommand", &value)) {
		cmd->use_shell = 1;

		strvec_push(&cmd->args, value);
		strvec_push(&cmd->args, repo_path);
	} else {
		cmd->git_cmd = 1;

		strvec_pushf(&cmd->args, "--git-dir=%s", repo_path);
		strvec_push(&cmd->args, "for-each-ref");
		strvec_push(&cmd->args, "--format=%(objectname)");

		if (!git_config_get_value("core.alternateRefsPrefixes", &value)) {
			strvec_push(&cmd->args, "--");
			strvec_split(&cmd->args, value);
		}
	}

	strvec_pushv(&cmd->env, (const char **)local_repo_env);
	cmd->out = -1;
}

static void read_alternate_refs(const char *path,
				alternate_ref_fn *cb,
				void *data)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf line = STRBUF_INIT;
	FILE *fh;

	fill_alternate_refs_command(&cmd, path);

	if (start_command(&cmd))
		return;

	fh = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, fh) != EOF) {
		struct object_id oid;
		const char *p;

		if (parse_oid_hex(line.buf, &oid, &p) || *p) {
			warning(_("invalid line while parsing alternate refs: %s"),
				line.buf);
			break;
		}

		cb(&oid, data);
	}

	fclose(fh);
	finish_command(&cmd);
	strbuf_release(&line);
}

struct alternate_refs_data {
	alternate_ref_fn *fn;
	void *data;
};

static int refs_from_alternate_cb(struct object_directory *e,
				  void *data)
{
	struct strbuf path = STRBUF_INIT;
	size_t base_len;
	struct alternate_refs_data *cb = data;

	if (!strbuf_realpath(&path, e->path, 0))
		goto out;
	if (!strbuf_strip_suffix(&path, "/objects"))
		goto out;
	base_len = path.len;

	/* Is this a git repository with refs? */
	strbuf_addstr(&path, "/refs");
	if (!is_directory(path.buf))
		goto out;
	strbuf_setlen(&path, base_len);

	read_alternate_refs(path.buf, cb->fn, cb->data);

out:
	strbuf_release(&path);
	return 0;
}

void for_each_alternate_ref(alternate_ref_fn fn, void *data)
{
	struct alternate_refs_data cb;
	cb.fn = fn;
	cb.data = data;
	foreach_alt_odb(refs_from_alternate_cb, &cb);
}

int foreach_alt_odb(alt_odb_fn fn, void *cb)
{
	struct object_directory *ent;
	int r = 0;

	prepare_alt_odb(the_repository);
	for (ent = the_repository->objects->odb->next; ent; ent = ent->next) {
		r = fn(ent, cb);
		if (r)
			break;
	}
	return r;
}

void prepare_alt_odb(struct repository *r)
{
	if (r->objects->loaded_alternates)
		return;

	link_alt_odb_entries(r, r->objects->alternate_db, PATH_SEP, NULL, 0);

	read_info_alternates(r, r->objects->odb->path, 0);
	r->objects->loaded_alternates = 1;
}

int has_alt_odb(struct repository *r)
{
	prepare_alt_odb(r);
	return !!r->objects->odb->next;
}

int obj_read_use_lock = 0;
pthread_mutex_t obj_read_mutex;

void enable_obj_read_lock(void)
{
	if (obj_read_use_lock)
		return;

	obj_read_use_lock = 1;
	init_recursive_mutex(&obj_read_mutex);
}

void disable_obj_read_lock(void)
{
	if (!obj_read_use_lock)
		return;

	obj_read_use_lock = 0;
	pthread_mutex_destroy(&obj_read_mutex);
}

int fetch_if_missing = 1;

static int do_oid_object_info_extended(struct repository *r,
				       const struct object_id *oid,
				       struct object_info *oi, unsigned flags)
{
	static struct object_info blank_oi = OBJECT_INFO_INIT;
	const struct cached_object *co;
	struct pack_entry e;
	int rtype;
	const struct object_id *real = oid;
	int already_retried = 0;


	if (flags & OBJECT_INFO_LOOKUP_REPLACE)
		real = lookup_replace_object(r, oid);

	if (is_null_oid(real))
		return -1;

	if (!oi)
		oi = &blank_oi;

	co = find_cached_object(r->objects, real);
	if (co) {
		if (oi->typep)
			*(oi->typep) = co->type;
		if (oi->sizep)
			*(oi->sizep) = co->size;
		if (oi->disk_sizep)
			*(oi->disk_sizep) = 0;
		if (oi->delta_base_oid)
			oidclr(oi->delta_base_oid, the_repository->hash_algo);
		if (oi->contentp)
			*oi->contentp = xmemdupz(co->buf, co->size);
		oi->whence = OI_CACHED;
		return 0;
	}

	while (1) {
		if (find_pack_entry(r, real, &e))
			break;

		/* Most likely it's a loose object. */
		if (!loose_object_info(r, real, oi, flags))
			return 0;

		/* Not a loose object; someone else may have just packed it. */
		if (!(flags & OBJECT_INFO_QUICK)) {
			reprepare_packed_git(r);
			if (find_pack_entry(r, real, &e))
				break;
		}

		/*
		 * If r is the_repository, this might be an attempt at
		 * accessing a submodule object as if it were in the_repository
		 * (having called add_submodule_odb() on that submodule's ODB).
		 * If any such ODBs exist, register them and try again.
		 */
		if (r == the_repository &&
		    register_all_submodule_odb_as_alternates())
			/* We added some alternates; retry */
			continue;

		/* Check if it is a missing object */
		if (fetch_if_missing && repo_has_promisor_remote(r) &&
		    !already_retried &&
		    !(flags & OBJECT_INFO_SKIP_FETCH_OBJECT)) {
			promisor_remote_get_direct(r, real, 1);
			already_retried = 1;
			continue;
		}

		if (flags & OBJECT_INFO_DIE_IF_CORRUPT) {
			const struct packed_git *p;
			if ((flags & OBJECT_INFO_LOOKUP_REPLACE) && !oideq(real, oid))
				die(_("replacement %s not found for %s"),
				    oid_to_hex(real), oid_to_hex(oid));
			if ((p = has_packed_and_bad(r, real)))
				die(_("packed object %s (stored in %s) is corrupt"),
				    oid_to_hex(real), p->pack_name);
		}
		return -1;
	}

	if (oi == &blank_oi)
		/*
		 * We know that the caller doesn't actually need the
		 * information below, so return early.
		 */
		return 0;
	rtype = packed_object_info(r, e.p, e.offset, oi);
	if (rtype < 0) {
		mark_bad_packed_object(e.p, real);
		return do_oid_object_info_extended(r, real, oi, 0);
	} else if (oi->whence == OI_PACKED) {
		oi->u.packed.offset = e.offset;
		oi->u.packed.pack = e.p;
		oi->u.packed.is_delta = (rtype == OBJ_REF_DELTA ||
					 rtype == OBJ_OFS_DELTA);
	}

	return 0;
}

static int oid_object_info_convert(struct repository *r,
				   const struct object_id *input_oid,
				   struct object_info *input_oi, unsigned flags)
{
	const struct git_hash_algo *input_algo = &hash_algos[input_oid->algo];
	int do_die = flags & OBJECT_INFO_DIE_IF_CORRUPT;
	enum object_type type;
	struct object_id oid, delta_base_oid;
	struct object_info new_oi, *oi;
	unsigned long size;
	void *content;
	int ret;

	if (repo_oid_to_algop(r, input_oid, the_hash_algo, &oid)) {
		if (do_die)
			die(_("missing mapping of %s to %s"),
			    oid_to_hex(input_oid), the_hash_algo->name);
		return -1;
	}

	/* Is new_oi needed? */
	oi = input_oi;
	if (input_oi && (input_oi->delta_base_oid || input_oi->sizep ||
			 input_oi->contentp)) {
		new_oi = *input_oi;
		/* Does delta_base_oid need to be converted? */
		if (input_oi->delta_base_oid)
			new_oi.delta_base_oid = &delta_base_oid;
		/* Will the attributes differ when converted? */
		if (input_oi->sizep || input_oi->contentp) {
			new_oi.contentp = &content;
			new_oi.sizep = &size;
			new_oi.typep = &type;
		}
		oi = &new_oi;
	}

	ret = oid_object_info_extended(r, &oid, oi, flags);
	if (ret)
		return -1;
	if (oi == input_oi)
		return ret;

	if (new_oi.contentp) {
		struct strbuf outbuf = STRBUF_INIT;

		if (type != OBJ_BLOB) {
			ret = convert_object_file(the_repository, &outbuf,
						  the_hash_algo, input_algo,
						  content, size, type, !do_die);
			free(content);
			if (ret == -1)
				return -1;
			size = outbuf.len;
			content = strbuf_detach(&outbuf, NULL);
		}
		if (input_oi->sizep)
			*input_oi->sizep = size;
		if (input_oi->contentp)
			*input_oi->contentp = content;
		else
			free(content);
		if (input_oi->typep)
			*input_oi->typep = type;
	}
	if (new_oi.delta_base_oid == &delta_base_oid) {
		if (repo_oid_to_algop(r, &delta_base_oid, input_algo,
				 input_oi->delta_base_oid)) {
			if (do_die)
				die(_("missing mapping of %s to %s"),
				    oid_to_hex(&delta_base_oid),
				    input_algo->name);
			return -1;
		}
	}
	input_oi->whence = new_oi.whence;
	input_oi->u = new_oi.u;
	return ret;
}

int oid_object_info_extended(struct repository *r, const struct object_id *oid,
			     struct object_info *oi, unsigned flags)
{
	int ret;

	if (oid->algo && (hash_algo_by_ptr(r->hash_algo) != oid->algo))
		return oid_object_info_convert(r, oid, oi, flags);

	obj_read_lock();
	ret = do_oid_object_info_extended(r, oid, oi, flags);
	obj_read_unlock();
	return ret;
}


/* returns enum object_type or negative */
int oid_object_info(struct repository *r,
		    const struct object_id *oid,
		    unsigned long *sizep)
{
	enum object_type type;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.typep = &type;
	oi.sizep = sizep;
	if (oid_object_info_extended(r, oid, &oi,
				      OBJECT_INFO_LOOKUP_REPLACE) < 0)
		return -1;
	return type;
}

int pretend_object_file(struct repository *repo,
			void *buf, unsigned long len, enum object_type type,
			struct object_id *oid)
{
	struct cached_object_entry *co;
	char *co_buf;

	hash_object_file(repo->hash_algo, buf, len, type, oid);
	if (has_object(repo, oid, 0) ||
	    find_cached_object(repo->objects, oid))
		return 0;

	ALLOC_GROW(repo->objects->cached_objects,
		   repo->objects->cached_object_nr + 1, repo->objects->cached_object_alloc);
	co = &repo->objects->cached_objects[repo->objects->cached_object_nr++];
	co->value.size = len;
	co->value.type = type;
	co_buf = xmalloc(len);
	memcpy(co_buf, buf, len);
	co->value.buf = co_buf;
	oidcpy(&co->oid, oid);
	return 0;
}

/*
 * This function dies on corrupt objects; the callers who want to
 * deal with them should arrange to call oid_object_info_extended() and give
 * error messages themselves.
 */
void *repo_read_object_file(struct repository *r,
			    const struct object_id *oid,
			    enum object_type *type,
			    unsigned long *size)
{
	struct object_info oi = OBJECT_INFO_INIT;
	unsigned flags = OBJECT_INFO_DIE_IF_CORRUPT | OBJECT_INFO_LOOKUP_REPLACE;
	void *data;

	oi.typep = type;
	oi.sizep = size;
	oi.contentp = &data;
	if (oid_object_info_extended(r, oid, &oi, flags))
		return NULL;

	return data;
}

void *read_object_with_reference(struct repository *r,
				 const struct object_id *oid,
				 enum object_type required_type,
				 unsigned long *size,
				 struct object_id *actual_oid_return)
{
	enum object_type type;
	void *buffer;
	unsigned long isize;
	struct object_id actual_oid;

	oidcpy(&actual_oid, oid);
	while (1) {
		int ref_length = -1;
		const char *ref_type = NULL;

		buffer = repo_read_object_file(r, &actual_oid, &type, &isize);
		if (!buffer)
			return NULL;
		if (type == required_type) {
			*size = isize;
			if (actual_oid_return)
				oidcpy(actual_oid_return, &actual_oid);
			return buffer;
		}
		/* Handle references */
		else if (type == OBJ_COMMIT)
			ref_type = "tree ";
		else if (type == OBJ_TAG)
			ref_type = "object ";
		else {
			free(buffer);
			return NULL;
		}
		ref_length = strlen(ref_type);

		if (ref_length + the_hash_algo->hexsz > isize ||
		    memcmp(buffer, ref_type, ref_length) ||
		    get_oid_hex((char *) buffer + ref_length, &actual_oid)) {
			free(buffer);
			return NULL;
		}
		free(buffer);
		/* Now we have the ID of the referred-to object in
		 * actual_oid.  Check again. */
	}
}

int has_object(struct repository *r, const struct object_id *oid,
	       unsigned flags)
{
	unsigned object_info_flags = 0;

	if (!startup_info->have_repository)
		return 0;
	if (!(flags & HAS_OBJECT_RECHECK_PACKED))
		object_info_flags |= OBJECT_INFO_QUICK;
	if (!(flags & HAS_OBJECT_FETCH_PROMISOR))
		object_info_flags |= OBJECT_INFO_SKIP_FETCH_OBJECT;

	return oid_object_info_extended(r, oid, NULL, object_info_flags) >= 0;
}

void assert_oid_type(const struct object_id *oid, enum object_type expect)
{
	enum object_type type = oid_object_info(the_repository, oid, NULL);
	if (type < 0)
		die(_("%s is not a valid object"), oid_to_hex(oid));
	if (type != expect)
		die(_("%s is not a valid '%s' object"), oid_to_hex(oid),
		    type_name(expect));
}

struct raw_object_store *raw_object_store_new(void)
{
	struct raw_object_store *o = xmalloc(sizeof(*o));

	memset(o, 0, sizeof(*o));
	INIT_LIST_HEAD(&o->packed_git_mru);
	hashmap_init(&o->pack_map, pack_map_entry_cmp, NULL, 0);
	pthread_mutex_init(&o->replace_mutex, NULL);
	return o;
}

static void free_object_directories(struct raw_object_store *o)
{
	while (o->odb) {
		struct object_directory *next;

		next = o->odb->next;
		free_object_directory(o->odb);
		o->odb = next;
	}
	kh_destroy_odb_path_map(o->odb_by_path);
	o->odb_by_path = NULL;
}

void raw_object_store_clear(struct raw_object_store *o)
{
	FREE_AND_NULL(o->alternate_db);

	oidmap_clear(&o->replace_map, 1);
	pthread_mutex_destroy(&o->replace_mutex);

	free_commit_graph(o->commit_graph);
	o->commit_graph = NULL;
	o->commit_graph_attempted = 0;

	free_object_directories(o);
	o->odb_tail = NULL;
	o->loaded_alternates = 0;

	for (size_t i = 0; i < o->cached_object_nr; i++)
		free((char *) o->cached_objects[i].value.buf);
	FREE_AND_NULL(o->cached_objects);

	INIT_LIST_HEAD(&o->packed_git_mru);
	close_object_store(o);

	/*
	 * `close_object_store()` only closes the packfiles, but doesn't free
	 * them. We thus have to do this manually.
	 */
	for (struct packed_git *p = o->packed_git, *next; p; p = next) {
		next = p->next;
		free(p);
	}
	o->packed_git = NULL;

	hashmap_clear(&o->pack_map);
}
