/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"
#include "tag.h"
#include "reflog-walk.h"
#include "patch-ids.h"
#include "refs.h"

static int default_show_root = 1;

/* this is in builtin-diff.c */
void add_head(struct rev_info *revs);

static void add_name_decoration(const char *prefix, const char *name, struct object *obj)
{
	int plen = strlen(prefix);
	int nlen = strlen(name);
	struct name_decoration *res = xmalloc(sizeof(struct name_decoration) + plen + nlen);
	memcpy(res->name, prefix, plen);
	memcpy(res->name + plen, name, nlen + 1);
	res->next = add_decoration(&name_decoration, obj, res);
}

static int add_ref_decoration(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct object *obj = parse_object(sha1);
	if (!obj)
		return 0;
	add_name_decoration("", refname, obj);
	while (obj->type == OBJ_TAG) {
		obj = ((struct tag *)obj)->tagged;
		if (!obj)
			break;
		add_name_decoration("tag: ", refname, obj);
	}
	return 0;
}

static void cmd_log_init(int argc, const char **argv, const char *prefix,
		      struct rev_info *rev)
{
	int i;
	int decorate = 0;

	rev->abbrev = DEFAULT_ABBREV;
	rev->commit_format = CMIT_FMT_DEFAULT;
	rev->verbose_header = 1;
	rev->show_root_diff = default_show_root;
	argc = setup_revisions(argc, argv, rev, "HEAD");
	if (rev->diffopt.pickaxe || rev->diffopt.filter)
		rev->always_show_header = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--decorate")) {
			if (!decorate)
				for_each_ref(add_ref_decoration, NULL);
			decorate = 1;
		} else
			die("unrecognized argument: %s", arg);
	}
}

static int cmd_log_walk(struct rev_info *rev)
{
	struct commit *commit;

	prepare_revision_walk(rev);
	while ((commit = get_revision(rev)) != NULL) {
		log_tree_commit(rev, commit);
		if (!rev->reflog_info) {
			/* we allow cycles in reflog ancestry */
			free(commit->buffer);
			commit->buffer = NULL;
		}
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	return 0;
}

static int git_log_config(const char *var, const char *value)
{
	if (!strcmp(var, "log.showroot")) {
		default_show_root = git_config_bool(var, value);
		return 0;
	}
	return git_diff_ui_config(var, value);
}

int cmd_whatchanged(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;

	git_config(git_log_config);
	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.simplify_history = 0;
	cmd_log_init(argc, argv, prefix, &rev);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return cmd_log_walk(&rev);
}

static int show_object(const unsigned char *sha1, int suppress_header)
{
	unsigned long size;
	enum object_type type;
	char *buf = read_sha1_file(sha1, &type, &size);
	int offset = 0;

	if (!buf)
		return error("Could not read object %s", sha1_to_hex(sha1));

	if (suppress_header)
		while (offset < size && buf[offset++] != '\n') {
			int new_offset = offset;
			while (new_offset < size && buf[new_offset++] != '\n')
				; /* do nothing */
			offset = new_offset;
		}

	if (offset < size)
		fwrite(buf + offset, size - offset, 1, stdout);
	free(buf);
	return 0;
}

static int show_tree_object(const unsigned char *sha1,
		const char *base, int baselen,
		const char *pathname, unsigned mode, int stage)
{
	printf("%s%s\n", pathname, S_ISDIR(mode) ? "/" : "");
	return 0;
}

int cmd_show(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct object_array_entry *objects;
	int i, count, ret = 0;

	git_config(git_log_config);
	init_revisions(&rev, prefix);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.combine_merges = 1;
	rev.dense_combined_merges = 1;
	rev.always_show_header = 1;
	rev.ignore_merges = 0;
	rev.no_walk = 1;
	cmd_log_init(argc, argv, prefix, &rev);

	count = rev.pending.nr;
	objects = rev.pending.objects;
	for (i = 0; i < count && !ret; i++) {
		struct object *o = objects[i].item;
		const char *name = objects[i].name;
		switch (o->type) {
		case OBJ_BLOB:
			ret = show_object(o->sha1, 0);
			break;
		case OBJ_TAG: {
			struct tag *t = (struct tag *)o;

			printf("%stag %s%s\n\n",
					diff_get_color(rev.diffopt.color_diff,
						DIFF_COMMIT),
					t->tag,
					diff_get_color(rev.diffopt.color_diff,
						DIFF_RESET));
			ret = show_object(o->sha1, 1);
			objects[i].item = (struct object *)t->tagged;
			i--;
			break;
		}
		case OBJ_TREE:
			printf("%stree %s%s\n\n",
					diff_get_color(rev.diffopt.color_diff,
						DIFF_COMMIT),
					name,
					diff_get_color(rev.diffopt.color_diff,
						DIFF_RESET));
			read_tree_recursive((struct tree *)o, "", 0, 0, NULL,
					show_tree_object);
			break;
		case OBJ_COMMIT:
			rev.pending.nr = rev.pending.alloc = 0;
			rev.pending.objects = NULL;
			add_object_array(o, name, &rev.pending);
			ret = cmd_log_walk(&rev);
			break;
		default:
			ret = error("Unknown type: %d", o->type);
		}
	}
	free(objects);
	return ret;
}

/*
 * This is equivalent to "git log -g --abbrev-commit --pretty=oneline"
 */
int cmd_log_reflog(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;

	git_config(git_log_config);
	init_revisions(&rev, prefix);
	init_reflog_walk(&rev.reflog_info);
	rev.abbrev_commit = 1;
	rev.verbose_header = 1;
	cmd_log_init(argc, argv, prefix, &rev);

	/*
	 * This means that we override whatever commit format the user gave
	 * on the cmd line.  Sad, but cmd_log_init() currently doesn't
	 * allow us to set a different default.
	 */
	rev.commit_format = CMIT_FMT_ONELINE;
	rev.always_show_header = 1;

	/*
	 * We get called through "git reflog", so unlike the other log
	 * routines, we need to set up our pager manually..
	 */
	setup_pager();

	return cmd_log_walk(&rev);
}

int cmd_log(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;

	git_config(git_log_config);
	init_revisions(&rev, prefix);
	rev.always_show_header = 1;
	cmd_log_init(argc, argv, prefix, &rev);
	return cmd_log_walk(&rev);
}

/* format-patch */
#define FORMAT_PATCH_NAME_MAX 64

static int istitlechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '.' || c == '_';
}

