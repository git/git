#include "git-compat-util.h"
#include "abspath.h"
#include "commit-graph.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hashmap.h"
#include "hex.h"
#include "lockfile.h"
#include "loose.h"
#include "midx.h"
#include "object-file-convert.h"
#include "object-file.h"
#include "object-name.h"
#include "odb.h"
#include "odb/source-inmemory.h"
#include "path.h"
#include "promisor-remote.h"
#include "quote.h"
#include "replace-object.h"
#include "run-command.h"
#include "setup.h"
#include "strbuf.h"
#include "strvec.h"
#include "submodule.h"
#include "tmp-objdir.h"
#include "trace2.h"
#include "write-or-die.h"

/*
 * NEEDSWORK: we're using "core.ignoreCase" to deduplicate alternates that
 * _may_ be the same. This requires quite a bit of boilerplate for dubious
 * benefit:
 *
 *   - Duplicating alternates should really only lead to regressed performance.
 *
 *   - We don't properly resolve symlinks or mointpoints, so we may still end
 *     up duplicating alternates.
 *
 *   - The value may be lying, in which case we might deduplicate alternates
 *     that are in fact not mapping to the same directory.
 *
 * We should investigate whether we can remove this whole mechanism outright.
 */
static int odb_source_paths_cmp(struct object_database *o,
				const char *a, const char *b)
{
	if (o->source_paths_icase < 0) {
		int icase = 0;
		repo_config_get_bool(o->repo, "core.ignorecase", &icase);
		o->source_paths_icase = icase;
	}

	return o->source_paths_icase ? strcasecmp(a, b) : strcmp(a, b);
}

