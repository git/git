/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "convert.h"
#include "diff.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "ident.h"
#include "parse-options.h"
#include "userdiff.h"
#include "streaming.h"
#include "oid-array.h"
#include "packfile.h"
#include "object-file.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "replace-object.h"
#include "promisor-remote.h"
#include "mailmap.h"
#include "write-or-die.h"
#include "alias.h"
#include "remote.h"
#include "transport.h"

enum batch_mode {
	BATCH_MODE_CONTENTS,
	BATCH_MODE_INFO,
	BATCH_MODE_QUEUE_AND_DISPATCH,
};

struct batch_options {
	int enabled;
	int follow_symlinks;
	enum batch_mode batch_mode;
	int buffer_output;
	int all_objects;
	int unordered;
	int transform_mode; /* may be 'w' or 'c' for --filters or --textconv */
	char input_delim;
	char output_delim;
	const char *format;
	int use_remote_info;
};

static const char *force_path;
static struct object_info *remote_object_info;
static struct oid_array object_info_oids = OID_ARRAY_INIT;

static struct string_list mailmap = STRING_LIST_INIT_NODUP;
static int use_mailmap;

static char *replace_idents_using_mailmap(char *, size_t *);

static char *replace_idents_using_mailmap(char *object_buf, size_t *size)
{
	struct strbuf sb = STRBUF_INIT;
	const char *headers[] = { "author ", "committer ", "tagger ", NULL };

	strbuf_attach(&sb, object_buf, *size, *size + 1);
	apply_mailmap_to_header(&sb, headers, &mailmap);
	*size = sb.len;
	return strbuf_detach(&sb, NULL);
}

static int filter_object(const char *path, unsigned mode,
			 const struct object_id *oid,
			 char **buf, unsigned long *size)
{
	enum object_type type;

	*buf = repo_read_object_file(the_repository, oid, &type, size);
	if (!*buf)
		return error(_("cannot read object %s '%s'"),
			     oid_to_hex(oid), path);
	if ((type == OBJ_BLOB) && S_ISREG(mode)) {
		struct strbuf strbuf = STRBUF_INIT;
		struct checkout_metadata meta;

		init_checkout_metadata(&meta, NULL, NULL, oid);
		if (convert_to_working_tree(the_repository->index, path, *buf, *size, &strbuf, &meta)) {
			free(*buf);
			*size = strbuf.len;
			*buf = strbuf_detach(&strbuf, NULL);
		}
	}

	return 0;
}

static int stream_blob(const struct object_id *oid)
{
	if (stream_blob_to_fd(1, oid, NULL, 0))
		die("unable to stream %s to stdout", oid_to_hex(oid));
	return 0;
}

