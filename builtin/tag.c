/*
 * Builtin "git tag"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *                    Carlos Rica <jasampler@gmail.com>
 * Based on git-tag.sh and mktag.c by Linus Torvalds.
 */

#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "refs.h"
#include "object-store.h"
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
	N_("git tag [-a | -s | -u <key-id>] [-f] [-m <msg> | -F <file>]\n"
		"\t\t<tagname> [<head>]"),
	N_("git tag -d <tagname>..."),
	N_("git tag -l [-n[<num>]] [--contains <commit>] [--no-contains <commit>] [--points-at <object>]\n"
		"\t\t[--format=<format>] [--[no-]merged [<commit>]] [<pattern>...]"),
	N_("git tag -v [--format=<format>] <tagname>..."),
	NULL
};

static unsigned int colopts;
static int force_sign_annotate;
static int config_sign_tag = -1; /* unspecified */

static int list_tags(struct ref_filter *filter, struct ref_sorting *sorting,
		     struct ref_format *format)
{
	struct ref_array array;
	char *to_free = NULL;
	int i;

	memset(&array, 0, sizeof(array));

	if (filter->lines == -1)
		filter->lines = 0;

	if (!format->format) {
		if (filter->lines) {
			to_free = xstrfmt("%s %%(contents:lines=%d)",
					  "%(align:15)%(refname:lstrip=2)%(end)",
					  filter->lines);
			format->format = to_free;
		} else
			format->format = "%(refname:lstrip=2)";
	}

	if (verify_ref_format(format))
		die(_("unable to parse format string"));
	filter->with_commit_tag_algo = 1;
	filter_refs(&array, filter, FILTER_REFS_TAGS);
	ref_array_sort(sorting, &array);

	for (i = 0; i < array.nr; i++)
		show_ref_array_item(array.items[i], format);
	ref_array_clear(&array);
	free(to_free);

	return 0;
}

typedef int (*each_tag_name_fn)(const char *name, const char *ref,
				const struct object_id *oid, const void *cb_data);

static int for_each_tag_name(const char **argv, each_tag_name_fn fn,
			     const void *cb_data)
{
	const char **p;
	struct strbuf ref = STRBUF_INIT;
	int had_error = 0;
	struct object_id oid;

	for (p = argv; *p; p++) {
		strbuf_reset(&ref);
		strbuf_addf(&ref, "refs/tags/%s", *p);
		if (read_ref(ref.buf, &oid)) {
			error(_("tag '%s' not found."), *p);
			had_error = 1;
			continue;
		}
		if (fn(*p, ref.buf, &oid, cb_data))
			had_error = 1;
	}
	strbuf_release(&ref);
	return had_error;
}

static int delete_tag(const char *name, const char *ref,
		      const struct object_id *oid, const void *cb_data)
{
	if (delete_ref(NULL, ref, oid, 0))
		return 1;
	printf(_("Deleted tag '%s' (was %s)\n"), name,
	       find_unique_abbrev(oid, DEFAULT_ABBREV));
	return 0;
}

