#include "git-compat-util.h"
#include "environment.h"
#include "gettext.h"
#include "config.h"
#include "gpg-interface.h"
#include "hex.h"
#include "parse-options.h"
#include "run-command.h"
#include "refs.h"
#include "wildmatch.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "repository.h"
#include "commit.h"
#include "mailmap.h"
#include "ident.h"
#include "remote.h"
#include "color.h"
#include "tag.h"
#include "quote.h"
#include "ref-filter.h"
#include "revision.h"
#include "utf8.h"
#include "version.h"
#include "versioncmp.h"
#include "trailer.h"
#include "wt-status.h"
#include "commit-slab.h"
#include "commit-graph.h"
#include "commit-reach.h"
#include "worktree.h"
#include "hashmap.h"
#include "strvec.h"

static struct ref_msg {
	const char *gone;
	const char *ahead;
	const char *behind;
	const char *ahead_behind;
} msgs = {
	 /* Untranslated plumbing messages: */
	"gone",
	"ahead %d",
	"behind %d",
	"ahead %d, behind %d"
};

void setup_ref_filter_porcelain_msg(void)
{
	msgs.gone = _("gone");
	msgs.ahead = _("ahead %d");
	msgs.behind = _("behind %d");
	msgs.ahead_behind = _("ahead %d, behind %d");
}

typedef enum { FIELD_STR, FIELD_ULONG, FIELD_TIME } cmp_type;
typedef enum { COMPARE_EQUAL, COMPARE_UNEQUAL, COMPARE_NONE } cmp_status;
typedef enum { SOURCE_NONE = 0, SOURCE_OBJ, SOURCE_OTHER } info_source;

struct align {
	align_type position;
	unsigned int width;
};

struct if_then_else {
	cmp_status cmp_status;
	const char *str;
	unsigned int then_atom_seen : 1,
		else_atom_seen : 1,
		condition_satisfied : 1;
};

struct refname_atom {
	enum { R_NORMAL, R_SHORT, R_LSTRIP, R_RSTRIP } option;
	int lstrip, rstrip;
};

static struct ref_trailer_buf {
	struct string_list filter_list;
	struct strbuf sepbuf;
	struct strbuf kvsepbuf;
} ref_trailer_buf = {STRING_LIST_INIT_NODUP, STRBUF_INIT, STRBUF_INIT};

static struct expand_data {
	struct object_id oid;
	enum object_type type;
	unsigned long size;
	off_t disk_size;
	struct object_id delta_base_oid;
	void *content;

	struct object_info info;
} oi, oi_deref;

struct ref_to_worktree_entry {
	struct hashmap_entry ent;
	struct worktree *wt; /* key is wt->head_ref */
};

static int ref_to_worktree_map_cmpfnc(const void *lookupdata UNUSED,
				      const struct hashmap_entry *eptr,
				      const struct hashmap_entry *kptr,
				      const void *keydata_aka_refname)
{
	const struct ref_to_worktree_entry *e, *k;

	e = container_of(eptr, const struct ref_to_worktree_entry, ent);
	k = container_of(kptr, const struct ref_to_worktree_entry, ent);

	return strcmp(e->wt->head_ref,
		keydata_aka_refname ? keydata_aka_refname : k->wt->head_ref);
}

static struct ref_to_worktree_map {
	struct hashmap map;
	struct worktree **worktrees;
} ref_to_worktree_map;

/*
 * The enum atom_type is used as the index of valid_atom array.
 * In the atom parsing stage, it will be passed to used_atom.atom_type
 * as the identifier of the atom type. We can check the type of used_atom
 * entry by `if (used_atom[i].atom_type == ATOM_*)`.
 */
enum atom_type {
	ATOM_REFNAME,
	ATOM_OBJECTTYPE,
	ATOM_OBJECTSIZE,
	ATOM_OBJECTNAME,
	ATOM_DELTABASE,
	ATOM_TREE,
	ATOM_PARENT,
	ATOM_NUMPARENT,
	ATOM_OBJECT,
	ATOM_TYPE,
	ATOM_TAG,
	ATOM_AUTHOR,
	ATOM_AUTHORNAME,
	ATOM_AUTHOREMAIL,
	ATOM_AUTHORDATE,
	ATOM_COMMITTER,
	ATOM_COMMITTERNAME,
	ATOM_COMMITTEREMAIL,
	ATOM_COMMITTERDATE,
	ATOM_TAGGER,
	ATOM_TAGGERNAME,
	ATOM_TAGGEREMAIL,
	ATOM_TAGGERDATE,
	ATOM_CREATOR,
	ATOM_CREATORDATE,
	ATOM_DESCRIBE,
	ATOM_SUBJECT,
	ATOM_BODY,
	ATOM_TRAILERS,
	ATOM_CONTENTS,
	ATOM_SIGNATURE,
	ATOM_RAW,
	ATOM_UPSTREAM,
	ATOM_PUSH,
	ATOM_SYMREF,
	ATOM_FLAG,
	ATOM_HEAD,
	ATOM_COLOR,
	ATOM_WORKTREEPATH,
	ATOM_ALIGN,
	ATOM_END,
	ATOM_IF,
	ATOM_THEN,
	ATOM_ELSE,
	ATOM_REST,
	ATOM_AHEADBEHIND,
};

/*
 * An atom is a valid field atom listed below, possibly prefixed with
 * a "*" to denote deref_tag().
 *
 * We parse given format string and sort specifiers, and make a list
 * of properties that we need to extract out of objects.  ref_array_item
 * structure will hold an array of values extracted that can be
 * indexed with the "atom number", which is an index into this
 * array.
 */
static struct used_atom {
	enum atom_type atom_type;
	const char *name;
	cmp_type type;
	info_source source;
	union {
		char color[COLOR_MAXLEN];
		struct align align;
		struct {
			enum {
				RR_REF, RR_TRACK, RR_TRACKSHORT, RR_REMOTE_NAME, RR_REMOTE_REF
			} option;
			struct refname_atom refname;
			unsigned int nobracket : 1, push : 1, push_remote : 1;
		} remote_ref;
		struct {
			enum { C_BARE, C_BODY, C_BODY_DEP, C_LENGTH, C_LINES,
			       C_SIG, C_SUB, C_SUB_SANITIZE, C_TRAILERS } option;
			struct process_trailer_options trailer_opts;
			unsigned int nlines;
		} contents;
		struct {
			enum { RAW_BARE, RAW_LENGTH } option;
		} raw_data;
		struct {
			cmp_status cmp_status;
			const char *str;
		} if_then_else;
		struct {
			enum { O_FULL, O_LENGTH, O_SHORT } option;
			unsigned int length;
		} oid;
		struct {
			enum { O_SIZE, O_SIZE_DISK } option;
		} objectsize;
		struct {
			enum { N_RAW, N_MAILMAP } option;
		} name_option;
		struct {
			enum {
				EO_RAW = 0,
				EO_TRIM = 1<<0,
				EO_LOCALPART = 1<<1,
				EO_MAILMAP = 1<<2,
			} option;
		} email_option;
		struct {
			enum { S_BARE, S_GRADE, S_SIGNER, S_KEY,
			       S_FINGERPRINT, S_PRI_KEY_FP, S_TRUST_LEVEL } option;
		} signature;
		const char **describe_args;
		struct refname_atom refname;
		char *head;
	} u;
} *used_atom;
static int used_atom_cnt, need_tagged, need_symref;

/*
 * Expand string, append it to strbuf *sb, then return error code ret.
 * Allow to save few lines of code.
 */
__attribute__((format (printf, 3, 4)))
static int strbuf_addf_ret(struct strbuf *sb, int ret, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vaddf(sb, fmt, ap);
	va_end(ap);
	return ret;
}

static int err_no_arg(struct strbuf *sb, const char *name)
{
	size_t namelen = strchrnul(name, ':') - name;
	strbuf_addf(sb, _("%%(%.*s) does not take arguments"),
		    (int)namelen, name);
	return -1;
}

static int err_bad_arg(struct strbuf *sb, const char *name, const char *arg)
{
	size_t namelen = strchrnul(name, ':') - name;
	strbuf_addf(sb, _("unrecognized %%(%.*s) argument: %s"),
		    (int)namelen, name, arg);
	return -1;
}

/*
 * Parse option of name "candidate" in the option string "to_parse" of
 * the form
 *
 *	"candidate1[=val1],candidate2[=val2],candidate3[=val3],..."
 *
 * The remaining part of "to_parse" is stored in "end" (if we are
 * parsing the last candidate, then this is NULL) and the value of
 * the candidate is stored in "valuestart" and its length in "valuelen",
 * that is the portion after "=". Since it is possible for a "candidate"
 * to not have a value, in such cases, "valuestart" is set to point to
 * NULL and "valuelen" to 0.
 *
 * The function returns 1 on success. It returns 0 if we don't find
 * "candidate" in "to_parse" or we find "candidate" but it is followed
 * by more chars (for example, "candidatefoo"), that is, we don't find
 * an exact match.
 *
 * This function only does the above for one "candidate" at a time. So
 * it has to be called each time trying to parse a "candidate" in the
 * option string "to_parse".
 */
static int match_atom_arg_value(const char *to_parse, const char *candidate,
				const char **end, const char **valuestart,
				size_t *valuelen)
{
	const char *atom;

	if (!skip_prefix(to_parse, candidate, &atom))
		return 0; /* definitely not "candidate" */

	if (*atom == '=') {
		/* we just saw "candidate=" */
		*valuestart = atom + 1;
		atom = strchrnul(*valuestart, ',');
		*valuelen = atom - *valuestart;
	} else if (*atom != ',' && *atom != '\0') {
		/* key begins with "candidate" but has more chars */
		return 0;
	} else {
		/* just "candidate" without "=val" */
		*valuestart = NULL;
		*valuelen = 0;
	}

	/* atom points at either the ',' or NUL after this key[=val] */
	if (*atom == ',')
		atom++;
	else if (*atom)
		BUG("Why is *atom not NULL yet?");

	*end = atom;
	return 1;
}

/*
 * Parse boolean option of name "candidate" in the option list "to_parse"
 * of the form
 *
 *	"candidate1[=bool1],candidate2[=bool2],candidate3[=bool3],..."
 *
 * The remaining part of "to_parse" is stored in "end" (if we are parsing
 * the last candidate, then this is NULL) and the value (if given) is
 * parsed and stored in "val", so "val" always points to either 0 or 1.
 * If the value is not given, then "val" is set to point to 1.
 *
 * The boolean value is parsed using "git_parse_maybe_bool()", so the
 * accepted values are
 *
 *	to set true  - "1", "yes", "true"
 *	to set false - "0", "no", "false"
 *
 * This function returns 1 on success. It returns 0 when we don't find
 * an exact match for "candidate" or when the boolean value given is
 * not valid.
 */
static int match_atom_bool_arg(const char *to_parse, const char *candidate,
				const char **end, int *val)
{
	const char *argval;
	char *strval;
	size_t arglen;
	int v;

	if (!match_atom_arg_value(to_parse, candidate, end, &argval, &arglen))
		return 0;

	if (!argval) {
		*val = 1;
		return 1;
	}

	strval = xstrndup(argval, arglen);
	v = git_parse_maybe_bool(strval);
	free(strval);

	if (v == -1)
		return 0;

	*val = v;

	return 1;
}

static int color_atom_parser(struct ref_format *format, struct used_atom *atom,
			     const char *color_value, struct strbuf *err)
{
	if (!color_value)
		return strbuf_addf_ret(err, -1, _("expected format: %%(color:<color>)"));
	if (color_parse(color_value, atom->u.color) < 0)
		return strbuf_addf_ret(err, -1, _("unrecognized color: %%(color:%s)"),
				       color_value);
	/*
	 * We check this after we've parsed the color, which lets us complain
	 * about syntactically bogus color names even if they won't be used.
	 */
	if (!want_color(format->use_color))
		color_parse("", atom->u.color);
	return 0;
}

static int refname_atom_parser_internal(struct refname_atom *atom, const char *arg,
					 const char *name, struct strbuf *err)
{
	if (!arg)
		atom->option = R_NORMAL;
	else if (!strcmp(arg, "short"))
		atom->option = R_SHORT;
	else if (skip_prefix(arg, "lstrip=", &arg) ||
		 skip_prefix(arg, "strip=", &arg)) {
		atom->option = R_LSTRIP;
		if (strtol_i(arg, 10, &atom->lstrip))
			return strbuf_addf_ret(err, -1, _("Integer value expected refname:lstrip=%s"), arg);
	} else if (skip_prefix(arg, "rstrip=", &arg)) {
		atom->option = R_RSTRIP;
		if (strtol_i(arg, 10, &atom->rstrip))
			return strbuf_addf_ret(err, -1, _("Integer value expected refname:rstrip=%s"), arg);
	} else
		return err_bad_arg(err, name, arg);
	return 0;
}

