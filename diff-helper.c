/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <limits.h>
#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static int matches_pathspec(const char *name, const char **spec, int cnt)
{
	int i;
	int namelen = strlen(name);
	for (i = 0; i < cnt; i++) {
		int speclen = strlen(spec[i]);
		if (! strncmp(spec[i], name, speclen) &&
		    speclen <= namelen &&
		    (name[speclen] == 0 ||
		     name[speclen] == '/'))
			return 1;
	}
	return 0;
}

static int parse_oneside_change(const char *cp, struct diff_spec *one,
				char *path)
{
	int ch;

	one->file_valid = one->sha1_valid = 1;
	one->mode = 0;
	while ((ch = *cp) && '0' <= ch && ch <= '7') {
		one->mode = (one->mode << 3) | (ch - '0');
		cp++;
	}

	if (strncmp(cp, "\tblob\t", 6))
		return -1;
	cp += 6;
	if (get_sha1_hex(cp, one->blob_sha1))
		return -1;
	cp += 40;
	if (*cp++ != '\t')
		return -1;
	strcpy(path, cp);
	return 0;
}

static int parse_diff_tree_output(const char *buf,
				  const char **spec, int cnt, int reverse)
{
	struct diff_spec old, new;
	char path[PATH_MAX];
	const char *cp = buf;
	int ch;

	switch (*cp++) {
	case 'U':
		if (!cnt || matches_pathspec(cp + 1, spec, cnt))
			diff_unmerge(cp + 1);
		return 0;
	case '+':
		old.file_valid = 0;
		parse_oneside_change(cp, &new, path);
		break;
	case '-':
		new.file_valid = 0;
		parse_oneside_change(cp, &old, path);
		break;
	case '*':
		old.file_valid = old.sha1_valid =
			new.file_valid = new.sha1_valid = 1;
		old.mode = new.mode = 0;
		while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
			old.mode = (old.mode << 3) | (ch - '0');
			cp++;
		}
		if (strncmp(cp, "->", 2))
			return -1;
		cp += 2;
		while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
			new.mode = (new.mode << 3) | (ch - '0');
			cp++;
		}
		if (strncmp(cp, "\tblob\t", 6))
			return -1;
		cp += 6;
		if (get_sha1_hex(cp, old.blob_sha1))
			return -1;
		cp += 40;
		if (strncmp(cp, "->", 2))
			return -1;
		cp += 2;
		if (get_sha1_hex(cp, new.blob_sha1))
			return -1;
		cp += 40;
		if (*cp++ != '\t')
			return -1;
		strcpy(path, cp);
		break;
	default:
		return -1;
	}
	if (!cnt || matches_pathspec(path, spec, cnt)) {
		if (reverse)
			run_external_diff(path, &new, &old);
		else
			run_external_diff(path, &old, &new);
	}
	return 0;
}

static const char *diff_tree_helper_usage =
"diff-tree-helper [-R] [-z] paths...";

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
		else
			usage(diff_tree_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	while (1) {
		int status;
		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		status = parse_diff_tree_output(sb.buf, av+1, ac-1, reverse);
		if (status)
			fprintf(stderr, "cannot parse %s\n", sb.buf);
	}
	return 0;
}