static int cat_one_file(int opt, const char *exp_type, const char *obj_name,
			int unknown_type)
{
	int ret;
	struct object_id oid;
	enum object_type type;
	char *buf;
	unsigned long size;
	struct object_context obj_context = {0};
	struct object_info oi = OBJECT_INFO_INIT;
	struct strbuf sb = STRBUF_INIT;
	unsigned flags = OBJECT_INFO_LOOKUP_REPLACE;
	unsigned get_oid_flags =
		GET_OID_RECORD_PATH |
		GET_OID_ONLY_TO_DIE |
		GET_OID_HASH_ANY;
	const char *path = force_path;
	const int opt_cw = (opt == 'c' || opt == 'w');
	if (!path && opt_cw)
		get_oid_flags |= GET_OID_REQUIRE_PATH;

	if (unknown_type)
		flags |= OBJECT_INFO_ALLOW_UNKNOWN_TYPE;

	if (get_oid_with_context(the_repository, obj_name, get_oid_flags, &oid,
				 &obj_context))
		die("Not a valid object name %s", obj_name);

	if (!path)
		path = obj_context.path;
	if (obj_context.mode == S_IFINVALID)
		obj_context.mode = 0100644;

	buf = NULL;
	switch (opt) {
	case 't':
		oi.type_name = &sb;
		if (oid_object_info_extended(the_repository, &oid, &oi, flags) < 0)
			die("git cat-file: could not get object info");
		if (sb.len) {
			printf("%s\n", sb.buf);
			strbuf_release(&sb);
			ret = 0;
			goto cleanup;
		}
		break;

	case 's':
		oi.sizep = &size;

		if (use_mailmap) {
			oi.typep = &type;
			oi.contentp = (void**)&buf;
		}

		if (oid_object_info_extended(the_repository, &oid, &oi, flags) < 0)
			die("git cat-file: could not get object info");

		if (use_mailmap && (type == OBJ_COMMIT || type == OBJ_TAG)) {
			size_t s = size;
			buf = replace_idents_using_mailmap(buf, &s);
			size = cast_size_t_to_ulong(s);
		}

		printf("%"PRIuMAX"\n", (uintmax_t)size);
		ret = 0;
		goto cleanup;

	case 'e':
		ret = !repo_has_object_file(the_repository, &oid);
		goto cleanup;

	case 'w':

		if (filter_object(path, obj_context.mode,
				  &oid, &buf, &size)) {
			ret = -1;
			goto cleanup;
		}
		break;

	case 'c':
		if (textconv_object(the_repository, path, obj_context.mode,
				    &oid, 1, &buf, &size))
			break;
		/* else fallthrough */

	case 'p':
		type = oid_object_info(the_repository, &oid, NULL);
		if (type < 0)
			die("Not a valid object name %s", obj_name);

		/* custom pretty-print here */
		if (type == OBJ_TREE) {
			const char *ls_args[3] = { NULL };
			ls_args[0] =  "ls-tree";
			ls_args[1] =  obj_name;
			ret = cmd_ls_tree(2, ls_args, NULL, the_repository);
			goto cleanup;
		}

		if (type == OBJ_BLOB) {
			ret = stream_blob(&oid);
			goto cleanup;
		}
		buf = repo_read_object_file(the_repository, &oid, &type,
					    &size);
		if (!buf)
			die("Cannot read object %s", obj_name);

		if (use_mailmap) {
			size_t s = size;
			buf = replace_idents_using_mailmap(buf, &s);
			size = cast_size_t_to_ulong(s);
		}

		/* otherwise just spit out the data */
		break;

	case 0:
	{
		enum object_type exp_type_id = type_from_string(exp_type);

		if (exp_type_id == OBJ_BLOB) {
			struct object_id blob_oid;
			if (oid_object_info(the_repository, &oid, NULL) == OBJ_TAG) {
				char *buffer = repo_read_object_file(the_repository,
								     &oid,
								     &type,
								     &size);
				const char *target;

				if (!buffer)
					die(_("unable to read %s"), oid_to_hex(&oid));

				if (!skip_prefix(buffer, "object ", &target) ||
				    get_oid_hex_algop(target, &blob_oid,
						      &hash_algos[oid.algo]))
					die("%s not a valid tag", oid_to_hex(&oid));
				free(buffer);
			} else
				oidcpy(&blob_oid, &oid);

			if (oid_object_info(the_repository, &blob_oid, NULL) == OBJ_BLOB) {
				ret = stream_blob(&blob_oid);
				goto cleanup;
			}
			/*
			 * we attempted to dereference a tag to a blob
			 * and failed; there may be new dereference
			 * mechanisms this code is not aware of.
			 * fall-back to the usual case.
			 */
		}
		buf = read_object_with_reference(the_repository, &oid,
						 exp_type_id, &size, NULL);

		if (use_mailmap) {
			size_t s = size;
			buf = replace_idents_using_mailmap(buf, &s);
			size = cast_size_t_to_ulong(s);
		}
		break;
	}
	default:
		die("git cat-file: unknown option: %s", exp_type);
	}

	if (!buf)
		die("git cat-file %s: bad file", obj_name);

	write_or_die(1, buf, size);
	ret = 0;
cleanup:
	free(buf);
	object_context_release(&obj_context);
	return ret;
}

struct expand_data {
	struct object_id oid;
	enum object_type type;
	unsigned long size;
	off_t disk_size;
	const char *rest;
	struct object_id delta_base_oid;
	struct git_iovec iov[3];

	/*
	 * If mark_query is true, we do not expand anything, but rather
	 * just mark the object_info with items we wish to query.
	 */
	int mark_query;

	/*
	 * Whether to split the input on whitespace before feeding it to
	 * get_sha1; this is decided during the mark_query phase based on
	 * whether we have a %(rest) token in our format.
	 */
	int split_on_whitespace;

	/*
	 * After a mark_query run, this object_info is set up to be
	 * passed to oid_object_info_extended. It will point to the data
	 * elements above, so you can retrieve the response from there.
	 */
	struct object_info info;

	/*
	 * This flag will be true if the requested batch format and options
	 * don't require us to call oid_object_info, which can then be
	 * optimized out.
	 */
	unsigned skip_object_info : 1;
};

static int is_atom(const char *atom, const char *s, int slen)
{
	int alen = strlen(atom);
	return alen == slen && !memcmp(atom, s, alen);
}

static int expand_atom(struct strbuf *sb, const char *atom, int len,
		       struct expand_data *data)
{
	if (is_atom("objectname", atom, len)) {
		if (!data->mark_query)
			strbuf_addstr(sb, oid_to_hex(&data->oid));
	} else if (is_atom("objecttype", atom, len)) {
		if (data->mark_query)
			data->info.typep = &data->type;
		else
			strbuf_addstr(sb, type_name(data->type));
	} else if (is_atom("objectsize", atom, len)) {
		if (data->mark_query)
			data->info.sizep = &data->size;
		else
			strbuf_addf(sb, "%"PRIuMAX , (uintmax_t)data->size);
	} else if (is_atom("objectsize:disk", atom, len)) {
		if (data->mark_query)
			data->info.disk_sizep = &data->disk_size;
		else
			strbuf_addf(sb, "%"PRIuMAX, (uintmax_t)data->disk_size);
	} else if (is_atom("rest", atom, len)) {
		if (data->mark_query)
			data->split_on_whitespace = 1;
		else if (data->rest)
			strbuf_addstr(sb, data->rest);
	} else if (is_atom("deltabase", atom, len)) {
		if (data->mark_query)
			data->info.delta_base_oid = &data->delta_base_oid;
		else
			strbuf_addstr(sb,
				      oid_to_hex(&data->delta_base_oid));
	} else
		return 0;
	return 1;
}