static int remote_ref_atom_parser(struct ref_format *format UNUSED,
				  struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	struct string_list params = STRING_LIST_INIT_DUP;
	int i;

	if (!strcmp(atom->name, "push") || starts_with(atom->name, "push:"))
		atom->u.remote_ref.push = 1;

	if (!arg) {
		atom->u.remote_ref.option = RR_REF;
		return refname_atom_parser_internal(&atom->u.remote_ref.refname,
						    arg, atom->name, err);
	}

	atom->u.remote_ref.nobracket = 0;
	string_list_split(&params, arg, ',', -1);

	for (i = 0; i < params.nr; i++) {
		const char *s = params.items[i].string;

		if (!strcmp(s, "track"))
			atom->u.remote_ref.option = RR_TRACK;
		else if (!strcmp(s, "trackshort"))
			atom->u.remote_ref.option = RR_TRACKSHORT;
		else if (!strcmp(s, "nobracket"))
			atom->u.remote_ref.nobracket = 1;
		else if (!strcmp(s, "remotename")) {
			atom->u.remote_ref.option = RR_REMOTE_NAME;
			atom->u.remote_ref.push_remote = 1;
		} else if (!strcmp(s, "remoteref")) {
			atom->u.remote_ref.option = RR_REMOTE_REF;
			atom->u.remote_ref.push_remote = 1;
		} else {
			atom->u.remote_ref.option = RR_REF;
			if (refname_atom_parser_internal(&atom->u.remote_ref.refname,
							 arg, atom->name, err)) {
				string_list_clear(&params, 0);
				return -1;
			}
		}
	}

	string_list_clear(&params, 0);
	return 0;
}

static int objecttype_atom_parser(struct ref_format *format UNUSED,
				  struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	if (arg)
		return err_no_arg(err, "objecttype");
	if (*atom->name == '*')
		oi_deref.info.typep = &oi_deref.type;
	else
		oi.info.typep = &oi.type;
	return 0;
}

static int objectsize_atom_parser(struct ref_format *format UNUSED,
				  struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	if (!arg) {
		atom->u.objectsize.option = O_SIZE;
		if (*atom->name == '*')
			oi_deref.info.sizep = &oi_deref.size;
		else
			oi.info.sizep = &oi.size;
	} else if (!strcmp(arg, "disk")) {
		atom->u.objectsize.option = O_SIZE_DISK;
		if (*atom->name == '*')
			oi_deref.info.disk_sizep = &oi_deref.disk_size;
		else
			oi.info.disk_sizep = &oi.disk_size;
	} else
		return err_bad_arg(err, "objectsize", arg);
	return 0;
}

static int deltabase_atom_parser(struct ref_format *format UNUSED,
				 struct used_atom *atom,
				 const char *arg, struct strbuf *err)
{
	if (arg)
		return err_no_arg(err, "deltabase");
	if (*atom->name == '*')
		oi_deref.info.delta_base_oid = &oi_deref.delta_base_oid;
	else
		oi.info.delta_base_oid = &oi.delta_base_oid;
	return 0;
}

static int body_atom_parser(struct ref_format *format UNUSED,
			    struct used_atom *atom,
			    const char *arg, struct strbuf *err)
{
	if (arg)
		return err_no_arg(err, "body");
	atom->u.contents.option = C_BODY_DEP;
	return 0;
}

static int subject_atom_parser(struct ref_format *format UNUSED,
			       struct used_atom *atom,
			       const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.contents.option = C_SUB;
	else if (!strcmp(arg, "sanitize"))
		atom->u.contents.option = C_SUB_SANITIZE;
	else
		return err_bad_arg(err, "subject", arg);
	return 0;
}

static int parse_signature_option(const char *arg)
{
	if (!arg)
		return S_BARE;
	else if (!strcmp(arg, "signer"))
		return S_SIGNER;
	else if (!strcmp(arg, "grade"))
		return S_GRADE;
	else if (!strcmp(arg, "key"))
		return S_KEY;
	else if (!strcmp(arg, "fingerprint"))
		return S_FINGERPRINT;
	else if (!strcmp(arg, "primarykeyfingerprint"))
		return S_PRI_KEY_FP;
	else if (!strcmp(arg, "trustlevel"))
		return S_TRUST_LEVEL;
	return -1;
}

static int signature_atom_parser(struct ref_format *format UNUSED,
				 struct used_atom *atom,
				 const char *arg, struct strbuf *err)
{
	int opt = parse_signature_option(arg);
	if (opt < 0)
		return err_bad_arg(err, "signature", arg);
	atom->u.signature.option = opt;
	return 0;
}

static int trailers_atom_parser(struct ref_format *format UNUSED,
				struct used_atom *atom,
				const char *arg, struct strbuf *err)
{
	atom->u.contents.trailer_opts.no_divider = 1;

	if (arg) {
		const char *argbuf = xstrfmt("%s)", arg);
		char *invalid_arg = NULL;

		if (format_set_trailers_options(&atom->u.contents.trailer_opts,
		    &ref_trailer_buf.filter_list,
		    &ref_trailer_buf.sepbuf,
		    &ref_trailer_buf.kvsepbuf,
		    &argbuf, &invalid_arg)) {
			if (!invalid_arg)
				strbuf_addf(err, _("expected %%(trailers:key=<value>)"));
			else
				strbuf_addf(err, _("unknown %%(trailers) argument: %s"), invalid_arg);
			free((char *)invalid_arg);
			return -1;
		}
	}
	atom->u.contents.option = C_TRAILERS;
	return 0;
}

static int contents_atom_parser(struct ref_format *format, struct used_atom *atom,
				const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.contents.option = C_BARE;
	else if (!strcmp(arg, "body"))
		atom->u.contents.option = C_BODY;
	else if (!strcmp(arg, "size")) {
		atom->type = FIELD_ULONG;
		atom->u.contents.option = C_LENGTH;
	} else if (!strcmp(arg, "signature"))
		atom->u.contents.option = C_SIG;
	else if (!strcmp(arg, "subject"))
		atom->u.contents.option = C_SUB;
	else if (!strcmp(arg, "trailers")) {
		if (trailers_atom_parser(format, atom, NULL, err))
			return -1;
	} else if (skip_prefix(arg, "trailers:", &arg)) {
		if (trailers_atom_parser(format, atom, arg, err))
			return -1;
	} else if (skip_prefix(arg, "lines=", &arg)) {
		atom->u.contents.option = C_LINES;
		if (strtoul_ui(arg, 10, &atom->u.contents.nlines))
			return strbuf_addf_ret(err, -1, _("positive value expected contents:lines=%s"), arg);
	} else
		return err_bad_arg(err, "contents", arg);
	return 0;
}

static int describe_atom_option_parser(struct strvec *args, const char **arg,
				       struct strbuf *err)
{
	const char *argval;
	size_t arglen = 0;
	int optval = 0;

	if (match_atom_bool_arg(*arg, "tags", arg, &optval)) {
		if (!optval)
			strvec_push(args, "--no-tags");
		else
			strvec_push(args, "--tags");
		return 1;
	}

	if (match_atom_arg_value(*arg, "abbrev", arg, &argval, &arglen)) {
		char *endptr;

		if (!arglen)
			return strbuf_addf_ret(err, -1,
					       _("argument expected for %s"),
					       "describe:abbrev");
		if (strtol(argval, &endptr, 10) < 0)
			return strbuf_addf_ret(err, -1,
					       _("positive value expected %s=%s"),
					       "describe:abbrev", argval);
		if (endptr - argval != arglen)
			return strbuf_addf_ret(err, -1,
					       _("cannot fully parse %s=%s"),
					       "describe:abbrev", argval);

		strvec_pushf(args, "--abbrev=%.*s", (int)arglen, argval);
		return 1;
	}

	if (match_atom_arg_value(*arg, "match", arg, &argval, &arglen)) {
		if (!arglen)
			return strbuf_addf_ret(err, -1,
					       _("value expected %s="),
					       "describe:match");

		strvec_pushf(args, "--match=%.*s", (int)arglen, argval);
		return 1;
	}

	if (match_atom_arg_value(*arg, "exclude", arg, &argval, &arglen)) {
		if (!arglen)
			return strbuf_addf_ret(err, -1,
					       _("value expected %s="),
					       "describe:exclude");

		strvec_pushf(args, "--exclude=%.*s", (int)arglen, argval);
		return 1;
	}

	return 0;
}

static int describe_atom_parser(struct ref_format *format UNUSED,
				struct used_atom *atom,
				const char *arg, struct strbuf *err)
{
	struct strvec args = STRVEC_INIT;

	for (;;) {
		int found = 0;
		const char *bad_arg = arg;

		if (!arg || !*arg)
			break;

		found = describe_atom_option_parser(&args, &arg, err);
		if (found < 0)
			return found;
		if (!found)
			return err_bad_arg(err, "describe", bad_arg);
	}
	atom->u.describe_args = strvec_detach(&args);
	return 0;
}

static int raw_atom_parser(struct ref_format *format UNUSED,
			   struct used_atom *atom,
			   const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.raw_data.option = RAW_BARE;
	else if (!strcmp(arg, "size")) {
		atom->type = FIELD_ULONG;
		atom->u.raw_data.option = RAW_LENGTH;
	} else
		return err_bad_arg(err, "raw", arg);
	return 0;
}

static int oid_atom_parser(struct ref_format *format UNUSED,
			   struct used_atom *atom,
			   const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.oid.option = O_FULL;
	else if (!strcmp(arg, "short"))
		atom->u.oid.option = O_SHORT;
	else if (skip_prefix(arg, "short=", &arg)) {
		atom->u.oid.option = O_LENGTH;
		if (strtoul_ui(arg, 10, &atom->u.oid.length) ||
		    atom->u.oid.length == 0)
			return strbuf_addf_ret(err, -1, _("positive value expected '%s' in %%(%s)"), arg, atom->name);
		if (atom->u.oid.length < MINIMUM_ABBREV)
			atom->u.oid.length = MINIMUM_ABBREV;
	} else
		return err_bad_arg(err, atom->name, arg);
	return 0;
}

static int person_name_atom_parser(struct ref_format *format UNUSED,
				   struct used_atom *atom,
				   const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.name_option.option = N_RAW;
	else if (!strcmp(arg, "mailmap"))
		atom->u.name_option.option = N_MAILMAP;
	else
		return err_bad_arg(err, atom->name, arg);
	return 0;
}

static int email_atom_option_parser(struct used_atom *atom,
				    const char **arg, struct strbuf *err)
{
	if (!*arg)
		return EO_RAW;
	if (skip_prefix(*arg, "trim", arg))
		return EO_TRIM;
	if (skip_prefix(*arg, "localpart", arg))
		return EO_LOCALPART;
	if (skip_prefix(*arg, "mailmap", arg))
		return EO_MAILMAP;
	return -1;
}

static int person_email_atom_parser(struct ref_format *format UNUSED,
				    struct used_atom *atom,
				    const char *arg, struct strbuf *err)
{
	for (;;) {
		int opt = email_atom_option_parser(atom, &arg, err);
		const char *bad_arg = arg;

		if (opt < 0)
			return err_bad_arg(err, atom->name, bad_arg);
		atom->u.email_option.option |= opt;

		if (!arg || !*arg)
			break;
		if (*arg == ',')
			arg++;
		else
			return err_bad_arg(err, atom->name, bad_arg);
	}
	return 0;
}

static int refname_atom_parser(struct ref_format *format UNUSED,
			       struct used_atom *atom,
			       const char *arg, struct strbuf *err)
{
	return refname_atom_parser_internal(&atom->u.refname, arg, atom->name, err);
}

static align_type parse_align_position(const char *s)
{
	if (!strcmp(s, "right"))
		return ALIGN_RIGHT;
	else if (!strcmp(s, "middle"))
		return ALIGN_MIDDLE;
	else if (!strcmp(s, "left"))
		return ALIGN_LEFT;
	return -1;
}

static int align_atom_parser(struct ref_format *format UNUSED,
			     struct used_atom *atom,
			     const char *arg, struct strbuf *err)
{
	struct align *align = &atom->u.align;
	struct string_list params = STRING_LIST_INIT_DUP;
	int i;
	unsigned int width = ~0U;

	if (!arg)
		return strbuf_addf_ret(err, -1, _("expected format: %%(align:<width>,<position>)"));

	align->position = ALIGN_LEFT;

	string_list_split(&params, arg, ',', -1);
	for (i = 0; i < params.nr; i++) {
		const char *s = params.items[i].string;
		int position;

		if (skip_prefix(s, "position=", &s)) {
			position = parse_align_position(s);
			if (position < 0) {
				strbuf_addf(err, _("unrecognized position:%s"), s);
				string_list_clear(&params, 0);
				return -1;
			}
			align->position = position;
		} else if (skip_prefix(s, "width=", &s)) {
			if (strtoul_ui(s, 10, &width)) {
				strbuf_addf(err, _("unrecognized width:%s"), s);
				string_list_clear(&params, 0);
				return -1;
			}
		} else if (!strtoul_ui(s, 10, &width))
			;
		else if ((position = parse_align_position(s)) >= 0)
			align->position = position;
		else {
			strbuf_addf(err, _("unrecognized %%(%s) argument: %s"), "align", s);
			string_list_clear(&params, 0);
			return -1;
		}
	}

	if (width == ~0U) {
		string_list_clear(&params, 0);
		return strbuf_addf_ret(err, -1, _("positive width expected with the %%(align) atom"));
	}
	align->width = width;
	string_list_clear(&params, 0);
	return 0;
}

static int if_atom_parser(struct ref_format *format UNUSED,
			  struct used_atom *atom,
			  const char *arg, struct strbuf *err)
{
	if (!arg) {
		atom->u.if_then_else.cmp_status = COMPARE_NONE;
		return 0;
	} else if (skip_prefix(arg, "equals=", &atom->u.if_then_else.str)) {
		atom->u.if_then_else.cmp_status = COMPARE_EQUAL;
	} else if (skip_prefix(arg, "notequals=", &atom->u.if_then_else.str)) {
		atom->u.if_then_else.cmp_status = COMPARE_UNEQUAL;
	} else
		return err_bad_arg(err, "if", arg);
	return 0;
}

