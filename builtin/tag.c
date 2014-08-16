/*
 * Builtin "git tag"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *                    Carlos Rica <jasampler@gmail.com>
 * Based on git-tag.sh and mktag.c by Linus Torvalds.
 */

#include "cache.h"
#include "builtin.h"
#include "refs.h"
#include "tag.h"
#include "run-command.h"
#include "parse-options.h"
#include "diff.h"
#include "revision.h"
#include "gpg-interface.h"
#include "sha1-array.h"
#include "column.h"

static const char * const git_tag_usage[] = {
	N_("git tag [-a|-s|-u <key-id>] [-f] [-m <msg>|-F <file>] <tagname> [<head>]"),
	N_("git tag -d <tagname>..."),
	N_("git tag -l [-n[<num>]] [--contains <commit>] [--points-at <object>] "
		"\n\t\t[<pattern>...]"),
	N_("git tag -v <tagname>..."),
	NULL
};

#define STRCMP_SORT     0	/* must be zero */
#define VERCMP_SORT     1
#define SORT_MASK       0x7fff
#define REVERSE_SORT    0x8000

static int tag_sort;

struct tag_filter {
	const char **patterns;
	int lines;
	int sort;
	struct string_list tags;
	struct commit_list *with_commit;
};

static struct sha1_array points_at;
static unsigned int colopts;

static int match_pattern(const char **patterns, const char *ref)
{
	/* no pattern means match everything */
	if (!*patterns)
		return 1;
	for (; *patterns; patterns++)
		if (!wildmatch(*patterns, ref, 0, NULL))
			return 1;
	return 0;
}

static const unsigned char *match_points_at(const char *refname,
					    const unsigned char *sha1)
{
	const unsigned char *tagged_sha1 = NULL;
	struct object *obj;

	if (sha1_array_lookup(&points_at, sha1) >= 0)
		return sha1;
	obj = parse_object(sha1);
	if (!obj)
		die(_("malformed object at '%s'"), refname);
	if (obj->type == OBJ_TAG)
		tagged_sha1 = ((struct tag *)obj)->tagged->sha1;
	if (tagged_sha1 && sha1_array_lookup(&points_at, tagged_sha1) >= 0)
		return tagged_sha1;
	return NULL;
}

static int in_commit_list(const struct commit_list *want, struct commit *c)
{
	for (; want; want = want->next)
		if (!hashcmp(want->item->object.sha1, c->object.sha1))
			return 1;
	return 0;
}

enum contains_result {
	CONTAINS_UNKNOWN = -1,
	CONTAINS_NO = 0,
	CONTAINS_YES = 1
};

/*
 * Test whether the candidate or one of its parents is contained in the list.
 * Do not recurse to find out, though, but return -1 if inconclusive.
 */
static enum contains_result contains_test(struct commit *candidate,
			    const struct commit_list *want)
{
	/* was it previously marked as containing a want commit? */
	if (candidate->object.flags & TMP_MARK)
		return 1;
	/* or marked as not possibly containing a want commit? */
	if (candidate->object.flags & UNINTERESTING)
		return 0;
	/* or are we it? */
	if (in_commit_list(want, candidate)) {
		candidate->object.flags |= TMP_MARK;
		return 1;
	}

	if (parse_commit(candidate) < 0)
		return 0;

	return -1;
}

/*
 * Mimicking the real stack, this stack lives on the heap, avoiding stack
 * overflows.
 *
 * At each recursion step, the stack items points to the commits whose
 * ancestors are to be inspected.
 */
struct stack {
	int nr, alloc;
	struct stack_entry {
		struct commit *commit;
		struct commit_list *parents;
	} *stack;
};

static void push_to_stack(struct commit *candidate, struct stack *stack)
{
	int index = stack->nr++;
	ALLOC_GROW(stack->stack, stack->nr, stack->alloc);
	stack->stack[index].commit = candidate;
	stack->stack[index].parents = candidate->parents;
}

static enum contains_result contains(struct commit *candidate,
		const struct commit_list *want)
{
	struct stack stack = { 0, 0, NULL };
	int result = contains_test(candidate, want);

	if (result != CONTAINS_UNKNOWN)
		return result;

	push_to_stack(candidate, &stack);
	while (stack.nr) {
		struct stack_entry *entry = &stack.stack[stack.nr - 1];
		struct commit *commit = entry->commit;
		struct commit_list *parents = entry->parents;

		if (!parents) {
			commit->object.flags |= UNINTERESTING;
			stack.nr--;
		}
		/*
		 * If we just popped the stack, parents->item has been marked,
		 * therefore contains_test will return a meaningful 0 or 1.
		 */
		else switch (contains_test(parents->item, want)) {
		case CONTAINS_YES:
			commit->object.flags |= TMP_MARK;
			stack.nr--;
			break;
		case CONTAINS_NO:
			entry->parents = parents->next;
			break;
		case CONTAINS_UNKNOWN:
			push_to_stack(parents->item, &stack);
			break;
		}
	}
	free(stack.stack);
	return contains_test(candidate, want);
}

