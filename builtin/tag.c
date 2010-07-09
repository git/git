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

static const char * const git_tag_usage[] = {
	"git tag [-a|-s|-u <key-id>] [-f] [-m <msg>|-F <file>] <tagname> [<head>]",
	"git tag -d <tagname>...",
	"git tag -l [-n[<num>]] [<pattern>]",
	"git tag -v <tagname>...",
	NULL
};

static char signingkey[1000];

struct tag_filter {
	const char *pattern;
	int lines;
	struct commit_list *with_commit;
};

#define PGP_SIGNATURE "-----BEGIN PGP SIGNATURE-----"

static int show_reference(const char *refname, const unsigned char *sha1,
			  int flag, void *cb_data)
{
	struct tag_filter *filter = cb_data;

	if (!fnmatch(filter->pattern, refname, 0)) {
		int i;
		unsigned long size;
		enum object_type type;
		char *buf, *sp, *eol;
		size_t len;

		if (filter->with_commit) {
			struct commit *commit;

			commit = lookup_commit_reference_gently(sha1, 1);
			if (!commit)
				return 0;
			if (!is_descendant_of(commit, filter->with_commit))
				return 0;
		}

		if (!filter->lines) {
			printf("%s\n", refname);
			return 0;
		}
		printf("%-15s ", refname);

		buf = read_sha1_file(sha1, &type, &size);
		if (!buf || !size)
			return 0;

		/* skip header */
		sp = strstr(buf, "\n\n");
		if (!sp) {
			free(buf);
			return 0;
		}
		/* only take up to "lines" lines, and strip the signature */
		for (i = 0, sp += 2;
				i < filter->lines && sp < buf + size &&
				prefixcmp(sp, PGP_SIGNATURE "\n");
				i++) {
			if (i)
				printf("\n    ");
			eol = memchr(sp, '\n', size - (sp - buf));
			len = eol ? eol - sp : size - (sp - buf);
			fwrite(sp, len, 1, stdout);
			if (!eol)
				break;
			sp = eol + 1;
		}
		putchar('\n');
		free(buf);
	}

	return 0;
}

static int list_tags(const char *pattern, int lines,
			struct commit_list *with_commit)
{
	struct tag_filter filter;

	if (pattern == NULL)
		pattern = "*";

	filter.pattern = pattern;
	filter.lines = lines;
	filter.with_commit = with_commit;

	for_each_tag_ref(show_reference, (void *) &filter);

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
			error("tag name too long: %.*s...", 50, *p);
			had_error = 1;
			continue;
		}
		if (!resolve_ref(ref, sha1, 1, NULL)) {
			error("tag '%s' not found.", *p);
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
	printf("Deleted tag '%s' (was %s)\n", name, find_unique_abbrev(sha1, DEFAULT_ABBREV));
	return 0;
}

static int verify_tag(const char *name, const char *ref,
				const unsigned char *sha1)
{
	const char *argv_verify_tag[] = {"verify-tag",
					"-v", "SHA1_HEX", NULL};
	argv_verify_tag[2] = sha1_to_hex(sha1);

	if (run_command_v_opt(argv_verify_tag, RUN_GIT_CMD))
		return error("could not verify the tag '%s'", name);
	return 0;
}