static int odb_source_by_path_cmp(const void *cb_data,
				  const struct hashmap_entry *entry,
				  const struct hashmap_entry *entry_or_key,
				  const void *keydata)
{
	struct object_database *o = (struct object_database *)cb_data;
	const struct odb_source *source = container_of(entry, const struct odb_source, by_path_entry);
	const char *path = keydata;

	if (!path)
		path = container_of(entry_or_key, const struct odb_source, by_path_entry)->path;

	return odb_source_paths_cmp(o, source->path, path);
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
static bool odb_is_source_usable(struct object_database *o, const char *path)
{
	struct strbuf normalized_objdir = STRBUF_INIT;
	struct hashmap_entry key;
	bool usable = false;

	strbuf_realpath(&normalized_objdir, o->sources->path, 1);

	/* Detect cases where alternate disappeared */
	if (!is_directory(path)) {
		error(_("object directory %s does not exist; "
			"check .git/objects/info/alternates"),
		      path);
		goto out;
	}

	/*
	 * Prevent the common mistake of listing the same
	 * thing twice, or object directory itself.
	 */
	if (!hashmap_get_size(&o->source_by_path)) {
		assert(!o->sources->next);
		hashmap_entry_init(&o->sources->by_path_entry,
				   strihash(o->sources->path));
		hashmap_add(&o->source_by_path, &o->sources->by_path_entry);
	}

	if (!odb_source_paths_cmp(o, path, normalized_objdir.buf))
		goto out;

	hashmap_entry_init(&key, strihash(path));
	if (hashmap_get(&o->source_by_path, &key, path))
		goto out;

	usable = true;

out:
	strbuf_release(&normalized_objdir);
	return usable;
}

void parse_alternates(const char *string,
		      int sep,
		      const char *relative_base,
		      struct strvec *out)
{
	struct strbuf pathbuf = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;

	if (!string || !*string)
		return;

	while (*string) {
		const char *end;

		strbuf_reset(&buf);
		strbuf_reset(&pathbuf);

		if (*string == '#') {
			/* comment; consume up to next separator */
			end = strchrnul(string, sep);
		} else if (*string == '"' && !unquote_c_style(&buf, string, &end)) {
			/*
			 * quoted path; unquote_c_style has copied the
			 * data for us and set "end". Broken quoting (e.g.,
			 * an entry that doesn't end with a quote) falls
			 * back to the unquoted case below.
			 */
		} else {
			/* normal, unquoted path */
			end = strchrnul(string, sep);
			strbuf_add(&buf, string, end - string);
		}

		if (*end)
			end++;
		string = end;

		if (!buf.len)
			continue;

		if (!is_absolute_path(buf.buf) && relative_base) {
			strbuf_realpath(&pathbuf, relative_base, 1);
			strbuf_addch(&pathbuf, '/');
		}
		strbuf_addbuf(&pathbuf, &buf);

		strbuf_reset(&buf);
		if (!strbuf_realpath(&buf, pathbuf.buf, 0)) {
			error(_("unable to normalize alternate object path: %s"),
			      pathbuf.buf);
			continue;
		}

		/*
		 * The trailing slash after the directory name is given by
		 * this function at the end. Remove duplicates.
		 */
		while (buf.len && buf.buf[buf.len - 1] == '/')
			strbuf_setlen(&buf, buf.len - 1);

		strvec_push(out, buf.buf);
	}

	strbuf_release(&pathbuf);
	strbuf_release(&buf);
}

static struct odb_source *odb_add_alternate_recursively(struct object_database *odb,
							const char *source,
							int depth)
{
	struct odb_source *alternate = NULL;
	struct strvec sources = STRVEC_INIT;

	if (!odb_is_source_usable(odb, source))
		goto error;

	alternate = odb_source_new(odb, source, false);

	/* add the alternate entry */
	*odb->sources_tail = alternate;
	odb->sources_tail = &(alternate->next);

	hashmap_entry_init(&alternate->by_path_entry, strihash(alternate->path));
	if (hashmap_get(&odb->source_by_path, &alternate->by_path_entry,
			alternate->path))
		BUG("source must not yet exist");
	hashmap_add(&odb->source_by_path, &alternate->by_path_entry);

	/* recursively add alternates */
	odb_source_read_alternates(alternate, &sources);
	if (sources.nr && depth + 1 > 5) {
		error(_("%s: ignoring alternate object stores, nesting too deep"),
		      source);
	} else {
		for (size_t i = 0; i < sources.nr; i++)
			odb_add_alternate_recursively(odb, sources.v[i], depth + 1);
	}

 error:
	strvec_clear(&sources);
	return alternate;
}

void odb_add_to_alternates_file(struct object_database *odb,
				const char *dir)
{
	int ret = odb_source_write_alternate(odb->sources, dir);
	if (ret < 0)
		die(NULL);
	odb_add_alternate_recursively(odb, dir, 0);
}

struct odb_source *odb_add_to_alternates_memory(struct object_database *odb,
						const char *dir)
{
	return odb_add_alternate_recursively(odb, dir, 0);
}

struct odb_source *odb_set_temporary_primary_source(struct object_database *odb,
						    const char *dir, int will_destroy,
						    struct odb_source **prev_source)
{
	struct odb_source *source;

	/*
	 * Make a new primary odb and link the old primary ODB in as an
	 * alternate
	 */
	source = odb_source_new(odb, dir, false);

	/*
	 * Disable ref updates while a temporary odb is active, since
	 * the objects in the database may roll back.
	 */
	odb->repo->disable_ref_updates = true;
	source->will_destroy = will_destroy;
	source->next = odb->sources;
	odb->sources = source;

	if (prev_source)
		*prev_source = source->next;

	return source;
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

	odb->repo->disable_ref_updates = false;
	odb->sources = restore_source;
	odb_source_free(cur_source);
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

	for (source = odb->sources; source; source = source->next) {
		strbuf_realpath(&odb_path_real, source->path, 1);
		if (!strcmp(obj_dir_real, odb_path_real.buf))
			break;
	}

	free(obj_dir_real);
	strbuf_release(&odb_path_real);

	return source;
}

struct odb_source *odb_find_source_or_die(struct object_database *odb, const char *obj_dir)
{
	struct odb_source *source = odb_find_source(odb, obj_dir);
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

	for (alternate = odb->sources->next; alternate; alternate = alternate->next) {
		r = cb(alternate, payload);
		if (r)
			break;
	}
	return r;
}

static void odb_prepare_alternates(struct object_database *odb,
				   const char *alternate_db)
{
	struct strvec sources = STRVEC_INIT;

	parse_alternates(alternate_db, PATH_SEP, NULL, &sources);
	odb_source_read_alternates(odb->sources, &sources);

	for (size_t i = 0; i < sources.nr; i++)
		odb_add_alternate_recursively(odb, sources.v[i], 0);

	strvec_clear(&sources);
}

int odb_has_alternates(struct object_database *odb)
{
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

static enum odb_read_status do_oid_object_info_extended(struct object_database *odb,
							const struct object_id *oid,
							struct object_info *oi, unsigned flags)
{
	struct strbuf corrupt_err = STRBUF_INIT;
	const struct object_id *real = oid;
	enum odb_read_status ret;
	int already_retried = 0;
	bool corrupt = false;

	if (flags & OBJECT_INFO_LOOKUP_REPLACE)
		real = lookup_replace_object(odb->repo, oid);

	if (is_null_oid(real))
		return -1;

	if (!odb_source_read_object_info(odb->inmemory_objects, oid, oi, flags, NULL))
		return 0;

	while (1) {
		struct odb_source *source;

		for (source = odb->sources; source; source = source->next) {
			ret = odb_source_read_object_info(source, real, oi, flags,
							  corrupt_err.len ? NULL : &corrupt_err);
			if (!ret)
				goto out;
			if (ret != ODB_READ_NOT_FOUND)
				corrupt = true;
		}

		/*
		 * When the object hasn't been found we try a second read and
		 * tell the sources so. This may cause them to invalidate
		 * caches or reload on-disk state.
		 */
		if (!(flags & OBJECT_INFO_QUICK)) {
			for (source = odb->sources; source; source = source->next) {
				ret = odb_source_read_object_info(source, real, oi,
								  flags | OBJECT_INFO_SECOND_READ,
								  corrupt_err.len ? NULL : &corrupt_err);
				if (!ret)
					goto out;
				if (ret != ODB_READ_NOT_FOUND)
					corrupt = true;
			}
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
		if (odb->repo->fetch_if_missing && repo_has_promisor_remote(odb->repo) &&
		    !already_retried &&
		    !(flags & OBJECT_INFO_SKIP_FETCH_OBJECT)) {
			promisor_remote_get_direct(odb->repo, real, 1);
			already_retried = 1;
			continue;
		}

		if (flags & OBJECT_INFO_DIE_IF_CORRUPT) {
			if ((flags & OBJECT_INFO_LOOKUP_REPLACE) && !oideq(real, oid))
				die(_("replacement %s not found for %s"),
				    oid_to_hex(real), oid_to_hex(oid));
			if (corrupt) {
				if (corrupt_err.len)
					die("%s", corrupt_err.buf);
				die(_("object %s is corrupt"), oid_to_hex(real));
			}
		}

		ret = corrupt ? ODB_READ_ERROR : ODB_READ_NOT_FOUND;
		goto out;
	}

out:
	strbuf_release(&corrupt_err);
	return ret;
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
	size_t size;
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
	if (input_oi->source_infop)
		*input_oi->source_infop = *new_oi.source_infop;
	return ret;
}

enum odb_read_status odb_read_object_info_extended(struct object_database *odb,
						   const struct object_id *oid,
						   struct object_info *oi,
						   enum object_info_flags flags)
{
	enum odb_read_status ret;

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
			 size_t *sizep)
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
		       void *buf, size_t len, enum object_type type,
		       struct object_id *oid)
{
	hash_object_file(odb->repo->hash_algo, buf, len, type, oid);
	if (odb_has_object(odb, oid, 0))
		return 0;

	return odb_source_write_object(odb->inmemory_objects,
				       buf, len, type, oid, NULL, NULL, 0);
}

void *odb_read_object(struct object_database *odb,
		      const struct object_id *oid,
		      enum object_type *type,
		      size_t *size)
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
			     size_t *size,
			     struct object_id *actual_oid_return)
{
	enum object_type type;
	void *buffer;
	size_t isize;
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
		   enum odb_has_object_flags flags)
{
	unsigned object_info_flags = 0;

	if (!startup_info->have_repository)
		return 0;
	if (!(flags & ODB_HAS_OBJECT_RECHECK_PACKED))
		object_info_flags |= OBJECT_INFO_QUICK;
	if (!(flags & ODB_HAS_OBJECT_FETCH_PROMISOR))
		object_info_flags |= OBJECT_INFO_SKIP_FETCH_OBJECT;

	return odb_read_object_info_extended(odb, oid, NULL, object_info_flags) >= 0;
}

int odb_freshen_object(struct object_database *odb,
		       const struct object_id *oid)
{
	struct odb_source *source;
	for (source = odb->sources; source; source = source->next)
		if (odb_source_freshen_object(source, oid, NULL))
			return 1;
	return 0;
}

int odb_for_each_object_ext(struct object_database *odb,
			    const struct object_info *request,
			    odb_for_each_object_cb cb,
			    void *cb_data,
			    const struct odb_for_each_object_options *opts)
{
	int ret;

	for (struct odb_source *source = odb->sources; source; source = source->next) {
		if (opts->flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY && !source->local)
			continue;

		ret = odb_source_for_each_object(source, request, cb, cb_data, opts);
		if (ret)
			return ret;
	}

	return 0;
}

int odb_for_each_object(struct object_database *odb,
			const struct object_info *request,
			odb_for_each_object_cb cb,
			void *cb_data,
			enum odb_for_each_object_flags flags)
{
	struct odb_for_each_object_options opts = {
		.flags = flags,
	};
	return odb_for_each_object_ext(odb, request, cb, cb_data, &opts);
}

int odb_count_objects(struct object_database *odb,
		      enum odb_count_objects_flags flags,
		      unsigned long *out)
{
	struct odb_source *source;
	unsigned long count = 0;
	int ret;

	if (odb->object_count_valid && odb->object_count_flags == flags) {
		*out = odb->object_count;
		return 0;
	}

	for (source = odb->sources; source; source = source->next) {
		unsigned long c;

		ret = odb_source_count_objects(source, flags, &c);
		if (ret < 0)
			goto out;

		count += c;
	}

	odb->object_count = count;
	odb->object_count_valid = 1;
	odb->object_count_flags = flags;

	*out = count;
	ret = 0;

out:
	return ret;
}

/*
 * Return the slot of the most-significant bit set in "val". There are various
 * ways to do this quickly with fls() or __builtin_clzl(), but speed is
 * probably not a big deal here.
 */
static unsigned msb(unsigned long val)
{
	unsigned r = 0;
	while (val >>= 1)
		r++;
	return r;
}

int odb_find_abbrev_len(struct object_database *odb,
			const struct object_id *oid,
			int min_length,
			unsigned *out)
{
	const struct git_hash_algo *algo =
		oid->algo ? &hash_algos[oid->algo] : odb->repo->hash_algo;
	const unsigned hexsz = algo->hexsz;
	unsigned len;
	int ret;

	if (min_length < 0) {
		unsigned long count;

		if (odb_count_objects(odb, ODB_COUNT_OBJECTS_APPROXIMATE, &count) < 0)
			count = 0;

		/*
		 * Add one because the MSB only tells us the highest bit set,
		 * not including the value of all the _other_ bits (so "15"
		 * is only one off of 2^4, but the MSB is the 3rd bit.
		 */
		len = msb(count) + 1;
		/*
		 * We now know we have on the order of 2^len objects, which
		 * expects a collision at 2^(len/2). But we also care about hex
		 * chars, not bits, and there are 4 bits per hex. So all
		 * together we need to divide by 2 and round up.
		 */
		len = DIV_ROUND_UP(len, 2);
		/*
		 * For very small repos, we stick with our regular fallback.
		 */
		if (len < FALLBACK_DEFAULT_ABBREV)
			len = FALLBACK_DEFAULT_ABBREV;
	} else {
		len = min_length;
	}

	if (len >= hexsz || !len) {
		*out = hexsz;
		ret = 0;
		goto out;
	}

	for (struct odb_source *source = odb->sources; source; source = source->next) {
		ret = odb_source_find_abbrev_len(source, oid, len, &len);
		if (ret)
			goto out;
	}

	ret = 0;
	*out = len;

out:
	return ret;
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

int odb_write_object_ext(struct object_database *odb,
			 const void *buf, unsigned long len,
			 enum object_type type,
			 struct object_id *oid,
			 const struct object_id *compat_oid_in,
			 enum odb_write_object_flags flags)
{
	const struct git_hash_algo *compat = odb->repo->compat_hash_algo;
	struct object_id compat_oid, *compat_oid_p = NULL;

	hash_object_file(odb->repo->hash_algo, buf, len, type, oid);

	/*
	 * We can skip the write in case we already have the object available.
	 * In that case, we only freshen its mtime.
	 */
	if (odb_freshen_object(odb, oid))
		return 0;

	if (compat) {
		const struct git_hash_algo *algo = odb->repo->hash_algo;

		if (compat_oid_in) {
			oidcpy(&compat_oid, compat_oid_in);
		} else if (type == OBJ_BLOB) {
			hash_object_file(compat, buf, len, type, &compat_oid);
		} else {
			struct strbuf converted = STRBUF_INIT;
			convert_object_file(odb->repo, &converted, algo, compat,
					    buf, len, type, 0);
			hash_object_file(compat, converted.buf, converted.len,
					 type, &compat_oid);
			strbuf_release(&converted);
		}

		compat_oid_p = &compat_oid;
	}

	return odb_source_write_object(odb->sources, buf, len, type,
				       oid, compat_oid_p, NULL, flags);
}

int odb_write_object_stream(struct object_database *odb,
			    struct odb_stream *stream,
			    struct object_id *oid)
{
	return odb_source_write_object_stream(odb->sources, stream, oid);
}

int odb_optimize(struct object_database *odb,
		 const struct odb_optimize_options *opts)
{
	return odb_source_optimize(odb->sources, opts);
}

bool odb_optimize_required(struct object_database *odb,
			   const struct odb_optimize_options *opts)
{
	return odb_source_optimize_required(odb->sources, opts);
}

void odb_generate_pack_options_release(struct odb_generate_pack_options *opts)
{
	oid_array_clear(&opts->wants);
	oid_array_clear(&opts->haves);
	oid_array_clear(&opts->shallows);
}

int odb_generate_pack(struct object_database *odb,
		      struct odb_pack_generator **out,
		      const struct odb_generate_pack_options *opts)
{
	if (!odb->sources->generate_pack)
		return error(_("primary object source does not support generating packfiles"));
	return odb_source_generate_pack(odb->sources, out, opts);
}

int odb_pack_generator_finish(struct odb_pack_generator *generator)
{
	return generator->finish(generator);
}

struct object_database *odb_new(struct repository *repo,
				enum odb_new_flags flags)
{
	char *primary_source = NULL, *secondary_sources = NULL;
	struct object_database *o;

	CALLOC_ARRAY(o, 1);
	o->repo = repo;
	pthread_mutex_init(&o->replace_mutex, NULL);
	string_list_init_dup(&o->submodule_source_paths);
	hashmap_init(&o->source_by_path, odb_source_by_path_cmp, o, 0);
	o->source_paths_icase = -1;

	if (flags & ODB_NEW_HONOR_ENV) {
		primary_source = xstrdup_or_null(getenv(DB_ENVIRONMENT));
		secondary_sources = xstrdup_or_null(getenv(ALTERNATE_DB_ENVIRONMENT));
	}
	if (!primary_source)
		primary_source = xstrfmt("%s/objects", repo->commondir);

	o->sources = odb_source_new(o, primary_source, true);
	o->sources_tail = &o->sources->next;
	o->inmemory_objects = &odb_source_inmemory_new(o)->base;

	odb_prepare_alternates(o, secondary_sources);

	free(secondary_sources);
	free(primary_source);
	return o;
}

void odb_close(struct object_database *o)
{
	struct odb_source *source;
	for (source = o->sources; source; source = source->next)
		odb_source_close(source);
	close_commit_graph(o);
}

static void odb_free_sources(struct object_database *o)
{
	while (o->sources) {
		struct odb_source *next;

		next = o->sources->next;
		odb_source_free(o->sources);
		o->sources = next;
	}

	odb_source_free(o->inmemory_objects);
	o->inmemory_objects = NULL;

	hashmap_clear(&o->source_by_path);
}

void odb_free(struct object_database *o)
{
	if (!o)
		return;

	oidmap_clear(&o->replace_map, 1);
	pthread_mutex_destroy(&o->replace_mutex);

	odb_close(o);
	odb_free_sources(o);

	string_list_clear(&o->submodule_source_paths, 0);

	free(o);
}

void odb_prepare(struct object_database *o, enum odb_prepare_flags flags)
{
	struct odb_source *source;

	obj_read_lock();

	/*
	 * Reprepare alt odbs, in case the alternates file was modified
	 * during the course of this process. This only _adds_ odbs to
	 * the linked list, so existing odbs will continue to exist for
	 * the lifetime of the process. Consequently, we don't have to
	 * reprocess GIT_ALTERNATE_OBJECT_DIRECTORIES here.
	 */
	if (flags & ODB_PREPARE_FLUSH_CACHES) {
		odb_prepare_alternates(o, NULL);
		o->object_count_valid = 0;
	}

	for (source = o->sources; source; source = source->next)
		odb_source_prepare(source, flags);

	obj_read_unlock();
}

void odb_reprepare(struct object_database *o)
{
	odb_prepare(o, ODB_PREPARE_FLUSH_CACHES);
}