static int rest_atom_parser(struct ref_format *format UNUSED,
			    struct used_atom *atom UNUSED,
			    const char *arg, struct strbuf *err)
{
	if (arg)
		return err_no_arg(err, "rest");
	return 0;
}

static int ahead_behind_atom_parser(struct ref_format *format,
				    struct used_atom *atom UNUSED,
				    const char *arg, struct strbuf *err)
{
	struct string_list_item *item;

	if (!arg)
		return strbuf_addf_ret(err, -1, _("expected format: %%(ahead-behind:<committish>)"));

	item = string_list_append(&format->bases, arg);
	item->util = lookup_commit_reference_by_name(arg);
	if (!item->util)
		die("failed to find '%s'", arg);

	return 0;
}

static int head_atom_parser(struct ref_format *format UNUSED,
			    struct used_atom *atom,
			    const char *arg, struct strbuf *err)
{
	if (arg)
		return err_no_arg(err, "HEAD");
	atom->u.head = resolve_refdup("HEAD", RESOLVE_REF_READING, NULL, NULL);
	return 0;
}

static struct {
	const char *name;
	info_source source;
	cmp_type cmp_type;
	int (*parser)(struct ref_format *format, struct used_atom *atom,
		      const char *arg, struct strbuf *err);
} valid_atom[] = {
	[ATOM_REFNAME] = { "refname", SOURCE_NONE, FIELD_STR, refname_atom_parser },
	[ATOM_OBJECTTYPE] = { "objecttype", SOURCE_OTHER, FIELD_STR, objecttype_atom_parser },
	[ATOM_OBJECTSIZE] = { "objectsize", SOURCE_OTHER, FIELD_ULONG, objectsize_atom_parser },
	[ATOM_OBJECTNAME] = { "objectname", SOURCE_OTHER, FIELD_STR, oid_atom_parser },
	[ATOM_DELTABASE] = { "deltabase", SOURCE_OTHER, FIELD_STR, deltabase_atom_parser },
	[ATOM_TREE] = { "tree", SOURCE_OBJ, FIELD_STR, oid_atom_parser },
	[ATOM_PARENT] = { "parent", SOURCE_OBJ, FIELD_STR, oid_atom_parser },
	[ATOM_NUMPARENT] = { "numparent", SOURCE_OBJ, FIELD_ULONG },
	[ATOM_OBJECT] = { "object", SOURCE_OBJ },
	[ATOM_TYPE] = { "type", SOURCE_OBJ },
	[ATOM_TAG] = { "tag", SOURCE_OBJ },
	[ATOM_AUTHOR] = { "author", SOURCE_OBJ },
	[ATOM_AUTHORNAME] = { "authorname", SOURCE_OBJ, FIELD_STR, person_name_atom_parser },
	[ATOM_AUTHOREMAIL] = { "authoremail", SOURCE_OBJ, FIELD_STR, person_email_atom_parser },
	[ATOM_AUTHORDATE] = { "authordate", SOURCE_OBJ, FIELD_TIME },
	[ATOM_COMMITTER] = { "committer", SOURCE_OBJ },
	[ATOM_COMMITTERNAME] = { "committername", SOURCE_OBJ, FIELD_STR, person_name_atom_parser },
	[ATOM_COMMITTEREMAIL] = { "committeremail", SOURCE_OBJ, FIELD_STR, person_email_atom_parser },
	[ATOM_COMMITTERDATE] = { "committerdate", SOURCE_OBJ, FIELD_TIME },
	[ATOM_TAGGER] = { "tagger", SOURCE_OBJ },
	[ATOM_TAGGERNAME] = { "taggername", SOURCE_OBJ, FIELD_STR, person_name_atom_parser },
	[ATOM_TAGGEREMAIL] = { "taggeremail", SOURCE_OBJ, FIELD_STR, person_email_atom_parser },
	[ATOM_TAGGERDATE] = { "taggerdate", SOURCE_OBJ, FIELD_TIME },
	[ATOM_CREATOR] = { "creator", SOURCE_OBJ },
	[ATOM_CREATORDATE] = { "creatordate", SOURCE_OBJ, FIELD_TIME },
	[ATOM_DESCRIBE] = { "describe", SOURCE_OBJ, FIELD_STR, describe_atom_parser },
	[ATOM_SUBJECT] = { "subject", SOURCE_OBJ, FIELD_STR, subject_atom_parser },
	[ATOM_BODY] = { "body", SOURCE_OBJ, FIELD_STR, body_atom_parser },
	[ATOM_TRAILERS] = { "trailers", SOURCE_OBJ, FIELD_STR, trailers_atom_parser },
	[ATOM_CONTENTS] = { "contents", SOURCE_OBJ, FIELD_STR, contents_atom_parser },
	[ATOM_SIGNATURE] = { "signature", SOURCE_OBJ, FIELD_STR, signature_atom_parser },
	[ATOM_RAW] = { "raw", SOURCE_OBJ, FIELD_STR, raw_atom_parser },
	[ATOM_UPSTREAM] = { "upstream", SOURCE_NONE, FIELD_STR, remote_ref_atom_parser },
	[ATOM_PUSH] = { "push", SOURCE_NONE, FIELD_STR, remote_ref_atom_parser },
	[ATOM_SYMREF] = { "symref", SOURCE_NONE, FIELD_STR, refname_atom_parser },
	[ATOM_FLAG] = { "flag", SOURCE_NONE },
	[ATOM_HEAD] = { "HEAD", SOURCE_NONE, FIELD_STR, head_atom_parser },
	[ATOM_COLOR] = { "color", SOURCE_NONE, FIELD_STR, color_atom_parser },
	[ATOM_WORKTREEPATH] = { "worktreepath", SOURCE_NONE },
	[ATOM_ALIGN] = { "align", SOURCE_NONE, FIELD_STR, align_atom_parser },
	[ATOM_END] = { "end", SOURCE_NONE },
	[ATOM_IF] = { "if", SOURCE_NONE, FIELD_STR, if_atom_parser },
	[ATOM_THEN] = { "then", SOURCE_NONE },
	[ATOM_ELSE] = { "else", SOURCE_NONE },
	[ATOM_REST] = { "rest", SOURCE_NONE, FIELD_STR, rest_atom_parser },
	[ATOM_AHEADBEHIND] = { "ahead-behind", SOURCE_OTHER, FIELD_STR, ahead_behind_atom_parser },
	/*
	 * Please update $__git_ref_fieldlist in git-completion.bash
	 * when you add new atoms
	 */
};

#define REF_FORMATTING_STATE_INIT  { 0 }

struct ref_formatting_stack {
	struct ref_formatting_stack *prev;
	struct strbuf output;
	void (*at_end)(struct ref_formatting_stack **stack);
	void *at_end_data;
};

struct ref_formatting_state {
	int quote_style;
	struct ref_formatting_stack *stack;
};

struct atom_value {
	const char *s;
	ssize_t s_size;
	int (*handler)(struct atom_value *atomv, struct ref_formatting_state *state,
		       struct strbuf *err);
	uintmax_t value; /* used for sorting when not FIELD_STR */
	struct used_atom *atom;
};

#define ATOM_SIZE_UNSPECIFIED (-1)

#define ATOM_VALUE_INIT { \
	.s_size = ATOM_SIZE_UNSPECIFIED \
}

/*
 * Used to parse format string and sort specifiers
 */
static int parse_ref_filter_atom(struct ref_format *format,
				 const char *atom, const char *ep,
				 struct strbuf *err)
{
	const char *sp;
	const char *arg;
	int i, at, atom_len;

	sp = atom;
	if (*sp == '*' && sp < ep)
		sp++; /* deref */
	if (ep <= sp)
		return strbuf_addf_ret(err, -1, _("malformed field name: %.*s"),
				       (int)(ep-atom), atom);

	/*
	 * If the atom name has a colon, strip it and everything after
	 * it off - it specifies the format for this entry, and
	 * shouldn't be used for checking against the valid_atom
	 * table.
	 */
	arg = memchr(sp, ':', ep - sp);
	atom_len = (arg ? arg : ep) - sp;

	/* Do we have the atom already used elsewhere? */
	for (i = 0; i < used_atom_cnt; i++) {
		int len = strlen(used_atom[i].name);
		if (len == ep - atom && !memcmp(used_atom[i].name, atom, len))
			return i;
	}

	/* Is the atom a valid one? */
	for (i = 0; i < ARRAY_SIZE(valid_atom); i++) {
		int len = strlen(valid_atom[i].name);
		if (len == atom_len && !memcmp(valid_atom[i].name, sp, len))
			break;
	}

	if (ARRAY_SIZE(valid_atom) <= i)
		return strbuf_addf_ret(err, -1, _("unknown field name: %.*s"),
				       (int)(ep-atom), atom);
	if (valid_atom[i].source != SOURCE_NONE && !have_git_dir())
		return strbuf_addf_ret(err, -1,
				       _("not a git repository, but the field '%.*s' requires access to object data"),
				       (int)(ep-atom), atom);

	/* Add it in, including the deref prefix */
	at = used_atom_cnt;
	used_atom_cnt++;
	REALLOC_ARRAY(used_atom, used_atom_cnt);
	used_atom[at].atom_type = i;
	used_atom[at].name = xmemdupz(atom, ep - atom);
	used_atom[at].type = valid_atom[i].cmp_type;
	used_atom[at].source = valid_atom[i].source;
	if (used_atom[at].source == SOURCE_OBJ) {
		if (*atom == '*')
			oi_deref.info.contentp = &oi_deref.content;
		else
			oi.info.contentp = &oi.content;
	}
	if (arg) {
		arg = used_atom[at].name + (arg - atom) + 1;
		if (!*arg) {
			/*
			 * Treat empty sub-arguments list as NULL (i.e.,
			 * "%(atom:)" is equivalent to "%(atom)").
			 */
			arg = NULL;
		}
	}
	memset(&used_atom[at].u, 0, sizeof(used_atom[at].u));
	if (valid_atom[i].parser && valid_atom[i].parser(format, &used_atom[at], arg, err))
		return -1;
	if (*atom == '*')
		need_tagged = 1;
	if (i == ATOM_SYMREF)
		need_symref = 1;
	return at;
}

static void quote_formatting(struct strbuf *s, const char *str, ssize_t len, int quote_style)
{
	switch (quote_style) {
	case QUOTE_NONE:
		if (len < 0)
			strbuf_addstr(s, str);
		else
			strbuf_add(s, str, len);
		break;
	case QUOTE_SHELL:
		sq_quote_buf(s, str);
		break;
	case QUOTE_PERL:
		if (len < 0)
			perl_quote_buf(s, str);
		else
			perl_quote_buf_with_len(s, str, len);
		break;
	case QUOTE_PYTHON:
		python_quote_buf(s, str);
		break;
	case QUOTE_TCL:
		tcl_quote_buf(s, str);
		break;
	}
}

static int append_atom(struct atom_value *v, struct ref_formatting_state *state,
		       struct strbuf *err UNUSED)
{
	/*
	 * Quote formatting is only done when the stack has a single
	 * element. Otherwise quote formatting is done on the
	 * element's entire output strbuf when the %(end) atom is
	 * encountered.
	 */
	if (!state->stack->prev)
		quote_formatting(&state->stack->output, v->s, v->s_size, state->quote_style);
	else if (v->s_size < 0)
		strbuf_addstr(&state->stack->output, v->s);
	else
		strbuf_add(&state->stack->output, v->s, v->s_size);
	return 0;
}

static void push_stack_element(struct ref_formatting_stack **stack)
{
	struct ref_formatting_stack *s = xcalloc(1, sizeof(struct ref_formatting_stack));

	strbuf_init(&s->output, 0);
	s->prev = *stack;
	*stack = s;
}

static void pop_stack_element(struct ref_formatting_stack **stack)
{
	struct ref_formatting_stack *current = *stack;
	struct ref_formatting_stack *prev = current->prev;

	if (prev)
		strbuf_addbuf(&prev->output, &current->output);
	strbuf_release(&current->output);
	free(current);
	*stack = prev;
}

static void end_align_handler(struct ref_formatting_stack **stack)
{
	struct ref_formatting_stack *cur = *stack;
	struct align *align = (struct align *)cur->at_end_data;
	struct strbuf s = STRBUF_INIT;

	strbuf_utf8_align(&s, align->position, align->width, cur->output.buf);
	strbuf_swap(&cur->output, &s);
	strbuf_release(&s);
}

static int align_atom_handler(struct atom_value *atomv, struct ref_formatting_state *state,
			      struct strbuf *err UNUSED)
{
	struct ref_formatting_stack *new_stack;

	push_stack_element(&state->stack);
	new_stack = state->stack;
	new_stack->at_end = end_align_handler;
	new_stack->at_end_data = &atomv->atom->u.align;
	return 0;
}