static int do_sign(struct strbuf *buffer)
{
	struct child_process gpg;
	const char *args[4];
	char *bracket;
	int len;
	int i, j;

	if (!*signingkey) {
		if (strlcpy(signingkey, git_committer_info(IDENT_ERROR_ON_NO_NAME),
				sizeof(signingkey)) > sizeof(signingkey) - 1)
			return error("committer info too long.");
		bracket = strchr(signingkey, '>');
		if (bracket)
			bracket[1] = '\0';
	}

	/* When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE. */
	signal(SIGPIPE, SIG_IGN);

	memset(&gpg, 0, sizeof(gpg));
	gpg.argv = args;
	gpg.in = -1;
	gpg.out = -1;
	args[0] = "gpg";
	args[1] = "-bsau";
	args[2] = signingkey;
	args[3] = NULL;

	if (start_command(&gpg))
		return error("could not run gpg.");

	if (write_in_full(gpg.in, buffer->buf, buffer->len) != buffer->len) {
		close(gpg.in);
		close(gpg.out);
		finish_command(&gpg);
		return error("gpg did not accept the tag data");
	}
	close(gpg.in);
	len = strbuf_read(buffer, gpg.out, 1024);
	close(gpg.out);

	if (finish_command(&gpg) || !len || len < 0)
		return error("gpg failed to sign the tag");

	/* Strip CR from the line endings, in case we are on Windows. */
	for (i = j = 0; i < buffer->len; i++)
		if (buffer->buf[i] != '\r') {
			if (i != j)
				buffer->buf[j] = buffer->buf[i];
			j++;
		}
	strbuf_setlen(buffer, j);

	return 0;
}

static const char tag_template[] =
	"\n"
	"#\n"
	"# Write a tag message\n"
	"#\n";

static void set_signingkey(const char *value)
{
	if (strlcpy(signingkey, value, sizeof(signingkey)) >= sizeof(signingkey))
		die("signing key value too long (%.10s...)", value);
}

