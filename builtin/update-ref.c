#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "parse-options.h"
#include "quote.h"
#include "argv-array.h"

static const char * const git_update_ref_usage[] = {
	N_("git update-ref [<options>] -d <refname> [<old-val>]"),
	N_("git update-ref [<options>]    <refname> <new-val> [<old-val>]"),
	N_("git update-ref [<options>] --stdin [-z]"),
	NULL
};

static char line_termination = '\n';
static int update_flags;
static unsigned create_reflog_flag;
static const char *msg;

/*
 * Parse one whitespace- or NUL-terminated, possibly C-quoted argument
 * and append the result to arg.  Return a pointer to the terminator.
 * Die if there is an error in how the argument is C-quoted.  This
 * function is only used if not -z.
 */
static const char *parse_arg(const char *next, struct strbuf *arg)
{
	if (*next == '"') {
		const char *orig = next;

		if (unquote_c_style(arg, next, &next))
			die("badly quoted argument: %s", orig);
		if (*next && !isspace(*next))
			die("unexpected character after quoted argument: %s", orig);
	} else {
		while (*next && !isspace(*next))
			strbuf_addch(arg, *next++);
	}

	return next;
}

/*
 * Parse the reference name immediately after "command SP".  If not
 * -z, then handle C-quoting.  Return a pointer to a newly allocated
 * string containing the name of the reference, or NULL if there was
 * an error.  Update *next to point at the character that terminates
 * the argument.  Die if C-quoting is malformed or the reference name
 * is invalid.
 */
static char *parse_refname(struct strbuf *input, const char **next)
{
	struct strbuf ref = STRBUF_INIT;

	if (line_termination) {
		/* Without -z, use the next argument */
		*next = parse_arg(*next, &ref);
	} else {
		/* With -z, use everything up to the next NUL */
		strbuf_addstr(&ref, *next);
		*next += ref.len;
	}

	if (!ref.len) {
		strbuf_release(&ref);
		return NULL;
	}

	if (check_refname_format(ref.buf, REFNAME_ALLOW_ONELEVEL))
		die("invalid ref format: %s", ref.buf);

	return strbuf_detach(&ref, NULL);
}

/*
 * The value being parsed is <oldvalue> (as opposed to <newvalue>; the
 * difference affects which error messages are generated):
 */
#define PARSE_SHA1_OLD 0x01

/*
 * For backwards compatibility, accept an empty string for update's
 * <newvalue> in binary mode to be equivalent to specifying zeros.
 */
#define PARSE_SHA1_ALLOW_EMPTY 0x02

/*
 * Parse an argument separator followed by the next argument, if any.
 * If there is an argument, convert it to a SHA-1, write it to sha1,
 * set *next to point at the character terminating the argument, and
 * return 0.  If there is no argument at all (not even the empty
 * string), return 1 and leave *next unchanged.  If the value is
 * provided but cannot be converted to a SHA-1, die.  flags can
 * include PARSE_SHA1_OLD and/or PARSE_SHA1_ALLOW_EMPTY.
 */
static int parse_next_sha1(struct strbuf *input, const char **next,
			   unsigned char *sha1,
			   const char *command, const char *refname,
			   int flags)
{
	struct strbuf arg = STRBUF_INIT;
	int ret = 0;

	if (*next == input->buf + input->len)
		goto eof;

	if (line_termination) {
		/* Without -z, consume SP and use next argument */
		if (!**next || **next == line_termination)
			return 1;
		if (**next != ' ')
			die("%s %s: expected SP but got: %s",
			    command, refname, *next);
		(*next)++;
		*next = parse_arg(*next, &arg);
		if (arg.len) {
			if (get_sha1(arg.buf, sha1))
				goto invalid;
		} else {
			/* Without -z, an empty value means all zeros: */
			hashclr(sha1);
		}
	} else {
		/* With -z, read the next NUL-terminated line */
		if (**next)
			die("%s %s: expected NUL but got: %s",
			    command, refname, *next);
		(*next)++;
		if (*next == input->buf + input->len)
			goto eof;
		strbuf_addstr(&arg, *next);
		*next += arg.len;

		if (arg.len) {
			if (get_sha1(arg.buf, sha1))
				goto invalid;
		} else if (flags & PARSE_SHA1_ALLOW_EMPTY) {
			/* With -z, treat an empty value as all zeros: */
			warning("%s %s: missing <newvalue>, treating as zero",
				command, refname);
			hashclr(sha1);
		} else {
			/*
			 * With -z, an empty non-required value means
			 * unspecified:
			 */
			ret = 1;
		}
	}

	strbuf_release(&arg);

	return ret;

 invalid:
	die(flags & PARSE_SHA1_OLD ?
	    "%s %s: invalid <oldvalue>: %s" :
	    "%s %s: invalid <newvalue>: %s",
	    command, refname, arg.buf);

 eof:
	die(flags & PARSE_SHA1_OLD ?
	    "%s %s: unexpected end of input when reading <oldvalue>" :
	    "%s %s: unexpected end of input when reading <newvalue>",
	    command, refname);
}