static void expand_format(struct strbuf *sb, const char *start,
			  struct expand_data *data)
{
	while (strbuf_expand_step(sb, &start)) {
		const char *end;

		if (skip_prefix(start, "%", &start) || *start != '(')
			strbuf_addch(sb, '%');
		else if ((end = strchr(start + 1, ')')) &&
			 expand_atom(sb, start + 1, end - start - 1, data))
			start = end + 1;
		else
			strbuf_expand_bad_format(start, "cat-file");
	}
}

static void batch_write(struct batch_options *opt, const void *data, size_t len)
{
	if (opt->buffer_output) {
		if (fwrite(data, 1, len, stdout) != len)
			die_errno("unable to write to stdout");
	} else
		write_or_die(1, data, len);
}

static void batch_writev(struct batch_options *opt, struct expand_data *data,
			const struct strbuf *hdr, size_t size)
{
	data->iov[0].iov_base = hdr->buf;
	data->iov[0].iov_len = hdr->len;
	data->iov[1].iov_len = size;

	/*
	 * Copying a (8|16)-byte iovec for a single byte is gross, but my
	 * attempt to stuff output_delim into the trailing NUL byte of
	 * iov[1].iov_base (and restoring it after writev(2) for the
	 * OI_DBCACHED case) to drop iovcnt from 3->2 wasn't faster.
	 */
	data->iov[2].iov_base = &opt->output_delim;
	data->iov[2].iov_len = 1;

	if (opt->buffer_output)
		fwritev_or_die(stdout, data->iov, 3);
	else
		writev_or_die(1, data->iov, 3);

	/* writev_or_die may move iov[1].iov_base, so it's invalid */
	data->iov[1].iov_base = NULL;
}

static void print_object_or_die(struct batch_options *opt,
				struct expand_data *data, struct strbuf *hdr)
{
	const struct object_id *oid = &data->oid;

	assert(data->info.typep);

	if (data->iov[1].iov_base) {
		void *content = data->iov[1].iov_base;
		unsigned long size = data->size;

		if (use_mailmap && (data->type == OBJ_COMMIT ||
					data->type == OBJ_TAG)) {
			size_t s = size;

			if (data->info.whence == OI_DBCACHED) {
				content = xmemdupz(content, s);
				data->info.whence = OI_PACKED;
			}

			content = replace_idents_using_mailmap(content, &s);
			data->iov[1].iov_base = content;
			size = cast_size_t_to_ulong(s);
		}
		batch_writev(opt, data, hdr, size);
		switch (data->info.whence) {
		case OI_CACHED:
			/*
			 * only blame uses OI_CACHED atm, so it's unlikely
			 * we'll ever hit this path
			 */
			BUG("TODO OI_CACHED support not done");
		case OI_LOOSE:
		case OI_PACKED:
			free(content);
			break;
		case OI_DBCACHED:
			unlock_delta_base_cache();
		}
	} else {
		assert(data->type == OBJ_BLOB);
		if (opt->transform_mode) {
			char *contents;
			unsigned long size;

			if (!data->rest)
				die("missing path for '%s'", oid_to_hex(oid));

			if (opt->transform_mode == 'w') {
				if (filter_object(data->rest, 0100644, oid,
						  &contents, &size))
					die("could not convert '%s' %s",
					    oid_to_hex(oid), data->rest);
			} else if (opt->transform_mode == 'c') {
				enum object_type type;
				if (!textconv_object(the_repository,
						     data->rest, 0100644, oid,
						     1, &contents, &size))
					contents = repo_read_object_file(the_repository,
									 oid,
									 &type,
									 &size);
				if (!contents)
					die("could not convert '%s' %s",
					    oid_to_hex(oid), data->rest);
			} else
				BUG("invalid transform_mode: %c", opt->transform_mode);
			data->iov[1].iov_base = contents;
			batch_writev(opt, data, hdr, size);
			free(contents);
		} else {
			batch_write(opt, hdr->buf, hdr->len);
			if (opt->buffer_output)
				fflush(stdout);
			stream_blob(oid);
			batch_write(opt, &opt->output_delim, 1);
		}
	}
}

static void print_default_format(struct strbuf *scratch, struct expand_data *data,
				 struct batch_options *opt)
{
	strbuf_addf(scratch, "%s %s %"PRIuMAX"%c", oid_to_hex(&data->oid),
		    type_name(data->type),
		    (uintmax_t)data->size, opt->output_delim);
}

