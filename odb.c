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
#include "odb/source-files.h"
#include "odb/source-inmemory.h"
#include "path.h"
#include "promisor-remote.h"
#include "replace-object.h"
#include "run-command.h"
#include "setup.h"
#include "strbuf.h"
#include "strvec.h"
#include "submodule.h"
#include "tmp-objdir.h"
#include "trace2.h"
#include "write-or-die.h"

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
	struct repository *repo;
	odb_for_each_alternate_ref_fn *fn;
	void *payload;
};

static int refs_from_alternate_cb(struct odb_files_dir *alternate,
				  void *payload)
{
	struct strbuf path = STRBUF_INIT;
	size_t base_len;
	struct alternate_refs_data *cb = payload;

	if (!strbuf_realpath(&path, alternate->abspath, 0))
		goto out;
	if (!strbuf_strip_suffix(&path, "/objects"))
		goto out;
	base_len = path.len;

	/* Is this a git repository with refs? */
	strbuf_addstr(&path, "/refs");
	if (!is_directory(path.buf))
		goto out;
	strbuf_setlen(&path, base_len);

	read_alternate_refs(cb->repo, path.buf, cb->fn, cb->payload);

out:
	strbuf_release(&path);
	return 0;
}

void odb_for_each_alternate_ref(struct object_database *odb,
				odb_for_each_alternate_ref_fn cb, void *payload)
{
	struct alternate_refs_data data = {
		.fn = cb,
		.payload = payload,
		.repo = odb->repo,
	};
	odb_for_each_alternate(odb, refs_from_alternate_cb, &data);
}

int odb_for_each_alternate(struct object_database *odb,
			 odb_for_each_alternate_fn cb, void *payload)
{
	struct odb_source_files *files = odb_source_files_downcast(odb->source);
	int r = 0;

	for (struct odb_files_dir *dir = files->dirs->next; dir; dir = dir->next) {
		r = cb(dir, payload);
		if (r)
			break;
	}
	return r;
}

int odb_has_alternates(struct object_database *odb)
{
	if (odb->source->type != ODB_SOURCE_FILES)
		return 0;
	return !!odb_source_files_downcast(odb->source)->dirs->next;
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
		ret = odb_source_read_object_info(odb->source, real, oi, flags,
						  corrupt_err.len ? NULL : &corrupt_err);
		if (!ret)
			goto out;
		if (ret != ODB_READ_NOT_FOUND)
			corrupt = true;

		/*
		 * When the object hasn't been found we try a second read and
		 * tell the sources so. This may cause them to invalidate
		 * caches or reload on-disk state.
		 */
		if (!(flags & OBJECT_INFO_QUICK)) {
			ret = odb_source_read_object_info(odb->source, real, oi,
							  flags | OBJECT_INFO_SECOND_READ,
							  corrupt_err.len ? NULL : &corrupt_err);
			if (!ret)
				goto out;
			if (ret != ODB_READ_NOT_FOUND)
				corrupt = true;
		}

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
	return odb_source_freshen_object(odb->source, oid, NULL);
}

int odb_for_each_object_ext(struct object_database *odb,
			    const struct object_info *request,
			    odb_for_each_object_cb cb,
			    void *cb_data,
			    const struct odb_for_each_object_options *opts)
{
	return odb_source_for_each_object(odb->source, request, cb, cb_data, opts);
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
	unsigned long count = 0;
	int ret;

	if (odb->object_count_valid && odb->object_count_flags == flags) {
		*out = odb->object_count;
		return 0;
	}

	ret = odb_source_count_objects(odb->source, flags, &count);
	if (ret < 0)
		goto out;

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

	ret = odb_source_find_abbrev_len(odb->source, oid, len, &len);
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

	return odb_source_write_object(odb->source, buf, len, type,
				       oid, compat_oid_p, NULL, flags);
}

int odb_write_object_stream(struct object_database *odb,
			    struct odb_stream *stream,
			    struct object_id *oid)
{
	return odb_source_write_object_stream(odb->source, stream, oid);
}

int odb_optimize(struct object_database *odb,
		 const struct odb_optimize_options *opts)
{
	return odb_source_optimize(odb->source, opts);
}

bool odb_optimize_required(struct object_database *odb,
			   const struct odb_optimize_options *opts)
{
	return odb_source_optimize_required(odb->source, opts);
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
	if (!odb->source->generate_pack)
		return error(_("primary object source does not support generating packfiles"));
	return odb_source_generate_pack(odb->source, out, opts);
}

int odb_pack_generator_finish(struct odb_pack_generator *generator)
{
	return generator->finish(generator);
}

struct object_database *odb_new(struct repository *repo,
				enum odb_new_flags flags)
{
	struct object_database *o;

	CALLOC_ARRAY(o, 1);
	o->repo = repo;
	pthread_mutex_init(&o->replace_mutex, NULL);

	o->source = odb_source_new(o, flags);
	o->inmemory_objects = &odb_source_inmemory_new(o)->base;

	return o;
}

void odb_close(struct object_database *o)
{
	odb_source_close(o->source);
	close_commit_graph(o);
}

static void odb_free_sources(struct object_database *o)
{
	odb_source_free(o->source);
	odb_source_free(o->inmemory_objects);
	o->inmemory_objects = NULL;
}

void odb_free(struct object_database *o)
{
	if (!o)
		return;

	oidmap_clear(&o->replace_map, 1);
	pthread_mutex_destroy(&o->replace_mutex);

	odb_close(o);
	odb_free_sources(o);

	free(o);
}

void odb_prepare(struct object_database *o, enum odb_prepare_flags flags)
{
	obj_read_lock();

	if (flags & ODB_PREPARE_FLUSH_CACHES)
		o->object_count_valid = 0;

	odb_source_prepare(o->source, flags);

	obj_read_unlock();
}

void odb_reprepare(struct object_database *o)
{
	odb_prepare(o, ODB_PREPARE_FLUSH_CACHES);
}

int odb_fsck(struct object_database *odb, struct odb_fsck_options *options)
{
	return odb_source_fsck(odb->source, options);
}
