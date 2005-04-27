/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "strbuf.h"
#include "diff.h"

static int matches_pathspec(const char *name, char **spec, int cnt)
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
	if (get_sha1_hex(cp, one->u.sha1))
		return -1;
	cp += 40;
	if (*cp++ != '\t')
		return -1;
	strcpy(path, cp);
	return 0;
}

#define PLEASE_WARN -1
#define WARNED_OURSELVES -2
 
static int parse_diff_tree_output(const char *buf,
				  struct diff_spec *old,
				  struct diff_spec *new,
				  char *path) {
	const char *cp = buf;
	int ch;

	switch (*cp++) {
	case 'U':
		diff_unmerge(cp + 1);
		return WARNED_OURSELVES;
	case '+':
		old->file_valid = 0;
		return parse_oneside_change(cp, new, path);
	case '-':
		new->file_valid = 0;
		return parse_oneside_change(cp, old, path);
	case '*':
		break;
	default:
		return PLEASE_WARN;
	}
	
	/* This is for '*' entries */
	old->file_valid = old->sha1_valid = 1;
	new->file_valid = new->sha1_valid = 1;

	old->mode = new->mode = 0;
	while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
		old->mode = (old->mode << 3) | (ch - '0');
		cp++;
	}
	if (strncmp(cp, "->", 2))
		return PLEASE_WARN;
	cp += 2;
	while ((ch = *cp) && ('0' <= ch && ch <= '7')) {
		new->mode = (new->mode << 3) | (ch - '0');
		cp++;
	}
	if (strncmp(cp, "\tblob\t", 6))
		return PLEASE_WARN;
	cp += 6;
	if (get_sha1_hex(cp, old->u.sha1))
		return PLEASE_WARN;
	cp += 40;
	if (strncmp(cp, "->", 2))
		return PLEASE_WARN;
	cp += 2;
	if (get_sha1_hex(cp, new->u.sha1))
		return PLEASE_WARN;
	cp += 40;
	if (*cp++ != '\t')
		return PLEASE_WARN;
	strcpy(path, cp);
	return 0;
}

static const char *diff_tree_helper_usage =
"diff-tree-helper [-R] [-z] paths...";

int main(int ac, char **av) {
	struct strbuf sb;
	int reverse_diff = 0;
	int line_termination = '\n';

	strbuf_init(&sb);

	while (1 < ac && av[1][0] == '-') {
		if (av[1][1] == 'R')
			reverse_diff = 1;
		else if (av[1][1] == 'z')
			line_termination = 0;
		else
			usage(diff_tree_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	while (1) {
		int status;
		struct diff_spec old, new;
		char path[PATH_MAX];
		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		status = parse_diff_tree_output(sb.buf, &old, &new, path);
		if (status) {
			if (status == PLEASE_WARN)
				fprintf(stderr, "cannot parse %s\n", sb.buf);
			continue;
		}
		if (1 < ac && !matches_pathspec(path, av+1, ac-1))
			continue;

		run_external_diff(path, &old, &new);
	}
	return 0;
}
