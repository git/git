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
#include "odb.h"
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
#include "trace2.h"
#include "write-or-die.h"

KHASH_INIT(odb_path_map, const char * /* key: odb_path */,
	struct odb_source *, 1, fspathhash, fspatheq)

/*
 * This is meant to hold a *small* number of objects that you would
 * want odb_read_object() to be able to return, but yet you do not want
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

static const struct cached_object *find_cached_object(struct object_database *object_store,
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

int odb_mkstemp(struct object_database *odb,
		struct strbuf *temp_filename, const char *pattern)
{
	int fd;
	/*
	 * we let the umask do its job, don't try to be more
	 * restrictive except to remove write permission.
	 */
	int mode = 0444;
	repo_git_path_replace(odb->repo, temp_filename, "objects/%s", pattern);
	fd = git_mkstemp_mode(temp_filename->buf, mode);
	if (0 <= fd)
		return fd;

	/* slow path */
	/* some mkstemp implementations erase temp_filename on failure */
	repo_git_path_replace(odb->repo, temp_filename, "objects/%s", pattern);
	safe_create_leading_directories(odb->repo, temp_filename->buf);
	return xmkstemp_mode(temp_filename->buf, mode);
}

/*
 * Return non-zero iff the path is usable as an alternate object database.
 */
static int alt_odb_usable(struct object_database *o,
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
	if (!o->source_by_path) {
		khiter_t p;

		o->source_by_path = kh_init_odb_path_map();
		assert(!o->sources->next);
		p = kh_put_odb_path_map(o->source_by_path, o->sources->path, &r);
		assert(r == 1); /* never used */
		kh_value(o->source_by_path, p) = o->sources;
	}
	if (fspatheq(path->buf, normalized_objdir))
		return 0;
	*pos = kh_put_odb_path_map(o->source_by_path, path->buf, &r);
	/* r: 0 = exists, 1 = never used, 2 = deleted */
	return r == 0 ? 0 : 1;
}

/*
 * Prepare alternate object database registry.
 *
 * The variable alt_odb_list points at the list of struct
 * odb_source.  The elements on this list come from
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
static void read_info_alternates(struct object_database *odb,
				 const char *relative_base,
				 int depth);

static int link_alt_odb_entry(struct object_database *odb,
			      const struct strbuf *entry,
			      const char *relative_base,
			      int depth,
			      const char *normalized_objdir)
{
	struct odb_source *alternate;
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

	if (!alt_odb_usable(odb, &pathbuf, normalized_objdir, &pos))
		goto error;

	CALLOC_ARRAY(alternate, 1);
	alternate->odb = odb;
	/* pathbuf.buf is already in r->objects->alternate_by_path */
	alternate->path = strbuf_detach(&pathbuf, NULL);

	/* add the alternate entry */
	*odb->sources_tail = alternate;
	odb->sources_tail = &(alternate->next);
	alternate->next = NULL;
	assert(odb->source_by_path);
	kh_value(odb->source_by_path, pos) = alternate;

	/* recursively add alternates */
	read_info_alternates(odb, alternate->path, depth + 1);
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

static void link_alt_odb_entries(struct object_database *odb, const char *alt,
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

	strbuf_realpath(&objdirbuf, odb->sources->path, 1);

	while (*alt) {
		alt = parse_alt_odb_entry(alt, sep, &entry);
		if (!entry.len)
			continue;
		link_alt_odb_entry(odb, &entry,
				   relative_base, depth, objdirbuf.buf);
	}
	strbuf_release(&entry);
	strbuf_release(&objdirbuf);
}

static void read_info_alternates(struct object_database *odb,
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

	link_alt_odb_entries(odb, buf.buf, '\n', relative_base, depth);
	strbuf_release(&buf);
	free(path);
}

void odb_add_to_alternates_file(struct object_database *odb,
				const char *reference)
{
	struct lock_file lock = LOCK_INIT;
	char *alts = repo_git_path(odb->repo, "objects/info/alternates");
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
		if (odb->loaded_alternates)
			link_alt_odb_entries(odb, reference,
					     '\n', NULL, 0);
	}
	free(alts);
}

void odb_add_to_alternates_memory(struct object_database *odb,
				  const char *reference)
{
	/*
	 * Make sure alternates are initialized, or else our entry may be
	 * overwritten when they are.
	 */
	odb_prepare_alternates(odb);

	link_alt_odb_entries(odb, reference,
			     '\n', NULL, 0);
}

