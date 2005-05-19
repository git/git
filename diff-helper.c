/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <limits.h>
#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static int detect_rename = 0;

static int parse_oneside_change(const char *cp, int *mode,
				unsigned char *sha1, char *path)
{
	int ch, m;

	m = 0;
	while ((ch = *cp) && '0' <= ch && ch <= '7') {
		m = (m << 3) | (ch - '0');
		cp++;
	}
	*mode = m;
	if (strncmp(cp, "\tblob\t", 6))
		return -1;
	cp += 6;
	if (get_sha1_hex(cp, sha1))
		return -1;
	cp += 40;
	if (*cp++ != '\t')
		return -1;
	strcpy(path, cp);
	return 0;
}

static int parse_diff_raw_output(const char *buf)
{
	char path[PATH_MAX];
	unsigned char old_sha1[20], new_sha1[20];
	const char *cp = buf;
	int ch, old_mode, new_mode;

	switch (*cp++) {
	case 'U':
		diff_unmerge(cp + 1);
		break;
	case '+':
		parse_oneside_change(cp, &new_mode, new_sha1, path);
		diff_addremove('+', new_mode, new_sha1, path, NULL);
		break;
	case '-':
		parse_oneside_change(cp, &old_mode, old_sha1, path);
		diff_addremove('-', old_mode, old_sha1, path, NULL);
		break;
	case '*':
		old_mode = new_mode = 0;
		while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
			old_mode = (old_mode << 3) | (ch - '0');
			cp++;
		}
		if (strncmp(cp, "->", 2))
			return -1;
		cp += 2;
		while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
			new_mode = (new_mode << 3) | (ch - '0');
			cp++;
		}
		if (strncmp(cp, "\tblob\t", 6))
			return -1;
		cp += 6;
		if (get_sha1_hex(cp, old_sha1))
			return -1;
		cp += 40;
		if (strncmp(cp, "->", 2))
			return -1;
		cp += 2;
		if (get_sha1_hex(cp, new_sha1))
			return -1;
		cp += 40;
		if (*cp++ != '\t')
			return -1;
		strcpy(path, cp);
		diff_change(old_mode, new_mode, old_sha1, new_sha1, path, 0);
		break;
	default:
		return -1;
	}
	return 0;
}

static const char *diff_helper_usage =
	"git-diff-helper [-z] [-R] [-M] paths...";

int main(int ac, const char **av) {
	struct strbuf sb;
	int reverse = 0;
	int line_termination = '\n';

	strbuf_init(&sb);

	while (1 < ac && av[1][0] == '-') {
		if (av[1][1] == 'R')
			reverse = 1;
		else if (av[1][1] == 'z')
			line_termination = 0;
		else if (av[1][1] == 'M')
			detect_rename = 1;
		else
			usage(diff_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	diff_setup(detect_rename, 0, reverse, av+1, ac-1);

	while (1) {
		int status;
		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		status = parse_diff_raw_output(sb.buf);
		if (status) {
			diff_flush();
			printf("%s%c", sb.buf, line_termination);
		}
	}

	diff_flush();
	return 0;
}