static void show_tag_lines(const unsigned char *sha1, int lines)
{
	int i;
	unsigned long size;
	enum object_type type;
	char *buf, *sp, *eol;
	size_t len;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		die_errno("unable to read object %s", sha1_to_hex(sha1));
	if (type != OBJ_COMMIT && type != OBJ_TAG)
		goto free_return;
	if (!size)
		die("an empty %s object %s?",
		    typename(type), sha1_to_hex(sha1));

	/* skip header */
	sp = strstr(buf, "\n\n");
	if (!sp)
		goto free_return;

	/* only take up to "lines" lines, and strip the signature from a tag */
	if (type == OBJ_TAG)
		size = parse_signature(buf, size);
	for (i = 0, sp += 2; i < lines && sp < buf + size; i++) {
		if (i)
			printf("\n    ");
		eol = memchr(sp, '\n', size - (sp - buf));
		len = eol ? eol - sp : size - (sp - buf);
		fwrite(sp, len, 1, stdout);
		if (!eol)
			break;
		sp = eol + 1;
	}
free_return:
	free(buf);
}

static int show_reference(const char *refname, const unsigned char *sha1,
			  int flag, void *cb_data)
{
	struct tag_filter *filter = cb_data;

	if (match_pattern(filter->patterns, refname)) {
		if (filter->with_commit) {
			struct commit *commit;

			commit = lookup_commit_reference_gently(sha1, 1);
			if (!commit)
				return 0;
			if (!contains(commit, filter->with_commit))
				return 0;
		}

		if (points_at.nr && !match_points_at(refname, sha1))
			return 0;

		if (!filter->lines) {
			if (filter->sort)
				string_list_append(&filter->tags, refname);
			else
				printf("%s\n", refname);
			return 0;
		}
		printf("%-15s ", refname);
		show_tag_lines(sha1, filter->lines);
		putchar('\n');
	}

	return 0;
}

static int sort_by_version(const void *a_, const void *b_)
{
	const struct string_list_item *a = a_;
	const struct string_list_item *b = b_;
	return versioncmp(a->string, b->string);
}

static int list_tags(const char **patterns, int lines,
		     struct commit_list *with_commit, int sort)
{
	struct tag_filter filter;

	filter.patterns = patterns;
	filter.lines = lines;
	filter.sort = sort;
	filter.with_commit = with_commit;
	memset(&filter.tags, 0, sizeof(filter.tags));
	filter.tags.strdup_strings = 1;

	for_each_tag_ref(show_reference, (void *) &filter);
	if (sort) {
		int i;
		if ((sort & SORT_MASK) == VERCMP_SORT)
			qsort(filter.tags.items, filter.tags.nr,
			      sizeof(struct string_list_item), sort_by_version);
		if (sort & REVERSE_SORT)
			for (i = filter.tags.nr - 1; i >= 0; i--)
				printf("%s\n", filter.tags.items[i].string);
		else
			for (i = 0; i < filter.tags.nr; i++)
				printf("%s\n", filter.tags.items[i].string);
		string_list_clear(&filter.tags, 0);
	}
	return 0;
}

typedef int (*each_tag_name_fn)(const char *name, const char *ref,
				const unsigned char *sha1);

static int for_each_tag_name(const char **argv, each_tag_name_fn fn)
{
	const char **p;
	char ref[PATH_MAX];
	int had_error = 0;
	unsigned char sha1[20];

	for (p = argv; *p; p++) {
		if (snprintf(ref, sizeof(ref), "refs/tags/%s", *p)
					>= sizeof(ref)) {
			error(_("tag name too long: %.*s..."), 50, *p);
			had_error = 1;
			continue;
		}
		if (read_ref(ref, sha1)) {
			error(_("tag '%s' not found."), *p);
			had_error = 1;
			continue;
		}
		if (fn(*p, ref, sha1))
			had_error = 1;
	}
	return had_error;
}