struct odb_source *odb_set_temporary_primary_source(struct object_database *odb,
						    const char *dir, int will_destroy)
{
	struct odb_source *source;

	/*
	 * Make sure alternates are initialized, or else our entry may be
	 * overwritten when they are.
	 */
	odb_prepare_alternates(odb);

	/*
	 * Make a new primary odb and link the old primary ODB in as an
	 * alternate
	 */
	source = xcalloc(1, sizeof(*source));
	source->odb = odb;
	source->path = xstrdup(dir);

	/*
	 * Disable ref updates while a temporary odb is active, since
	 * the objects in the database may roll back.
	 */
	source->disable_ref_updates = 1;
	source->will_destroy = will_destroy;
	source->next = odb->sources;
	odb->sources = source;
	return source->next;
}

static void free_object_directory(struct odb_source *source)
{
	free(source->path);
	odb_clear_loose_cache(source);
	loose_object_map_clear(&source->loose_map);
	free(source);
}

void odb_restore_primary_source(struct object_database *odb,
				struct odb_source *restore_source,
				const char *old_path)
{
	struct odb_source *cur_source = odb->sources;

	if (strcmp(old_path, cur_source->path))
		BUG("expected %s as primary object store; found %s",
		    old_path, cur_source->path);

	if (cur_source->next != restore_source)
		BUG("we expect the old primary object store to be the first alternate");

	odb->sources = restore_source;
	free_object_directory(cur_source);
}

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

struct odb_source *odb_find_source(struct object_database *odb, const char *obj_dir)
{
	struct odb_source *source;
	char *obj_dir_real = real_pathdup(obj_dir, 1);
	struct strbuf odb_path_real = STRBUF_INIT;

	odb_prepare_alternates(odb);
	for (source = odb->sources; source; source = source->next) {
		strbuf_realpath(&odb_path_real, source->path, 1);
		if (!strcmp(obj_dir_real, odb_path_real.buf))
			break;
	}

	free(obj_dir_real);
	strbuf_release(&odb_path_real);

	if (!source)
		die(_("could not find object directory matching %s"), obj_dir);
	return source;
}

void odb_add_submodule_source_by_path(struct object_database *odb,
				      const char *path)
{
	string_list_insert(&odb->submodule_source_paths, path);
}

static void fill_alternate_refs_command(struct repository *repo,
					struct child_process *cmd,
					const char *repo_path)
{
	const char *value;

	if (!repo_config_get_value(repo, "core.alternateRefsCommand", &value)) {
		cmd->use_shell = 1;

		strvec_push(&cmd->args, value);
		strvec_push(&cmd->args, repo_path);
	} else {
		cmd->git_cmd = 1;

		strvec_pushf(&cmd->args, "--git-dir=%s", repo_path);
		strvec_push(&cmd->args, "for-each-ref");
		strvec_push(&cmd->args, "--format=%(objectname)");

		if (!repo_config_get_value(repo, "core.alternateRefsPrefixes", &value)) {
			strvec_push(&cmd->args, "--");
			strvec_split(&cmd->args, value);
		}
	}

	strvec_pushv(&cmd->env, (const char **)local_repo_env);
	cmd->out = -1;
}

static void read_alternate_refs(struct repository *repo,
				const char *path,
				odb_for_each_alternate_ref_fn *cb,
				void *payload)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf line = STRBUF_INIT;
	FILE *fh;

	fill_alternate_refs_command(repo, &cmd, path);

	if (start_command(&cmd))
		return;

	fh = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, fh) != EOF) {
		struct object_id oid;
		const char *p;

		if (parse_oid_hex_algop(line.buf, &oid, &p, repo->hash_algo) || *p) {
			warning(_("invalid line while parsing alternate refs: %s"),
				line.buf);
			break;
		}

		cb(&oid, payload);
	}

	fclose(fh);
	finish_command(&cmd);
	strbuf_release(&line);
}

struct alternate_refs_data {
	odb_for_each_alternate_ref_fn *fn;
	void *payload;
};

static int refs_from_alternate_cb(struct odb_source *alternate,
				  void *payload)
{
	struct strbuf path = STRBUF_INIT;
	size_t base_len;
	struct alternate_refs_data *cb = payload;

	if (!strbuf_realpath(&path, alternate->path, 0))
		goto out;
	if (!strbuf_strip_suffix(&path, "/objects"))
		goto out;
	base_len = path.len;

	/* Is this a git repository with refs? */
	strbuf_addstr(&path, "/refs");
	if (!is_directory(path.buf))
		goto out;
	strbuf_setlen(&path, base_len);

	read_alternate_refs(alternate->odb->repo, path.buf, cb->fn, cb->payload);

out:
	strbuf_release(&path);
	return 0;
}

void odb_for_each_alternate_ref(struct object_database *odb,
				odb_for_each_alternate_ref_fn cb, void *payload)
{
	struct alternate_refs_data data;
	data.fn = cb;
	data.payload = payload;
	odb_for_each_alternate(odb, refs_from_alternate_cb, &data);
}