static int git_tag_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "user.signingkey")) {
		if (!value)
			return config_error_nonbool(var);
		set_signingkey(value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static void write_tag_body(int fd, const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	char *buf, *sp, *eob;
	size_t len;

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
	eob = strstr(sp, "\n" PGP_SIGNATURE "\n");
	if (eob)
		len = eob - sp;
	else
		len = buf + size - sp;
	write_or_die(fd, sp, len);

	free(buf);
}

static int build_tag_object(struct strbuf *buf, int sign, unsigned char *result)
{
	if (sign && do_sign(buf) < 0)
		return error("unable to sign the tag");
	if (write_sha1_file(buf->buf, buf->len, tag_type, result) < 0)
		return error("unable to write tag file");
	return 0;
}

static void create_tag(const unsigned char *object, const char *tag,
		       struct strbuf *buf, int message, int sign,
		       unsigned char *prev, unsigned char *result)
{
	enum object_type type;
	char header_buf[1024];
	int header_len;
	char *path = NULL;

	type = sha1_object_info(object, NULL);
	if (type <= OBJ_NONE)
	    die("bad object type.");

	header_len = snprintf(header_buf, sizeof(header_buf),
			  "object %s\n"
			  "type %s\n"
			  "tag %s\n"
			  "tagger %s\n\n",
			  sha1_to_hex(object),
			  typename(type),
			  tag,
			  git_committer_info(IDENT_ERROR_ON_NO_NAME));

	if (header_len > sizeof(header_buf) - 1)
		die("tag header too big.");

	if (!message) {
		int fd;

		/* write the template message before editing: */
		path = git_pathdup("TAG_EDITMSG");
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die_errno("could not create file '%s'", path);

		if (!is_null_sha1(prev))
			write_tag_body(fd, prev);
		else
			write_or_die(fd, tag_template, strlen(tag_template));
		close(fd);

		if (launch_editor(path, buf, NULL)) {
			fprintf(stderr,
			"Please supply the message using either -m or -F option.\n");
			exit(1);
		}
	}

	stripspace(buf, 1);

	if (!message && !buf->len)
		die("no tag message?");

	strbuf_insert(buf, 0, header_buf, header_len);

	if (build_tag_object(buf, sign, result) < 0) {
		if (path)
			fprintf(stderr, "The tag message has been left in %s\n",
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

int cmd_tag(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char object[20], prev[20];
	char ref[PATH_MAX];
	const char *object_ref, *tag;
	struct ref_lock *lock;

	int annotate = 0, sign = 0, force = 0, lines = -1,
		list = 0, delete = 0, verify = 0;
	const char *msgfile = NULL, *keyid = NULL;
	struct msg_arg msg = { 0, STRBUF_INIT };
	struct commit_list *with_commit = NULL;
	struct option options[] = {
		OPT_BOOLEAN('l', NULL, &list, "list tag names"),
		{ OPTION_INTEGER, 'n', NULL, &lines, "n",
				"print <n> lines of each tag message",
				PARSE_OPT_OPTARG, NULL, 1 },
		OPT_BOOLEAN('d', NULL, &delete, "delete tags"),
		OPT_BOOLEAN('v', NULL, &verify, "verify tags"),

		OPT_GROUP("Tag creation options"),
		OPT_BOOLEAN('a', NULL, &annotate,
					"annotated tag, needs a message"),
		OPT_CALLBACK('m', NULL, &msg, "msg",
			     "message for the tag", parse_msg_arg),
		OPT_FILENAME('F', NULL, &msgfile, "message in a file"),
		OPT_BOOLEAN('s', NULL, &sign, "annotated and GPG-signed tag"),
		OPT_STRING('u', NULL, &keyid, "key-id",
					"use another key to sign the tag"),
		OPT_BOOLEAN('f', "force", &force, "replace the tag if exists"),

		OPT_GROUP("Tag listing options"),
		{
			OPTION_CALLBACK, 0, "contains", &with_commit, "commit",
			"print only tags that contain the commit",
			PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_with_commit, (intptr_t)"HEAD",
		},
		OPT_END()
	};

	git_config(git_tag_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_tag_usage, 0);

	if (keyid) {
		sign = 1;
		set_signingkey(keyid);
	}
	if (sign)
		annotate = 1;
	if (argc == 0 && !(delete || verify))
		list = 1;

	if ((annotate || msg.given || msgfile || force) &&
	    (list || delete || verify))
		usage_with_options(git_tag_usage, options);

	if (list + delete + verify > 1)
		usage_with_options(git_tag_usage, options);
	if (list)
		return list_tags(argv[0], lines == -1 ? 0 : lines,
				 with_commit);
	if (lines != -1)
		die("-n option is only allowed with -l.");
	if (with_commit)
		die("--contains option is only allowed with -l.");
	if (delete)
		return for_each_tag_name(argv, delete_tag);
	if (verify)
		return for_each_tag_name(argv, verify_tag);

	if (msg.given || msgfile) {
		if (msg.given && msgfile)
			die("only one -F or -m option is allowed.");
		annotate = 1;
		if (msg.given)
			strbuf_addbuf(&buf, &(msg.buf));
		else {
			if (!strcmp(msgfile, "-")) {
				if (strbuf_read(&buf, 0, 1024) < 0)
					die_errno("cannot read '%s'", msgfile);
			} else {
				if (strbuf_read_file(&buf, msgfile, 1024) < 0)
					die_errno("could not open or read '%s'",
						msgfile);
			}
		}
	}

	tag = argv[0];

	object_ref = argc == 2 ? argv[1] : "HEAD";
	if (argc > 2)
		die("too many params");

	if (get_sha1(object_ref, object))
		die("Failed to resolve '%s' as a valid ref.", object_ref);

	if (snprintf(ref, sizeof(ref), "refs/tags/%s", tag) > sizeof(ref) - 1)
		die("tag name too long: %.*s...", 50, tag);
	if (check_ref_format(ref))
		die("'%s' is not a valid tag name.", tag);

	if (!resolve_ref(ref, prev, 1, NULL))
		hashclr(prev);
	else if (!force)
		die("tag '%s' already exists", tag);

	if (annotate)
		create_tag(object, tag, &buf, msg.given || msgfile,
			   sign, prev, object);

	lock = lock_any_ref_for_update(ref, prev, 0);
	if (!lock)
		die("%s: cannot lock the ref", ref);
	if (write_ref_sha1(lock, object, NULL) < 0)
		die("%s: cannot update the ref", ref);
	if (force && hashcmp(prev, object))
		printf("Updated tag '%s' (was %s)\n", tag, find_unique_abbrev(prev, DEFAULT_ABBREV));

	strbuf_release(&buf);
	return 0;
}
