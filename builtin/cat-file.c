/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "diff.h"
#include "parse-options.h"
#include "userdiff.h"
#include "streaming.h"
#include "tree-walk.h"
#include "sha1-array.h"
#include "packfile.h"
#include "object-store.h"

struct batch_options {
	int enabled;
	int follow_symlinks;
	int print_contents;
	int buffer_output;
	int all_objects;
	int unordered;
	int cmdmode; /* may be 'w' or 'c' for --filters or --textconv */
	const char *format;
};

static const char *force_path;

static int filter_object(const char *path, unsigned mode,
			 const struct object_id *oid,
			 char **buf, unsigned long *size)
{
	enum object_type type;

	*buf = read_object_file(oid, &type, size);
	if (!*buf)
		return error(_("cannot read object %s '%s'"),
			     oid_to_hex(oid), path);
	if ((type == OBJ_BLOB) && S_ISREG(mode)) {
		struct strbuf strbuf = STRBUF_INIT;
		if (convert_to_working_tree(&the_index, path, *buf, *size, &strbuf)) {
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
	struct object_id oid;
	enum object_type type;
	char *buf;
	unsigned long size;
	struct object_context obj_context;
	struct object_info oi = OBJECT_INFO_INIT;
	struct strbuf sb = STRBUF_INIT;
	unsigned flags = OBJECT_INFO_LOOKUP_REPLACE;
	const char *path = force_path;

	if (unknown_type)
		flags |= OBJECT_INFO_ALLOW_UNKNOWN_TYPE;

	if (get_oid_with_context(obj_name, GET_OID_RECORD_PATH,
				 &oid, &obj_context))
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
			return 0;
		}
		break;

	case 's':
		oi.sizep = &size;
		if (oid_object_info_extended(the_repository, &oid, &oi, flags) < 0)
			die("git cat-file: could not get object info");
		printf("%"PRIuMAX"\n", (uintmax_t)size);
		return 0;

	case 'e':
		return !has_object_file(&oid);

	case 'w':
		if (!path)
			die("git cat-file --filters %s: <object> must be "
			    "<sha1:path>", obj_name);

		if (filter_object(path, obj_context.mode,
				  &oid, &buf, &size))
			return -1;
		break;

	case 'c':
		if (!path)
			die("git cat-file --textconv %s: <object> must be <sha1:path>",
			    obj_name);

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
			return cmd_ls_tree(2, ls_args, NULL);
		}

		if (type == OBJ_BLOB)
			return stream_blob(&oid);
		buf = read_object_file(&oid, &type, &size);
		if (!buf)
			die("Cannot read object %s", obj_name);

		/* otherwise just spit out the data */
		break;

	case 0:
		if (type_from_string(exp_type) == OBJ_BLOB) {
			struct object_id blob_oid;
			if (oid_object_info(the_repository, &oid, NULL) == OBJ_TAG) {
				char *buffer = read_object_file(&oid, &type,
								&size);
				const char *target;
				if (!skip_prefix(buffer, "object ", &target) ||
				    get_oid_hex(target, &blob_oid))
					die("%s not a valid tag", oid_to_hex(&oid));
				free(buffer);
			} else
				oidcpy(&blob_oid, &oid);

			if (oid_object_info(the_repository, &blob_oid, NULL) == OBJ_BLOB)
				return stream_blob(&blob_oid);
			/*
			 * we attempted to dereference a tag to a blob
			 * and failed; there may be new dereference
			 * mechanisms this code is not aware of.
			 * fall-back to the usual case.
			 */
		}
		buf = read_object_with_reference(&oid, exp_type, &size, NULL);
		break;

	default:
		die("git cat-file: unknown option: %s", exp_type);
	}

	if (!buf)
		die("git cat-file %s: bad file", obj_name);

	write_or_die(1, buf, size);
	free(buf);
	free(obj_context.path);
	return 0;
}

struct expand_data {
	struct object_id oid;
	enum object_type type;
	unsigned long size;
	off_t disk_size;
	const char *rest;
	struct object_id delta_base_oid;

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
	 * passed to sha1_object_info_extended. It will point to the data
	 * elements above, so you can retrieve the response from there.
	 */
	struct object_info info;

