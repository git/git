#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "parse-options.h"
#include "quote.h"
#include "argv-array.h"

static const char * const git_update_ref_usage[] = {
	N_("git update-ref [options] -d <refname> [<oldval>]"),
	N_("git update-ref [options]    <refname> <newval> [<oldval>]"),
	N_("git update-ref [options] --stdin [-z]"),
	NULL
};

static int updates_alloc;
static int updates_count;
static struct ref_update **updates;

static char line_termination = '\n';
static int update_flags;

static struct ref_update *update_alloc(void)
{
	struct ref_update *update;

	/* Allocate and zero-init a struct ref_update */
	update = xcalloc(1, sizeof(*update));
	ALLOC_GROW(updates, updates_count + 1, updates_alloc);
	updates[updates_count++] = update;

	/* Store and reset accumulated options */
	update->flags = update_flags;
	update_flags = 0;

	return update;
}

static void update_store_new_sha1(const char *command,
				  struct ref_update *update,
				  const char *newvalue)
{
	if (*newvalue && get_sha1(newvalue, update->new_sha1))
		die("%s %s: invalid <newvalue>: %s",
		    command, update->ref_name, newvalue);
}

static void update_store_old_sha1(const char *command,
				  struct ref_update *update,
				  const char *oldvalue)
{
	if (*oldvalue && get_sha1(oldvalue, update->old_sha1))
		die("%s %s: invalid <oldvalue>: %s",
		    command, update->ref_name, oldvalue);

	/* We have an old value if non-empty, or if empty without -z */
	update->have_old = *oldvalue || line_termination;
}

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
 * Parse a SP/NUL separator followed by the next SP- or NUL-terminated
 * argument, if any.  If there is an argument, write it to arg, set
 * *next to point at the character terminating the argument, and
 * return 0.  If there is no argument at all (not even the empty
 * string), return a non-zero result and leave *next unchanged.
 */
static int parse_next_arg(struct strbuf *input, const char **next,
			  struct strbuf *arg)
{
	strbuf_reset(arg);
	if (line_termination) {
		/* Without -z, consume SP and use next argument */
		if (!**next || **next == line_termination)
			return -1;
		if (**next != ' ')
			die("expected SP but got: %s", *next);
		(*next)++;
		*next = parse_arg(*next, arg);
	} else {
		/* With -z, read the next NUL-terminated line */
		if (**next)
			die("expected NUL but got: %s", *next);
		(*next)++;
		if (*next == input->buf + input->len)
			return -1;
		strbuf_addstr(arg, *next);
		*next += arg->len;
	}
	return 0;
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

static const char *parse_cmd_update(struct strbuf *input, const char *next)
{
	struct strbuf newvalue = STRBUF_INIT;
	struct strbuf oldvalue = STRBUF_INIT;
	struct ref_update *update;

	update = update_alloc();

	update->ref_name = parse_refname(input, &next);
	if (!update->ref_name)
		die("update line missing <ref>");

	if (!parse_next_arg(input, &next, &newvalue))
		update_store_new_sha1("update", update, newvalue.buf);
	else
		die("update %s missing <newvalue>", update->ref_name);

	if (!parse_next_arg(input, &next, &oldvalue)) {
		update_store_old_sha1("update", update, oldvalue.buf);
		if (*next != line_termination)
			die("update %s has extra input: %s", update->ref_name, next);
	} else if (!line_termination)
		die("update %s missing <oldvalue>", update->ref_name);

	return next;
}

static const char *parse_cmd_create(struct strbuf *input, const char *next)
{
	struct strbuf newvalue = STRBUF_INIT;
	struct ref_update *update;

	update = update_alloc();

	update->ref_name = parse_refname(input, &next);
	if (!update->ref_name)
		die("create line missing <ref>");

	if (!parse_next_arg(input, &next, &newvalue))
		update_store_new_sha1("create", update, newvalue.buf);
	else
		die("create %s missing <newvalue>", update->ref_name);

	if (is_null_sha1(update->new_sha1))
		die("create %s given zero <newvalue>", update->ref_name);

	if (*next != line_termination)
		die("create %s has extra input: %s", update->ref_name, next);

	return next;
}

static const char *parse_cmd_delete(struct strbuf *input, const char *next)
{
	struct strbuf oldvalue = STRBUF_INIT;
	struct ref_update *update;

	update = update_alloc();

	update->ref_name = parse_refname(input, &next);
	if (!update->ref_name)
		die("delete line missing <ref>");

	if (!parse_next_arg(input, &next, &oldvalue)) {
		update_store_old_sha1("delete", update, oldvalue.buf);
		if (update->have_old && is_null_sha1(update->old_sha1))
			die("delete %s given zero <oldvalue>", update->ref_name);
	} else if (!line_termination)
		die("delete %s missing <oldvalue>", update->ref_name);

	if (*next != line_termination)
		die("delete %s has extra input: %s", update->ref_name, next);

	return next;
}

static const char *parse_cmd_verify(struct strbuf *input, const char *next)
{
	struct strbuf value = STRBUF_INIT;
	struct ref_update *update;

	update = update_alloc();

	update->ref_name = parse_refname(input, &next);
	if (!update->ref_name)
		die("verify line missing <ref>");

	if (!parse_next_arg(input, &next, &value)) {
		update_store_old_sha1("verify", update, value.buf);
		hashcpy(update->new_sha1, update->old_sha1);
	} else if (!line_termination)
		die("verify %s missing <oldvalue>", update->ref_name);

	if (*next != line_termination)
		die("verify %s has extra input: %s", update->ref_name, next);

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

static void update_refs_stdin(void)
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
			next = parse_cmd_update(&input, next + 7);
		else if (starts_with(next, "create "))
			next = parse_cmd_create(&input, next + 7);
		else if (starts_with(next, "delete "))
			next = parse_cmd_delete(&input, next + 7);
		else if (starts_with(next, "verify "))
			next = parse_cmd_verify(&input, next + 7);
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
	const char *refname, *oldval, *msg = NULL;
	unsigned char sha1[20], oldsha1[20];
	int delete = 0, no_deref = 0, read_stdin = 0, end_null = 0, flags = 0;
	struct option options[] = {
		OPT_STRING( 'm', NULL, &msg, N_("reason"), N_("reason of the update")),
		OPT_BOOL('d', NULL, &delete, N_("delete the reference")),
		OPT_BOOL( 0 , "no-deref", &no_deref,
					N_("update <refname> not the one it points to")),
		OPT_BOOL('z', NULL, &end_null, N_("stdin has NUL-terminated arguments")),
		OPT_BOOL( 0 , "stdin", &read_stdin, N_("read updates from stdin")),
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, git_update_ref_usage,
			     0);
	if (msg && !*msg)
		die("Refusing to perform update with empty message.");

	if (read_stdin) {
		if (delete || no_deref || argc > 0)
			usage_with_options(git_update_ref_usage, options);
		if (end_null)
			line_termination = '\0';
		update_refs_stdin();
		return update_refs(msg, updates, updates_count,
				   UPDATE_REFS_DIE_ON_ERR);
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

	hashclr(oldsha1); /* all-zero hash in case oldval is the empty string */
	if (oldval && *oldval && get_sha1(oldval, oldsha1))
		die("%s: not a valid old SHA1", oldval);

	if (no_deref)
		flags = REF_NODEREF;
	if (delete)
		return delete_ref(refname, oldval ? oldsha1 : NULL, flags);
	else
		return update_ref(msg, refname, sha1, oldval ? oldsha1 : NULL,
				  flags, UPDATE_REFS_DIE_ON_ERR);
}