static int delete_tag(const char *name, const char *ref,
				const unsigned char *sha1)
{
	if (delete_ref(ref, sha1, 0))
		return 1;
	printf(_("Deleted tag '%s' (was %s)\n"), name, find_unique_abbrev(sha1, DEFAULT_ABBREV));
	return 0;
}

static int verify_tag(const char *name, const char *ref,
				const unsigned char *sha1)
{
	const char *argv_verify_tag[] = {"verify-tag",
					"-v", "SHA1_HEX", NULL};
	argv_verify_tag[2] = sha1_to_hex(sha1);

	if (run_command_v_opt(argv_verify_tag, RUN_GIT_CMD))
		return error(_("could not verify the tag '%s'"), name);
	return 0;
}

static int do_sign(struct strbuf *buffer)
{
	return sign_buffer(buffer, buffer, get_signing_key());
}

static const char tag_template[] =
	N_("\nWrite a message for tag:\n  %s\n"
	"Lines starting with '%c' will be ignored.\n");

static const char tag_template_nocleanup[] =
	N_("\nWrite a message for tag:\n  %s\n"
	"Lines starting with '%c' will be kept; you may remove them"
	" yourself if you want to.\n");

/*
 * Parse a sort string, and return 0 if parsed successfully. Will return
 * non-zero when the sort string does not parse into a known type. If var is
 * given, the error message becomes a warning and includes information about
 * the configuration value.
 */
static int parse_sort_string(const char *var, const char *arg, int *sort)
{
	int type = 0, flags = 0;

	if (skip_prefix(arg, "-", &arg))
		flags |= REVERSE_SORT;

	if (skip_prefix(arg, "version:", &arg) || skip_prefix(arg, "v:", &arg))
		type = VERCMP_SORT;
	else
		type = STRCMP_SORT;

	if (strcmp(arg, "refname")) {
		if (!var)
			return error(_("unsupported sort specification '%s'"), arg);
		else {
			warning(_("unsupported sort specification '%s' in variable '%s'"),
				var, arg);
			return -1;
		}
	}

	*sort = (type | flags);

	return 0;
}

static int git_tag_config(const char *var, const char *value, void *cb)
{
	int status;

	if (!strcmp(var, "tag.sort")) {
		if (!value)
			return config_error_nonbool(var);
		parse_sort_string(var, value, &tag_sort);
		return 0;
	}

	status = git_gpg_config(var, value, cb);
	if (status)
		return status;
	if (starts_with(var, "column."))
		return git_column_config(var, value, "tag", &colopts);
	return git_default_config(var, value, cb);
}

static void write_tag_body(int fd, const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	char *buf, *sp;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		return;
	/* skip header */
	sp = strstr(buf, "\n\n");

	if (!sp || !size || type != OBJ_TAG) {
		free(buf);
		return;
	}
	sp += 2; /* skip the 2 LFs */
	write_or_die(fd, sp, parse_signature(sp, buf + size - sp));

	free(buf);
}

static int build_tag_object(struct strbuf *buf, int sign, unsigned char *result)
{
	if (sign && do_sign(buf) < 0)
		return error(_("unable to sign the tag"));
	if (write_sha1_file(buf->buf, buf->len, tag_type, result) < 0)
		return error(_("unable to write tag file"));
	return 0;
}

struct create_tag_options {
	unsigned int message_given:1;
	unsigned int sign;
	enum {
		CLEANUP_NONE,
		CLEANUP_SPACE,
		CLEANUP_ALL
	} cleanup_mode;
};