	/*
	 * This flag will be true if the requested batch format and options
	 * don't require us to call sha1_object_info, which can then be
	 * optimized out.
	 */
	unsigned skip_object_info : 1;
};

static int is_atom(const char *atom, const char *s, int slen)
{
	int alen = strlen(atom);
	return alen == slen && !memcmp(atom, s, alen);
}

static void expand_atom(struct strbuf *sb, const char *atom, int len,
			void *vdata)
{
	struct expand_data *data = vdata;

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
			data->info.delta_base_sha1 = data->delta_base_oid.hash;
		else
			strbuf_addstr(sb,
				      oid_to_hex(&data->delta_base_oid));
	} else
		die("unknown format element: %.*s", len, atom);
}

static size_t expand_format(struct strbuf *sb, const char *start, void *data)
{
	const char *end;

	if (*start != '(')
		return 0;
	end = strchr(start + 1, ')');
	if (!end)
		die("format element '%s' does not end in ')'", start);

	expand_atom(sb, start + 1, end - start - 1, data);

	return end - start + 1;
}

static void batch_write(struct batch_options *opt, const void *data, int len)
{
	if (opt->buffer_output) {
		if (fwrite(data, 1, len, stdout) != len)
			die_errno("unable to write to stdout");
	} else
		write_or_die(1, data, len);
}

static void print_object_or_die(struct batch_options *opt, struct expand_data *data)
{
	const struct object_id *oid = &data->oid;

	assert(data->info.typep);

	if (data->type == OBJ_BLOB) {
		if (opt->buffer_output)
			fflush(stdout);
		if (opt->cmdmode) {
			char *contents;
			unsigned long size;

			if (!data->rest)
				die("missing path for '%s'", oid_to_hex(oid));

			if (opt->cmdmode == 'w') {
				if (filter_object(data->rest, 0100644, oid,
						  &contents, &size))
					die("could not convert '%s' %s",
					    oid_to_hex(oid), data->rest);
			} else if (opt->cmdmode == 'c') {
				enum object_type type;
				if (!textconv_object(the_repository,
						     data->rest, 0100644, oid,
						     1, &contents, &size))
					contents = read_object_file(oid,
								    &type,
								    &size);
				if (!contents)
					die("could not convert '%s' %s",
					    oid_to_hex(oid), data->rest);
			} else
				BUG("invalid cmdmode: %c", opt->cmdmode);
			batch_write(opt, contents, size);
			free(contents);
		} else {
			stream_blob(oid);
		}
	}
	else {
		enum object_type type;
		unsigned long size;
		void *contents;

		contents = read_object_file(oid, &type, &size);
		if (!contents)
			die("object %s disappeared", oid_to_hex(oid));
		if (type != data->type)
			die("object %s changed type!?", oid_to_hex(oid));
		if (data->info.sizep && size != data->size)
			die("object %s changed size!?", oid_to_hex(oid));

		batch_write(opt, contents, size);
		free(contents);
	}
}

static void batch_object_write(const char *obj_name,
			       struct strbuf *scratch,
			       struct batch_options *opt,
			       struct expand_data *data)
{
	if (!data->skip_object_info &&
	    oid_object_info_extended(the_repository, &data->oid, &data->info,
				     OBJECT_INFO_LOOKUP_REPLACE) < 0) {
		printf("%s missing\n",
		       obj_name ? obj_name : oid_to_hex(&data->oid));
		fflush(stdout);
		return;
	}

	strbuf_reset(scratch);
	strbuf_expand(scratch, opt->format, expand_format, data);
	strbuf_addch(scratch, '\n');
	batch_write(opt, scratch->buf, scratch->len);

	if (opt->print_contents) {
		print_object_or_die(opt, data);
		batch_write(opt, "\n", 1);
	}
}

static void batch_one_object(const char *obj_name,
			     struct strbuf *scratch,
			     struct batch_options *opt,
			     struct expand_data *data)
{
	struct object_context ctx;
	int flags = opt->follow_symlinks ? GET_OID_FOLLOW_SYMLINKS : 0;
	enum follow_symlinks_result result;