/*
 * If "pack" is non-NULL, then "offset" is the byte offset within the pack from
 * which the object may be accessed (though note that we may also rely on
 * data->oid, too). If "pack" is NULL, then offset is ignored.
 */
static void batch_object_write(const char *obj_name,
			       struct strbuf *scratch,
			       struct batch_options *opt,
			       struct expand_data *data,
			       struct packed_git *pack,
			       off_t offset)
{
	if (!data->skip_object_info) {
		int ret;

		if (use_mailmap)
			data->info.typep = &data->type;

		if (pack)
			ret = packed_object_info(the_repository, pack, offset,
						 &data->info);
		else
			ret = oid_object_info_extended(the_repository,
						       &data->oid, &data->info,
						       OBJECT_INFO_LOOKUP_REPLACE);
		if (ret < 0) {
			printf("%s missing%c",
			       obj_name ? obj_name : oid_to_hex(&data->oid), opt->output_delim);
			fflush(stdout);
			return;
		}

		if (use_mailmap && (data->type == OBJ_COMMIT || data->type == OBJ_TAG)) {
			size_t s = data->size;
			char *buf = NULL;

			buf = repo_read_object_file(the_repository, &data->oid, &data->type,
						    &data->size);
			if (!buf)
				die(_("unable to read %s"), oid_to_hex(&data->oid));
			buf = replace_idents_using_mailmap(buf, &s);
			data->size = cast_size_t_to_ulong(s);

			free(buf);
		}
	}

	strbuf_reset(scratch);

	if (!opt->format) {
		print_default_format(scratch, data, opt);
	} else {
		expand_format(scratch, opt->format, data);
		strbuf_addch(scratch, opt->output_delim);
	}

	if (opt->batch_mode == BATCH_MODE_CONTENTS)
		print_object_or_die(opt, data, scratch);
	else
		batch_write(opt, scratch->buf, scratch->len);
}

static void batch_one_object(const char *obj_name,
			     struct strbuf *scratch,
			     struct batch_options *opt,
			     struct expand_data *data)
{
	struct object_context ctx = {0};
	int flags =
		GET_OID_HASH_ANY |
		(opt->follow_symlinks ? GET_OID_FOLLOW_SYMLINKS : 0);
	enum get_oid_result result;

	result = get_oid_with_context(the_repository, obj_name,
								  flags, &data->oid, &ctx);
	if (result != FOUND) {
		switch (result) {
		case MISSING_OBJECT:
			printf("%s missing%c", obj_name, opt->output_delim);
			break;
		case SHORT_NAME_AMBIGUOUS:
			printf("%s ambiguous%c", obj_name, opt->output_delim);
			break;
		case DANGLING_SYMLINK:
			printf("dangling %"PRIuMAX"%c%s%c",
			       (uintmax_t)strlen(obj_name),
			       opt->output_delim, obj_name, opt->output_delim);
			break;
		case SYMLINK_LOOP:
			printf("loop %"PRIuMAX"%c%s%c",
			       (uintmax_t)strlen(obj_name),
			       opt->output_delim, obj_name, opt->output_delim);
			break;
		case NOT_DIR:
			printf("notdir %"PRIuMAX"%c%s%c",
			       (uintmax_t)strlen(obj_name),
			       opt->output_delim, obj_name, opt->output_delim);
			break;
		default:
			BUG("unknown get_sha1_with_context result %d\n",
			       result);
			break;
		}
		fflush(stdout);

		goto out;
	}

	if (ctx.mode == 0) {
		printf("symlink %"PRIuMAX"%c%s%c",
		       (uintmax_t)ctx.symlink_path.len,
		       opt->output_delim, ctx.symlink_path.buf, opt->output_delim);
		fflush(stdout);
		goto out;
	}

	batch_object_write(obj_name, scratch, opt, data, NULL, 0);

out:
	object_context_release(&ctx);
}

static int get_remote_info(struct batch_options *opt, int argc, const char **argv)
{
	int retval = 0;
	struct remote *remote = NULL;
	struct object_id oid;
	struct string_list object_info_options = STRING_LIST_INIT_NODUP;
	static struct transport *gtransport;

	/*
	 * Change the format to "%(objectname) %(objectsize)" when
	 * remote-object-info command is used. Once we start supporting objecttype
	 * the default format should change to DEFAULT_FORMAT
	*/
	if (!opt->format)
		opt->format = "%(objectname) %(objectsize)";

	remote = remote_get(argv[0]);
	if (!remote)
		die(_("must supply valid remote when using remote-object-info"));

	oid_array_clear(&object_info_oids);
	for (size_t i = 1; i < argc; i++) {
		if (get_oid_hex(argv[i], &oid))
			die(_("Not a valid object name %s"), argv[i]);
		oid_array_append(&object_info_oids, &oid);
	}

	gtransport = transport_get(remote, NULL);
	if (gtransport->smart_options) {
		CALLOC_ARRAY(remote_object_info, object_info_oids.nr);
		gtransport->smart_options->object_info = 1;
		gtransport->smart_options->object_info_oids = &object_info_oids;

		/* 'objectsize' is the only option currently supported */
		if (!strstr(opt->format, "%(objectsize)"))
			die(_("%s is currently not supported with remote-object-info"), opt->format);

		string_list_append(&object_info_options, "size");

		if (object_info_options.nr > 0) {
			gtransport->smart_options->object_info_options = &object_info_options;
			gtransport->smart_options->object_info_data = remote_object_info;
			retval = transport_fetch_refs(gtransport, NULL);
		}
	} else {
		retval = -1;
	}

	string_list_clear(&object_info_options, 0);
	transport_disconnect(gtransport);
	return retval;
}

