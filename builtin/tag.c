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
#include "ref-filter.h"

static const char * const git_tag_usage[] = {
	N_("git tag [-a | -s | -u <key-id>] [-f] [-m <msg> | -F <file>] <tagname> [<head>]"),
	N_("git tag -d <tagname>..."),
	N_("git tag -l [-n[<num>]] [--contains <commit>] [--points-at <object>]"
		"\n\t\t[--format=<format>] [--[no-]merged [<commit>]] [<pattern>...]"),
	N_("git tag -v [--format=<format>] <tagname>..."),
	NULL
};

static unsigned int colopts;
static int force_sign_annotate;

static int list_tags(struct ref_filter *filter, struct ref_sorting *sorting, const char *format)
{
	struct ref_array array;
	char *to_free = NULL;
	int i;

	memset(&array, 0, sizeof(array));

	if (filter->lines == -1)
		filter->lines = 0;

	if (!format) {
		if (filter->lines) {
			to_free = xstrfmt("%s %%(contents:lines=%d)",
					  "%(align:15)%(refname:lstrip=2)%(end)",
					  filter->lines);
			format = to_free;
		} else
			format = "%(refname:lstrip=2)";
	}

	verify_ref_format(format);
	filter->with_commit_tag_algo = 1;
	filter_refs(&array, filter, FILTER_REFS_TAGS);
	ref_array_sort(sorting, &array);

	for (i = 0; i < array.nr; i++)
		show_ref_array_item(array.items[i], format, 0);
	ref_array_clear(&array);
	free(to_free);

	return 0;
}

typedef int (*each_tag_name_fn)(const char *name, const char *ref,
				const unsigned char *sha1, const void *cb_data);

static int for_each_tag_name(const char **argv, each_tag_name_fn fn,
			     const void *cb_data)
{
	const char **p;
	struct strbuf ref = STRBUF_INIT;
	int had_error = 0;
	unsigned char sha1[20];

	for (p = argv; *p; p++) {
		strbuf_reset(&ref);
		strbuf_addf(&ref, "refs/tags/%s", *p);
		if (read_ref(ref.buf, sha1)) {
			error(_("tag '%s' not found."), *p);
			had_error = 1;
			continue;
		}
		if (fn(*p, ref.buf, sha1, cb_data))
			had_error = 1;
	}
	strbuf_release(&ref);
	return had_error;
}

static int delete_tag(const char *name, const char *ref,
		      const unsigned char *sha1, const void *cb_data)
{
	if (delete_ref(NULL, ref, sha1, 0))
		return 1;
	printf(_("Deleted tag '%s' (was %s)\n"), name, find_unique_abbrev(sha1, DEFAULT_ABBREV));
	return 0;
}