static void if_then_else_handler(struct ref_formatting_stack **stack)
{
	struct ref_formatting_stack *cur = *stack;
	struct ref_formatting_stack *prev = cur->prev;
	struct if_then_else *if_then_else = (struct if_then_else *)cur->at_end_data;

	if (!if_then_else->then_atom_seen)
		die(_("format: %%(%s) atom used without a %%(%s) atom"), "if", "then");

	if (if_then_else->else_atom_seen) {
		/*
		 * There is an %(else) atom: we need to drop one state from the
		 * stack, either the %(else) branch if the condition is satisfied, or
		 * the %(then) branch if it isn't.
		 */
		if (if_then_else->condition_satisfied) {
			strbuf_reset(&cur->output);
			pop_stack_element(&cur);
		} else {
			strbuf_swap(&cur->output, &prev->output);
			strbuf_reset(&cur->output);
			pop_stack_element(&cur);
		}
	} else if (!if_then_else->condition_satisfied) {
		/*
		 * No %(else) atom: just drop the %(then) branch if the
		 * condition is not satisfied.
		 */
		strbuf_reset(&cur->output);
	}

	*stack = cur;
	free(if_then_else);
}

static int if_atom_handler(struct atom_value *atomv, struct ref_formatting_state *state,
			   struct strbuf *err UNUSED)
{
	struct ref_formatting_stack *new_stack;
	struct if_then_else *if_then_else = xcalloc(1,
						    sizeof(struct if_then_else));

	if_then_else->str = atomv->atom->u.if_then_else.str;
	if_then_else->cmp_status = atomv->atom->u.if_then_else.cmp_status;

	push_stack_element(&state->stack);
	new_stack = state->stack;
	new_stack->at_end = if_then_else_handler;
	new_stack->at_end_data = if_then_else;
	return 0;
}

static int is_empty(struct strbuf *buf)
{
	const char *cur = buf->buf;
	const char *end = buf->buf + buf->len;

	while (cur != end && (isspace(*cur)))
		cur++;

	return cur == end;
 }

static int then_atom_handler(struct atom_value *atomv UNUSED,
			     struct ref_formatting_state *state,
			     struct strbuf *err)
{
	struct ref_formatting_stack *cur = state->stack;
	struct if_then_else *if_then_else = NULL;
	size_t str_len = 0;

	if (cur->at_end == if_then_else_handler)
		if_then_else = (struct if_then_else *)cur->at_end_data;
	if (!if_then_else)
		return strbuf_addf_ret(err, -1, _("format: %%(%s) atom used without a %%(%s) atom"), "then", "if");
	if (if_then_else->then_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(then) atom used more than once"));
	if (if_then_else->else_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(then) atom used after %%(else)"));
	if_then_else->then_atom_seen = 1;
	if (if_then_else->str)
		str_len = strlen(if_then_else->str);
	/*
	 * If the 'equals' or 'notequals' attribute is used then
	 * perform the required comparison. If not, only non-empty
	 * strings satisfy the 'if' condition.
	 */
	if (if_then_else->cmp_status == COMPARE_EQUAL) {
		if (str_len == cur->output.len &&
		    !memcmp(if_then_else->str, cur->output.buf, cur->output.len))
			if_then_else->condition_satisfied = 1;
	} else if (if_then_else->cmp_status == COMPARE_UNEQUAL) {
		if (str_len != cur->output.len ||
		    memcmp(if_then_else->str, cur->output.buf, cur->output.len))
			if_then_else->condition_satisfied = 1;
	} else if (cur->output.len && !is_empty(&cur->output))
		if_then_else->condition_satisfied = 1;
	strbuf_reset(&cur->output);
	return 0;
}

static int else_atom_handler(struct atom_value *atomv UNUSED,
			     struct ref_formatting_state *state,
			     struct strbuf *err)
{
	struct ref_formatting_stack *prev = state->stack;
	struct if_then_else *if_then_else = NULL;

	if (prev->at_end == if_then_else_handler)
		if_then_else = (struct if_then_else *)prev->at_end_data;
	if (!if_then_else)
		return strbuf_addf_ret(err, -1, _("format: %%(%s) atom used without a %%(%s) atom"), "else", "if");
	if (!if_then_else->then_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(%s) atom used without a %%(%s) atom"), "else", "then");
	if (if_then_else->else_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(else) atom used more than once"));
	if_then_else->else_atom_seen = 1;
	push_stack_element(&state->stack);
	state->stack->at_end_data = prev->at_end_data;
	state->stack->at_end = prev->at_end;
	return 0;
}

static int end_atom_handler(struct atom_value *atomv UNUSED,
			    struct ref_formatting_state *state,
			    struct strbuf *err)
{
	struct ref_formatting_stack *current = state->stack;
	struct strbuf s = STRBUF_INIT;

	if (!current->at_end)
		return strbuf_addf_ret(err, -1, _("format: %%(end) atom used without corresponding atom"));
	current->at_end(&state->stack);

	/*  Stack may have been popped within at_end(), hence reset the current pointer */
	current = state->stack;

	/*
	 * Perform quote formatting when the stack element is that of
	 * a supporting atom. If nested then perform quote formatting
	 * only on the topmost supporting atom.
	 */
	if (!current->prev->prev) {
		quote_formatting(&s, current->output.buf, current->output.len, state->quote_style);
		strbuf_swap(&current->output, &s);
	}
	strbuf_release(&s);
	pop_stack_element(&state->stack);
	return 0;
}

/*
 * In a format string, find the next occurrence of %(atom).
 */
static const char *find_next(const char *cp)
{
	while (*cp) {
		if (*cp == '%') {
			/*
			 * %( is the start of an atom;
			 * %% is a quoted per-cent.
			 */
			if (cp[1] == '(')
				return cp;
			else if (cp[1] == '%')
				cp++; /* skip over two % */
			/* otherwise this is a singleton, literal % */
		}
		cp++;
	}
	return NULL;
}

static int reject_atom(enum atom_type atom_type)
{
	return atom_type == ATOM_REST;
}

/*
 * Make sure the format string is well formed, and parse out
 * the used atoms.
 */
int verify_ref_format(struct ref_format *format)
{
	const char *cp, *sp;

	format->need_color_reset_at_eol = 0;
	for (cp = format->format; *cp && (sp = find_next(cp)); ) {
		struct strbuf err = STRBUF_INIT;
		const char *color, *ep = strchr(sp, ')');
		int at;

		if (!ep)
			return error(_("malformed format string %s"), sp);
		/* sp points at "%(" and ep points at the closing ")" */
		at = parse_ref_filter_atom(format, sp + 2, ep, &err);
		if (at < 0)
			die("%s", err.buf);
		if (reject_atom(used_atom[at].atom_type))
			die(_("this command reject atom %%(%.*s)"), (int)(ep - sp - 2), sp + 2);

		if ((format->quote_style == QUOTE_PYTHON ||
		     format->quote_style == QUOTE_SHELL ||
		     format->quote_style == QUOTE_TCL) &&
		     used_atom[at].atom_type == ATOM_RAW &&
		     used_atom[at].u.raw_data.option == RAW_BARE)
			die(_("--format=%.*s cannot be used with "
			      "--python, --shell, --tcl"), (int)(ep - sp - 2), sp + 2);
		cp = ep + 1;

		if (skip_prefix(used_atom[at].name, "color:", &color))
			format->need_color_reset_at_eol = !!strcmp(color, "reset");
		strbuf_release(&err);
	}
	if (format->need_color_reset_at_eol && !want_color(format->use_color))
		format->need_color_reset_at_eol = 0;
	return 0;
}

static const char *do_grab_oid(const char *field, const struct object_id *oid,
			       struct used_atom *atom)
{
	switch (atom->u.oid.option) {
	case O_FULL:
		return oid_to_hex(oid);
	case O_LENGTH:
		return repo_find_unique_abbrev(the_repository, oid,
					       atom->u.oid.length);
	case O_SHORT:
		return repo_find_unique_abbrev(the_repository, oid,
					       DEFAULT_ABBREV);
	default:
		BUG("unknown %%(%s) option", field);
	}
}

static int grab_oid(const char *name, const char *field, const struct object_id *oid,
		    struct atom_value *v, struct used_atom *atom)
{
	if (starts_with(name, field)) {
		v->s = xstrdup(do_grab_oid(field, oid, atom));
		return 1;
	}
	return 0;
}

/* See grab_values */
static void grab_common_values(struct atom_value *val, int deref, struct expand_data *oi)
{
	int i;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		enum atom_type atom_type = used_atom[i].atom_type;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (atom_type == ATOM_OBJECTTYPE)
			v->s = xstrdup(type_name(oi->type));
		else if (atom_type == ATOM_OBJECTSIZE) {
			if (used_atom[i].u.objectsize.option == O_SIZE_DISK) {
				v->value = oi->disk_size;
				v->s = xstrfmt("%"PRIuMAX, (uintmax_t)oi->disk_size);
			} else if (used_atom[i].u.objectsize.option == O_SIZE) {
				v->value = oi->size;
				v->s = xstrfmt("%"PRIuMAX , (uintmax_t)oi->size);
			}
		} else if (atom_type == ATOM_DELTABASE)
			v->s = xstrdup(oid_to_hex(&oi->delta_base_oid));
		else if (atom_type == ATOM_OBJECTNAME && deref)
			grab_oid(name, "objectname", &oi->oid, v, &used_atom[i]);
	}
}

/* See grab_values */
static void grab_tag_values(struct atom_value *val, int deref, struct object *obj)
{
	int i;
	struct tag *tag = (struct tag *) obj;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		enum atom_type atom_type = used_atom[i].atom_type;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (atom_type == ATOM_TAG)
			v->s = xstrdup(tag->tag);
		else if (atom_type == ATOM_TYPE && tag->tagged)
			v->s = xstrdup(type_name(tag->tagged->type));
		else if (atom_type == ATOM_OBJECT && tag->tagged)
			v->s = xstrdup(oid_to_hex(&tag->tagged->oid));
	}
}

/* See grab_values */
static void grab_commit_values(struct atom_value *val, int deref, struct object *obj)
{
	int i;
	struct commit *commit = (struct commit *) obj;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		enum atom_type atom_type = used_atom[i].atom_type;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (atom_type == ATOM_TREE &&
		    grab_oid(name, "tree", get_commit_tree_oid(commit), v, &used_atom[i]))
			continue;
		if (atom_type == ATOM_NUMPARENT) {
			v->value = commit_list_count(commit->parents);
			v->s = xstrfmt("%lu", (unsigned long)v->value);
		}
		else if (atom_type == ATOM_PARENT) {
			struct commit_list *parents;
			struct strbuf s = STRBUF_INIT;
			for (parents = commit->parents; parents; parents = parents->next) {
				struct object_id *oid = &parents->item->object.oid;
				if (parents != commit->parents)
					strbuf_addch(&s, ' ');
				strbuf_addstr(&s, do_grab_oid("parent", oid, &used_atom[i]));
			}
			v->s = strbuf_detach(&s, NULL);
		}
	}
}

static const char *find_wholine(const char *who, int wholen, const char *buf)
{
	const char *eol;
	while (*buf) {
		if (!strncmp(buf, who, wholen) &&
		    buf[wholen] == ' ')
			return buf + wholen + 1;
		eol = strchr(buf, '\n');
		if (!eol)
			return "";
		eol++;
		if (*eol == '\n')
			return ""; /* end of header */
		buf = eol;
	}
	return "";
}

static const char *copy_line(const char *buf)
{
	const char *eol = strchrnul(buf, '\n');
	return xmemdupz(buf, eol - buf);
}

static const char *copy_name(const char *buf)
{
	const char *cp;
	for (cp = buf; *cp && *cp != '\n'; cp++) {
		if (starts_with(cp, " <"))
			return xmemdupz(buf, cp - buf);
	}
	return xstrdup("");
}

static const char *find_end_of_email(const char *email, int opt)
{
	const char *eoemail;

	if (opt & EO_LOCALPART) {
		eoemail = strchr(email, '@');
		if (eoemail)
			return eoemail;
		return strchr(email, '>');
	}

	if (opt & EO_TRIM)
		return strchr(email, '>');

	/*
	 * The option here is either the raw email option or the raw
	 * mailmap option (that is EO_RAW or EO_MAILMAP). In such cases,
	 * we directly grab the whole email including the closing
	 * angle brackets.
	 *
	 * If EO_MAILMAP was set with any other option (that is either
	 * EO_TRIM or EO_LOCALPART), we already grab the end of email
	 * above.
	 */
	eoemail = strchr(email, '>');
	if (eoemail)
		eoemail++;
	return eoemail;
}

static const char *copy_email(const char *buf, struct used_atom *atom)
{
	const char *email = strchr(buf, '<');
	const char *eoemail;
	int opt = atom->u.email_option.option;

	if (!email)
		return xstrdup("");

	if (opt & (EO_LOCALPART | EO_TRIM))
		email++;

	eoemail = find_end_of_email(email, opt);
	if (!eoemail)
		return xstrdup("");
	return xmemdupz(email, eoemail - email);
}

static char *copy_subject(const char *buf, unsigned long len)
{
	struct strbuf sb = STRBUF_INIT;
	int i;

	for (i = 0; i < len; i++) {
		if (buf[i] == '\r' && i + 1 < len && buf[i + 1] == '\n')
			continue; /* ignore CR in CRLF */

		if (buf[i] == '\n')
			strbuf_addch(&sb, ' ');
		else
			strbuf_addch(&sb, buf[i]);
	}
	return strbuf_detach(&sb, NULL);
}

static void grab_date(const char *buf, struct atom_value *v, const char *atomname)
{
	const char *eoemail = strstr(buf, "> ");
	char *zone;
	timestamp_t timestamp;
	long tz;
	struct date_mode date_mode = DATE_MODE_INIT;
	const char *formatp;

	/*
	 * We got here because atomname ends in "date" or "date<something>";
	 * it's not possible that <something> is not ":<format>" because
	 * parse_ref_filter_atom() wouldn't have allowed it, so we can assume that no
	 * ":" means no format is specified, and use the default.
	 */
	formatp = strchr(atomname, ':');
	if (formatp) {
		formatp++;
		parse_date_format(formatp, &date_mode);
	}

	if (!eoemail)
		goto bad;
	timestamp = parse_timestamp(eoemail + 2, &zone, 10);
	if (timestamp == TIME_MAX)
		goto bad;
	tz = strtol(zone, NULL, 10);
	if ((tz == LONG_MIN || tz == LONG_MAX) && errno == ERANGE)
		goto bad;
	v->s = xstrdup(show_date(timestamp, tz, &date_mode));
	v->value = timestamp;
	date_mode_release(&date_mode);
	return;
 bad:
	v->s = xstrdup("");
	v->value = 0;
}

static struct string_list mailmap = STRING_LIST_INIT_NODUP;

/* See grab_values */
static void grab_person(const char *who, struct atom_value *val, int deref, void *buf)
{
	int i;
	int wholen = strlen(who);
	const char *wholine = NULL;
	const char *headers[] = { "author ", "committer ",
				  "tagger ", NULL };

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		const char *name = atom->name;
		struct atom_value *v = &val[i];
		struct strbuf mailmap_buf = STRBUF_INIT;

		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (strncmp(who, name, wholen))
			continue;
		if (name[wholen] != 0 &&
		    !starts_with(name + wholen, "name") &&
		    !starts_with(name + wholen, "email") &&
		    !starts_with(name + wholen, "date"))
			continue;

		if ((starts_with(name + wholen, "name") &&
		    (atom->u.name_option.option == N_MAILMAP)) ||
		    (starts_with(name + wholen, "email") &&
		    (atom->u.email_option.option & EO_MAILMAP))) {
			if (!mailmap.items)
				read_mailmap(&mailmap);
			strbuf_addstr(&mailmap_buf, buf);
			apply_mailmap_to_header(&mailmap_buf, headers, &mailmap);
			wholine = find_wholine(who, wholen, mailmap_buf.buf);
		} else {
			wholine = find_wholine(who, wholen, buf);
		}

		if (!wholine)
			return; /* no point looking for it */
		if (name[wholen] == 0)
			v->s = copy_line(wholine);
		else if (starts_with(name + wholen, "name"))
			v->s = copy_name(wholine);
		else if (starts_with(name + wholen, "email"))
			v->s = copy_email(wholine, &used_atom[i]);
		else if (starts_with(name + wholen, "date"))
			grab_date(wholine, v, name);

		strbuf_release(&mailmap_buf);
	}