struct object_cb_data {
	struct batch_options *opt;
	struct expand_data *expand;
	struct oidset *seen;
	struct strbuf *scratch;
};

static int batch_object_cb(const struct object_id *oid, void *vdata)
{
	struct object_cb_data *data = vdata;
	oidcpy(&data->expand->oid, oid);
	batch_object_write(NULL, data->scratch, data->opt, data->expand,
			   NULL, 0);
	return 0;
}

static int collect_loose_object(const struct object_id *oid,
				const char *path UNUSED,
				void *data)
{
	oid_array_append(data, oid);
	return 0;
}

static int collect_packed_object(const struct object_id *oid,
				 struct packed_git *pack UNUSED,
				 uint32_t pos UNUSED,
				 void *data)
{
	oid_array_append(data, oid);
	return 0;
}

static int batch_unordered_object(const struct object_id *oid,
				  struct packed_git *pack, off_t offset,
				  void *vdata)
{
	struct object_cb_data *data = vdata;

	if (oidset_insert(data->seen, oid))
		return 0;

	oidcpy(&data->expand->oid, oid);
	batch_object_write(NULL, data->scratch, data->opt, data->expand,
			   pack, offset);
	return 0;
}

static int batch_unordered_loose(const struct object_id *oid,
				 const char *path UNUSED,
				 void *data)
{
	return batch_unordered_object(oid, NULL, 0, data);
}

static int batch_unordered_packed(const struct object_id *oid,
				  struct packed_git *pack,
				  uint32_t pos,
				  void *data)
{
	return batch_unordered_object(oid, pack,
				      nth_packed_object_offset(pack, pos),
				      data);
}

typedef void (*parse_cmd_fn_t)(struct batch_options *, const char *,
			       struct strbuf *, struct expand_data *);

struct queued_cmd {
	parse_cmd_fn_t fn;
	char *line;
};

static void parse_cmd_contents(struct batch_options *opt,
			     const char *line,
			     struct strbuf *output,
			     struct expand_data *data)
{
	opt->batch_mode = BATCH_MODE_CONTENTS;
	data->info.contentp = &data->iov[1].iov_base;
	batch_one_object(line, output, opt, data);
}

static void parse_cmd_info(struct batch_options *opt,
			   const char *line,
			   struct strbuf *output,
			   struct expand_data *data)
{
	opt->batch_mode = BATCH_MODE_INFO;
	data->info.contentp = NULL;
	batch_one_object(line, output, opt, data);
}

static void parse_cmd_remote_object_info(struct batch_options *opt,
			   const char *line,
			   struct strbuf *output,
			   struct expand_data *data)
{
	int count;
	const char **argv;

	char *line_to_split = xstrdup_or_null(line);
	count = split_cmdline(line_to_split, &argv);
	if (get_remote_info(opt, count, argv))
		goto cleanup;

	opt->use_remote_info = 1;
	data->skip_object_info = 1;
	for (size_t i = 0; i < object_info_oids.nr; i++) {

		data->oid = object_info_oids.oid[i];

		if (remote_object_info[i].sizep) {
			data->size = *remote_object_info[i].sizep;
		} else {
			/*
			 * When reaching here, it means remote-object-info can't retrieve
			 * information from server without downloading them, and the objects
			 * have been fetched to client already.
			 * Print the information using the logic for local objects.
			 */
			data->skip_object_info = 0;
		}

		opt->batch_mode = BATCH_MODE_INFO;
		batch_object_write(argv[i+1], output, opt, data, NULL, 0);

	}
	opt->use_remote_info = 0;
	data->skip_object_info = 0;

cleanup:
	for (size_t i = 0; i < object_info_oids.nr; i++)
		free_object_info_contents(&remote_object_info[i]);
	free(line_to_split);
	free(argv);
	free(remote_object_info);
}

static void dispatch_calls(struct batch_options *opt,
		struct strbuf *output,
		struct expand_data *data,
		struct queued_cmd *cmd,
		int nr)
{
	if (!opt->buffer_output)
		die(_("flush is only for --buffer mode"));

	for (size_t i = 0; i < nr; i++)
		cmd[i].fn(opt, cmd[i].line, output, data);

	fflush(stdout);
}

