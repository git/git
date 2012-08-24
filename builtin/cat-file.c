/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "exec_cmd.h"
#include "tag.h"
#include "tree.h"
#include "builtin.h"
#include "parse-options.h"
#include "diff.h"
#include "userdiff.h"
#include "streaming.h"

#define BATCH 1
#define BATCH_CHECK 2

static void pprint_tag(const unsigned char *sha1, const char *buf, unsigned long size)
{
	/* the parser in tag.c is useless here. */
	const char *endp = buf + size;
	const char *cp = buf;

	while (cp < endp) {
		char c = *cp++;
		if (c != '\n')
			continue;
		if (7 <= endp - cp && !memcmp("tagger ", cp, 7)) {
			const char *tagger = cp;

			/* Found the tagger line.  Copy out the contents
			 * of the buffer so far.
			 */
			write_or_die(1, buf, cp - buf);

			/*
			 * Do something intelligent, like pretty-printing
			 * the date.
			 */
			while (cp < endp) {
				if (*cp++ == '\n') {
					/* tagger to cp is a line
					 * that has ident and time.
					 */
					const char *sp = tagger;
					char *ep;
					unsigned long date;
					long tz;
					while (sp < cp && *sp != '>')
						sp++;
					if (sp == cp) {
						/* give up */
						write_or_die(1, tagger,
							     cp - tagger);
						break;
					}
					while (sp < cp &&
					       !('0' <= *sp && *sp <= '9'))
						sp++;
					write_or_die(1, tagger, sp - tagger);
					date = strtoul(sp, &ep, 10);
					tz = strtol(ep, NULL, 10);
					sp = show_date(date, tz, 0);
					write_or_die(1, sp, strlen(sp));
					xwrite(1, "\n", 1);
					break;
				}
			}
			break;
		}
		if (cp < endp && *cp == '\n')
			/* end of header */
			break;
	}
	/* At this point, we have copied out the header up to the end of
	 * the tagger line and cp points at one past \n.  It could be the
	 * next header line after the tagger line, or it could be another
	 * \n that marks the end of the headers.  We need to copy out the
	 * remainder as is.
	 */
	if (cp < endp)
		write_or_die(1, cp, endp - cp);
}

static int cat_one_file(int opt, const char *exp_type, const char *obj_name)
{
	unsigned char sha1[20];
	enum object_type type;
	char *buf;
	unsigned long size;
	struct object_context obj_context;

	if (get_sha1_with_context(obj_name, 0, sha1, &obj_context))
		die("Not a valid object name %s", obj_name);

	buf = NULL;
	switch (opt) {
	case 't':
		type = sha1_object_info(sha1, NULL);
		if (type > 0) {
			printf("%s\n", typename(type));
			return 0;
		}
		break;

	case 's':
		type = sha1_object_info(sha1, &size);
		if (type > 0) {
			printf("%lu\n", size);
			return 0;
		}
		break;

	case 'e':
		return !has_sha1_file(sha1);

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
		if (type == OBJ_TAG) {
			pprint_tag(sha1, buf, size);
			return 0;
		}

		/* otherwise just spit out the data */
		break;

	case 'c':
		if (!obj_context.path[0])
			die("git cat-file --textconv %s: <object> must be <sha1:path>",
			    obj_name);

		if (!textconv_object(obj_context.path, obj_context.mode, sha1, 1, &buf, &size))
			die("git cat-file --textconv: unable to run textconv on %s",
			    obj_name);
		break;

	case 0:
		if (type_from_string(exp_type) == OBJ_BLOB) {
			unsigned char blob_sha1[20];
			if (sha1_object_info(sha1, NULL) == OBJ_TAG) {
				enum object_type type;
				unsigned long size;
				char *buffer = read_sha1_file(sha1, &type, &size);
				if (memcmp(buffer, "object ", 7) ||
				    get_sha1_hex(buffer + 7, blob_sha1))
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

static int batch_one_object(const char *obj_name, int print_contents)
{
	unsigned char sha1[20];
	enum object_type type = 0;
	unsigned long size;
	void *contents = contents;

	if (!obj_name)
	   return 1;

	if (get_sha1(obj_name, sha1)) {
		printf("%s missing\n", obj_name);
		fflush(stdout);
		return 0;
	}

	if (print_contents == BATCH)
		contents = read_sha1_file(sha1, &type, &size);
	else
		type = sha1_object_info(sha1, &size);

	if (type <= 0) {
		printf("%s missing\n", obj_name);
		fflush(stdout);
		if (print_contents == BATCH)
			free(contents);
		return 0;
	}

	printf("%s %s %lu\n", sha1_to_hex(sha1), typename(type), size);
	fflush(stdout);

	if (print_contents == BATCH) {
		write_or_die(1, contents, size);
		printf("\n");
		fflush(stdout);
		free(contents);
	}

	return 0;
}

static int batch_objects(int print_contents)
{
	struct strbuf buf = STRBUF_INIT;

	while (strbuf_getline(&buf, stdin, '\n') != EOF) {
		int error = batch_one_object(buf.buf, print_contents);
		if (error)
			return error;
	}

	return 0;
}

static const char * const cat_file_usage[] = {
	N_("git cat-file (-t|-s|-e|-p|<type>|--textconv) <object>"),
	N_("git cat-file (--batch|--batch-check) < <list_of_objects>"),
	NULL
};

static int git_cat_file_config(const char *var, const char *value, void *cb)
{
	if (userdiff_config(var, value) < 0)
		return -1;

	return git_default_config(var, value, cb);
}

int cmd_cat_file(int argc, const char **argv, const char *prefix)
{
	int opt = 0, batch = 0;
	const char *exp_type = NULL, *obj_name = NULL;

	const struct option options[] = {
		OPT_GROUP(N_("<type> can be one of: blob, tree, commit, tag")),
		OPT_SET_INT('t', NULL, &opt, N_("show object type"), 't'),
		OPT_SET_INT('s', NULL, &opt, N_("show object size"), 's'),
		OPT_SET_INT('e', NULL, &opt,
			    N_("exit with zero when there's no error"), 'e'),
		OPT_SET_INT('p', NULL, &opt, N_("pretty-print object's content"), 'p'),
		OPT_SET_INT(0, "textconv", &opt,
			    N_("for blob objects, run textconv on object's content"), 'c'),
		OPT_SET_INT(0, "batch", &batch,
			    N_("show info and content of objects fed from the standard input"),
			    BATCH),
		OPT_SET_INT(0, "batch-check", &batch,
			    N_("show info about objects fed from the standard input"),
			    BATCH_CHECK),
		OPT_END()
	};

	git_config(git_cat_file_config, NULL);

	if (argc != 3 && argc != 2)
		usage_with_options(cat_file_usage, options);

	argc = parse_options(argc, argv, prefix, options, cat_file_usage, 0);

	if (opt) {
		if (argc == 1)
			obj_name = argv[0];
		else
			usage_with_options(cat_file_usage, options);
	}
	if (!opt && !batch) {
		if (argc == 2) {
			exp_type = argv[0];
			obj_name = argv[1];
		} else
			usage_with_options(cat_file_usage, options);
	}
	if (batch && (opt || argc)) {
		usage_with_options(cat_file_usage, options);
	}

	if (batch)
		return batch_objects(batch);

	return cat_one_file(opt, exp_type, obj_name);
}
