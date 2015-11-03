/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "userdiff.h"
#include "streaming.h"
#include "tree-walk.h"
#include "sha1-array.h"

struct batch_options {
	int enabled;
	int follow_symlinks;
	int print_contents;
	int buffer_output;
	int all_objects;
	const char *format;
};

static int cat_one_file(int opt, const char *exp_type, const char *obj_name,
			int unknown_type)
{
	unsigned char sha1[20];
	enum object_type type;
	char *buf;
	unsigned long size;
	struct object_context obj_context;
	struct object_info oi = {NULL};
	struct strbuf sb = STRBUF_INIT;
	unsigned flags = LOOKUP_REPLACE_OBJECT;

	if (unknown_type)
		flags |= LOOKUP_UNKNOWN_OBJECT;

	if (get_sha1_with_context(obj_name, 0, sha1, &obj_context))
		die("Not a valid object name %s", obj_name);

	buf = NULL;
	switch (opt) {
	case 't':
		oi.typename = &sb;
		if (sha1_object_info_extended(sha1, &oi, flags) < 0)
			die("git cat-file: could not get object info");
		if (sb.len) {
			printf("%s\n", sb.buf);
			strbuf_release(&sb);
			return 0;
		}
		break;

	case 's':
		oi.sizep = &size;
		if (sha1_object_info_extended(sha1, &oi, flags) < 0)
			die("git cat-file: could not get object info");
		printf("%lu\n", size);
		return 0;

	case 'e':
		return !has_sha1_file(sha1);

	case 'c':
		if (!obj_context.path[0])
			die("git cat-file --textconv %s: <object> must be <sha1:path>",
			    obj_name);

		if (textconv_object(obj_context.path, obj_context.mode, sha1, 1, &buf, &size))
			break;

	case 'p':
		type = sha1_object_info(sha1, NULL);
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
			return stream_blob_to_fd(1, sha1, NULL, 0);
		buf = read_sha1_file(sha1, &type, &size);
		if (!buf)
			die("Cannot read object %s", obj_name);

		/* otherwise just spit out the data */
		break;

	case 0:
		if (type_from_string(exp_type) == OBJ_BLOB) {
			unsigned char blob_sha1[20];
			if (sha1_object_info(sha1, NULL) == OBJ_TAG) {
				char *buffer = read_sha1_file(sha1, &type, &size);
				const char *target;
				if (!skip_prefix(buffer, "object ", &target) ||
				    get_sha1_hex(target, blob_sha1))
					die("%s not a valid tag", sha1_to_hex(sha1));
				free(buffer);
			} else
				hashcpy(blob_sha1, sha1);

			if (sha1_object_info(blob_sha1, NULL) == OBJ_BLOB)
				return stream_blob_to_fd(1, blob_sha1, NULL, 0);
			/*
			 * we attempted to dereference a tag to a blob
			 * and failed; there may be new dereference
			 * mechanisms this code is not aware of.
			 * fall-back to the usual case.
			 */
		}
		buf = read_object_with_reference(sha1, exp_type, &size, NULL);
		break;

	default:
		die("git cat-file: unknown option: %s", exp_type);
	}

	if (!buf)
		die("git cat-file %s: bad file", obj_name);

	write_or_die(1, buf, size);
	return 0;
}

struct expand_data {
	unsigned char sha1[20];
	enum object_type type;
	unsigned long size;
	unsigned long disk_size;
	const char *rest;
	unsigned char delta_base_sha1[20];

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
			strbuf_addstr(sb, sha1_to_hex(data->sha1));
	} else if (is_atom("objecttype", atom, len)) {
		if (data->mark_query)
			data->info.typep = &data->type;
		else
			strbuf_addstr(sb, typename(data->type));
	} else if (is_atom("objectsize", atom, len)) {
		if (data->mark_query)
			data->info.sizep = &data->size;
		else
			strbuf_addf(sb, "%lu", data->size);
	} else if (is_atom("objectsize:disk", atom, len)) {
		if (data->mark_query)
			data->info.disk_sizep = &data->disk_size;
		else
			strbuf_addf(sb, "%lu", data->disk_size);
	} else if (is_atom("rest", atom, len)) {
		if (data->mark_query)
			data->split_on_whitespace = 1;
		else if (data->rest)
			strbuf_addstr(sb, data->rest);
	} else if (is_atom("deltabase", atom, len)) {
		if (data->mark_query)
			data->info.delta_base_sha1 = data->delta_base_sha1;
		else
			strbuf_addstr(sb, sha1_to_hex(data->delta_base_sha1));
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
	const unsigned char *sha1 = data->sha1;

	assert(data->info.typep);

	if (data->type == OBJ_BLOB) {
		if (opt->buffer_output)
			fflush(stdout);
		if (stream_blob_to_fd(1, sha1, NULL, 0) < 0)
			die("unable to stream %s to stdout", sha1_to_hex(sha1));
	}
	else {
		enum object_type type;
		unsigned long size;
		void *contents;

		contents = read_sha1_file(sha1, &type, &size);
		if (!contents)
			die("object %s disappeared", sha1_to_hex(sha1));
		if (type != data->type)
			die("object %s changed type!?", sha1_to_hex(sha1));
		if (data->info.sizep && size != data->size)
			die("object %s changed size!?", sha1_to_hex(sha1));

		batch_write(opt, contents, size);
		free(contents);
	}
}