static char *extra_headers = NULL;
static int extra_headers_size = 0;
static const char *fmt_patch_suffix = ".patch";

static int git_format_config(const char *var, const char *value)
{
	if (!strcmp(var, "format.headers")) {
		int len;

		if (!value)
			die("format.headers without value");
		len = strlen(value);
		extra_headers_size += len + 1;
		extra_headers = xrealloc(extra_headers, extra_headers_size);
		extra_headers[extra_headers_size - len - 1] = 0;
		strcat(extra_headers, value);
		return 0;
	}
	if (!strcmp(var, "format.suffix")) {
		if (!value)
			die("format.suffix without value");
		fmt_patch_suffix = xstrdup(value);
		return 0;
	}
	if (!strcmp(var, "diff.color") || !strcmp(var, "color.diff")) {
		return 0;
	}
	return git_log_config(var, value);
}


static FILE *realstdout = NULL;
static const char *output_directory = NULL;

static int reopen_stdout(struct commit *commit, int nr, int keep_subject,
			 int numbered_files)
{
	char filename[PATH_MAX];
	char *sol;
	int len = 0;
	int suffix_len = strlen(fmt_patch_suffix) + 1;

	if (output_directory) {
		if (strlen(output_directory) >=
		    sizeof(filename) - FORMAT_PATCH_NAME_MAX - suffix_len)
			return error("name of output directory is too long");
		strlcpy(filename, output_directory, sizeof(filename) - suffix_len);
		len = strlen(filename);
		if (filename[len - 1] != '/')
			filename[len++] = '/';
	}

	if (numbered_files) {
		sprintf(filename + len, "%d", nr);
		len = strlen(filename);

	} else {
		sprintf(filename + len, "%04d", nr);
		len = strlen(filename);

		sol = strstr(commit->buffer, "\n\n");
		if (sol) {
			int j, space = 1;

			sol += 2;
			/* strip [PATCH] or [PATCH blabla] */
			if (!keep_subject && !prefixcmp(sol, "[PATCH")) {
				char *eos = strchr(sol + 6, ']');
				if (eos) {
					while (isspace(*eos))
						eos++;
					sol = eos;
				}
			}

			for (j = 0;
			     j < FORMAT_PATCH_NAME_MAX - suffix_len - 5 &&
				     len < sizeof(filename) - suffix_len &&
				     sol[j] && sol[j] != '\n';
			     j++) {
				if (istitlechar(sol[j])) {
					if (space) {
						filename[len++] = '-';
						space = 0;
					}
					filename[len++] = sol[j];
					if (sol[j] == '.')
						while (sol[j + 1] == '.')
							j++;
				} else
					space = 1;
			}
			while (filename[len - 1] == '.'
			       || filename[len - 1] == '-')
				len--;
			filename[len] = 0;
		}
		if (len + suffix_len >= sizeof(filename))
			return error("Patch pathname too long");
		strcpy(filename + len, fmt_patch_suffix);
	}

	fprintf(realstdout, "%s\n", filename);
	if (freopen(filename, "w", stdout) == NULL)
		return error("Cannot open patch file %s",filename);

	return 0;
}