int odb_for_each_alternate(struct object_database *odb,
			 odb_for_each_alternate_fn cb, void *payload)
{
	struct odb_source *alternate;
	int r = 0;

	odb_prepare_alternates(odb);
	for (alternate = odb->sources->next; alternate; alternate = alternate->next) {
		r = cb(alternate, payload);
		if (r)
			break;
	}
	return r;
}

void odb_prepare_alternates(struct object_database *odb)
{
	if (odb->loaded_alternates)
		return;

	link_alt_odb_entries(odb, odb->alternate_db, PATH_SEP, NULL, 0);

	read_info_alternates(odb, odb->sources->path, 0);
	odb->loaded_alternates = 1;
}

int odb_has_alternates(struct object_database *odb)
{
	odb_prepare_alternates(odb);
	return !!odb->sources->next;
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

static int register_all_submodule_sources(struct object_database *odb)
{
	int ret = odb->submodule_source_paths.nr;

	for (size_t i = 0; i < odb->submodule_source_paths.nr; i++)
		odb_add_to_alternates_memory(odb,
					     odb->submodule_source_paths.items[i].string);
	if (ret) {
		string_list_clear(&odb->submodule_source_paths, 0);
		trace2_data_intmax("submodule", odb->repo,
				   "register_all_submodule_sources/registered", ret);
		if (git_env_bool("GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB", 0))
			BUG("register_all_submodule_sources() called");
	}
	return ret;
}

static int do_oid_object_info_extended(struct object_database *odb,
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
		real = lookup_replace_object(odb->repo, oid);

	if (is_null_oid(real))
		return -1;

	if (!oi)
		oi = &blank_oi;

	co = find_cached_object(odb, real);
	if (co) {
		if (oi->typep)
			*(oi->typep) = co->type;
		if (oi->sizep)
			*(oi->sizep) = co->size;
		if (oi->disk_sizep)
			*(oi->disk_sizep) = 0;
		if (oi->delta_base_oid)
			oidclr(oi->delta_base_oid, odb->repo->hash_algo);
		if (oi->contentp)
			*oi->contentp = xmemdupz(co->buf, co->size);
		oi->whence = OI_CACHED;
		return 0;
	}

	while (1) {
		if (find_pack_entry(odb->repo, real, &e))
			break;

		/* Most likely it's a loose object. */
		if (!loose_object_info(odb->repo, real, oi, flags))
			return 0;

		/* Not a loose object; someone else may have just packed it. */
		if (!(flags & OBJECT_INFO_QUICK)) {
			reprepare_packed_git(odb->repo);
			if (find_pack_entry(odb->repo, real, &e))
				break;
		}

		/*
		 * This might be an attempt at accessing a submodule object as
		 * if it were in main object store (having called
		 * `odb_add_submodule_source_by_path()` on that submodule's
		 * ODB). If any such ODBs exist, register them and try again.
		 */
		if (register_all_submodule_sources(odb))
			/* We added some alternates; retry */
			continue;

		/* Check if it is a missing object */
		if (fetch_if_missing && repo_has_promisor_remote(odb->repo) &&
		    !already_retried &&
		    !(flags & OBJECT_INFO_SKIP_FETCH_OBJECT)) {
			promisor_remote_get_direct(odb->repo, real, 1);
			already_retried = 1;
			continue;
		}

		if (flags & OBJECT_INFO_DIE_IF_CORRUPT) {
			const struct packed_git *p;
			if ((flags & OBJECT_INFO_LOOKUP_REPLACE) && !oideq(real, oid))
				die(_("replacement %s not found for %s"),
				    oid_to_hex(real), oid_to_hex(oid));
			if ((p = has_packed_and_bad(odb->repo, real)))
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
	rtype = packed_object_info(odb->repo, e.p, e.offset, oi);
	if (rtype < 0) {
		mark_bad_packed_object(e.p, real);
		return do_oid_object_info_extended(odb, real, oi, 0);
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

	if (repo_oid_to_algop(r, input_oid, r->hash_algo, &oid)) {
		if (do_die)
			die(_("missing mapping of %s to %s"),
			    oid_to_hex(input_oid), r->hash_algo->name);
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

	ret = odb_read_object_info_extended(r->objects, &oid, oi, flags);
	if (ret)
		return -1;
	if (oi == input_oi)
		return ret;

	if (new_oi.contentp) {
		struct strbuf outbuf = STRBUF_INIT;

		if (type != OBJ_BLOB) {
			ret = convert_object_file(r, &outbuf,
						  r->hash_algo, input_algo,
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

int odb_read_object_info_extended(struct object_database *odb,
				  const struct object_id *oid,
				  struct object_info *oi,
				  unsigned flags)
{
	int ret;

	if (oid->algo && (hash_algo_by_ptr(odb->repo->hash_algo) != oid->algo))
		return oid_object_info_convert(odb->repo, oid, oi, flags);

	obj_read_lock();
	ret = do_oid_object_info_extended(odb, oid, oi, flags);
	obj_read_unlock();
	return ret;
}


/* returns enum object_type or negative */
int odb_read_object_info(struct object_database *odb,
			 const struct object_id *oid,
			 unsigned long *sizep)
{
	enum object_type type;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.typep = &type;
	oi.sizep = sizep;
	if (odb_read_object_info_extended(odb, oid, &oi,
					  OBJECT_INFO_LOOKUP_REPLACE) < 0)
		return -1;
	return type;
}

int odb_pretend_object(struct object_database *odb,
		       void *buf, unsigned long len, enum object_type type,
		       struct object_id *oid)
{
	struct cached_object_entry *co;
	char *co_buf;

	hash_object_file(odb->repo->hash_algo, buf, len, type, oid);
	if (odb_has_object(odb, oid, 0) ||
	    find_cached_object(odb, oid))
		return 0;

	ALLOC_GROW(odb->cached_objects,
		   odb->cached_object_nr + 1, odb->cached_object_alloc);
	co = &odb->cached_objects[odb->cached_object_nr++];
	co->value.size = len;
	co->value.type = type;
	co_buf = xmalloc(len);
	memcpy(co_buf, buf, len);
	co->value.buf = co_buf;
	oidcpy(&co->oid, oid);
	return 0;
}

void *odb_read_object(struct object_database *odb,
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
	if (odb_read_object_info_extended(odb, oid, &oi, flags))
		return NULL;

	return data;
}

void *odb_read_object_peeled(struct object_database *odb,
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

		buffer = odb_read_object(odb, &actual_oid, &type, &isize);
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

		if (ref_length + odb->repo->hash_algo->hexsz > isize ||
		    memcmp(buffer, ref_type, ref_length) ||
		    get_oid_hex_algop((char *) buffer + ref_length, &actual_oid,
				      odb->repo->hash_algo)) {
			free(buffer);
			return NULL;
		}
		free(buffer);
		/* Now we have the ID of the referred-to object in
		 * actual_oid.  Check again. */
	}
}

int odb_has_object(struct object_database *odb, const struct object_id *oid,
	       unsigned flags)
{
	unsigned object_info_flags = 0;

	if (!startup_info->have_repository)
		return 0;
	if (!(flags & HAS_OBJECT_RECHECK_PACKED))
		object_info_flags |= OBJECT_INFO_QUICK;
	if (!(flags & HAS_OBJECT_FETCH_PROMISOR))
		object_info_flags |= OBJECT_INFO_SKIP_FETCH_OBJECT;

	return odb_read_object_info_extended(odb, oid, NULL, object_info_flags) >= 0;
}

void odb_assert_oid_type(struct object_database *odb,
			 const struct object_id *oid, enum object_type expect)
{
	enum object_type type = odb_read_object_info(odb, oid, NULL);
	if (type < 0)
		die(_("%s is not a valid object"), oid_to_hex(oid));
	if (type != expect)
		die(_("%s is not a valid '%s' object"), oid_to_hex(oid),
		    type_name(expect));
}

struct object_database *odb_new(struct repository *repo)
{
	struct object_database *o = xmalloc(sizeof(*o));

	memset(o, 0, sizeof(*o));
	o->repo = repo;
	INIT_LIST_HEAD(&o->packed_git_mru);
	hashmap_init(&o->pack_map, pack_map_entry_cmp, NULL, 0);
	pthread_mutex_init(&o->replace_mutex, NULL);
	string_list_init_dup(&o->submodule_source_paths);
	return o;
}

static void free_object_directories(struct object_database *o)
{
	while (o->sources) {
		struct odb_source *next;

		next = o->sources->next;
		free_object_directory(o->sources);
		o->sources = next;
	}
	kh_destroy_odb_path_map(o->source_by_path);
	o->source_by_path = NULL;
}

void odb_clear(struct object_database *o)
{
	FREE_AND_NULL(o->alternate_db);

	oidmap_clear(&o->replace_map, 1);
	pthread_mutex_destroy(&o->replace_mutex);

	free_commit_graph(o->commit_graph);
	o->commit_graph = NULL;
	o->commit_graph_attempted = 0;

	free_object_directories(o);
	o->sources_tail = NULL;
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
	string_list_clear(&o->submodule_source_paths, 0);
}