static void free_cmds(struct queued_cmd *cmd, size_t *nr)
{
	for (size_t i = 0; i < *nr; i++)
		FREE_AND_NULL(cmd[i].line);

	*nr = 0;
}


static const struct parse_cmd {
	const char *name;
	parse_cmd_fn_t fn;
	unsigned takes_args;
} commands[] = {
	{ "contents", parse_cmd_contents, 1},
	{ "info", parse_cmd_info, 1},
	{ "remote-object-info", parse_cmd_remote_object_info, 1},
	{ "flush", NULL, 0},
};

static void batch_objects_command(struct batch_options *opt,
				    struct strbuf *output,
				    struct expand_data *data)
{
	struct strbuf input = STRBUF_INIT;
	struct queued_cmd *queued_cmd = NULL;
	size_t alloc = 0, nr = 0;

	while (strbuf_getdelim_strip_crlf(&input, stdin, opt->input_delim) != EOF) {
		const struct parse_cmd *cmd = NULL;
		const char *p = NULL, *cmd_end;
		struct queued_cmd call = {0};

		if (!input.len)
			die(_("empty command in input"));
		if (isspace(*input.buf))
			die(_("whitespace before command: '%s'"), input.buf);

		for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
			if (!skip_prefix(input.buf, commands[i].name, &cmd_end))
				continue;

			cmd = &commands[i];
			if (cmd->takes_args) {
				if (*cmd_end != ' ')
					die(_("%s requires arguments"),
					    commands[i].name);

				p = cmd_end + 1;
			} else if (*cmd_end) {
				die(_("%s takes no arguments"),
				    commands[i].name);
			}

			break;
		}

		if (!cmd)
			die(_("unknown command: '%s'"), input.buf);

		if (!strcmp(cmd->name, "flush")) {
			dispatch_calls(opt, output, data, queued_cmd, nr);
			free_cmds(queued_cmd, &nr);
		} else if (!opt->buffer_output) {
			cmd->fn(opt, p, output, data);
		} else {
			ALLOC_GROW(queued_cmd, nr + 1, alloc);
			call.fn = cmd->fn;
			call.line = xstrdup_or_null(p);
			queued_cmd[nr++] = call;
		}
	}

	if (opt->buffer_output &&
	    nr &&
	    !git_env_bool("GIT_TEST_CAT_FILE_NO_FLUSH_ON_EXIT", 0)) {
		dispatch_calls(opt, output, data, queued_cmd, nr);
		free_cmds(queued_cmd, &nr);
	}

	free_cmds(queued_cmd, &nr);
	free(queued_cmd);
	strbuf_release(&input);
}

#define DEFAULT_FORMAT "%(objectname) %(objecttype) %(objectsize)"

static int batch_objects(struct batch_options *opt)
{
	struct strbuf input = STRBUF_INIT;
	struct strbuf output = STRBUF_INIT;
	struct expand_data data;
	int save_warning;
	int retval = 0;

	/*
	 * Expand once with our special mark_query flag, which will prime the
	 * object_info to be handed to oid_object_info_extended for each
	 * object.
	 */
	memset(&data, 0, sizeof(data));
	data.mark_query = 1;
	expand_format(&output,
		      opt->format ? opt->format : DEFAULT_FORMAT,
		      &data);
	data.mark_query = 0;
	strbuf_release(&output);
	if (opt->transform_mode)
		data.split_on_whitespace = 1;

	if (opt->format && !strcmp(opt->format, DEFAULT_FORMAT))
		opt->format = NULL;
	/*
	 * If we are printing out the object, then always fill in the type,
	 * since we will want to decide whether or not to stream.
	 *
	 * Likewise, grab the content in the initial request if it's small
	 * and we're not planning to filter it.
	 */
	if ((opt->batch_mode == BATCH_MODE_CONTENTS) ||
			(opt->batch_mode == BATCH_MODE_QUEUE_AND_DISPATCH)) {
		data.info.typep = &data.type;
		if (!opt->transform_mode) {
			data.info.sizep = &data.size;
			data.info.contentp = &data.iov[1].iov_base;
			data.info.content_limit = big_file_threshold;
			data.info.direct_cache = 1;
		}
	}

	if (opt->all_objects) {
		struct object_cb_data cb;
		struct object_info empty = OBJECT_INFO_INIT;

		if (!memcmp(&data.info, &empty, sizeof(empty)))
			data.skip_object_info = 1;

		if (repo_has_promisor_remote(the_repository))
			warning("This repository uses promisor remotes. Some objects may not be loaded.");

		disable_replace_refs();

		cb.opt = opt;
		cb.expand = &data;
		cb.scratch = &output;

		if (opt->unordered) {
			struct oidset seen = OIDSET_INIT;

			cb.seen = &seen;

			for_each_loose_object(batch_unordered_loose, &cb, 0);
			for_each_packed_object(batch_unordered_packed, &cb,
					       FOR_EACH_OBJECT_PACK_ORDER);

			oidset_clear(&seen);
		} else {
			struct oid_array sa = OID_ARRAY_INIT;

			for_each_loose_object(collect_loose_object, &sa, 0);
			for_each_packed_object(collect_packed_object, &sa, 0);

			oid_array_for_each_unique(&sa, batch_object_cb, &cb);

			oid_array_clear(&sa);
		}

		strbuf_release(&output);
		return 0;
	}

	/*
	 * We are going to call get_sha1 on a potentially very large number of
	 * objects. In most large cases, these will be actual object sha1s. The
	 * cost to double-check that each one is not also a ref (just so we can
	 * warn) ends up dwarfing the actual cost of the object lookups
	 * themselves. We can work around it by just turning off the warning.
	 */
	save_warning = warn_on_object_refname_ambiguity;
	warn_on_object_refname_ambiguity = 0;

	if (opt->batch_mode == BATCH_MODE_QUEUE_AND_DISPATCH) {
		batch_objects_command(opt, &output, &data);
		goto cleanup;
	}

	while (strbuf_getdelim_strip_crlf(&input, stdin, opt->input_delim) != EOF) {
		if (data.split_on_whitespace) {
			/*
			 * Split at first whitespace, tying off the beginning
			 * of the string and saving the remainder (or NULL) in
			 * data.rest.
			 */
			char *p = strpbrk(input.buf, " \t");
			if (p) {
				while (*p && strchr(" \t", *p))
					*p++ = '\0';
			}
			data.rest = p;
		}

		batch_one_object(input.buf, &output, opt, &data);
	}

 cleanup:
	strbuf_release(&input);
	strbuf_release(&output);
	warn_on_object_refname_ambiguity = save_warning;
	return retval;
}