static void get_patch_ids(struct rev_info *rev, struct patch_ids *ids, const char *prefix)
{
	struct rev_info check_rev;
	struct commit *commit;
	struct object *o1, *o2;
	unsigned flags1, flags2;

	if (rev->pending.nr != 2)
		die("Need exactly one range.");

	o1 = rev->pending.objects[0].item;
	flags1 = o1->flags;
	o2 = rev->pending.objects[1].item;
	flags2 = o2->flags;

	if ((flags1 & UNINTERESTING) == (flags2 & UNINTERESTING))
		die("Not a range.");

	init_patch_ids(ids);

	/* given a range a..b get all patch ids for b..a */
	init_revisions(&check_rev, prefix);
	o1->flags ^= UNINTERESTING;
	o2->flags ^= UNINTERESTING;
	add_pending_object(&check_rev, o1, "o1");
	add_pending_object(&check_rev, o2, "o2");
	prepare_revision_walk(&check_rev);

	while ((commit = get_revision(&check_rev)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		add_commit_patch_id(commit, ids);
	}

	/* reset for next revision walk */
	clear_commit_marks((struct commit *)o1,
			SEEN | UNINTERESTING | SHOWN | ADDED);
	clear_commit_marks((struct commit *)o2,
			SEEN | UNINTERESTING | SHOWN | ADDED);
	o1->flags = flags1;
	o2->flags = flags2;
}

static void gen_message_id(char *dest, unsigned int length, char *base)
{
	const char *committer = git_committer_info(-1);
	const char *email_start = strrchr(committer, '<');
	const char *email_end = strrchr(committer, '>');
	if(!email_start || !email_end || email_start > email_end - 1)
		die("Could not extract email from committer identity.");
	snprintf(dest, length, "%s.%lu.git.%.*s", base,
		 (unsigned long) time(NULL),
		 (int)(email_end - email_start - 1), email_start + 1);
}

int cmd_format_patch(int argc, const char **argv, const char *prefix)
{
	struct commit *commit;
	struct commit **list = NULL;
	struct rev_info rev;
	int nr = 0, total, i, j;
	int use_stdout = 0;
	int numbered = 0;
	int start_number = -1;
	int keep_subject = 0;
	int numbered_files = 0;		/* _just_ numbers */
	int subject_prefix = 0;
	int ignore_if_in_upstream = 0;
	int thread = 0;
	const char *in_reply_to = NULL;
	struct patch_ids ids;
	char *add_signoff = NULL;
	char message_id[1024];
	char ref_message_id[1024];

	git_config(git_format_config);
	init_revisions(&rev, prefix);
	rev.commit_format = CMIT_FMT_EMAIL;
	rev.verbose_header = 1;
	rev.diff = 1;
	rev.combine_merges = 0;
	rev.ignore_merges = 1;
	rev.diffopt.msg_sep = "";
	rev.diffopt.recursive = 1;

	rev.extra_headers = extra_headers;

	/*
	 * Parse the arguments before setup_revisions(), or something
	 * like "git format-patch -o a123 HEAD^.." may fail; a123 is
	 * possibly a valid SHA1.
	 */
	for (i = 1, j = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--stdout"))
			use_stdout = 1;
		else if (!strcmp(argv[i], "-n") ||
				!strcmp(argv[i], "--numbered"))
			numbered = 1;
		else if (!prefixcmp(argv[i], "--start-number="))
			start_number = strtol(argv[i] + 15, NULL, 10);
		else if (!strcmp(argv[i], "--numbered-files"))
			numbered_files = 1;
		else if (!strcmp(argv[i], "--start-number")) {
			i++;
			if (i == argc)
				die("Need a number for --start-number");
			start_number = strtol(argv[i], NULL, 10);
		}
		else if (!strcmp(argv[i], "-k") ||
				!strcmp(argv[i], "--keep-subject")) {
			keep_subject = 1;
			rev.total = -1;
		}
		else if (!strcmp(argv[i], "--output-directory") ||
			 !strcmp(argv[i], "-o")) {
			i++;
			if (argc <= i)
				die("Which directory?");
			if (output_directory)
				die("Two output directories?");
			output_directory = argv[i];
		}
		else if (!strcmp(argv[i], "--signoff") ||
			 !strcmp(argv[i], "-s")) {
			const char *committer;
			const char *endpos;
			committer = git_committer_info(1);
			endpos = strchr(committer, '>');
			if (!endpos)
				die("bogos committer info %s\n", committer);
			add_signoff = xmalloc(endpos - committer + 2);
			memcpy(add_signoff, committer, endpos - committer + 1);
			add_signoff[endpos - committer + 1] = 0;
		}
		else if (!strcmp(argv[i], "--attach")) {
			rev.mime_boundary = git_version_string;
			rev.no_inline = 1;
		}
		else if (!prefixcmp(argv[i], "--attach=")) {
			rev.mime_boundary = argv[i] + 9;
			rev.no_inline = 1;
		}
		else if (!strcmp(argv[i], "--inline")) {
			rev.mime_boundary = git_version_string;
			rev.no_inline = 0;
		}
		else if (!prefixcmp(argv[i], "--inline=")) {
			rev.mime_boundary = argv[i] + 9;
			rev.no_inline = 0;
		}
		else if (!strcmp(argv[i], "--ignore-if-in-upstream"))
			ignore_if_in_upstream = 1;
		else if (!strcmp(argv[i], "--thread"))
			thread = 1;
		else if (!prefixcmp(argv[i], "--in-reply-to="))
			in_reply_to = argv[i] + 14;
		else if (!strcmp(argv[i], "--in-reply-to")) {
			i++;
			if (i == argc)
				die("Need a Message-Id for --in-reply-to");
			in_reply_to = argv[i];
		} else if (!prefixcmp(argv[i], "--subject-prefix=")) {
			subject_prefix = 1;
			rev.subject_prefix = argv[i] + 17;
		} else if (!prefixcmp(argv[i], "--suffix="))
			fmt_patch_suffix = argv[i] + 9;
		else
			argv[j++] = argv[i];
	}
	argc = j;

	if (start_number < 0)
		start_number = 1;
	if (numbered && keep_subject)
		die ("-n and -k are mutually exclusive.");
	if (keep_subject && subject_prefix)
		die ("--subject-prefix and -k are mutually exclusive.");
	if (numbered_files && use_stdout)
		die ("--numbered-files and --stdout are mutually exclusive.");

	argc = setup_revisions(argc, argv, &rev, "HEAD");
	if (argc > 1)
		die ("unrecognized argument: %s", argv[1]);

	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_SUMMARY | DIFF_FORMAT_PATCH;

	if (!rev.diffopt.text)
		rev.diffopt.binary = 1;

	if (!output_directory && !use_stdout)
		output_directory = prefix;

	if (output_directory) {
		if (use_stdout)
			die("standard output, or directory, which one?");
		if (mkdir(output_directory, 0777) < 0 && errno != EEXIST)
			die("Could not create directory %s",
			    output_directory);
	}

	if (rev.pending.nr == 1) {
		if (rev.max_count < 0) {
			rev.pending.objects[0].item->flags |= UNINTERESTING;
			add_head(&rev);
		}
		/* Otherwise, it is "format-patch -22 HEAD", and
		 * get_revision() would return only the specified count.
		 */
	}

	if (ignore_if_in_upstream)
		get_patch_ids(&rev, &ids, prefix);

	if (!use_stdout)
		realstdout = fdopen(dup(1), "w");

	prepare_revision_walk(&rev);
	while ((commit = get_revision(&rev)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		if (ignore_if_in_upstream &&
				has_commit_patch_id(commit, &ids))
			continue;

		nr++;
		list = xrealloc(list, nr * sizeof(list[0]));
		list[nr - 1] = commit;
	}
	total = nr;
	if (numbered)
		rev.total = total + start_number - 1;
	rev.add_signoff = add_signoff;
	rev.ref_message_id = in_reply_to;
	while (0 <= --nr) {
		int shown;
		commit = list[nr];
		rev.nr = total - nr + (start_number - 1);
		/* Make the second and subsequent mails replies to the first */
		if (thread) {
			if (nr == (total - 2)) {
				strncpy(ref_message_id, message_id,
					sizeof(ref_message_id));
				ref_message_id[sizeof(ref_message_id)-1]='\0';
				rev.ref_message_id = ref_message_id;
			}
			gen_message_id(message_id, sizeof(message_id),
				       sha1_to_hex(commit->object.sha1));
			rev.message_id = message_id;
		}
		if (!use_stdout)
			if (reopen_stdout(commit, rev.nr, keep_subject,
					  numbered_files))
				die("Failed to create output files");
		shown = log_tree_commit(&rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;

		/* We put one extra blank line between formatted
		 * patches and this flag is used by log-tree code
		 * to see if it needs to emit a LF before showing
		 * the log; when using one file per patch, we do
		 * not want the extra blank line.
		 */
		if (!use_stdout)
			rev.shown_one = 0;
		if (shown) {
			if (rev.mime_boundary)
				printf("\n--%s%s--\n\n\n",
				       mime_boundary_leader,
				       rev.mime_boundary);
			else
				printf("-- \n%s\n\n", git_version_string);
		}
		if (!use_stdout)
			fclose(stdout);
	}
	free(list);
	if (ignore_if_in_upstream)
		free_patch_ids(&ids);
	return 0;
}

static int add_pending_commit(const char *arg, struct rev_info *revs, int flags)
{
	unsigned char sha1[20];
	if (get_sha1(arg, sha1) == 0) {
		struct commit *commit = lookup_commit_reference(sha1);
		if (commit) {
			commit->object.flags |= flags;
			add_pending_object(revs, &commit->object, arg);
			return 0;
		}
	}
	return -1;
}

static const char cherry_usage[] =
"git-cherry [-v] <upstream> [<head>] [<limit>]";
int cmd_cherry(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct patch_ids ids;
	struct commit *commit;
	struct commit_list *list = NULL;
	const char *upstream;
	const char *head = "HEAD";
	const char *limit = NULL;
	int verbose = 0;

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		verbose = 1;
		argc--;
		argv++;
	}

	switch (argc) {
	case 4:
		limit = argv[3];
		/* FALLTHROUGH */
	case 3:
		head = argv[2];
		/* FALLTHROUGH */
	case 2:
		upstream = argv[1];
		break;
	default:
		usage(cherry_usage);
	}

	init_revisions(&revs, prefix);
	revs.diff = 1;
	revs.combine_merges = 0;
	revs.ignore_merges = 1;
	revs.diffopt.recursive = 1;

	if (add_pending_commit(head, &revs, 0))
		die("Unknown commit %s", head);
	if (add_pending_commit(upstream, &revs, UNINTERESTING))
		die("Unknown commit %s", upstream);

	/* Don't say anything if head and upstream are the same. */
	if (revs.pending.nr == 2) {
		struct object_array_entry *o = revs.pending.objects;
		if (hashcmp(o[0].item->sha1, o[1].item->sha1) == 0)
			return 0;
	}

	get_patch_ids(&revs, &ids, prefix);

	if (limit && add_pending_commit(limit, &revs, UNINTERESTING))
		die("Unknown commit %s", limit);

	/* reverse the list of commits */
	prepare_revision_walk(&revs);
	while ((commit = get_revision(&revs)) != NULL) {
		/* ignore merges */
		if (commit->parents && commit->parents->next)
			continue;

		commit_list_insert(commit, &list);
	}

	while (list) {
		char sign = '+';

		commit = list->item;
		if (has_commit_patch_id(commit, &ids))
			sign = '-';

		if (verbose) {
			static char buf[16384];
			pretty_print_commit(CMIT_FMT_ONELINE, commit, ~0,
			                    buf, sizeof(buf), 0, NULL, NULL, 0);
			printf("%c %s %s\n", sign,
			       sha1_to_hex(commit->object.sha1), buf);
		}
		else {
			printf("%c %s\n", sign,
			       sha1_to_hex(commit->object.sha1));
		}

		list = list->next;
	}

	free_patch_ids(&ids);
	return 0;
}