	/*
	 * For a tag or a commit object, if "creator" or "creatordate" is
	 * requested, do something special.
	 */
	if (strcmp(who, "tagger") && strcmp(who, "committer"))
		return; /* "author" for commit object is not wanted */
	if (!wholine)
		wholine = find_wholine(who, wholen, buf);
	if (!wholine)
		return;
	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		enum atom_type atom_type = used_atom[i].atom_type;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;

		if (atom_type == ATOM_CREATORDATE)
			grab_date(wholine, v, name);
		else if (atom_type == ATOM_CREATOR)
			v->s = copy_line(wholine);
	}
}

static void grab_signature(struct atom_value *val, int deref, struct object *obj)
{
	int i;
	struct commit *commit = (struct commit *) obj;
	struct signature_check sigc = { 0 };
	int signature_checked = 0;

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		const char *name = atom->name;
		struct atom_value *v = &val[i];
		int opt;

		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;

		if (!skip_prefix(name, "signature", &name) ||
		    (*name && *name != ':'))
			continue;
		if (!*name)
			name = NULL;
		else
			name++;

		opt = parse_signature_option(name);
		if (opt < 0)
			continue;

		if (!signature_checked) {
			check_commit_signature(commit, &sigc);
			signature_checked = 1;
		}

		switch (opt) {
		case S_BARE:
			v->s = xstrdup(sigc.output ? sigc.output: "");
			break;
		case S_SIGNER:
			v->s = xstrdup(sigc.signer ? sigc.signer : "");
			break;
		case S_GRADE:
			switch (sigc.result) {
			case 'G':
				switch (sigc.trust_level) {
				case TRUST_UNDEFINED:
				case TRUST_NEVER:
					v->s = xstrfmt("%c", (char)'U');
					break;
				default:
					v->s = xstrfmt("%c", (char)'G');
					break;
				}
				break;
			case 'B':
			case 'E':
			case 'N':
			case 'X':
			case 'Y':
			case 'R':
				v->s = xstrfmt("%c", (char)sigc.result);
				break;
			}
			break;
		case S_KEY:
			v->s = xstrdup(sigc.key ? sigc.key : "");
			break;
		case S_FINGERPRINT:
			v->s = xstrdup(sigc.fingerprint ?
				       sigc.fingerprint : "");
			break;
		case S_PRI_KEY_FP:
			v->s = xstrdup(sigc.primary_key_fingerprint ?
				       sigc.primary_key_fingerprint : "");
			break;
		case S_TRUST_LEVEL:
			v->s = xstrdup(gpg_trust_level_to_str(sigc.trust_level));
			break;
		}
	}

	if (signature_checked)
		signature_check_clear(&sigc);
}

static void find_subpos(const char *buf,
			const char **sub, size_t *sublen,
			const char **body, size_t *bodylen,
			size_t *nonsiglen,
			const char **sig, size_t *siglen)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	const char *eol;
	const char *end = buf + strlen(buf);
	const char *sigstart;

	/* parse signature first; we might not even have a subject line */
	parse_signature(buf, end - buf, &payload, &signature);
	strbuf_release(&payload);

	/* skip past header until we hit empty line */
	while (*buf && *buf != '\n') {
		eol = strchrnul(buf, '\n');
		if (*eol)
			eol++;
		buf = eol;
	}
	/* skip any empty lines */
	while (*buf == '\n')
		buf++;
	*sig = strbuf_detach(&signature, siglen);
	sigstart = buf + parse_signed_buffer(buf, strlen(buf));

	/* subject is first non-empty line */
	*sub = buf;
	/* subject goes to first empty line before signature begins */
	if ((eol = strstr(*sub, "\n\n")) ||
	    (eol = strstr(*sub, "\r\n\r\n"))) {
		eol = eol < sigstart ? eol : sigstart;
	} else {
		/* treat whole message as subject */
		eol = sigstart;
	}
	buf = eol;
	*sublen = buf - *sub;
	/* drop trailing newline, if present */
	while (*sublen && ((*sub)[*sublen - 1] == '\n' ||
			   (*sub)[*sublen - 1] == '\r'))
		*sublen -= 1;

	/* skip any empty lines */
	while (*buf == '\n' || *buf == '\r')
		buf++;
	*body = buf;
	*bodylen = strlen(buf);
	*nonsiglen = sigstart - buf;
}

/*
 * If 'lines' is greater than 0, append that many lines from the given
 * 'buf' of length 'size' to the given strbuf.
 */
static void append_lines(struct strbuf *out, const char *buf, unsigned long size, int lines)
{
	int i;
	const char *sp, *eol;
	size_t len;

	sp = buf;

	for (i = 0; i < lines && sp < buf + size; i++) {
		if (i)
			strbuf_addstr(out, "\n    ");
		eol = memchr(sp, '\n', size - (sp - buf));
		len = eol ? eol - sp : size - (sp - buf);
		strbuf_add(out, sp, len);
		if (!eol)
			break;
		sp = eol + 1;
	}
}

static void grab_describe_values(struct atom_value *val, int deref,
				 struct object *obj)
{
	struct commit *commit = (struct commit *)obj;
	int i;

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		enum atom_type type = atom->atom_type;
		const char *name = atom->name;
		struct atom_value *v = &val[i];

		struct child_process cmd = CHILD_PROCESS_INIT;
		struct strbuf out = STRBUF_INIT;
		struct strbuf err = STRBUF_INIT;

		if (type != ATOM_DESCRIBE)
			continue;

		if (!!deref != (*name == '*'))
			continue;

		cmd.git_cmd = 1;
		strvec_push(&cmd.args, "describe");
		strvec_pushv(&cmd.args, atom->u.describe_args);
		strvec_push(&cmd.args, oid_to_hex(&commit->object.oid));
		if (pipe_command(&cmd, NULL, 0, &out, 0, &err, 0) < 0) {
			error(_("failed to run 'describe'"));
			v->s = xstrdup("");
			continue;
		}
		strbuf_rtrim(&out);
		v->s = strbuf_detach(&out, NULL);

		strbuf_release(&err);
	}
}

/* See grab_values */
static void grab_sub_body_contents(struct atom_value *val, int deref, struct expand_data *data)
{
	int i;
	const char *subpos = NULL, *bodypos = NULL, *sigpos = NULL;
	size_t sublen = 0, bodylen = 0, nonsiglen = 0, siglen = 0;
	void *buf = data->content;

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		const char *name = atom->name;
		struct atom_value *v = &val[i];
		enum atom_type atom_type = atom->atom_type;

		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;

		if (atom_type == ATOM_RAW) {
			unsigned long buf_size = data->size;

			if (atom->u.raw_data.option == RAW_BARE) {
				v->s = xmemdupz(buf, buf_size);
				v->s_size = buf_size;
			} else if (atom->u.raw_data.option == RAW_LENGTH) {
				v->value = buf_size;
				v->s = xstrfmt("%"PRIuMAX, v->value);
			}
			continue;
		}

		if ((data->type != OBJ_TAG &&
		     data->type != OBJ_COMMIT) ||
		    (strcmp(name, "body") &&
		     !starts_with(name, "subject") &&
		     !starts_with(name, "trailers") &&
		     !starts_with(name, "contents")))
			continue;
		if (!subpos)
			find_subpos(buf,
				    &subpos, &sublen,
				    &bodypos, &bodylen, &nonsiglen,
				    &sigpos, &siglen);

		if (atom->u.contents.option == C_SUB)
			v->s = copy_subject(subpos, sublen);
		else if (atom->u.contents.option == C_SUB_SANITIZE) {
			struct strbuf sb = STRBUF_INIT;
			format_sanitized_subject(&sb, subpos, sublen);
			v->s = strbuf_detach(&sb, NULL);
		} else if (atom->u.contents.option == C_BODY_DEP)
			v->s = xmemdupz(bodypos, bodylen);
		else if (atom->u.contents.option == C_LENGTH) {
			v->value = strlen(subpos);
			v->s = xstrfmt("%"PRIuMAX, v->value);
		} else if (atom->u.contents.option == C_BODY)
			v->s = xmemdupz(bodypos, nonsiglen);
		else if (atom->u.contents.option == C_SIG)
			v->s = xmemdupz(sigpos, siglen);
		else if (atom->u.contents.option == C_LINES) {
			struct strbuf s = STRBUF_INIT;
			const char *contents_end = bodypos + nonsiglen;

			/*  Size is the length of the message after removing the signature */
			append_lines(&s, subpos, contents_end - subpos, atom->u.contents.nlines);
			v->s = strbuf_detach(&s, NULL);
		} else if (atom->u.contents.option == C_TRAILERS) {
			struct strbuf s = STRBUF_INIT;

			/* Format the trailer info according to the trailer_opts given */
			format_trailers_from_commit(&s, subpos, &atom->u.contents.trailer_opts);

			v->s = strbuf_detach(&s, NULL);
		} else if (atom->u.contents.option == C_BARE)
			v->s = xstrdup(subpos);

	}
	free((void *)sigpos);
}

/*
 * We want to have empty print-string for field requests
 * that do not apply (e.g. "authordate" for a tag object)
 */
static void fill_missing_values(struct atom_value *val)
{
	int i;
	for (i = 0; i < used_atom_cnt; i++) {
		struct atom_value *v = &val[i];
		if (!v->s)
			v->s = xstrdup("");
	}
}

/*
 * val is a list of atom_value to hold returned values.  Extract
 * the values for atoms in used_atom array out of (obj, buf, sz).
 * when deref is false, (obj, buf, sz) is the object that is
 * pointed at by the ref itself; otherwise it is the object the
 * ref (which is a tag) refers to.
 */
static void grab_values(struct atom_value *val, int deref, struct object *obj, struct expand_data *data)
{
	void *buf = data->content;

	switch (obj->type) {
	case OBJ_TAG:
		grab_tag_values(val, deref, obj);
		grab_sub_body_contents(val, deref, data);
		grab_person("tagger", val, deref, buf);
		grab_describe_values(val, deref, obj);
		break;
	case OBJ_COMMIT:
		grab_commit_values(val, deref, obj);
		grab_sub_body_contents(val, deref, data);
		grab_person("author", val, deref, buf);
		grab_person("committer", val, deref, buf);
		grab_signature(val, deref, obj);
		grab_describe_values(val, deref, obj);
		break;
	case OBJ_TREE:
		/* grab_tree_values(val, deref, obj, buf, sz); */
		grab_sub_body_contents(val, deref, data);
		break;
	case OBJ_BLOB:
		/* grab_blob_values(val, deref, obj, buf, sz); */
		grab_sub_body_contents(val, deref, data);
		break;
	default:
		die("Eh?  Object of type %d?", obj->type);
	}
}