static int git_cat_file_config(const char *var, const char *value,
			       const struct config_context *ctx, void *cb)
{
	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, ctx, cb);
}

static int batch_option_callback(const struct option *opt,
				 const char *arg,
				 int unset)
{
	struct batch_options *bo = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (bo->enabled) {
		return error(_("only one batch option may be specified"));
	}

	bo->enabled = 1;

	if (!strcmp(opt->long_name, "batch"))
		bo->batch_mode = BATCH_MODE_CONTENTS;
	else if (!strcmp(opt->long_name, "batch-check"))
		bo->batch_mode = BATCH_MODE_INFO;
	else if (!strcmp(opt->long_name, "batch-command"))
		bo->batch_mode = BATCH_MODE_QUEUE_AND_DISPATCH;
	else
		BUG("%s given to batch-option-callback", opt->long_name);

	bo->format = arg;

	return 0;
}

int cmd_cat_file(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo UNUSED)
{
	int opt = 0;
	int opt_cw = 0;
	int opt_epts = 0;
	const char *exp_type = NULL, *obj_name = NULL;
	struct batch_options batch = {0};
	int unknown_type = 0;
	int input_nul_terminated = 0;
	int nul_terminated = 0;

	const char * const usage[] = {
		N_("git cat-file <type> <object>"),
		N_("git cat-file (-e | -p) <object>"),
		N_("git cat-file (-t | -s) [--allow-unknown-type] <object>"),
		N_("git cat-file (--textconv | --filters)\n"
		   "             [<rev>:<path|tree-ish> | --path=<path|tree-ish> <rev>]"),
		N_("git cat-file (--batch | --batch-check | --batch-command) [--batch-all-objects]\n"
		   "             [--buffer] [--follow-symlinks] [--unordered]\n"
		   "             [--textconv | --filters] [-Z]"),
		NULL
	};
	const struct option options[] = {
		/* Simple queries */
		OPT_GROUP(N_("Check object existence or emit object contents")),
		OPT_CMDMODE('e', NULL, &opt,
			    N_("check if <object> exists"), 'e'),
		OPT_CMDMODE('p', NULL, &opt, N_("pretty-print <object> content"), 'p'),

		OPT_GROUP(N_("Emit [broken] object attributes")),
		OPT_CMDMODE('t', NULL, &opt, N_("show object type (one of 'blob', 'tree', 'commit', 'tag', ...)"), 't'),
		OPT_CMDMODE('s', NULL, &opt, N_("show object size"), 's'),
		OPT_BOOL(0, "allow-unknown-type", &unknown_type,
			  N_("allow -s and -t to work with broken/corrupt objects")),
		OPT_BOOL(0, "use-mailmap", &use_mailmap, N_("use mail map file")),
		OPT_ALIAS(0, "mailmap", "use-mailmap"),
		/* Batch mode */
		OPT_GROUP(N_("Batch objects requested on stdin (or --batch-all-objects)")),
		OPT_CALLBACK_F(0, "batch", &batch, N_("format"),
			N_("show full <object> or <rev> contents"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			batch_option_callback),
		OPT_CALLBACK_F(0, "batch-check", &batch, N_("format"),
			N_("like --batch, but don't emit <contents>"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			batch_option_callback),
		OPT_BOOL_F('z', NULL, &input_nul_terminated, N_("stdin is NUL-terminated"),
			PARSE_OPT_HIDDEN),
		OPT_BOOL('Z', NULL, &nul_terminated, N_("stdin and stdout is NUL-terminated")),
		OPT_CALLBACK_F(0, "batch-command", &batch, N_("format"),
			N_("read commands from stdin"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			batch_option_callback),
		OPT_CMDMODE(0, "batch-all-objects", &opt,
			    N_("with --batch[-check]: ignores stdin, batches all known objects"), 'b'),
		/* Batch-specific options */
		OPT_GROUP(N_("Change or optimize batch output")),
		OPT_BOOL(0, "buffer", &batch.buffer_output, N_("buffer --batch output")),
		OPT_BOOL(0, "follow-symlinks", &batch.follow_symlinks,
			 N_("follow in-tree symlinks")),
		OPT_BOOL(0, "unordered", &batch.unordered,
			 N_("do not order objects before emitting them")),
		/* Textconv options, stand-ole*/
		OPT_GROUP(N_("Emit object (blob or tree) with conversion or filter (stand-alone, or with batch)")),
		OPT_CMDMODE(0, "textconv", &opt,
			    N_("run textconv on object's content"), 'c'),
		OPT_CMDMODE(0, "filters", &opt,
			    N_("run filters on object's content"), 'w'),
		OPT_STRING(0, "path", &force_path, N_("blob|tree"),
			   N_("use a <path> for (--textconv | --filters); Not with 'batch'")),
		OPT_END()
	};

	git_config(git_cat_file_config, NULL);

	batch.buffer_output = -1;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	opt_cw = (opt == 'c' || opt == 'w');
	opt_epts = (opt == 'e' || opt == 'p' || opt == 't' || opt == 's');

	if (use_mailmap)
		read_mailmap(&mailmap);

	/* --batch-all-objects? */
	if (opt == 'b')
		batch.all_objects = 1;

	/* Option compatibility */
	if (force_path && !opt_cw)
		usage_msg_optf(_("'%s=<%s>' needs '%s' or '%s'"),
			       usage, options,
			       "--path", _("path|tree-ish"), "--filters",
			       "--textconv");

	/* Option compatibility with batch mode */
	if (batch.enabled)
		;
	else if (batch.follow_symlinks)
		usage_msg_optf(_("'%s' requires a batch mode"), usage, options,
			       "--follow-symlinks");
	else if (batch.buffer_output >= 0)
		usage_msg_optf(_("'%s' requires a batch mode"), usage, options,
			       "--buffer");
	else if (batch.all_objects)
		usage_msg_optf(_("'%s' requires a batch mode"), usage, options,
			       "--batch-all-objects");
	else if (input_nul_terminated)
		usage_msg_optf(_("'%s' requires a batch mode"), usage, options,
			       "-z");
	else if (nul_terminated)
		usage_msg_optf(_("'%s' requires a batch mode"), usage, options,
			       "-Z");

	batch.input_delim = batch.output_delim = '\n';
	if (input_nul_terminated)
		batch.input_delim = '\0';
	if (nul_terminated)
		batch.input_delim = batch.output_delim = '\0';

	/* Batch defaults */
	if (batch.buffer_output < 0)
		batch.buffer_output = batch.all_objects;

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	/* Return early if we're in batch mode? */
	if (batch.enabled) {
		if (opt_cw)
			batch.transform_mode = opt;
		else if (opt && opt != 'b')
			usage_msg_optf(_("'-%c' is incompatible with batch mode"),
				       usage, options, opt);
		else if (argc)
			usage_msg_opt(_("batch modes take no arguments"), usage,
				      options);

		return batch_objects(&batch);
	}

	if (opt) {
		if (!argc && opt == 'c')
			usage_msg_optf(_("<rev> required with '%s'"),
				       usage, options, "--textconv");
		else if (!argc && opt == 'w')
			usage_msg_optf(_("<rev> required with '%s'"),
				       usage, options, "--filters");
		else if (!argc && opt_epts)
			usage_msg_optf(_("<object> required with '-%c'"),
				       usage, options, opt);
		else if (!argc)
			BUG("argc==0 with opt=%c", opt);
		else if (argc == 1)
			obj_name = argv[0];
		else
			usage_msg_optf(_("unexpected argument: '%s'"),
				       usage, options, argv[1]);
	} else if (!argc) {
		usage_with_options(usage, options);
	} else if (argc != 2) {
		usage_msg_optf(_("only two arguments allowed in <type> <object> mode, not %d"),
			      usage, options, argc);
	} else if (argc) {
		exp_type = argv[0];
		obj_name = argv[1];
	}

	if (unknown_type && opt != 't' && opt != 's')
		die("git cat-file --allow-unknown-type: use with -s or -t");
	return cat_one_file(opt, exp_type, obj_name, unknown_type);
}