/*
 * The following five parse_cmd_*() functions parse the corresponding
 * command.  In each case, next points at the character following the
 * command name and the following space.  They each return a pointer
 * to the character terminating the command, and die with an
 * explanatory message if there are any parsing problems.  All of
 * these functions handle either text or binary format input,
 * depending on how line_termination is set.
 */

static const char *parse_cmd_update(struct ref_transaction *transaction,
				    struct strbuf *input, const char *next)
{
	struct strbuf err = STRBUF_INIT;
	char *refname;
	unsigned char new_sha1[20];
	unsigned char old_sha1[20];
	int have_old;

	refname = parse_refname(input, &next);
	if (!refname)
		die("update: missing <ref>");

	if (parse_next_sha1(input, &next, new_sha1, "update", refname,
			    PARSE_SHA1_ALLOW_EMPTY))
		die("update %s: missing <newvalue>", refname);

	have_old = !parse_next_sha1(input, &next, old_sha1, "update", refname,
				    PARSE_SHA1_OLD);

	if (*next != line_termination)
		die("update %s: extra input: %s", refname, next);

	if (ref_transaction_update(transaction, refname,
				   new_sha1, have_old ? old_sha1 : NULL,
				   update_flags | create_reflog_flag,
				   msg, &err))
		die("%s", err.buf);

	update_flags = 0;
	free(refname);
	strbuf_release(&err);

	return next;
}

static const char *parse_cmd_create(struct ref_transaction *transaction,
				    struct strbuf *input, const char *next)
{
	struct strbuf err = STRBUF_INIT;
	char *refname;
	unsigned char new_sha1[20];

	refname = parse_refname(input, &next);
	if (!refname)
		die("create: missing <ref>");

	if (parse_next_sha1(input, &next, new_sha1, "create", refname, 0))
		die("create %s: missing <newvalue>", refname);

	if (is_null_sha1(new_sha1))
		die("create %s: zero <newvalue>", refname);

	if (*next != line_termination)
		die("create %s: extra input: %s", refname, next);

	if (ref_transaction_create(transaction, refname, new_sha1,
				   update_flags | create_reflog_flag,
				   msg, &err))
		die("%s", err.buf);

	update_flags = 0;
	free(refname);
	strbuf_release(&err);

	return next;
}

static const char *parse_cmd_delete(struct ref_transaction *transaction,
				    struct strbuf *input, const char *next)
{
	struct strbuf err = STRBUF_INIT;
	char *refname;
	unsigned char old_sha1[20];
	int have_old;

	refname = parse_refname(input, &next);
	if (!refname)
		die("delete: missing <ref>");

	if (parse_next_sha1(input, &next, old_sha1, "delete", refname,
			    PARSE_SHA1_OLD)) {
		have_old = 0;
	} else {
		if (is_null_sha1(old_sha1))
			die("delete %s: zero <oldvalue>", refname);
		have_old = 1;
	}

	if (*next != line_termination)
		die("delete %s: extra input: %s", refname, next);

	if (ref_transaction_delete(transaction, refname,
				   have_old ? old_sha1 : NULL,
				   update_flags, msg, &err))
		die("%s", err.buf);

	update_flags = 0;
	free(refname);
	strbuf_release(&err);

	return next;
}

static const char *parse_cmd_verify(struct ref_transaction *transaction,
				    struct strbuf *input, const char *next)
{
	struct strbuf err = STRBUF_INIT;
	char *refname;
	unsigned char old_sha1[20];

	refname = parse_refname(input, &next);
	if (!refname)
		die("verify: missing <ref>");

	if (parse_next_sha1(input, &next, old_sha1, "verify", refname,
			    PARSE_SHA1_OLD))
		hashclr(old_sha1);

	if (*next != line_termination)
		die("verify %s: extra input: %s", refname, next);

	if (ref_transaction_verify(transaction, refname, old_sha1,
				   update_flags, &err))
		die("%s", err.buf);