static int verify_tag(const char *name, const char *ref,
		      const struct object_id *oid, const void *cb_data)
{
	int flags;
	const struct ref_format *format = cb_data;
	flags = GPG_VERIFY_VERBOSE;

	if (format->format)
		flags = GPG_VERIFY_OMIT_STATUS;

	if (gpg_verify_tag(oid, name, flags))
		return -1;

	if (format->format)
		pretty_print_ref(name, oid, format);

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

static int git_tag_config(const char *var, const char *value, void *cb)
{
	int status;
	struct ref_sorting **sorting_tail = (struct ref_sorting **)cb;

	if (!strcmp(var, "tag.gpgsign")) {
		config_sign_tag = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "tag.sort")) {
		if (!value)
			return config_error_nonbool(var);
		parse_ref_sorting(sorting_tail, value);
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
	return git_color_default_config(var, value, cb);
}

static void write_tag_body(int fd, const struct object_id *oid)
{
	unsigned long size;
	enum object_type type;
	char *buf, *sp;

	buf = read_object_file(oid, &type, &size);
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

static int build_tag_object(struct strbuf *buf, int sign, struct object_id *result)
{
	if (sign && do_sign(buf) < 0)
		return error(_("unable to sign the tag"));
	if (write_object_file(buf->buf, buf->len, tag_type, result) < 0)
		return error(_("unable to write tag file"));
	return 0;
}

struct create_tag_options {
	unsigned int message_given:1;
	unsigned int use_editor:1;
	unsigned int sign;
	enum {
		CLEANUP_NONE,
		CLEANUP_SPACE,
		CLEANUP_ALL
	} cleanup_mode;
};

static const char message_advice_nested_tag[] =
	N_("You have created a nested tag. The object referred to by your new tag is\n"
	   "already a tag. If you meant to tag the object that it points to, use:\n"
	   "\n"
	   "\tgit tag -f %s %s^{}");

static void create_tag(const struct object_id *object, const char *object_ref,
		       const char *tag,
		       struct strbuf *buf, struct create_tag_options *opt,
		       struct object_id *prev, struct object_id *result)
{
	enum object_type type;
	struct strbuf header = STRBUF_INIT;
	char *path = NULL;

	type = oid_object_info(the_repository, object, NULL);
	if (type <= OBJ_NONE)
		die(_("bad object type."));

	if (type == OBJ_TAG && advice_nested_tag)
		advise(_(message_advice_nested_tag), tag, object_ref);

	strbuf_addf(&header,
		    "object %s\n"
		    "type %s\n"
		    "tag %s\n"
		    "tagger %s\n\n",
		    oid_to_hex(object),
		    type_name(type),
		    tag,
		    git_committer_info(IDENT_STRICT));

	if (!opt->message_given || opt->use_editor) {
		int fd;

		/* write the template message before editing: */
		path = git_pathdup("TAG_EDITMSG");
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die_errno(_("could not create file '%s'"), path);

		if (opt->message_given) {
			write_or_die(fd, buf->buf, buf->len);
			strbuf_reset(buf);
		} else if (!is_null_oid(prev)) {
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

static void create_reflog_msg(const struct object_id *oid, struct strbuf *sb)
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
		strbuf_addstr(sb, "tag: tagging ");
		strbuf_add_unique_abbrev(sb, oid, DEFAULT_ABBREV);
	}

	strbuf_addstr(sb, " (");
	type = oid_object_info(the_repository, oid, NULL);
	switch (type) {
	default:
		strbuf_addstr(sb, "object of unknown type");
		break;
	case OBJ_COMMIT:
		if ((buf = read_object_file(oid, &type, &size)) != NULL) {
			subject_len = find_commit_subject(buf, &subject_start);
			strbuf_insert(sb, sb->len, subject_start, subject_len);
		} else {
			strbuf_addstr(sb, "commit object");
		}
		free(buf);

		if ((c = lookup_commit_reference(the_repository, oid)) != NULL)
			strbuf_addf(sb, ", %s", show_date(c->date, 0, DATE_MODE(SHORT)));
		break;
	case OBJ_TREE:
		strbuf_addstr(sb, "tree object");
		break;
	case OBJ_BLOB:
		strbuf_addstr(sb, "blob object");
		break;
	case OBJ_TAG:
		strbuf_addstr(sb, "other tag object");
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

	BUG_ON_OPT_NEG(unset);

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
	struct object_id object, prev;
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
	struct ref_format format = REF_FORMAT_INIT;
	int icase = 0;
	int edit_flag = 0;
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
		{ OPTION_CALLBACK, 'm', "message", &msg, N_("message"),
		  N_("tag message"), PARSE_OPT_NONEG, parse_msg_arg },
		OPT_FILENAME('F', "file", &msgfile, N_("read message from file")),
		OPT_BOOL('e', "edit", &edit_flag, N_("force edit of tag message")),
		OPT_BOOL('s', "sign", &opt.sign, N_("annotated and GPG-signed tag")),
		OPT_CLEANUP(&cleanup_arg),
		OPT_STRING('u', "local-user", &keyid, N_("key-id"),
					N_("use another key to sign the tag")),
		OPT__FORCE(&force, N_("replace the tag if exists"), 0),
		OPT_BOOL(0, "create-reflog", &create_reflog, N_("create a reflog")),

		OPT_GROUP(N_("Tag listing options")),
		OPT_COLUMN(0, "column", &colopts, N_("show tag list in columns")),
		OPT_CONTAINS(&filter.with_commit, N_("print only tags that contain the commit")),
		OPT_NO_CONTAINS(&filter.no_commit, N_("print only tags that don't contain the commit")),
		OPT_WITH(&filter.with_commit, N_("print only tags that contain the commit")),
		OPT_WITHOUT(&filter.no_commit, N_("print only tags that don't contain the commit")),
		OPT_MERGED(&filter, N_("print only tags that are merged")),
		OPT_NO_MERGED(&filter, N_("print only tags that are not merged")),
		OPT_REF_SORT(sorting_tail),
		{
			OPTION_CALLBACK, 0, "points-at", &filter.points_at, N_("object"),
			N_("print only tags of the object"), PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_object_name, (intptr_t) "HEAD"
		},
		OPT_STRING(  0 , "format", &format.format, N_("format"),
			   N_("format to use for the output")),
		OPT__COLOR(&format.use_color, N_("respect format colors")),
		OPT_BOOL('i', "ignore-case", &icase, N_("sorting and filtering are case insensitive")),
		OPT_END()
	};

	setup_ref_filter_porcelain_msg();

	git_config(git_tag_config, sorting_tail);

	memset(&opt, 0, sizeof(opt));
	memset(&filter, 0, sizeof(filter));
	filter.lines = -1;
	opt.sign = -1;

	argc = parse_options(argc, argv, prefix, options, git_tag_usage, 0);

	if (!cmdmode) {
		if (argc == 0)
			cmdmode = 'l';
		else if (filter.with_commit || filter.no_commit ||
			 filter.points_at.nr || filter.merge_commit ||
			 filter.lines != -1)
			cmdmode = 'l';
	}

	if (cmdmode == 'l')
		setup_auto_pager("tag", 1);

	if (opt.sign == -1)
		opt.sign = cmdmode ? 0 : config_sign_tag > 0;

	if (keyid) {
		opt.sign = 1;
		set_signing_key(keyid);
	}
	create_tag_object = (opt.sign || annotate || msg.given || msgfile);

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
		ret = list_tags(&filter, sorting, &format);
		if (column_active(colopts))
			stop_column_filter();
		return ret;
	}
	if (filter.lines != -1)
		die(_("-n option is only allowed in list mode"));
	if (filter.with_commit)
		die(_("--contains option is only allowed in list mode"));
	if (filter.no_commit)
		die(_("--no-contains option is only allowed in list mode"));
	if (filter.points_at.nr)
		die(_("--points-at option is only allowed in list mode"));
	if (filter.merge_commit)
		die(_("--merged and --no-merged options are only allowed in list mode"));
	if (cmdmode == 'd')
		return for_each_tag_name(argv, delete_tag, NULL);
	if (cmdmode == 'v') {
		if (format.format && verify_ref_format(&format))
			usage_with_options(git_tag_usage, options);
		return for_each_tag_name(argv, verify_tag, &format);
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

	if (get_oid(object_ref, &object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	if (strbuf_check_tag_ref(&ref, tag))
		die(_("'%s' is not a valid tag name."), tag);

	if (read_ref(ref.buf, &prev))
		oidclr(&prev);
	else if (!force)
		die(_("tag '%s' already exists"), tag);

	opt.message_given = msg.given || msgfile;
	opt.use_editor = edit_flag;

	if (!cleanup_arg || !strcmp(cleanup_arg, "strip"))
		opt.cleanup_mode = CLEANUP_ALL;
	else if (!strcmp(cleanup_arg, "verbatim"))
		opt.cleanup_mode = CLEANUP_NONE;
	else if (!strcmp(cleanup_arg, "whitespace"))
		opt.cleanup_mode = CLEANUP_SPACE;
	else
		die(_("Invalid cleanup mode %s"), cleanup_arg);

	create_reflog_msg(&object, &reflog_msg);

	if (create_tag_object) {
		if (force_sign_annotate && !annotate)
			opt.sign = 1;
		create_tag(&object, object_ref, tag, &buf, &opt, &prev, &object);
	}

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref.buf, &object, &prev,
				   create_reflog ? REF_FORCE_CREATE_REFLOG : 0,
				   reflog_msg.buf, &err) ||
	    ref_transaction_commit(transaction, &err))
		die("%s", err.buf);
	ref_transaction_free(transaction);
	if (force && !is_null_oid(&prev) && !oideq(&prev, &object))
		printf(_("Updated tag '%s' (was %s)\n"), tag,
		       find_unique_abbrev(&prev, DEFAULT_ABBREV));

	UNLEAK(buf);
	UNLEAK(ref);
	UNLEAK(reflog_msg);
	UNLEAK(msg);
	UNLEAK(err);
	return 0;
}