	result = get_oid_with_context(obj_name, flags, &data->oid, &ctx);
	if (result != FOUND) {
		switch (result) {
		case MISSING_OBJECT:
			printf("%s missing\n", obj_name);
			break;
		case DANGLING_SYMLINK:
			printf("dangling %"PRIuMAX"\n%s\n",
			       (uintmax_t)strlen(obj_name), obj_name);
			break;
		case SYMLINK_LOOP:
			printf("loop %"PRIuMAX"\n%s\n",
			       (uintmax_t)strlen(obj_name), obj_name);
			break;
		case NOT_DIR:
			printf("notdir %"PRIuMAX"\n%s\n",
			       (uintmax_t)strlen(obj_name), obj_name);
			break;
		default:
			BUG("unknown get_sha1_with_context result %d\n",
			       result);
			break;
		}
		fflush(stdout);
		return;
	}

	if (ctx.mode == 0) {
		printf("symlink %"PRIuMAX"\n%s\n",
		       (uintmax_t)ctx.symlink_path.len,
		       ctx.symlink_path.buf);
		fflush(stdout);
		return;
	}

	batch_object_write(obj_name, scratch, opt, data);
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
	batch_object_write(NULL, data->scratch, data->opt, data->expand);
	return 0;
}

static int collect_loose_object(const struct object_id *oid,
				const char *path,
				void *data)
{
	oid_array_append(data, oid);
	return 0;
}

static int collect_packed_object(const struct object_id *oid,
				 struct packed_git *pack,
				 uint32_t pos,
				 void *data)
{
	oid_array_append(data, oid);
	return 0;
}

static int batch_unordered_object(const struct object_id *oid, void *vdata)
{
	struct object_cb_data *data = vdata;

	if (oidset_insert(data->seen, oid))
		return 0;

	return batch_object_cb(oid, data);
}

static int batch_unordered_loose(const struct object_id *oid,
				 const char *path,
				 void *data)
{
	return batch_unordered_object(oid, data);
}

static int batch_unordered_packed(const struct object_id *oid,
				  struct packed_git *pack,
				  uint32_t pos,
				  void *data)
{
	return batch_unordered_object(oid, data);
}

