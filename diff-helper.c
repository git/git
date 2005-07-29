/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static const char *pickaxe = NULL;
static int pickaxe_opts = 0;
static const char *orderfile = NULL;
static const char *diff_filter = NULL;
static int line_termination = '\n';
static int inter_name_termination = '\t';

static void flush_them(int ac, const char **av)
{
	diffcore_std_no_resolve(av + 1,
				pickaxe, pickaxe_opts,
				orderfile, diff_filter);
	diff_flush(DIFF_FORMAT_PATCH, '\n');
}

static const char diff_helper_usage[] =
"git-diff-helper [-z] [-O<orderfile>] [-S<string>] [--pickaxe-all] [<path>...]";

int main(int ac, const char **av) {
	struct strbuf sb;
	const char *garbage_flush_format;

	strbuf_init(&sb);

	while (1 < ac && av[1][0] == '-') {
		if (av[1][1] == 'z')
			line_termination = inter_name_termination = 0;
		else if (av[1][1] == 'S') {
			pickaxe = av[1] + 2;
		}
		else if (!strcmp(av[1], "--pickaxe-all"))
			pickaxe_opts = DIFF_PICKAXE_ALL;
		else if (!strncmp(av[1], "--diff-filter=", 14))
			diff_filter = av[1] + 14;
		else if (!strncmp(av[1], "-O", 2))
			orderfile = av[1] + 2;
		else
			usage(diff_helper_usage);
		ac--; av++;
	}
	garbage_flush_format = (line_termination == 0) ? "%s" : "%s\n";

	/* the remaining parameters are paths patterns */

	diff_setup(0);
	while (1) {
		unsigned old_mode, new_mode;
		unsigned char old_sha1[20], new_sha1[20];
		char old_path[PATH_MAX];
		int status, score, two_paths;
		char new_path[PATH_MAX];

		int ch;
		char *cp, *ep;

		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		switch (sb.buf[0]) {
		case ':':
			/* parse the first part up to the status */
			cp = sb.buf + 1;
			old_mode = new_mode = 0;
			while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
				old_mode = (old_mode << 3) | (ch - '0');
				cp++;
			}
			if (*cp++ != ' ')
				break;
			while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
				new_mode = (new_mode << 3) | (ch - '0');
				cp++;
			}
			if (*cp++ != ' ')
				break;
			if (get_sha1_hex(cp, old_sha1))
				break;
			cp += 40;
			if (*cp++ != ' ')
				break;
			if (get_sha1_hex(cp, new_sha1))
				break;
			cp += 40;
			if (*cp++ != ' ')
				break;
			status = *cp++;
			if (!strchr("MCRNDU", status))
				break;
			two_paths = score = 0;
			if (status == DIFF_STATUS_RENAMED ||
			    status == DIFF_STATUS_COPIED)
				two_paths = 1;

			/* pick up score if exists */
			if (sscanf(cp, "%d", &score) != 1)
				score = 0;
			cp = strchr(cp,
				    inter_name_termination);
			if (!cp)
				break;
			if (*cp++ != inter_name_termination)
				break;

			/* first pathname */
			if (!line_termination) {
				read_line(&sb, stdin, line_termination);
				if (sb.eof)
					break;
				strcpy(old_path, sb.buf);
			}
			else if (!two_paths)
				strcpy(old_path, cp);
			else {
				ep = strchr(cp, inter_name_termination);
				if (!ep)
					break;
				strncpy(old_path, cp, ep-cp);
				old_path[ep-cp] = 0;
				cp = ep + 1;
			}

			/* second pathname */
			if (!two_paths)
				strcpy(new_path, old_path);
			else {
				if (!line_termination) {
					read_line(&sb, stdin,
						  line_termination);
					if (sb.eof)
						break;
					strcpy(new_path, sb.buf);
				}
				else
					strcpy(new_path, cp);
			}
			diff_helper_input(old_mode, new_mode,
					  old_sha1, new_sha1,
					  old_path, status, score,
					  new_path);
			continue;
		}
		flush_them(ac, av);
		printf(garbage_flush_format, sb.buf);
	}
	flush_them(ac, av);
	return 0;
}
