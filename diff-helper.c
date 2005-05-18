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

static int detect_rename = 0;

/*
 * We do not detect circular renames.  Just hold created and deleted
 * entries and later attempt to match them up.  If they do not match,
 * then spit them out as deletes or creates as original.
 */

static struct diff_spec_hold {
	struct diff_spec_hold *next;
	struct diff_spec_hold *matched;
	struct diff_spec old, new;
	char path[1];
} *createdfile, *deletedfile;

static void hold_spec(const char *path,
		      struct diff_spec *old, struct diff_spec *new)
{
	struct diff_spec_hold **list, *elem;
	list = (! old->file_valid) ? &createdfile : &deletedfile;
	elem = xmalloc(sizeof(*elem) + strlen(path));
	strcpy(elem->path, path);
	elem->next = *list;
	*list = elem;
	elem->old = *old;
	elem->new = *new;
	elem->matched = 0;
}

#define MINIMUM_SCORE 7000
int estimate_similarity(struct diff_spec *one, struct diff_spec *two)
{
	/* Return how similar they are, representing the score as an
	 * integer between 0 and 10000.
	 *
	 * This version is very dumb and detects exact matches only.
	 * Wnen Nico's delta stuff gets in, I'll use the delta
	 * algorithm to estimate the similarity score in core.
	 */

	if (one->sha1_valid && two->sha1_valid &&
	    !memcmp(one->blob_sha1, two->blob_sha1, 20))
		return 10000;
	return 0;
}

static void flush_renames(const char **spec, int cnt, int reverse)
{
	struct diff_spec_hold *rename_src, *rename_dst, *elem;
	struct diff_spec_hold *leftover = NULL;
	int score, best_score;

	while (createdfile) {
		rename_dst = createdfile;
		createdfile = rename_dst->next;
		best_score = MINIMUM_SCORE;
		rename_src = NULL;
		for (elem = deletedfile;
		     elem;
		     elem = elem->next) {
			if (elem->matched)
				continue;
			score = estimate_similarity(&elem->old,
						    &rename_dst->new);
			if (best_score < score) {
				rename_src = elem;
				best_score = score;
			}
		}
		if (rename_src) {
			rename_src->matched = rename_dst;
			rename_dst->matched = rename_src;

			if (!cnt ||
			    matches_pathspec(rename_src->path, spec, cnt) ||
			    matches_pathspec(rename_dst->path, spec, cnt)) {
				if (reverse)
					run_external_diff(rename_dst->path,
							  rename_src->path,
							  &rename_dst->new,
							  &rename_src->old);
				else
					run_external_diff(rename_src->path,
							  rename_dst->path,
							  &rename_src->old,
							  &rename_dst->new);
			}
		}
		else {
			rename_dst->next = leftover;
			leftover = rename_dst;
		}
	}

	/* unmatched deletes */
	for (elem = deletedfile; elem; elem = elem->next) {
		if (elem->matched)
			continue;
		if (!cnt ||
		    matches_pathspec(elem->path, spec, cnt)) {
			if (reverse)
				run_external_diff(elem->path, NULL,
						  &elem->new, &elem->old);
			else
				run_external_diff(elem->path, NULL,
						  &elem->old, &elem->new);
		}
	}

	/* unmatched creates */
	for (elem = leftover; elem; elem = elem->next) {
		if (!cnt ||
		    matches_pathspec(elem->path, spec, cnt)) {
			if (reverse)
				run_external_diff(elem->path, NULL,
						  &elem->new, &elem->old);
			else
				run_external_diff(elem->path, NULL,
						  &elem->old, &elem->new);
		}
	}
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

static int parse_diff_raw_output(const char *buf,
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

	if (detect_rename && old.file_valid != new.file_valid) {
		/* hold these */
		hold_spec(path, &old, &new);
		return 0;
	}

	if (!cnt || matches_pathspec(path, spec, cnt)) {
		if (reverse)
			run_external_diff(path, NULL, &new, &old);
		else
			run_external_diff(path, NULL, &old, &new);
	}
	return 0;
}

static const char *diff_helper_usage =
	"git-diff-helper [-r] [-R] [-z] paths...";

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
		else if (av[1][1] == 'r')
			detect_rename = 1;
		else
			usage(diff_helper_usage);
		ac--; av++;
	}
	/* the remaining parameters are paths patterns */

	while (1) {
		int status;
		read_line(&sb, stdin, line_termination);
		if (sb.eof)
			break;
		status = parse_diff_raw_output(sb.buf, av+1, ac-1, reverse);
		if (status)
			fprintf(stderr, "cannot parse %s\n", sb.buf);
	}

	if (detect_rename)
		flush_renames(av+1, ac-1, reverse);
	return 0;
}