static int batch_objects(struct batch_options *opt)
{
	struct strbuf input = STRBUF_INIT;
	struct strbuf output = STRBUF_INIT;
	struct expand_data data;
	int save_warning;
	int retval = 0;

	if (!opt->format)
		opt->format = "%(objectname) %(objecttype) %(objectsize)";

	/*
	 * Expand once with our special mark_query flag, which will prime the
	 * object_info to be handed to sha1_object_info_extended for each
	 * object.
	 */
	memset(&data, 0, sizeof(data));
	data.mark_query = 1;
	strbuf_expand(&output, opt->format, expand_format, &data);
	data.mark_query = 0;
	strbuf_release(&output);
	if (opt->cmdmode)
		data.split_on_whitespace = 1;

	if (opt->all_objects) {
		struct object_info empty = OBJECT_INFO_INIT;
		if (!memcmp(&data.info, &empty, sizeof(empty)))
			data.skip_object_info = 1;
	}

	/*
	 * If we are printing out the object, then always fill in the type,
	 * since we will want to decide whether or not to stream.
	 */
	if (opt->print_contents)
		data.info.typep = &data.type;

	if (opt->all_objects) {
		struct object_cb_data cb;

		if (repository_format_partial_clone)
			warning("This repository has extensions.partialClone set. Some objects may not be loaded.");

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

	while (strbuf_getline(&input, stdin) != EOF) {
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

	strbuf_release(&input);
	strbuf_release(&output);
	warn_on_object_refname_ambiguity = save_warning;
	return retval;
}

static const char * const cat_file_usage[] = {
	N_("git cat-file (-t [--allow-unknown-type] | -s [--allow-unknown-type] | -e | -p | <type> | --textconv | --filters) [--path=<path>] <object>"),
	N_("git cat-file (--batch | --batch-check) [--follow-symlinks] [--textconv | --filters]"),
	NULL
};

static int git_cat_file_config(const char *var, const char *value, void *cb)
{
	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, cb);
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
	bo->print_contents = !strcmp(opt->long_name, "batch");
	bo->format = arg;

	return 0;
}

int cmd_cat_file(int argc, const char **argv, const char *prefix)
{
	int opt = 0;
	const char *exp_type = NULL, *obj_name = NULL;
	struct batch_options batch = {0};
	int unknown_type = 0;

	const struct option options[] = {
		OPT_GROUP(N_("<type> can be one of: blob, tree, commit, tag")),
		OPT_CMDMODE('t', NULL, &opt, N_("show object type"), 't'),
		OPT_CMDMODE('s', NULL, &opt, N_("show object size"), 's'),
		OPT_CMDMODE('e', NULL, &opt,
			    N_("exit with zero when there's no error"), 'e'),
		OPT_CMDMODE('p', NULL, &opt, N_("pretty-print object's content"), 'p'),
		OPT_CMDMODE(0, "textconv", &opt,
			    N_("for blob objects, run textconv on object's content"), 'c'),
		OPT_CMDMODE(0, "filters", &opt,
			    N_("for blob objects, run filters on object's content"), 'w'),
		OPT_STRING(0, "path", &force_path, N_("blob"),
			   N_("use a specific path for --textconv/--filters")),
		OPT_BOOL(0, "allow-unknown-type", &unknown_type,
			  N_("allow -s and -t to work with broken/corrupt objects")),
		OPT_BOOL(0, "buffer", &batch.buffer_output, N_("buffer --batch output")),
		{ OPTION_CALLBACK, 0, "batch", &batch, "format",
			N_("show info and content of objects fed from the standard input"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			batch_option_callback },
		{ OPTION_CALLBACK, 0, "batch-check", &batch, "format",
			N_("show info about objects fed from the standard input"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			batch_option_callback },
		OPT_BOOL(0, "follow-symlinks", &batch.follow_symlinks,
			 N_("follow in-tree symlinks (used with --batch or --batch-check)")),
		OPT_BOOL(0, "batch-all-objects", &batch.all_objects,
			 N_("show all objects with --batch or --batch-check")),
		OPT_BOOL(0, "unordered", &batch.unordered,
			 N_("do not order --batch-all-objects output")),
		OPT_END()
	};

	git_config(git_cat_file_config, NULL);

	batch.buffer_output = -1;
	argc = parse_options(argc, argv, prefix, options, cat_file_usage, 0);

	if (opt) {
		if (batch.enabled && (opt == 'c' || opt == 'w'))
			batch.cmdmode = opt;
		else if (argc == 1)
			obj_name = argv[0];
		else
			usage_with_options(cat_file_usage, options);
	}
	if (!opt && !batch.enabled) {
		if (argc == 2) {
			exp_type = argv[0];
			obj_name = argv[1];
		} else
			usage_with_options(cat_file_usage, options);
	}
	if (batch.enabled) {
		if (batch.cmdmode != opt || argc)
			usage_with_options(cat_file_usage, options);
		if (batch.cmdmode && batch.all_objects)
			die("--batch-all-objects cannot be combined with "
			    "--textconv nor with --filters");
	}

	if ((batch.follow_symlinks || batch.all_objects) && !batch.enabled) {
		usage_with_options(cat_file_usage, options);
	}

	if (force_path && opt != 'c' && opt != 'w') {
		error("--path=<path> needs --textconv or --filters");
		usage_with_options(cat_file_usage, options);
	}

	if (force_path && batch.enabled) {
		error("--path=<path> incompatible with --batch");
		usage_with_options(cat_file_usage, options);
	}

	if (batch.buffer_output < 0)
		batch.buffer_output = batch.all_objects;

	if (batch.enabled)
		return batch_objects(&batch);

	if (unknown_type && opt != 't' && opt != 's')
		die("git cat-file --allow-unknown-type: use with -s or -t");
	return cat_one_file(opt, exp_type, obj_name, unknown_type);
}
