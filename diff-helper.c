/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static int detect_rename = 0;
static int diff_score_opt = 0;
static const char *pickaxe = NULL;
static int diff_output_style = DIFF_FORMAT_PATCH;
static int line_termination = '\n';
static int inter_name_termination = '\t';

static int parse_diff_raw(char *buf1, char *buf2, char *buf3)
{
	char old_path[PATH_MAX];
	unsigned char old_sha1[20], new_sha1[20];
	char *ep;
	char *cp = buf1;
	int ch, old_mode, new_mode;

	old_mode = new_mode = 0;
	while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
		old_mode = (old_mode << 3) | (ch - '0');
		cp++;
	}
	if (*cp++ != ' ')
		return -1;
	while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
		new_mode = (new_mode << 3) | (ch - '0');
		cp++;
	}
	if (*cp++ != ' ')
		return -1;
	if (get_sha1_hex(cp, old_sha1))
		return -1;
	cp += 40;
	if (*cp++ != ' ')
		return -1;
	if (get_sha1_hex(cp, new_sha1))
		return -1;
	cp += 40;
	if (*cp++ != inter_name_termination)
		return -1;
	if (buf2)
		cp = buf2;
	ep = strchr(cp, inter_name_termination);
	if (!ep)
		return -1;
	*ep++ = 0;
	strcpy(old_path, cp);
	diff_guif(old_mode, new_mode, old_sha1, new_sha1,
		  old_path, buf3 ? buf3 : ep);
	return 0;
}

static const char *diff_helper_usage =
	"git-diff-helper [-z] [-R] [-M] [-C] [-S<string>] paths...";

int main(int ac, const char **av) {
	struct strbuf sb1, sb2, sb3;
	int reverse_diff = 0;

	strbuf_init(&sb1);
	strbuf_init(&sb2);
	strbuf_init(&sb3);

	while (1 < ac && av[1][0] == '-') {
		if (av[1][1] == 'R')
			reverse_diff = 1;
		else if (av[1][1] == 'z')
			line_termination = inter_name_termination = 0;
		else if (av[1][1] == 'p') /* hidden from the help */
			diff_output_style = DIFF_FORMAT_HUMAN;
		else if (av[1][1] == 'P') /* hidden from the help */
			diff_output_style = DIFF_FORMAT_MACHINE;
		else if (av[1][1] == 'M') {
			detect_rename = DIFF_DETECT_RENAME;
			diff_score_opt = diff_scoreopt_parse(av[1]);
		}
		else if (av[1][1] == 'C') {
			detect_rename = DIFF_DETECT_COPY;
			diff_score_opt = diff_scoreopt_parse(av[1]);
		}
		else if (av[1][1] == 'S') {
			pickaxe = av[1] + 2;
		}
		else
			usage(diff_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	diff_setup(reverse_diff);
	while (1) {
		int status;
		read_line(&sb1, stdin, line_termination);
		if (sb1.eof)
			break;
		switch (sb1.buf[0]) {
		case 'U':
			diff_unmerge(sb1.buf + 2);
			continue;
		case ':':
			break;
		default:
			goto unrecognized;
		}
		if (!line_termination) {
			read_line(&sb2, stdin, line_termination);
			if (sb2.eof)
				break;
			read_line(&sb3, stdin, line_termination);
			if (sb3.eof)
				break;
			status = parse_diff_raw(sb1.buf+1, sb2.buf, sb3.buf);
		}
		else
			status = parse_diff_raw(sb1.buf+1, NULL, NULL);
		if (status) {
		unrecognized:
			diff_flush(diff_output_style);
			printf("%s%c", sb1.buf, line_termination);
		}
	}
	if (detect_rename)
		diffcore_rename(detect_rename, diff_score_opt);
	diffcore_prune();
	if (pickaxe)
		diffcore_pickaxe(pickaxe);
	if (ac)
		diffcore_pathspec(av + 1);
	diff_flush(diff_output_style);
	return 0;
}
