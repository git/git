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
	void *buf;
	unsigned long size;

	if (get_sha1(obj_name, sha1))
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
			const char *ls_args[3] = {"ls-tree", obj_name, NULL};
			return cmd_ls_tree(2, ls_args, NULL);
		}

		buf = read_sha1_file(sha1, &type, &size);
		if (!buf)
			die("Cannot read object %s", obj_name);
		if (type == OBJ_TAG) {
			pprint_tag(sha1, buf, size);
			return 0;
		}

		/* otherwise just spit out the data */
		break;
	case 0:
		buf = read_object_with_reference(sha1, exp_type, &size, NULL);
		break;

	default:
		die("git-cat-file: unknown option: %s\n", exp_type);
	}

	if (!buf)
		die("git-cat-file %s: bad file", obj_name);

	write_or_die(1, buf, size);
	return 0;
}

static int batch_one_object(const char *obj_name)
{
	unsigned char sha1[20];
	enum object_type type;
	unsigned long size;

	if (!obj_name)
	   return 1;

	if (get_sha1(obj_name, sha1)) {
		printf("%s missing\n", obj_name);
		return 0;
	}

	type = sha1_object_info(sha1, &size);
	if (type <= 0)
		return 1;

	printf("%s %s %lu\n", sha1_to_hex(sha1), typename(type), size);

	return 0;
}

static int batch_objects(void)
{
	struct strbuf buf;

	strbuf_init(&buf, 0);
	while (strbuf_getline(&buf, stdin, '\n') != EOF) {
		int error = batch_one_object(buf.buf);
		if (error)
			return error;
	}

	return 0;
}

static const char cat_file_usage[] = "git-cat-file [ [-t|-s|-e|-p|<type>] <sha1> | --batch-check < <list_of_sha1s> ]";

int cmd_cat_file(int argc, const char **argv, const char *prefix)
{
	int i, opt = 0, batch_check = 0;
	const char *exp_type = NULL, *obj_name = NULL;

	git_config(git_default_config);

	for (i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--batch-check")) {
			if (opt) {
				error("git-cat-file: Can't use --batch-check with -%c", opt);
				usage(cat_file_usage);
			} else if (exp_type) {
				error("git-cat-file: Can't use --batch-check when a type (\"%s\") is specified", exp_type);
				usage(cat_file_usage);
			} else if (obj_name) {
				error("git-cat-file: Can't use --batch-check when an object (\"%s\") is specified", obj_name);
				usage(cat_file_usage);
			}

			batch_check = 1;
			continue;
		}

		if (!strcmp(arg, "-t") || !strcmp(arg, "-s") || !strcmp(arg, "-e") || !strcmp(arg, "-p")) {
			if (batch_check) {
				error("git-cat-file: Can't use %s with --batch-check", arg);
				usage(cat_file_usage);
			}

			exp_type = arg;
			opt = exp_type[1];
			continue;
		}

		if (arg[0] == '-')
			usage(cat_file_usage);

		if (!exp_type) {
			if (batch_check) {
				error("git-cat-file: Can't specify a type (\"%s\") with --batch-check", arg);
				usage(cat_file_usage);
			}

			exp_type = arg;
			continue;
		}

		if (obj_name)
			usage(cat_file_usage);

		// We should have hit one of the earlier if (batch_check) cases before
		// getting here.
		assert(!batch_check);

		obj_name = arg;
		break;
	}

	if (batch_check)
		return batch_objects();

	if (!exp_type || !obj_name)
		usage(cat_file_usage);

	return cat_one_file(opt, exp_type, obj_name);
}