static void create_tag(const unsigned char *object, const char *tag,
		       struct strbuf *buf, struct create_tag_options *opt,
		       unsigned char *prev, unsigned char *result)
{
	enum object_type type;
	char header_buf[1024];
	int header_len;
	char *path = NULL;

	type = sha1_object_info(object, NULL);
	if (type <= OBJ_NONE)
	    die(_("bad object type."));

	header_len = snprintf(header_buf, sizeof(header_buf),
			  "object %s\n"
			  "type %s\n"
			  "tag %s\n"
			  "tagger %s\n\n",
			  sha1_to_hex(object),
			  typename(type),
			  tag,
			  git_committer_info(IDENT_STRICT));

	if (header_len > sizeof(header_buf) - 1)
		die(_("tag header too big."));

	if (!opt->message_given) {
		int fd;

		/* write the template message before editing: */
		path = git_pathdup("TAG_EDITMSG");
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die_errno(_("could not create file '%s'"), path);

		if (!is_null_sha1(prev)) {
			write_tag_body(fd, prev);
		} else {
			struct strbuf buf = STRBUF_INIT;
			strbuf_addch(&buf, '\n');
			if (opt->cleanup_mode == CLEANUP_ALL)
				strbuf_commented_addf(&buf, _(tag_template), tag, comment_line_char);
			else
				strbuf_commented_addf(&buf, _(tag_template_nocleanup), tag, comment_line_char);
			write_or_die(fd, buf.buf, buf.len);
			strbuf_release(&buf);
		}
		close(fd);

		if (launch_editor(path, buf, NULL)) {
			fprintf(stderr,
			_("Please supply the message using either -m or -F option.\n"));
			exit(1);
		}
	}

	if (opt->cleanup_mode != CLEANUP_NONE)
		stripspace(buf, opt->cleanup_mode == CLEANUP_ALL);

	if (!opt->message_given && !buf->len)
		die(_("no tag message?"));

	strbuf_insert(buf, 0, header_buf, header_len);

	if (build_tag_object(buf, opt->sign, result) < 0) {
		if (path)
			fprintf(stderr, _("The tag message has been left in %s\n"),
				path);
		exit(128);
	}
	if (path) {
		unlink_or_warn(path);
		free(path);
	}
}

struct msg_arg {
	int given;
	struct strbuf buf;
};

static int parse_msg_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;

	if (!arg)
		return -1;
	if (msg->buf.len)
		strbuf_addstr(&(msg->buf), "\n\n");
	strbuf_addstr(&(msg->buf), arg);
	msg->given = 1;
	return 0;
}

static int strbuf_check_tag_ref(struct strbuf *sb, const char *name)
{
	if (name[0] == '-')
		return -1;

	strbuf_reset(sb);
	strbuf_addf(sb, "refs/tags/%s", name);

	return check_refname_format(sb->buf, 0);
}

static int parse_opt_points_at(const struct option *opt __attribute__((unused)),
			const char *arg, int unset)
{
	unsigned char sha1[20];

	if (unset) {
		sha1_array_clear(&points_at);
		return 0;
	}
	if (!arg)
		return error(_("switch 'points-at' requires an object"));
	if (get_sha1(arg, sha1))
		return error(_("malformed object name '%s'"), arg);
	sha1_array_append(&points_at, sha1);
	return 0;
}

static int parse_opt_sort(const struct option *opt, const char *arg, int unset)
{
	int *sort = opt->value;

	return parse_sort_string(NULL, arg, sort);
}