static inline char *copy_advance(char *dst, const char *src)
{
	while (*src)
		*dst++ = *src++;
	return dst;
}

static const char *lstrip_ref_components(const char *refname, int len)
{
	long remaining = len;
	const char *start = xstrdup(refname);
	const char *to_free = start;

	if (len < 0) {
		int i;
		const char *p = refname;

		/* Find total no of '/' separated path-components */
		for (i = 0; p[i]; p[i] == '/' ? i++ : *p++)
			;
		/*
		 * The number of components we need to strip is now
		 * the total minus the components to be left (Plus one
		 * because we count the number of '/', but the number
		 * of components is one more than the no of '/').
		 */
		remaining = i + len + 1;
	}

	while (remaining > 0) {
		switch (*start++) {
		case '\0':
			free((char *)to_free);
			return xstrdup("");
		case '/':
			remaining--;
			break;
		}
	}

	start = xstrdup(start);
	free((char *)to_free);
	return start;
}

static const char *rstrip_ref_components(const char *refname, int len)
{
	long remaining = len;
	const char *start = xstrdup(refname);
	const char *to_free = start;

	if (len < 0) {
		int i;
		const char *p = refname;

		/* Find total no of '/' separated path-components */
		for (i = 0; p[i]; p[i] == '/' ? i++ : *p++)
			;
		/*
		 * The number of components we need to strip is now
		 * the total minus the components to be left (Plus one
		 * because we count the number of '/', but the number
		 * of components is one more than the no of '/').
		 */
		remaining = i + len + 1;
	}

	while (remaining-- > 0) {
		char *p = strrchr(start, '/');
		if (!p) {
			free((char *)to_free);
			return xstrdup("");
		} else
			p[0] = '\0';
	}
	return start;
}

static const char *show_ref(struct refname_atom *atom, const char *refname)
{
	if (atom->option == R_SHORT)
		return shorten_unambiguous_ref(refname, warn_ambiguous_refs);
	else if (atom->option == R_LSTRIP)
		return lstrip_ref_components(refname, atom->lstrip);
	else if (atom->option == R_RSTRIP)
		return rstrip_ref_components(refname, atom->rstrip);
	else
		return xstrdup(refname);
}

static void fill_remote_ref_details(struct used_atom *atom, const char *refname,
				    struct branch *branch, const char **s)
{
	int num_ours, num_theirs;
	if (atom->u.remote_ref.option == RR_REF)
		*s = show_ref(&atom->u.remote_ref.refname, refname);
	else if (atom->u.remote_ref.option == RR_TRACK) {
		if (stat_tracking_info(branch, &num_ours, &num_theirs,
				       NULL, atom->u.remote_ref.push,
				       AHEAD_BEHIND_FULL) < 0) {
			*s = xstrdup(msgs.gone);
		} else if (!num_ours && !num_theirs)
			*s = xstrdup("");
		else if (!num_ours)
			*s = xstrfmt(msgs.behind, num_theirs);
		else if (!num_theirs)
			*s = xstrfmt(msgs.ahead, num_ours);
		else
			*s = xstrfmt(msgs.ahead_behind,
				     num_ours, num_theirs);
		if (!atom->u.remote_ref.nobracket && *s[0]) {
			const char *to_free = *s;
			*s = xstrfmt("[%s]", *s);
			free((void *)to_free);
		}
	} else if (atom->u.remote_ref.option == RR_TRACKSHORT) {
		if (stat_tracking_info(branch, &num_ours, &num_theirs,
				       NULL, atom->u.remote_ref.push,
				       AHEAD_BEHIND_FULL) < 0) {
			*s = xstrdup("");
			return;
		}
		if (!num_ours && !num_theirs)
			*s = xstrdup("=");
		else if (!num_ours)
			*s = xstrdup("<");
		else if (!num_theirs)
			*s = xstrdup(">");
		else
			*s = xstrdup("<>");
	} else if (atom->u.remote_ref.option == RR_REMOTE_NAME) {
		int explicit;
		const char *remote = atom->u.remote_ref.push ?
			pushremote_for_branch(branch, &explicit) :
			remote_for_branch(branch, &explicit);
		*s = xstrdup(explicit ? remote : "");
	} else if (atom->u.remote_ref.option == RR_REMOTE_REF) {
		const char *merge;

		merge = remote_ref_for_branch(branch, atom->u.remote_ref.push);
		*s = xstrdup(merge ? merge : "");
	} else
		BUG("unhandled RR_* enum");
}

char *get_head_description(void)
{
	struct strbuf desc = STRBUF_INIT;
	struct wt_status_state state;
	memset(&state, 0, sizeof(state));
	wt_status_get_state(the_repository, &state, 1);
	if (state.rebase_in_progress ||
	    state.rebase_interactive_in_progress) {
		if (state.branch)
			strbuf_addf(&desc, _("(no branch, rebasing %s)"),
				    state.branch);
		else
			strbuf_addf(&desc, _("(no branch, rebasing detached HEAD %s)"),
				    state.detached_from);
	} else if (state.bisect_in_progress)
		strbuf_addf(&desc, _("(no branch, bisect started on %s)"),
			    state.bisecting_from);
	else if (state.detached_from) {
		if (state.detached_at)
			strbuf_addf(&desc, _("(HEAD detached at %s)"),
				state.detached_from);
		else
			strbuf_addf(&desc, _("(HEAD detached from %s)"),
				state.detached_from);
	} else
		strbuf_addstr(&desc, _("(no branch)"));

	wt_status_state_free_buffers(&state);

	return strbuf_detach(&desc, NULL);
}

static const char *get_symref(struct used_atom *atom, struct ref_array_item *ref)
{
	if (!ref->symref)
		return xstrdup("");
	else
		return show_ref(&atom->u.refname, ref->symref);
}

static const char *get_refname(struct used_atom *atom, struct ref_array_item *ref)
{
	if (ref->kind & FILTER_REFS_DETACHED_HEAD)
		return get_head_description();
	return show_ref(&atom->u.refname, ref->refname);
}

static int get_object(struct ref_array_item *ref, int deref, struct object **obj,
		      struct expand_data *oi, struct strbuf *err)
{
	/* parse_object_buffer() will set eaten to 0 if free() will be needed */
	int eaten = 1;
	if (oi->info.contentp) {
		/* We need to know that to use parse_object_buffer properly */
		oi->info.sizep = &oi->size;
		oi->info.typep = &oi->type;
	}
	if (oid_object_info_extended(the_repository, &oi->oid, &oi->info,
				     OBJECT_INFO_LOOKUP_REPLACE))
		return strbuf_addf_ret(err, -1, _("missing object %s for %s"),
				       oid_to_hex(&oi->oid), ref->refname);
	if (oi->info.disk_sizep && oi->disk_size < 0)
		BUG("Object size is less than zero.");

	if (oi->info.contentp) {
		*obj = parse_object_buffer(the_repository, &oi->oid, oi->type, oi->size, oi->content, &eaten);
		if (!*obj) {
			if (!eaten)
				free(oi->content);
			return strbuf_addf_ret(err, -1, _("parse_object_buffer failed on %s for %s"),
					       oid_to_hex(&oi->oid), ref->refname);
		}
		grab_values(ref->value, deref, *obj, oi);
	}

	grab_common_values(ref->value, deref, oi);
	if (!eaten)
		free(oi->content);
	return 0;
}

static void populate_worktree_map(struct hashmap *map, struct worktree **worktrees)
{
	int i;

	for (i = 0; worktrees[i]; i++) {
		if (worktrees[i]->head_ref) {
			struct ref_to_worktree_entry *entry;
			entry = xmalloc(sizeof(*entry));
			entry->wt = worktrees[i];
			hashmap_entry_init(&entry->ent,
					strhash(worktrees[i]->head_ref));

			hashmap_add(map, &entry->ent);
		}
	}
}

static void lazy_init_worktree_map(void)
{
	if (ref_to_worktree_map.worktrees)
		return;

	ref_to_worktree_map.worktrees = get_worktrees();
	hashmap_init(&(ref_to_worktree_map.map), ref_to_worktree_map_cmpfnc, NULL, 0);
	populate_worktree_map(&(ref_to_worktree_map.map), ref_to_worktree_map.worktrees);
}

static char *get_worktree_path(const struct ref_array_item *ref)
{
	struct hashmap_entry entry, *e;
	struct ref_to_worktree_entry *lookup_result;

	lazy_init_worktree_map();

	hashmap_entry_init(&entry, strhash(ref->refname));
	e = hashmap_get(&(ref_to_worktree_map.map), &entry, ref->refname);

	if (!e)
		return xstrdup("");

	lookup_result = container_of(e, struct ref_to_worktree_entry, ent);

	return xstrdup(lookup_result->wt->path);
}

/*
 * Parse the object referred by ref, and grab needed value.
 */
static int populate_value(struct ref_array_item *ref, struct strbuf *err)
{
	struct object *obj;
	int i;
	struct object_info empty = OBJECT_INFO_INIT;
	int ahead_behind_atoms = 0;

	CALLOC_ARRAY(ref->value, used_atom_cnt);

	if (need_symref && (ref->flag & REF_ISSYMREF) && !ref->symref) {
		ref->symref = resolve_refdup(ref->refname, RESOLVE_REF_READING,
					     NULL, NULL);
		if (!ref->symref)
			ref->symref = xstrdup("");
	}

	/* Fill in specials first */
	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		enum atom_type atom_type = atom->atom_type;
		const char *name = used_atom[i].name;
		struct atom_value *v = &ref->value[i];
		int deref = 0;
		const char *refname;
		struct branch *branch = NULL;

		v->s_size = ATOM_SIZE_UNSPECIFIED;
		v->handler = append_atom;
		v->value = 0;
		v->atom = atom;

		if (*name == '*') {
			deref = 1;
			name++;
		}

		if (atom_type == ATOM_REFNAME)
			refname = get_refname(atom, ref);
		else if (atom_type == ATOM_WORKTREEPATH) {
			if (ref->kind == FILTER_REFS_BRANCHES)
				v->s = get_worktree_path(ref);
			else
				v->s = xstrdup("");
			continue;
		}
		else if (atom_type == ATOM_SYMREF)
			refname = get_symref(atom, ref);
		else if (atom_type == ATOM_UPSTREAM) {
			const char *branch_name;
			/* only local branches may have an upstream */
			if (!skip_prefix(ref->refname, "refs/heads/",
					 &branch_name)) {
				v->s = xstrdup("");
				continue;
			}
			branch = branch_get(branch_name);

			refname = branch_get_upstream(branch, NULL);
			if (refname)
				fill_remote_ref_details(atom, refname, branch, &v->s);
			else
				v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_PUSH && atom->u.remote_ref.push) {
			const char *branch_name;
			v->s = xstrdup("");
			if (!skip_prefix(ref->refname, "refs/heads/",
					 &branch_name))
				continue;
			branch = branch_get(branch_name);

			if (atom->u.remote_ref.push_remote)
				refname = NULL;
			else {
				refname = branch_get_push(branch, NULL);
				if (!refname)
					continue;
			}
			/* We will definitely re-init v->s on the next line. */
			free((char *)v->s);
			fill_remote_ref_details(atom, refname, branch, &v->s);
			continue;
		} else if (atom_type == ATOM_COLOR) {
			v->s = xstrdup(atom->u.color);
			continue;
		} else if (atom_type == ATOM_FLAG) {
			char buf[256], *cp = buf;
			if (ref->flag & REF_ISSYMREF)
				cp = copy_advance(cp, ",symref");
			if (ref->flag & REF_ISPACKED)
				cp = copy_advance(cp, ",packed");
			if (cp == buf)
				v->s = xstrdup("");
			else {
				*cp = '\0';
				v->s = xstrdup(buf + 1);
			}
			continue;
		} else if (!deref && atom_type == ATOM_OBJECTNAME &&
			   grab_oid(name, "objectname", &ref->objectname, v, atom)) {
				continue;
		} else if (atom_type == ATOM_HEAD) {
			if (atom->u.head && !strcmp(ref->refname, atom->u.head))
				v->s = xstrdup("*");
			else
				v->s = xstrdup(" ");
			continue;
		} else if (atom_type == ATOM_ALIGN) {
			v->handler = align_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_END) {
			v->handler = end_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_IF) {
			const char *s;
			if (skip_prefix(name, "if:", &s))
				v->s = xstrdup(s);
			else
				v->s = xstrdup("");
			v->handler = if_atom_handler;
			continue;
		} else if (atom_type == ATOM_THEN) {
			v->handler = then_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_ELSE) {
			v->handler = else_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_REST) {
			if (ref->rest)
				v->s = xstrdup(ref->rest);
			else
				v->s = xstrdup("");
			continue;
		} else if (atom_type == ATOM_AHEADBEHIND) {
			if (ref->counts) {
				const struct ahead_behind_count *count;
				count = ref->counts[ahead_behind_atoms++];
				v->s = xstrfmt("%d %d", count->ahead, count->behind);
			} else {
				/* Not a commit. */
				v->s = xstrdup("");
			}
			continue;
		} else
			continue;

		if (!deref)
			v->s = xstrdup(refname);
		else
			v->s = xstrfmt("%s^{}", refname);
		free((char *)refname);
	}

	for (i = 0; i < used_atom_cnt; i++) {
		struct atom_value *v = &ref->value[i];
		if (v->s == NULL && used_atom[i].source == SOURCE_NONE)
			return strbuf_addf_ret(err, -1, _("missing object %s for %s"),
					       oid_to_hex(&ref->objectname), ref->refname);
	}

	if (need_tagged)
		oi.info.contentp = &oi.content;
	if (!memcmp(&oi.info, &empty, sizeof(empty)) &&
	    !memcmp(&oi_deref.info, &empty, sizeof(empty)))
		return 0;


	oi.oid = ref->objectname;
	if (get_object(ref, 0, &obj, &oi, err))
		return -1;

	/*
	 * If there is no atom that wants to know about tagged
	 * object, we are done.
	 */
	if (!need_tagged || (obj->type != OBJ_TAG))
		return 0;

	/*
	 * If it is a tag object, see if we use a value that derefs
	 * the object, and if we do grab the object it refers to.
	 */
	oi_deref.oid = *get_tagged_oid((struct tag *)obj);

	/*
	 * NEEDSWORK: This derefs tag only once, which
	 * is good to deal with chains of trust, but
	 * is not consistent with what deref_tag() does
	 * which peels the onion to the core.
	 */
	return get_object(ref, 1, &obj, &oi_deref, err);
}