	update_flags = 0;
	free(refname);
	strbuf_release(&err);

	return next;
}

static const char *parse_cmd_option(struct strbuf *input, const char *next)
{
	if (!strncmp(next, "no-deref", 8) && next[8] == line_termination)
		update_flags |= REF_NODEREF;
	else
		die("option unknown: %s", next);
	return next + 8;
}

static void update_refs_stdin(struct ref_transaction *transaction)
{
	struct strbuf input = STRBUF_INIT;
	const char *next;

	if (strbuf_read(&input, 0, 1000) < 0)
		die_errno("could not read from stdin");
	next = input.buf;
	/* Read each line dispatch its command */
	while (next < input.buf + input.len) {
		if (*next == line_termination)
			die("empty command in input");
		else if (isspace(*next))
			die("whitespace before command: %s", next);
		else if (starts_with(next, "update "))
			next = parse_cmd_update(transaction, &input, next + 7);
		else if (starts_with(next, "create "))
			next = parse_cmd_create(transaction, &input, next + 7);
		else if (starts_with(next, "delete "))
			next = parse_cmd_delete(transaction, &input, next + 7);
		else if (starts_with(next, "verify "))
			next = parse_cmd_verify(transaction, &input, next + 7);
		else if (starts_with(next, "option "))
			next = parse_cmd_option(&input, next + 7);
		else
			die("unknown command: %s", next);

		next++;
	}

	strbuf_release(&input);
}

int cmd_update_ref(int argc, const char **argv, const char *prefix)
{
	const char *refname, *oldval;
	unsigned char sha1[20], oldsha1[20];
	int delete = 0, no_deref = 0, read_stdin = 0, end_null = 0;
	unsigned int flags = 0;
	int create_reflog = 0;
	struct option options[] = {
		OPT_STRING( 'm', NULL, &msg, N_("reason"), N_("reason of the update")),
		OPT_BOOL('d', NULL, &delete, N_("delete the reference")),
		OPT_BOOL( 0 , "no-deref", &no_deref,
					N_("update <refname> not the one it points to")),
		OPT_BOOL('z', NULL, &end_null, N_("stdin has NUL-terminated arguments")),
		OPT_BOOL( 0 , "stdin", &read_stdin, N_("read updates from stdin")),
		OPT_BOOL( 0 , "create-reflog", &create_reflog, N_("create a reflog")),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, git_update_ref_usage,
			     0);
	if (msg && !*msg)
		die("Refusing to perform update with empty message.");

	create_reflog_flag = create_reflog ? REF_FORCE_CREATE_REFLOG : 0;

	if (read_stdin) {
		struct strbuf err = STRBUF_INIT;
		struct ref_transaction *transaction;

		transaction = ref_transaction_begin(&err);
		if (!transaction)
			die("%s", err.buf);
		if (delete || no_deref || argc > 0)
			usage_with_options(git_update_ref_usage, options);
		if (end_null)
			line_termination = '\0';
		update_refs_stdin(transaction);
		if (ref_transaction_commit(transaction, &err))
			die("%s", err.buf);
		ref_transaction_free(transaction);
		strbuf_release(&err);
		return 0;
	}

	if (end_null)
		usage_with_options(git_update_ref_usage, options);

	if (delete) {
		if (argc < 1 || argc > 2)
			usage_with_options(git_update_ref_usage, options);
		refname = argv[0];
		oldval = argv[1];
	} else {
		const char *value;
		if (argc < 2 || argc > 3)
			usage_with_options(git_update_ref_usage, options);
		refname = argv[0];
		value = argv[1];
		oldval = argv[2];
		if (get_sha1(value, sha1))
			die("%s: not a valid SHA1", value);
	}

	if (oldval) {
		if (!*oldval)
			/*
			 * The empty string implies that the reference
			 * must not already exist:
			 */
			hashclr(oldsha1);
		else if (get_sha1(oldval, oldsha1))
			die("%s: not a valid old SHA1", oldval);
	}

	if (no_deref)
		flags = REF_NODEREF;
	if (delete)
		/*
		 * For purposes of backwards compatibility, we treat
		 * NULL_SHA1 as "don't care" here:
		 */
		return delete_ref(refname,
				  (oldval && !is_null_sha1(oldsha1)) ? oldsha1 : NULL,
				  flags);
	else
		return update_ref(msg, refname, sha1, oldval ? oldsha1 : NULL,
				  flags | create_reflog_flag,
				  UPDATE_REFS_DIE_ON_ERR);
}