int cmd_tag(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf ref = STRBUF_INIT;
	unsigned char object[20], prev[20];
	const char *object_ref, *tag;
	struct ref_lock *lock;
	struct create_tag_options opt;
	char *cleanup_arg = NULL;
	int annotate = 0, force = 0, lines = -1;
	int cmdmode = 0;
	const char *msgfile = NULL, *keyid = NULL;
	struct msg_arg msg = { 0, STRBUF_INIT };
	struct commit_list *with_commit = NULL;
	struct option options[] = {
		OPT_CMDMODE('l', "list", &cmdmode, N_("list tag names"), 'l'),
		{ OPTION_INTEGER, 'n', NULL, &lines, N_("n"),
				N_("print <n> lines of each tag message"),
				PARSE_OPT_OPTARG, NULL, 1 },
		OPT_CMDMODE('d', "delete", &cmdmode, N_("delete tags"), 'd'),
		OPT_CMDMODE('v', "verify", &cmdmode, N_("verify tags"), 'v'),

		OPT_GROUP(N_("Tag creation options")),
		OPT_BOOL('a', "annotate", &annotate,
					N_("annotated tag, needs a message")),
		OPT_CALLBACK('m', "message", &msg, N_("message"),
			     N_("tag message"), parse_msg_arg),
		OPT_FILENAME('F', "file", &msgfile, N_("read message from file")),
		OPT_BOOL('s', "sign", &opt.sign, N_("annotated and GPG-signed tag")),
		OPT_STRING(0, "cleanup", &cleanup_arg, N_("mode"),
			N_("how to strip spaces and #comments from message")),
		OPT_STRING('u', "local-user", &keyid, N_("key-id"),
					N_("use another key to sign the tag")),
		OPT__FORCE(&force, N_("replace the tag if exists")),
		OPT_COLUMN(0, "column", &colopts, N_("show tag list in columns")),
		{
			OPTION_CALLBACK, 0, "sort", &tag_sort, N_("type"), N_("sort tags"),
			PARSE_OPT_NONEG, parse_opt_sort
		},

		OPT_GROUP(N_("Tag listing options")),
		{
			OPTION_CALLBACK, 0, "contains", &with_commit, N_("commit"),
			N_("print only tags that contain the commit"),
			PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_with_commit, (intptr_t)"HEAD",
		},
		{
			OPTION_CALLBACK, 0, "with", &with_commit, N_("commit"),
			N_("print only tags that contain the commit"),
			PARSE_OPT_HIDDEN | PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_with_commit, (intptr_t)"HEAD",
		},
		{
			OPTION_CALLBACK, 0, "points-at", NULL, N_("object"),
			N_("print only tags of the object"), 0, parse_opt_points_at
		},
		OPT_END()
	};

	git_config(git_tag_config, NULL);

	memset(&opt, 0, sizeof(opt));

	argc = parse_options(argc, argv, prefix, options, git_tag_usage, 0);

	if (keyid) {
		opt.sign = 1;
		set_signing_key(keyid);
	}
	if (opt.sign)
		annotate = 1;
	if (argc == 0 && !cmdmode)
		cmdmode = 'l';

	if ((annotate || msg.given || msgfile || force) && (cmdmode != 0))
		usage_with_options(git_tag_usage, options);

	finalize_colopts(&colopts, -1);
	if (cmdmode == 'l' && lines != -1) {
		if (explicitly_enable_column(colopts))
			die(_("--column and -n are incompatible"));
		colopts = 0;
	}
	if (cmdmode == 'l') {
		int ret;
		if (column_active(colopts)) {
			struct column_options copts;
			memset(&copts, 0, sizeof(copts));
			copts.padding = 2;
			run_column_filter(colopts, &copts);
		}
		if (lines != -1 && tag_sort)
			die(_("--sort and -n are incompatible"));
		ret = list_tags(argv, lines == -1 ? 0 : lines, with_commit, tag_sort);
		if (column_active(colopts))
			stop_column_filter();
		return ret;
	}
	if (lines != -1)
		die(_("-n option is only allowed with -l."));
	if (with_commit)
		die(_("--contains option is only allowed with -l."));
	if (points_at.nr)
		die(_("--points-at option is only allowed with -l."));
	if (cmdmode == 'd')
		return for_each_tag_name(argv, delete_tag);
	if (cmdmode == 'v')
		return for_each_tag_name(argv, verify_tag);

	if (msg.given || msgfile) {
		if (msg.given && msgfile)
			die(_("only one -F or -m option is allowed."));
		annotate = 1;
		if (msg.given)
			strbuf_addbuf(&buf, &(msg.buf));
		else {
			if (!strcmp(msgfile, "-")) {
				if (strbuf_read(&buf, 0, 1024) < 0)
					die_errno(_("cannot read '%s'"), msgfile);
			} else {
				if (strbuf_read_file(&buf, msgfile, 1024) < 0)
					die_errno(_("could not open or read '%s'"),
						msgfile);
			}
		}
	}

	tag = argv[0];

	object_ref = argc == 2 ? argv[1] : "HEAD";
	if (argc > 2)
		die(_("too many params"));

	if (get_sha1(object_ref, object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	if (strbuf_check_tag_ref(&ref, tag))
		die(_("'%s' is not a valid tag name."), tag);

	if (read_ref(ref.buf, prev))
		hashclr(prev);
	else if (!force)
		die(_("tag '%s' already exists"), tag);

	opt.message_given = msg.given || msgfile;

	if (!cleanup_arg || !strcmp(cleanup_arg, "strip"))
		opt.cleanup_mode = CLEANUP_ALL;
	else if (!strcmp(cleanup_arg, "verbatim"))
		opt.cleanup_mode = CLEANUP_NONE;
	else if (!strcmp(cleanup_arg, "whitespace"))
		opt.cleanup_mode = CLEANUP_SPACE;
	else
		die(_("Invalid cleanup mode %s"), cleanup_arg);

	if (annotate)
		create_tag(object, tag, &buf, &opt, prev, object);

	lock = lock_any_ref_for_update(ref.buf, prev, 0, NULL);
	if (!lock)
		die(_("%s: cannot lock the ref"), ref.buf);
	if (write_ref_sha1(lock, object, NULL) < 0)
		die(_("%s: cannot update the ref"), ref.buf);
	if (force && !is_null_sha1(prev) && hashcmp(prev, object))
		printf(_("Updated tag '%s' (was %s)\n"), tag, find_unique_abbrev(prev, DEFAULT_ABBREV));

	strbuf_release(&buf);
	strbuf_release(&ref);
	return 0;
}