/*
 * Given a ref, return the value for the atom.  This lazily gets value
 * out of the object by calling populate value.
 */
static int get_ref_atom_value(struct ref_array_item *ref, int atom,
			      struct atom_value **v, struct strbuf *err)
{
	if (!ref->value) {
		if (populate_value(ref, err))
			return -1;
		fill_missing_values(ref->value);
	}
	*v = &ref->value[atom];
	return 0;
}

/*
 * Return 1 if the refname matches one of the patterns, otherwise 0.
 * A pattern can be a literal prefix (e.g. a refname "refs/heads/master"
 * matches a pattern "refs/heads/mas") or a wildcard (e.g. the same ref
 * matches "refs/heads/mas*", too).
 */
static int match_pattern(const char **patterns, const char *refname,
			 int ignore_case)
{
	unsigned flags = 0;

	if (ignore_case)
		flags |= WM_CASEFOLD;

	/*
	 * When no '--format' option is given we need to skip the prefix
	 * for matching refs of tags and branches.
	 */
	(void)(skip_prefix(refname, "refs/tags/", &refname) ||
	       skip_prefix(refname, "refs/heads/", &refname) ||
	       skip_prefix(refname, "refs/remotes/", &refname) ||
	       skip_prefix(refname, "refs/", &refname));

	for (; *patterns; patterns++) {
		if (!wildmatch(*patterns, refname, flags))
			return 1;
	}
	return 0;
}

/*
 * Return 1 if the refname matches one of the patterns, otherwise 0.
 * A pattern can be path prefix (e.g. a refname "refs/heads/master"
 * matches a pattern "refs/heads/" but not "refs/heads/m") or a
 * wildcard (e.g. the same ref matches "refs/heads/m*", too).
 */
static int match_name_as_path(const char **pattern, const char *refname,
			      int ignore_case)
{
	int namelen = strlen(refname);
	unsigned flags = WM_PATHNAME;

	if (ignore_case)
		flags |= WM_CASEFOLD;

	for (; *pattern; pattern++) {
		const char *p = *pattern;
		int plen = strlen(p);

		if ((plen <= namelen) &&
		    !strncmp(refname, p, plen) &&
		    (refname[plen] == '\0' ||
		     refname[plen] == '/' ||
		     p[plen-1] == '/'))
			return 1;
		if (!wildmatch(p, refname, flags))
			return 1;
	}
	return 0;
}

/* Return 1 if the refname matches one of the patterns, otherwise 0. */
static int filter_pattern_match(struct ref_filter *filter, const char *refname)
{
	if (!*filter->name_patterns)
		return 1; /* No pattern always matches */
	if (filter->match_as_path)
		return match_name_as_path(filter->name_patterns, refname,
					  filter->ignore_case);
	return match_pattern(filter->name_patterns, refname,
			     filter->ignore_case);
}

static int filter_exclude_match(struct ref_filter *filter, const char *refname)
{
	if (!filter->exclude.nr)
		return 0;
	if (filter->match_as_path)
		return match_name_as_path(filter->exclude.v, refname,
					  filter->ignore_case);
	return match_pattern(filter->exclude.v, refname, filter->ignore_case);
}

/*
 * This is the same as for_each_fullref_in(), but it tries to iterate
 * only over the patterns we'll care about. Note that it _doesn't_ do a full
 * pattern match, so the callback still has to match each ref individually.
 */
static int for_each_fullref_in_pattern(struct ref_filter *filter,
				       each_ref_fn cb,
				       void *cb_data)
{
	if (!filter->match_as_path) {
		/*
		 * in this case, the patterns are applied after
		 * prefixes like "refs/heads/" etc. are stripped off,
		 * so we have to look at everything:
		 */
		return for_each_fullref_in("", cb, cb_data);
	}

	if (filter->ignore_case) {
		/*
		 * we can't handle case-insensitive comparisons,
		 * so just return everything and let the caller
		 * sort it out.
		 */
		return for_each_fullref_in("", cb, cb_data);
	}

	if (!filter->name_patterns[0]) {
		/* no patterns; we have to look at everything */
		return refs_for_each_fullref_in(get_main_ref_store(the_repository),
						 "", filter->exclude.v, cb, cb_data);
	}

	return refs_for_each_fullref_in_prefixes(get_main_ref_store(the_repository),
						 NULL, filter->name_patterns,
						 filter->exclude.v,
						 cb, cb_data);
}

/*
 * Given a ref (oid, refname), check if the ref belongs to the array
 * of oids. If the given ref is a tag, check if the given tag points
 * at one of the oids in the given oid array. Returns non-zero if a
 * match is found.
 *
 * NEEDSWORK:
 * As the refs are cached we might know what refname peels to without
 * the need to parse the object via parse_object(). peel_ref() might be a
 * more efficient alternative to obtain the pointee.
 */
static int match_points_at(struct oid_array *points_at,
			   const struct object_id *oid,
			   const char *refname)
{
	struct object *obj;

	if (oid_array_lookup(points_at, oid) >= 0)
		return 1;
	obj = parse_object_with_flags(the_repository, oid,
				      PARSE_OBJECT_SKIP_HASH_CHECK);
	while (obj && obj->type == OBJ_TAG) {
		struct tag *tag = (struct tag *)obj;

		if (parse_tag(tag) < 0) {
			obj = NULL;
			break;
		}

		if (oid_array_lookup(points_at, get_tagged_oid(tag)) >= 0)
			return 1;

		obj = tag->tagged;
	}
	if (!obj)
		die(_("malformed object at '%s'"), refname);
	return 0;
}

/*
 * Allocate space for a new ref_array_item and copy the name and oid to it.
 *
 * Callers can then fill in other struct members at their leisure.
 */
static struct ref_array_item *new_ref_array_item(const char *refname,
						 const struct object_id *oid)
{
	struct ref_array_item *ref;

	FLEX_ALLOC_STR(ref, refname, refname);
	oidcpy(&ref->objectname, oid);
	ref->rest = NULL;

	return ref;
}

struct ref_array_item *ref_array_push(struct ref_array *array,
				      const char *refname,
				      const struct object_id *oid)
{
	struct ref_array_item *ref = new_ref_array_item(refname, oid);

	ALLOC_GROW(array->items, array->nr + 1, array->alloc);
	array->items[array->nr++] = ref;

	return ref;
}

static int ref_kind_from_refname(const char *refname)
{
	unsigned int i;

	static struct {
		const char *prefix;
		unsigned int kind;
	} ref_kind[] = {
		{ "refs/heads/" , FILTER_REFS_BRANCHES },
		{ "refs/remotes/" , FILTER_REFS_REMOTES },
		{ "refs/tags/", FILTER_REFS_TAGS}
	};

	if (!strcmp(refname, "HEAD"))
		return FILTER_REFS_DETACHED_HEAD;

	for (i = 0; i < ARRAY_SIZE(ref_kind); i++) {
		if (starts_with(refname, ref_kind[i].prefix))
			return ref_kind[i].kind;
	}

	return FILTER_REFS_OTHERS;
}

static int filter_ref_kind(struct ref_filter *filter, const char *refname)
{
	if (filter->kind == FILTER_REFS_BRANCHES ||
	    filter->kind == FILTER_REFS_REMOTES ||
	    filter->kind == FILTER_REFS_TAGS)
		return filter->kind;
	return ref_kind_from_refname(refname);
}

struct ref_filter_cbdata {
	struct ref_array *array;
	struct ref_filter *filter;
	struct contains_cache contains_cache;
	struct contains_cache no_contains_cache;
};

/*
 * A call-back given to for_each_ref().  Filter refs and keep them for
 * later object processing.
 */
static int ref_filter_handler(const char *refname, const struct object_id *oid, int flag, void *cb_data)
{
	struct ref_filter_cbdata *ref_cbdata = cb_data;
	struct ref_filter *filter = ref_cbdata->filter;
	struct ref_array_item *ref;
	struct commit *commit = NULL;
	unsigned int kind;

	if (flag & REF_BAD_NAME) {
		warning(_("ignoring ref with broken name %s"), refname);
		return 0;
	}

	if (flag & REF_ISBROKEN) {
		warning(_("ignoring broken ref %s"), refname);
		return 0;
	}

	/* Obtain the current ref kind from filter_ref_kind() and ignore unwanted refs. */
	kind = filter_ref_kind(filter, refname);
	if (!(kind & filter->kind))
		return 0;

	if (!filter_pattern_match(filter, refname))
		return 0;

	if (filter_exclude_match(filter, refname))
		return 0;

	if (filter->points_at.nr && !match_points_at(&filter->points_at, oid, refname))
		return 0;

	/*
	 * A merge filter is applied on refs pointing to commits. Hence
	 * obtain the commit using the 'oid' available and discard all
	 * non-commits early. The actual filtering is done later.
	 */
	if (filter->reachable_from || filter->unreachable_from ||
	    filter->with_commit || filter->no_commit || filter->verbose) {
		commit = lookup_commit_reference_gently(the_repository, oid, 1);
		if (!commit)
			return 0;
		/* We perform the filtering for the '--contains' option... */
		if (filter->with_commit &&
		    !commit_contains(filter, commit, filter->with_commit, &ref_cbdata->contains_cache))
			return 0;
		/* ...or for the `--no-contains' option */
		if (filter->no_commit &&
		    commit_contains(filter, commit, filter->no_commit, &ref_cbdata->no_contains_cache))
			return 0;
	}

	/*
	 * We do not open the object yet; sort may only need refname
	 * to do its job and the resulting list may yet to be pruned
	 * by maxcount logic.
	 */
	ref = ref_array_push(ref_cbdata->array, refname, oid);
	ref->commit = commit;
	ref->flag = flag;
	ref->kind = kind;

	return 0;
}

/*  Free memory allocated for a ref_array_item */
static void free_array_item(struct ref_array_item *item)
{
	free((char *)item->symref);
	if (item->value) {
		int i;
		for (i = 0; i < used_atom_cnt; i++)
			free((char *)item->value[i].s);
		free(item->value);
	}
	free(item->counts);
	free(item);
}

/* Free all memory allocated for ref_array */
void ref_array_clear(struct ref_array *array)
{
	int i;

	for (i = 0; i < array->nr; i++)
		free_array_item(array->items[i]);
	FREE_AND_NULL(array->items);
	array->nr = array->alloc = 0;

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		if (atom->atom_type == ATOM_HEAD)
			free(atom->u.head);
		free((char *)atom->name);
	}
	FREE_AND_NULL(used_atom);
	used_atom_cnt = 0;

	if (ref_to_worktree_map.worktrees) {
		hashmap_clear_and_free(&(ref_to_worktree_map.map),
					struct ref_to_worktree_entry, ent);
		free_worktrees(ref_to_worktree_map.worktrees);
		ref_to_worktree_map.worktrees = NULL;
	}

	FREE_AND_NULL(array->counts);
}

#define EXCLUDE_REACHED 0
#define INCLUDE_REACHED 1
static void reach_filter(struct ref_array *array,
			 struct commit_list **check_reachable,
			 int include_reached)
{
	int i, old_nr;
	struct commit **to_clear;

	if (!*check_reachable)
		return;

	CALLOC_ARRAY(to_clear, array->nr);
	for (i = 0; i < array->nr; i++) {
		struct ref_array_item *item = array->items[i];
		to_clear[i] = item->commit;
	}

	tips_reachable_from_bases(the_repository,
				  *check_reachable,
				  to_clear, array->nr,
				  UNINTERESTING);

	old_nr = array->nr;
	array->nr = 0;

	for (i = 0; i < old_nr; i++) {
		struct ref_array_item *item = array->items[i];
		struct commit *commit = item->commit;

		int is_merged = !!(commit->object.flags & UNINTERESTING);

		if (is_merged == include_reached)
			array->items[array->nr++] = array->items[i];
		else
			free_array_item(item);
	}

	clear_commit_marks_many(old_nr, to_clear, ALL_REV_FLAGS);

	while (*check_reachable) {
		struct commit *merge_commit = pop_commit(check_reachable);
		clear_commit_marks(merge_commit, ALL_REV_FLAGS);
	}

	free(to_clear);
}

void filter_ahead_behind(struct repository *r,
			 struct ref_format *format,
			 struct ref_array *array)
{
	struct commit **commits;
	size_t commits_nr = format->bases.nr + array->nr;

	if (!format->bases.nr || !array->nr)
		return;