static int verify_tag(const char *name, const char *ref,
		      const unsigned char *sha1, const void *cb_data)
{
	int flags;
	const char *fmt_pretty = cb_data;
	flags = GPG_VERIFY_VERBOSE;

	if (fmt_pretty)
		flags = GPG_VERIFY_OMIT_STATUS;

	if (gpg_verify_tag(sha1, name, flags))
		return -1;

	if (fmt_pretty)
		pretty_print_ref(name, sha1, fmt_pretty);

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

/* Parse arg given and add it the ref_sorting array */
static int parse_sorting_string(const char *arg, struct ref_sorting **sorting_tail)
{
	struct ref_sorting *s;
	int len;

	s = xcalloc(1, sizeof(*s));
	s->next = *sorting_tail;
	*sorting_tail = s;

	if (*arg == '-') {
		s->reverse = 1;
		arg++;
	}
	if (skip_prefix(arg, "version:", &arg) ||
	    skip_prefix(arg, "v:", &arg))
		s->version = 1;

	len = strlen(arg);
	s->atom = parse_ref_filter_atom(arg, arg+len);

	return 0;
}

static int git_tag_config(const char *var, const char *value, void *cb)
{
	int status;
	struct ref_sorting **sorting_tail = (struct ref_sorting **)cb;

	if (!strcmp(var, "tag.sort")) {
		if (!value)
			return config_error_nonbool(var);
		parse_sorting_string(value, sorting_tail);
		return 0;
	}

	status = git_gpg_config(var, value, cb);
	if (status)
		return status;
	if (!strcmp(var, "tag.forcesignannotated")) {
		force_sign_annotate = git_config_bool(var, value);
		return 0;
	}

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
	struct strbuf header = STRBUF_INIT;
	char *path = NULL;

	type = sha1_object_info(object, NULL);
	if (type <= OBJ_NONE)
	    die(_("bad object type."));

	strbuf_addf(&header,
		    "object %s\n"
		    "type %s\n"
		    "tag %s\n"
		    "tagger %s\n\n",
		    sha1_to_hex(object),
		    typename(type),
		    tag,
		    git_committer_info(IDENT_STRICT));

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
		strbuf_stripspace(buf, opt->cleanup_mode == CLEANUP_ALL);

	if (!opt->message_given && !buf->len)
		die(_("no tag message?"));

	strbuf_insert(buf, 0, header.buf, header.len);
	strbuf_release(&header);

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

static void create_reflog_msg(const unsigned char *sha1, struct strbuf *sb)
{
	enum object_type type;
	struct commit *c;
	char *buf;
	unsigned long size;
	int subject_len = 0;
	const char *subject_start;

	char *rla = getenv("GIT_REFLOG_ACTION");
	if (rla) {
		strbuf_addstr(sb, rla);
	} else {
		strbuf_addstr(sb, _("tag: tagging "));
		strbuf_add_unique_abbrev(sb, sha1, DEFAULT_ABBREV);
	}

	strbuf_addstr(sb, " (");
	type = sha1_object_info(sha1, NULL);
	switch (type) {
	default:
		strbuf_addstr(sb, _("object of unknown type"));
		break;
	case OBJ_COMMIT:
		if ((buf = read_sha1_file(sha1, &type, &size)) != NULL) {
			subject_len = find_commit_subject(buf, &subject_start);
			strbuf_insert(sb, sb->len, subject_start, subject_len);
		} else {
			strbuf_addstr(sb, _("commit object"));
		}
		free(buf);

		if ((c = lookup_commit_reference(sha1)) != NULL)
			strbuf_addf(sb, ", %s", show_date(c->date, 0, DATE_MODE(SHORT)));
		break;
	case OBJ_TREE:
		strbuf_addstr(sb, _("tree object"));
		break;
	case OBJ_BLOB:
		strbuf_addstr(sb, _("blob object"));
		break;
	case OBJ_TAG:
		strbuf_addstr(sb, _("other tag object"));
		break;
	}
	strbuf_addch(sb, ')');
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

int cmd_tag(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf ref = STRBUF_INIT;
	struct strbuf reflog_msg = STRBUF_INIT;
	unsigned char object[20], prev[20];
	const char *object_ref, *tag;
	struct create_tag_options opt;
	char *cleanup_arg = NULL;
	int create_reflog = 0;
	int annotate = 0, force = 0;
	int cmdmode = 0, create_tag_object = 0;
	const char *msgfile = NULL, *keyid = NULL;
	struct msg_arg msg = { 0, STRBUF_INIT };
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	struct ref_filter filter;
	static struct ref_sorting *sorting = NULL, **sorting_tail = &sorting;
	const char *format = NULL;
	int icase = 0;
	struct option options[] = {
		OPT_CMDMODE('l', "list", &cmdmode, N_("list tag names"), 'l'),
		{ OPTION_INTEGER, 'n', NULL, &filter.lines, N_("n"),
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
		OPT_BOOL(0, "create-reflog", &create_reflog, N_("create a reflog")),

		OPT_GROUP(N_("Tag listing options")),
		OPT_COLUMN(0, "column", &colopts, N_("show tag list in columns")),
		OPT_CONTAINS(&filter.with_commit, N_("print only tags that contain the commit")),
		OPT_WITH(&filter.with_commit, N_("print only tags that contain the commit")),
		OPT_MERGED(&filter, N_("print only tags that are merged")),
		OPT_NO_MERGED(&filter, N_("print only tags that are not merged")),
		OPT_CALLBACK(0 , "sort", sorting_tail, N_("key"),
			     N_("field name to sort on"), &parse_opt_ref_sorting),
		{
			OPTION_CALLBACK, 0, "points-at", &filter.points_at, N_("object"),
			N_("print only tags of the object"), 0, parse_opt_object_name
		},
		OPT_STRING(  0 , "format", &format, N_("format"), N_("format to use for the output")),
		OPT_BOOL('i', "ignore-case", &icase, N_("sorting and filtering are case insensitive")),
		OPT_END()
	};

	setup_ref_filter_porcelain_msg();

	git_config(git_tag_config, sorting_tail);

	memset(&opt, 0, sizeof(opt));
	memset(&filter, 0, sizeof(filter));
	filter.lines = -1;

	argc = parse_options(argc, argv, prefix, options, git_tag_usage, 0);

	if (keyid) {
		opt.sign = 1;
		set_signing_key(keyid);
	}
	create_tag_object = (opt.sign || annotate || msg.given || msgfile);

	if (argc == 0 && !cmdmode)
		cmdmode = 'l';

	if ((create_tag_object || force) && (cmdmode != 0))
		usage_with_options(git_tag_usage, options);

	finalize_colopts(&colopts, -1);
	if (cmdmode == 'l' && filter.lines != -1) {
		if (explicitly_enable_column(colopts))
			die(_("--column and -n are incompatible"));
		colopts = 0;
	}
	if (!sorting)
		sorting = ref_default_sorting();
	sorting->ignore_case = icase;
	filter.ignore_case = icase;
	if (cmdmode == 'l') {
		int ret;
		if (column_active(colopts)) {
			struct column_options copts;
			memset(&copts, 0, sizeof(copts));
			copts.padding = 2;
			run_column_filter(colopts, &copts);
		}
		filter.name_patterns = argv;
		ret = list_tags(&filter, sorting, format);
		if (column_active(colopts))
			stop_column_filter();
		return ret;
	}
	if (filter.lines != -1)
		die(_("-n option is only allowed with -l."));
	if (filter.with_commit)
		die(_("--contains option is only allowed with -l."));
	if (filter.points_at.nr)
		die(_("--points-at option is only allowed with -l."));
	if (filter.merge_commit)
		die(_("--merged and --no-merged option are only allowed with -l"));
	if (cmdmode == 'd')
		return for_each_tag_name(argv, delete_tag, NULL);
	if (cmdmode == 'v') {
		if (format)
			verify_ref_format(format);
		return for_each_tag_name(argv, verify_tag, format);
	}

	if (msg.given || msgfile) {
		if (msg.given && msgfile)
			die(_("only one -F or -m option is allowed."));
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

	create_reflog_msg(object, &reflog_msg);

	if (create_tag_object) {
		if (force_sign_annotate && !annotate)
			opt.sign = 1;
		create_tag(object, tag, &buf, &opt, prev, object);
	}

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref.buf, object, prev,
				   create_reflog ? REF_FORCE_CREATE_REFLOG : 0,
				   reflog_msg.buf, &err) ||
	    ref_transaction_commit(transaction, &err))
		die("%s", err.buf);
	ref_transaction_free(transaction);
	if (force && !is_null_sha1(prev) && hashcmp(prev, object))
		printf(_("Updated tag '%s' (was %s)\n"), tag, find_unique_abbrev(prev, DEFAULT_ABBREV));

	strbuf_release(&err);
	strbuf_release(&buf);
	strbuf_release(&ref);
	strbuf_release(&reflog_msg);
	return 0;
}