static void batch_object_write(const char *obj_name, struct batch_options *opt,
			       struct expand_data *data)
{
	struct strbuf buf = STRBUF_INIT;

	if (sha1_object_info_extended(data->sha1, &data->info, LOOKUP_REPLACE_OBJECT) < 0) {
		printf("%s missing\n", obj_name ? obj_name : sha1_to_hex(data->sha1));
		fflush(stdout);
		return;
	}

	strbuf_expand(&buf, opt->format, expand_format, data);
	strbuf_addch(&buf, '\n');
	batch_write(opt, buf.buf, buf.len);
	strbuf_release(&buf);

	if (opt->print_contents) {
		print_object_or_die(opt, data);
		batch_write(opt, "\n", 1);
	}
}

static void batch_one_object(const char *obj_name, struct batch_options *opt,
			     struct expand_data *data)
{
	struct object_context ctx;
	int flags = opt->follow_symlinks ? GET_SHA1_FOLLOW_SYMLINKS : 0;
	enum follow_symlinks_result result;

	result = get_sha1_with_context(obj_name, flags, data->sha1, &ctx);
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
			die("BUG: unknown get_sha1_with_context result %d\n",
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

	batch_object_write(obj_name, opt, data);
}

struct object_cb_data {
	struct batch_options *opt;
	struct expand_data *expand;
};

static void batch_object_cb(const unsigned char sha1[20], void *vdata)
{
	struct object_cb_data *data = vdata;
	hashcpy(data->expand->sha1, sha1);
	batch_object_write(NULL, data->opt, data->expand);
}

static int batch_loose_object(const unsigned char *sha1,
			      const char *path,
			      void *data)
{
	sha1_array_append(data, sha1);
	return 0;
}

static int batch_packed_object(const unsigned char *sha1,
			       struct packed_git *pack,
			       uint32_t pos,
			       void *data)
{
	sha1_array_append(data, sha1);
	return 0;
}

static int batch_objects(struct batch_options *opt)
{
	struct strbuf buf = STRBUF_INIT;
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
	strbuf_expand(&buf, opt->format, expand_format, &data);
	data.mark_query = 0;

	/*
	 * If we are printing out the object, then always fill in the type,
	 * since we will want to decide whether or not to stream.
	 */
	if (opt->print_contents)
		data.info.typep = &data.type;

	if (opt->all_objects) {
		struct sha1_array sa = SHA1_ARRAY_INIT;
		struct object_cb_data cb;

		for_each_loose_object(batch_loose_object, &sa, 0);
		for_each_packed_object(batch_packed_object, &sa, 0);

		cb.opt = opt;
		cb.expand = &data;
		sha1_array_for_each_unique(&sa, batch_object_cb, &cb);

		sha1_array_clear(&sa);
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

	while (strbuf_getline(&buf, stdin, '\n') != EOF) {
		if (data.split_on_whitespace) {
			/*
			 * Split at first whitespace, tying off the beginning
			 * of the string and saving the remainder (or NULL) in
			 * data.rest.
			 */
			char *p = strpbrk(buf.buf, " \t");
			if (p) {
				while (*p && strchr(" \t", *p))
					*p++ = '\0';
			}
			data.rest = p;
		}

		batch_one_object(buf.buf, opt, &data);
	}

	strbuf_release(&buf);
	warn_on_object_refname_ambiguity = save_warning;
	return retval;
}

static const char * const cat_file_usage[] = {
	N_("git cat-file (-t [--allow-unknown-type]|-s [--allow-unknown-type]|-e|-p|<type>|--textconv) <object>"),
	N_("git cat-file (--batch | --batch-check) [--follow-symlinks]"),
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

	if (bo->enabled) {
		return 1;
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
		OPT_BOOL(0, "allow-unknown-type", &unknown_type,
			  N_("allow -s and -t to work with broken/corrupt objects")),
		OPT_BOOL(0, "buffer", &batch.buffer_output, N_("buffer --batch output")),
		{ OPTION_CALLBACK, 0, "batch", &batch, "format",
			N_("show info and content of objects fed from the standard input"),
			PARSE_OPT_OPTARG, batch_option_callback },
		{ OPTION_CALLBACK, 0, "batch-check", &batch, "format",
			N_("show info about objects fed from the standard input"),
			PARSE_OPT_OPTARG, batch_option_callback },
		OPT_BOOL(0, "follow-symlinks", &batch.follow_symlinks,
			 N_("follow in-tree symlinks (used with --batch or --batch-check)")),
		OPT_BOOL(0, "batch-all-objects", &batch.all_objects,
			 N_("show all objects with --batch or --batch-check")),
		OPT_END()
	};

	git_config(git_cat_file_config, NULL);

	argc = parse_options(argc, argv, prefix, options, cat_file_usage, 0);

	if (opt) {
		if (argc == 1)
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
	if (batch.enabled && (opt || argc)) {
		usage_with_options(cat_file_usage, options);
	}

	if ((batch.follow_symlinks || batch.all_objects) && !batch.enabled) {
		usage_with_options(cat_file_usage, options);
	}

	if (batch.enabled)
		return batch_objects(&batch);

	if (unknown_type && opt != 't' && opt != 's')
		die("git cat-file --allow-unknown-type: use with -s or -t");
	return cat_one_file(opt, exp_type, obj_name, unknown_type);
}