	ALLOC_ARRAY(commits, commits_nr);
	for (size_t i = 0; i < format->bases.nr; i++)
		commits[i] = format->bases.items[i].util;

	ALLOC_ARRAY(array->counts, st_mult(format->bases.nr, array->nr));

	commits_nr = format->bases.nr;
	array->counts_nr = 0;
	for (size_t i = 0; i < array->nr; i++) {
		const char *name = array->items[i]->refname;
		commits[commits_nr] = lookup_commit_reference_by_name(name);

		if (!commits[commits_nr])
			continue;

		CALLOC_ARRAY(array->items[i]->counts, format->bases.nr);
		for (size_t j = 0; j < format->bases.nr; j++) {
			struct ahead_behind_count *count;
			count = &array->counts[array->counts_nr++];
			count->tip_index = commits_nr;
			count->base_index = j;

			array->items[i]->counts[j] = count;
		}
		commits_nr++;
	}

	ahead_behind(r, commits, commits_nr, array->counts, array->counts_nr);
	free(commits);
}

/*
 * API for filtering a set of refs. Based on the type of refs the user
 * has requested, we iterate through those refs and apply filters
 * as per the given ref_filter structure and finally store the
 * filtered refs in the ref_array structure.
 */
int filter_refs(struct ref_array *array, struct ref_filter *filter, unsigned int type)
{
	struct ref_filter_cbdata ref_cbdata;
	int save_commit_buffer_orig;
	int ret = 0;

	ref_cbdata.array = array;
	ref_cbdata.filter = filter;

	filter->kind = type & FILTER_REFS_KIND_MASK;

	save_commit_buffer_orig = save_commit_buffer;
	save_commit_buffer = 0;

	init_contains_cache(&ref_cbdata.contains_cache);
	init_contains_cache(&ref_cbdata.no_contains_cache);

	/*  Simple per-ref filtering */
	if (!filter->kind)
		die("filter_refs: invalid type");
	else {
		/*
		 * For common cases where we need only branches or remotes or tags,
		 * we only iterate through those refs. If a mix of refs is needed,
		 * we iterate over all refs and filter out required refs with the help
		 * of filter_ref_kind().
		 */
		if (filter->kind == FILTER_REFS_BRANCHES)
			ret = for_each_fullref_in("refs/heads/", ref_filter_handler, &ref_cbdata);
		else if (filter->kind == FILTER_REFS_REMOTES)
			ret = for_each_fullref_in("refs/remotes/", ref_filter_handler, &ref_cbdata);
		else if (filter->kind == FILTER_REFS_TAGS)
			ret = for_each_fullref_in("refs/tags/", ref_filter_handler, &ref_cbdata);
		else if (filter->kind & FILTER_REFS_ALL)
			ret = for_each_fullref_in_pattern(filter, ref_filter_handler, &ref_cbdata);
		if (!ret && (filter->kind & FILTER_REFS_DETACHED_HEAD))
			head_ref(ref_filter_handler, &ref_cbdata);
	}

	clear_contains_cache(&ref_cbdata.contains_cache);
	clear_contains_cache(&ref_cbdata.no_contains_cache);

	/*  Filters that need revision walking */
	reach_filter(array, &filter->reachable_from, INCLUDE_REACHED);
	reach_filter(array, &filter->unreachable_from, EXCLUDE_REACHED);

	save_commit_buffer = save_commit_buffer_orig;
	return ret;
}

static int compare_detached_head(struct ref_array_item *a, struct ref_array_item *b)
{
	if (!(a->kind ^ b->kind))
		BUG("ref_kind_from_refname() should only mark one ref as HEAD");
	if (a->kind & FILTER_REFS_DETACHED_HEAD)
		return -1;
	else if (b->kind & FILTER_REFS_DETACHED_HEAD)
		return 1;
	BUG("should have died in the xor check above");
	return 0;
}

static int memcasecmp(const void *vs1, const void *vs2, size_t n)
{
	const char *s1 = vs1, *s2 = vs2;
	const char *end = s1 + n;

	for (; s1 < end; s1++, s2++) {
		int diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	}
	return 0;
}

struct ref_sorting {
	struct ref_sorting *next;
	int atom; /* index into used_atom array (internal) */
	enum ref_sorting_order sort_flags;
};

static int cmp_ref_sorting(struct ref_sorting *s, struct ref_array_item *a, struct ref_array_item *b)
{
	struct atom_value *va, *vb;
	int cmp;
	int cmp_detached_head = 0;
	cmp_type cmp_type = used_atom[s->atom].type;
	struct strbuf err = STRBUF_INIT;

	if (get_ref_atom_value(a, s->atom, &va, &err))
		die("%s", err.buf);
	if (get_ref_atom_value(b, s->atom, &vb, &err))
		die("%s", err.buf);
	strbuf_release(&err);
	if (s->sort_flags & REF_SORTING_DETACHED_HEAD_FIRST &&
	    ((a->kind | b->kind) & FILTER_REFS_DETACHED_HEAD)) {
		cmp = compare_detached_head(a, b);
		cmp_detached_head = 1;
	} else if (s->sort_flags & REF_SORTING_VERSION) {
		cmp = versioncmp(va->s, vb->s);
	} else if (cmp_type == FIELD_STR) {
		if (va->s_size < 0 && vb->s_size < 0) {
			int (*cmp_fn)(const char *, const char *);
			cmp_fn = s->sort_flags & REF_SORTING_ICASE
				? strcasecmp : strcmp;
			cmp = cmp_fn(va->s, vb->s);
		} else {
			size_t a_size = va->s_size < 0 ?
					strlen(va->s) : va->s_size;
			size_t b_size = vb->s_size < 0 ?
					strlen(vb->s) : vb->s_size;
			int (*cmp_fn)(const void *, const void *, size_t);
			cmp_fn = s->sort_flags & REF_SORTING_ICASE
				? memcasecmp : memcmp;

			cmp = cmp_fn(va->s, vb->s, b_size > a_size ?
				     a_size : b_size);
			if (!cmp) {
				if (a_size > b_size)
					cmp = 1;
				else if (a_size < b_size)
					cmp = -1;
			}
		}
	} else {
		if (va->value < vb->value)
			cmp = -1;
		else if (va->value == vb->value)
			cmp = 0;
		else
			cmp = 1;
	}

	return (s->sort_flags & REF_SORTING_REVERSE && !cmp_detached_head)
		? -cmp : cmp;
}

static int compare_refs(const void *a_, const void *b_, void *ref_sorting)
{
	struct ref_array_item *a = *((struct ref_array_item **)a_);
	struct ref_array_item *b = *((struct ref_array_item **)b_);
	struct ref_sorting *s;

	for (s = ref_sorting; s; s = s->next) {
		int cmp = cmp_ref_sorting(s, a, b);
		if (cmp)
			return cmp;
	}
	s = ref_sorting;
	return s && s->sort_flags & REF_SORTING_ICASE ?
		strcasecmp(a->refname, b->refname) :
		strcmp(a->refname, b->refname);
}

void ref_sorting_set_sort_flags_all(struct ref_sorting *sorting,
				    unsigned int mask, int on)
{
	for (; sorting; sorting = sorting->next) {
		if (on)
			sorting->sort_flags |= mask;
		else
			sorting->sort_flags &= ~mask;
	}
}

void ref_array_sort(struct ref_sorting *sorting, struct ref_array *array)
{
	QSORT_S(array->items, array->nr, compare_refs, sorting);
}

static void append_literal(const char *cp, const char *ep, struct ref_formatting_state *state)
{
	struct strbuf *s = &state->stack->output;

	while (*cp && (!ep || cp < ep)) {
		if (*cp == '%') {
			if (cp[1] == '%')
				cp++;
			else {
				int ch = hex2chr(cp + 1);
				if (0 <= ch) {
					strbuf_addch(s, ch);
					cp += 3;
					continue;
				}
			}
		}
		strbuf_addch(s, *cp);
		cp++;
	}
}

int format_ref_array_item(struct ref_array_item *info,
			  struct ref_format *format,
			  struct strbuf *final_buf,
			  struct strbuf *error_buf)
{
	const char *cp, *sp, *ep;
	struct ref_formatting_state state = REF_FORMATTING_STATE_INIT;

	state.quote_style = format->quote_style;
	push_stack_element(&state.stack);

	for (cp = format->format; *cp && (sp = find_next(cp)); cp = ep + 1) {
		struct atom_value *atomv;
		int pos;

		ep = strchr(sp, ')');
		if (cp < sp)
			append_literal(cp, sp, &state);
		pos = parse_ref_filter_atom(format, sp + 2, ep, error_buf);
		if (pos < 0 || get_ref_atom_value(info, pos, &atomv, error_buf) ||
		    atomv->handler(atomv, &state, error_buf)) {
			pop_stack_element(&state.stack);
			return -1;
		}
	}
	if (*cp) {
		sp = cp + strlen(cp);
		append_literal(cp, sp, &state);
	}
	if (format->need_color_reset_at_eol) {
		struct atom_value resetv = ATOM_VALUE_INIT;
		resetv.s = GIT_COLOR_RESET;
		if (append_atom(&resetv, &state, error_buf)) {
			pop_stack_element(&state.stack);
			return -1;
		}
	}
	if (state.stack->prev) {
		pop_stack_element(&state.stack);
		return strbuf_addf_ret(error_buf, -1, _("format: %%(end) atom missing"));
	}
	strbuf_addbuf(final_buf, &state.stack->output);
	pop_stack_element(&state.stack);
	return 0;
}

void pretty_print_ref(const char *name, const struct object_id *oid,
		      struct ref_format *format)
{
	struct ref_array_item *ref_item;
	struct strbuf output = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;

	ref_item = new_ref_array_item(name, oid);
	ref_item->kind = ref_kind_from_refname(name);
	if (format_ref_array_item(ref_item, format, &output, &err))
		die("%s", err.buf);
	fwrite(output.buf, 1, output.len, stdout);
	putchar('\n');

	strbuf_release(&err);
	strbuf_release(&output);
	free_array_item(ref_item);
}

static int parse_sorting_atom(const char *atom)
{
	/*
	 * This parses an atom using a dummy ref_format, since we don't
	 * actually care about the formatting details.
	 */
	struct ref_format dummy = REF_FORMAT_INIT;
	const char *end = atom + strlen(atom);
	struct strbuf err = STRBUF_INIT;
	int res = parse_ref_filter_atom(&dummy, atom, end, &err);
	if (res < 0)
		die("%s", err.buf);
	strbuf_release(&err);
	return res;
}

/*  If no sorting option is given, use refname to sort as default */
static struct ref_sorting *ref_default_sorting(void)
{
	static const char cstr_name[] = "refname";

	struct ref_sorting *sorting = xcalloc(1, sizeof(*sorting));

	sorting->next = NULL;
	sorting->atom = parse_sorting_atom(cstr_name);
	return sorting;
}

static void parse_ref_sorting(struct ref_sorting **sorting_tail, const char *arg)
{
	struct ref_sorting *s;

	CALLOC_ARRAY(s, 1);
	s->next = *sorting_tail;
	*sorting_tail = s;

	if (*arg == '-') {
		s->sort_flags |= REF_SORTING_REVERSE;
		arg++;
	}
	if (skip_prefix(arg, "version:", &arg) ||
	    skip_prefix(arg, "v:", &arg))
		s->sort_flags |= REF_SORTING_VERSION;
	s->atom = parse_sorting_atom(arg);
}

struct ref_sorting *ref_sorting_options(struct string_list *options)
{
	struct string_list_item *item;
	struct ref_sorting *sorting = NULL, **tail = &sorting;

	if (!options->nr) {
		sorting = ref_default_sorting();
	} else {
		for_each_string_list_item(item, options)
			parse_ref_sorting(tail, item->string);
	}

	/*
	 * From here on, the ref_sorting list should be used to talk
	 * about the sort order used for the output.  The caller
	 * should not touch the string form anymore.
	 */
	string_list_clear(options, 0);
	return sorting;
}

void ref_sorting_release(struct ref_sorting *sorting)
{
	while (sorting) {
		struct ref_sorting *next = sorting->next;
		free(sorting);
		sorting = next;
	}
}

int parse_opt_merge_filter(const struct option *opt, const char *arg, int unset)
{
	struct ref_filter *rf = opt->value;
	struct object_id oid;
	struct commit *merge_commit;

	BUG_ON_OPT_NEG(unset);

	if (repo_get_oid(the_repository, arg, &oid))
		die(_("malformed object name %s"), arg);

	merge_commit = lookup_commit_reference_gently(the_repository, &oid, 0);

	if (!merge_commit)
		return error(_("option `%s' must point to a commit"), opt->long_name);

	if (starts_with(opt->long_name, "no"))
		commit_list_insert(merge_commit, &rf->unreachable_from);
	else
		commit_list_insert(merge_commit, &rf->reachable_from);

	return 0;
}

void ref_filter_init(struct ref_filter *filter)
{
	struct ref_filter blank = REF_FILTER_INIT;
	memcpy(filter, &blank, sizeof(blank));
}

void ref_filter_clear(struct ref_filter *filter)
{
	strvec_clear(&filter->exclude);
	oid_array_clear(&filter->points_at);
	free_commit_list(filter->with_commit);
	free_commit_list(filter->no_commit);
	free_commit_list(filter->reachable_from);
	free_commit_list(filter->unreachable_from);
	ref_filter_init(filter);
}
