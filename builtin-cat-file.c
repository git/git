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

int cmd_cat_file(int argc, const char **argv, const char *prefix)
{
	unsigned char sha1[20];
	enum object_type type;
	void *buf;
	unsigned long size;
	int opt;
	const char *exp_type, *obj_name;

	git_config(git_default_config);
	if (argc != 3)
		usage("git-cat-file [-t|-s|-e|-p|<type>] <sha1>");
	exp_type = argv[1];
	obj_name = argv[2];

	if (get_sha1(obj_name, sha1))
		die("Not a valid object name %s", obj_name);

	opt = 0;
	if ( exp_type[0] == '-' ) {
		opt = exp_type[1];
		if ( !opt || exp_type[2] )
			opt = -1; /* Not a single character option */
	}

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
