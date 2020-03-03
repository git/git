#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "refs.h"
#include "wildmatch.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "remote.h"
#include "color.h"
#include "tag.h"
#include "quote.h"
#include "ref-filter.h"
#include "revision.h"
#include "utf8.h"
#include "git-compat-util.h"
#include "version.h"
#include "trailer.h"
#include "wt-status.h"
#include "commit-slab.h"
#include "commit-graph.h"
#include "commit-reach.h"
#include "worktree.h"
#include "hashmap.h"
#include "argv-array.h"

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

static int ref_to_worktree_map_cmpfnc(const void *unused_lookupdata,
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
			enum { C_BARE, C_BODY, C_BODY_DEP, C_LINES, C_SIG, C_SUB, C_TRAILERS } option;
			struct process_trailer_options trailer_opts;
			unsigned int nlines;
		} contents;
		struct {
			cmp_status cmp_status;
			const char *str;
		} if_then_else;
		struct {
			enum { O_FULL, O_LENGTH, O_SHORT } option;
			unsigned int length;
		} objectname;
		struct refname_atom refname;
		char *head;
	} u;
} *used_atom;
static int used_atom_cnt, need_tagged, need_symref;

/*
 * Expand string, append it to strbuf *sb, then return error code ret.
 * Allow to save few lines of code.
 */
static int strbuf_addf_ret(struct strbuf *sb, int ret, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vaddf(sb, fmt, ap);
	va_end(ap);
	return ret;
}

static int color_atom_parser(const struct ref_format *format, struct used_atom *atom,
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
		return strbuf_addf_ret(err, -1, _("unrecognized %%(%s) argument: %s"), name, arg);
	return 0;
}

static int remote_ref_atom_parser(const struct ref_format *format, struct used_atom *atom,
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

static int objecttype_atom_parser(const struct ref_format *format, struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	if (arg)
		return strbuf_addf_ret(err, -1, _("%%(objecttype) does not take arguments"));
	if (*atom->name == '*')
		oi_deref.info.typep = &oi_deref.type;
	else
		oi.info.typep = &oi.type;
	return 0;
}

static int objectsize_atom_parser(const struct ref_format *format, struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	if (!arg) {
		if (*atom->name == '*')
			oi_deref.info.sizep = &oi_deref.size;
		else
			oi.info.sizep = &oi.size;
	} else if (!strcmp(arg, "disk")) {
		if (*atom->name == '*')
			oi_deref.info.disk_sizep = &oi_deref.disk_size;
		else
			oi.info.disk_sizep = &oi.disk_size;
	} else
		return strbuf_addf_ret(err, -1, _("unrecognized %%(objectsize) argument: %s"), arg);
	return 0;
}

static int deltabase_atom_parser(const struct ref_format *format, struct used_atom *atom,
				 const char *arg, struct strbuf *err)
{
	if (arg)
		return strbuf_addf_ret(err, -1, _("%%(deltabase) does not take arguments"));
	if (*atom->name == '*')
		oi_deref.info.delta_base_oid = &oi_deref.delta_base_oid;
	else
		oi.info.delta_base_oid = &oi.delta_base_oid;
	return 0;
}

static int body_atom_parser(const struct ref_format *format, struct used_atom *atom,
			    const char *arg, struct strbuf *err)
{
	if (arg)
		return strbuf_addf_ret(err, -1, _("%%(body) does not take arguments"));
	atom->u.contents.option = C_BODY_DEP;
	return 0;
}

static int subject_atom_parser(const struct ref_format *format, struct used_atom *atom,
			       const char *arg, struct strbuf *err)
{
	if (arg)
		return strbuf_addf_ret(err, -1, _("%%(subject) does not take arguments"));
	atom->u.contents.option = C_SUB;
	return 0;
}

static int trailers_atom_parser(const struct ref_format *format, struct used_atom *atom,
				const char *arg, struct strbuf *err)
{
	struct string_list params = STRING_LIST_INIT_DUP;
	int i;

	atom->u.contents.trailer_opts.no_divider = 1;

	if (arg) {
		string_list_split(&params, arg, ',', -1);
		for (i = 0; i < params.nr; i++) {
			const char *s = params.items[i].string;
			if (!strcmp(s, "unfold"))
				atom->u.contents.trailer_opts.unfold = 1;
			else if (!strcmp(s, "only"))
				atom->u.contents.trailer_opts.only_trailers = 1;
			else {
				strbuf_addf(err, _("unknown %%(trailers) argument: %s"), s);
				string_list_clear(&params, 0);
				return -1;
			}
		}
	}
	atom->u.contents.option = C_TRAILERS;
	string_list_clear(&params, 0);
	return 0;
}

static int contents_atom_parser(const struct ref_format *format, struct used_atom *atom,
				const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.contents.option = C_BARE;
	else if (!strcmp(arg, "body"))
		atom->u.contents.option = C_BODY;
	else if (!strcmp(arg, "signature"))
		atom->u.contents.option = C_SIG;
	else if (!strcmp(arg, "subject"))
		atom->u.contents.option = C_SUB;
	else if (skip_prefix(arg, "trailers", &arg)) {
		skip_prefix(arg, ":", &arg);
		if (trailers_atom_parser(format, atom, *arg ? arg : NULL, err))
			return -1;
	} else if (skip_prefix(arg, "lines=", &arg)) {
		atom->u.contents.option = C_LINES;
		if (strtoul_ui(arg, 10, &atom->u.contents.nlines))
			return strbuf_addf_ret(err, -1, _("positive value expected contents:lines=%s"), arg);
	} else
		return strbuf_addf_ret(err, -1, _("unrecognized %%(contents) argument: %s"), arg);
	return 0;
}

static int objectname_atom_parser(const struct ref_format *format, struct used_atom *atom,
				  const char *arg, struct strbuf *err)
{
	if (!arg)
		atom->u.objectname.option = O_FULL;
	else if (!strcmp(arg, "short"))
		atom->u.objectname.option = O_SHORT;
	else if (skip_prefix(arg, "short=", &arg)) {
		atom->u.objectname.option = O_LENGTH;
		if (strtoul_ui(arg, 10, &atom->u.objectname.length) ||
		    atom->u.objectname.length == 0)
			return strbuf_addf_ret(err, -1, _("positive value expected objectname:short=%s"), arg);
		if (atom->u.objectname.length < MINIMUM_ABBREV)
			atom->u.objectname.length = MINIMUM_ABBREV;
	} else
		return strbuf_addf_ret(err, -1, _("unrecognized %%(objectname) argument: %s"), arg);
	return 0;
}

static int refname_atom_parser(const struct ref_format *format, struct used_atom *atom,
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

static int align_atom_parser(const struct ref_format *format, struct used_atom *atom,
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
			strbuf_addf(err, _("unrecognized %%(align) argument: %s"), s);
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

static int if_atom_parser(const struct ref_format *format, struct used_atom *atom,
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
		return strbuf_addf_ret(err, -1, _("unrecognized %%(if) argument: %s"), arg);
	return 0;
}

static int head_atom_parser(const struct ref_format *format, struct used_atom *atom,
			    const char *arg, struct strbuf *unused_err)
{
	atom->u.head = resolve_refdup("HEAD", RESOLVE_REF_READING, NULL, NULL);
	return 0;
}

static struct {
	const char *name;
	info_source source;
	cmp_type cmp_type;
	int (*parser)(const struct ref_format *format, struct used_atom *atom,
		      const char *arg, struct strbuf *err);
} valid_atom[] = {
	{ "refname", SOURCE_NONE, FIELD_STR, refname_atom_parser },
	{ "objecttype", SOURCE_OTHER, FIELD_STR, objecttype_atom_parser },
	{ "objectsize", SOURCE_OTHER, FIELD_ULONG, objectsize_atom_parser },
	{ "objectname", SOURCE_OTHER, FIELD_STR, objectname_atom_parser },
	{ "deltabase", SOURCE_OTHER, FIELD_STR, deltabase_atom_parser },
	{ "tree", SOURCE_OBJ },
	{ "parent", SOURCE_OBJ },
	{ "numparent", SOURCE_OBJ, FIELD_ULONG },
	{ "object", SOURCE_OBJ },
	{ "type", SOURCE_OBJ },
	{ "tag", SOURCE_OBJ },
	{ "author", SOURCE_OBJ },
	{ "authorname", SOURCE_OBJ },
	{ "authoremail", SOURCE_OBJ },
	{ "authordate", SOURCE_OBJ, FIELD_TIME },
	{ "committer", SOURCE_OBJ },
	{ "committername", SOURCE_OBJ },
	{ "committeremail", SOURCE_OBJ },
	{ "committerdate", SOURCE_OBJ, FIELD_TIME },
	{ "tagger", SOURCE_OBJ },
	{ "taggername", SOURCE_OBJ },
	{ "taggeremail", SOURCE_OBJ },
	{ "taggerdate", SOURCE_OBJ, FIELD_TIME },
	{ "creator", SOURCE_OBJ },
	{ "creatordate", SOURCE_OBJ, FIELD_TIME },
	{ "subject", SOURCE_OBJ, FIELD_STR, subject_atom_parser },
	{ "body", SOURCE_OBJ, FIELD_STR, body_atom_parser },
	{ "trailers", SOURCE_OBJ, FIELD_STR, trailers_atom_parser },
	{ "contents", SOURCE_OBJ, FIELD_STR, contents_atom_parser },
	{ "upstream", SOURCE_NONE, FIELD_STR, remote_ref_atom_parser },
	{ "push", SOURCE_NONE, FIELD_STR, remote_ref_atom_parser },
	{ "symref", SOURCE_NONE, FIELD_STR, refname_atom_parser },
	{ "flag", SOURCE_NONE },
	{ "HEAD", SOURCE_NONE, FIELD_STR, head_atom_parser },
	{ "color", SOURCE_NONE, FIELD_STR, color_atom_parser },
	{ "worktreepath", SOURCE_NONE },
	{ "align", SOURCE_NONE, FIELD_STR, align_atom_parser },
	{ "end", SOURCE_NONE },
	{ "if", SOURCE_NONE, FIELD_STR, if_atom_parser },
	{ "then", SOURCE_NONE },
	{ "else", SOURCE_NONE },
	/*
	 * Please update $__git_ref_fieldlist in git-completion.bash
	 * when you add new atoms
	 */
};

#define REF_FORMATTING_STATE_INIT  { 0, NULL }

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
	int (*handler)(struct atom_value *atomv, struct ref_formatting_state *state,
		       struct strbuf *err);
	uintmax_t value; /* used for sorting when not FIELD_STR */
	struct used_atom *atom;
};

/*
 * Used to parse format string and sort specifiers
 */
static int parse_ref_filter_atom(const struct ref_format *format,
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

	/* Do we have the atom already used elsewhere? */
	for (i = 0; i < used_atom_cnt; i++) {
		int len = strlen(used_atom[i].name);
		if (len == ep - atom && !memcmp(used_atom[i].name, atom, len))
			return i;
	}

	/*
	 * If the atom name has a colon, strip it and everything after
	 * it off - it specifies the format for this entry, and
	 * shouldn't be used for checking against the valid_atom
	 * table.
	 */
	arg = memchr(sp, ':', ep - sp);
	atom_len = (arg ? arg : ep) - sp;

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
	if (!strcmp(valid_atom[i].name, "symref"))
		need_symref = 1;
	return at;
}

static void quote_formatting(struct strbuf *s, const char *str, int quote_style)
{
	switch (quote_style) {
	case QUOTE_NONE:
		strbuf_addstr(s, str);
		break;
	case QUOTE_SHELL:
		sq_quote_buf(s, str);
		break;
	case QUOTE_PERL:
		perl_quote_buf(s, str);
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
		       struct strbuf *unused_err)
{
	/*
	 * Quote formatting is only done when the stack has a single
	 * element. Otherwise quote formatting is done on the
	 * element's entire output strbuf when the %(end) atom is
	 * encountered.
	 */
	if (!state->stack->prev)
		quote_formatting(&state->stack->output, v->s, state->quote_style);
	else
		strbuf_addstr(&state->stack->output, v->s);
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
			      struct strbuf *unused_err)
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
		die(_("format: %%(if) atom used without a %%(then) atom"));

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
			   struct strbuf *unused_err)
{
	struct ref_formatting_stack *new_stack;
	struct if_then_else *if_then_else = xcalloc(sizeof(struct if_then_else), 1);

	if_then_else->str = atomv->atom->u.if_then_else.str;
	if_then_else->cmp_status = atomv->atom->u.if_then_else.cmp_status;

	push_stack_element(&state->stack);
	new_stack = state->stack;
	new_stack->at_end = if_then_else_handler;
	new_stack->at_end_data = if_then_else;
	return 0;
}

static int is_empty(const char *s)
{
	while (*s != '\0') {
		if (!isspace(*s))
			return 0;
		s++;
	}
	return 1;
}

static int then_atom_handler(struct atom_value *atomv, struct ref_formatting_state *state,
			     struct strbuf *err)
{
	struct ref_formatting_stack *cur = state->stack;
	struct if_then_else *if_then_else = NULL;

	if (cur->at_end == if_then_else_handler)
		if_then_else = (struct if_then_else *)cur->at_end_data;
	if (!if_then_else)
		return strbuf_addf_ret(err, -1, _("format: %%(then) atom used without an %%(if) atom"));
	if (if_then_else->then_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(then) atom used more than once"));
	if (if_then_else->else_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(then) atom used after %%(else)"));
	if_then_else->then_atom_seen = 1;
	/*
	 * If the 'equals' or 'notequals' attribute is used then
	 * perform the required comparison. If not, only non-empty
	 * strings satisfy the 'if' condition.
	 */
	if (if_then_else->cmp_status == COMPARE_EQUAL) {
		if (!strcmp(if_then_else->str, cur->output.buf))
			if_then_else->condition_satisfied = 1;
	} else if (if_then_else->cmp_status == COMPARE_UNEQUAL) {
		if (strcmp(if_then_else->str, cur->output.buf))
			if_then_else->condition_satisfied = 1;
	} else if (cur->output.len && !is_empty(cur->output.buf))
		if_then_else->condition_satisfied = 1;
	strbuf_reset(&cur->output);
	return 0;
}

static int else_atom_handler(struct atom_value *atomv, struct ref_formatting_state *state,
			     struct strbuf *err)
{
	struct ref_formatting_stack *prev = state->stack;
	struct if_then_else *if_then_else = NULL;

	if (prev->at_end == if_then_else_handler)
		if_then_else = (struct if_then_else *)prev->at_end_data;
	if (!if_then_else)
		return strbuf_addf_ret(err, -1, _("format: %%(else) atom used without an %%(if) atom"));
	if (!if_then_else->then_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(else) atom used without a %%(then) atom"));
	if (if_then_else->else_atom_seen)
		return strbuf_addf_ret(err, -1, _("format: %%(else) atom used more than once"));
	if_then_else->else_atom_seen = 1;
	push_stack_element(&state->stack);
	state->stack->at_end_data = prev->at_end_data;
	state->stack->at_end = prev->at_end;
	return 0;
}

static int end_atom_handler(struct atom_value *atomv, struct ref_formatting_state *state,
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
		quote_formatting(&s, current->output.buf, state->quote_style);
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
		cp = ep + 1;

		if (skip_prefix(used_atom[at].name, "color:", &color))
			format->need_color_reset_at_eol = !!strcmp(color, "reset");
		strbuf_release(&err);
	}
	if (format->need_color_reset_at_eol && !want_color(format->use_color))
		format->need_color_reset_at_eol = 0;
	return 0;
}

static int grab_objectname(const char *name, const struct object_id *oid,
			   struct atom_value *v, struct used_atom *atom)
{
	if (starts_with(name, "objectname")) {
		if (atom->u.objectname.option == O_SHORT) {
			v->s = xstrdup(find_unique_abbrev(oid, DEFAULT_ABBREV));
			return 1;
		} else if (atom->u.objectname.option == O_FULL) {
			v->s = xstrdup(oid_to_hex(oid));
			return 1;
		} else if (atom->u.objectname.option == O_LENGTH) {
			v->s = xstrdup(find_unique_abbrev(oid, atom->u.objectname.length));
			return 1;
		} else
			BUG("unknown %%(objectname) option");
	}
	return 0;
}

/* See grab_values */
static void grab_common_values(struct atom_value *val, int deref, struct expand_data *oi)
{
	int i;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "objecttype"))
			v->s = xstrdup(type_name(oi->type));
		else if (!strcmp(name, "objectsize:disk")) {
			v->value = oi->disk_size;
			v->s = xstrfmt("%"PRIuMAX, (uintmax_t)oi->disk_size);
		} else if (!strcmp(name, "objectsize")) {
			v->value = oi->size;
			v->s = xstrfmt("%"PRIuMAX , (uintmax_t)oi->size);
		} else if (!strcmp(name, "deltabase"))
			v->s = xstrdup(oid_to_hex(&oi->delta_base_oid));
		else if (deref)
			grab_objectname(name, &oi->oid, v, &used_atom[i]);
	}
}

/* See grab_values */
static void grab_tag_values(struct atom_value *val, int deref, struct object *obj)
{
	int i;
	struct tag *tag = (struct tag *) obj;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "tag"))
			v->s = xstrdup(tag->tag);
		else if (!strcmp(name, "type") && tag->tagged)
			v->s = xstrdup(type_name(tag->tagged->type));
		else if (!strcmp(name, "object") && tag->tagged)
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
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "tree")) {
			v->s = xstrdup(oid_to_hex(get_commit_tree_oid(commit)));
		}
		else if (!strcmp(name, "numparent")) {
			v->value = commit_list_count(commit->parents);
			v->s = xstrfmt("%lu", (unsigned long)v->value);
		}
		else if (!strcmp(name, "parent")) {
			struct commit_list *parents;
			struct strbuf s = STRBUF_INIT;
			for (parents = commit->parents; parents; parents = parents->next) {
				struct commit *parent = parents->item;
				if (parents != commit->parents)
					strbuf_addch(&s, ' ');
				strbuf_addstr(&s, oid_to_hex(&parent->object.oid));
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
		if (!strncmp(cp, " <", 2))
			return xmemdupz(buf, cp - buf);
	}
	return xstrdup("");
}

static const char *copy_email(const char *buf)
{
	const char *email = strchr(buf, '<');
	const char *eoemail;
	if (!email)
		return xstrdup("");
	eoemail = strchr(email, '>');
	if (!eoemail)
		return xstrdup("");
	return xmemdupz(email, eoemail + 1 - email);
}

static char *copy_subject(const char *buf, unsigned long len)
{
	char *r = xmemdupz(buf, len);
	int i;

	for (i = 0; i < len; i++)
		if (r[i] == '\n')
			r[i] = ' ';

	return r;
}

static void grab_date(const char *buf, struct atom_value *v, const char *atomname)
{
	const char *eoemail = strstr(buf, "> ");
	char *zone;
	timestamp_t timestamp;
	long tz;
	struct date_mode date_mode = { DATE_NORMAL };
	const char *formatp;

	/*
	 * We got here because atomname ends in "date" or "date<something>";
	 * it's not possible that <something> is not ":<format>" because
	 * parse_ref_filter_atom() wouldn't have allowed it, so we can assume that no
	 * ":" means no format is specified, and use the default.
	 */
	formatp = strchr(atomname, ':');
	if (formatp != NULL) {
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
	return;
 bad:
	v->s = xstrdup("");
	v->value = 0;
}

/* See grab_values */
static void grab_person(const char *who, struct atom_value *val, int deref, void *buf)
{
	int i;
	int wholen = strlen(who);
	const char *wholine = NULL;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i].name;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (strncmp(who, name, wholen))
			continue;
		if (name[wholen] != 0 &&
		    strcmp(name + wholen, "name") &&
		    strcmp(name + wholen, "email") &&
		    !starts_with(name + wholen, "date"))
			continue;
		if (!wholine)
			wholine = find_wholine(who, wholen, buf);
		if (!wholine)
			return; /* no point looking for it */
		if (name[wholen] == 0)
			v->s = copy_line(wholine);
		else if (!strcmp(name + wholen, "name"))
			v->s = copy_name(wholine);
		else if (!strcmp(name + wholen, "email"))
			v->s = copy_email(wholine);
		else if (starts_with(name + wholen, "date"))
			grab_date(wholine, v, name);
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
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;

		if (starts_with(name, "creatordate"))
			grab_date(wholine, v, name);
		else if (!strcmp(name, "creator"))
			v->s = copy_line(wholine);
	}
}

static void find_subpos(const char *buf,
			const char **sub, unsigned long *sublen,
			const char **body, unsigned long *bodylen,
			unsigned long *nonsiglen,
			const char **sig, unsigned long *siglen)
{
	const char *eol;
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

	/* parse signature first; we might not even have a subject line */
	*sig = buf + parse_signature(buf, strlen(buf));
	*siglen = strlen(*sig);

	/* subject is first non-empty line */
	*sub = buf;
	/* subject goes to first empty line */
	while (buf < *sig && *buf && *buf != '\n') {
		eol = strchrnul(buf, '\n');
		if (*eol)
			eol++;
		buf = eol;
	}
	*sublen = buf - *sub;
	/* drop trailing newline, if present */
	if (*sublen && (*sub)[*sublen - 1] == '\n')
		*sublen -= 1;

	/* skip any empty lines */
	while (*buf == '\n')
		buf++;
	*body = buf;
	*bodylen = strlen(buf);
	*nonsiglen = *sig - buf;
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

/* See grab_values */
static void grab_sub_body_contents(struct atom_value *val, int deref, void *buf)
{
	int i;
	const char *subpos = NULL, *bodypos = NULL, *sigpos = NULL;
	unsigned long sublen = 0, bodylen = 0, nonsiglen = 0, siglen = 0;

	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		const char *name = atom->name;
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (strcmp(name, "subject") &&
		    strcmp(name, "body") &&
		    !starts_with(name, "trailers") &&
		    !starts_with(name, "contents"))
			continue;
		if (!subpos)
			find_subpos(buf,
				    &subpos, &sublen,
				    &bodypos, &bodylen, &nonsiglen,
				    &sigpos, &siglen);

		if (atom->u.contents.option == C_SUB)
			v->s = copy_subject(subpos, sublen);
		else if (atom->u.contents.option == C_BODY_DEP)
			v->s = xmemdupz(bodypos, bodylen);
		else if (atom->u.contents.option == C_BODY)
			v->s = xmemdupz(bodypos, nonsiglen);
		else if (atom->u.contents.option == C_SIG)
			v->s = xmemdupz(sigpos, siglen);
		else if (atom->u.contents.option == C_LINES) {
			struct strbuf s = STRBUF_INIT;
			const char *contents_end = bodylen + bodypos - siglen;

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
		if (v->s == NULL)
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
static void grab_values(struct atom_value *val, int deref, struct object *obj, void *buf)
{
	switch (obj->type) {
	case OBJ_TAG:
		grab_tag_values(val, deref, obj);
		grab_sub_body_contents(val, deref, buf);
		grab_person("tagger", val, deref, buf);
		break;
	case OBJ_COMMIT:
		grab_commit_values(val, deref, obj);
		grab_sub_body_contents(val, deref, buf);
		grab_person("author", val, deref, buf);
		grab_person("committer", val, deref, buf);
		break;
	case OBJ_TREE:
		/* grab_tree_values(val, deref, obj, buf, sz); */
		break;
	case OBJ_BLOB:
		/* grab_blob_values(val, deref, obj, buf, sz); */
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
		if (p == NULL) {
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

	/*
	 * The ( character must be hard-coded and not part of a localizable
	 * string, since the description is used as a sort key and compared
	 * with ref names.
	 */
	strbuf_addch(&desc, '(');
	if (state.rebase_in_progress ||
	    state.rebase_interactive_in_progress) {
		if (state.branch)
			strbuf_addf(&desc, _("no branch, rebasing %s"),
				    state.branch);
		else
			strbuf_addf(&desc, _("no branch, rebasing detached HEAD %s"),
				    state.detached_from);
	} else if (state.bisect_in_progress)
		strbuf_addf(&desc, _("no branch, bisect started on %s"),
			    state.branch);
	else if (state.detached_from) {
		if (state.detached_at)
			strbuf_addstr(&desc, HEAD_DETACHED_AT);
		else
			strbuf_addstr(&desc, HEAD_DETACHED_FROM);
		strbuf_addstr(&desc, state.detached_from);
	}
	else
		strbuf_addstr(&desc, _("no branch"));
	strbuf_addch(&desc, ')');

	free(state.branch);
	free(state.onto);
	free(state.detached_from);
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
		if (!obj) {
			if (!eaten)
				free(oi->content);
			return strbuf_addf_ret(err, -1, _("parse_object_buffer failed on %s for %s"),
					       oid_to_hex(&oi->oid), ref->refname);
		}
		grab_values(ref->value, deref, *obj, oi->content);
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

	ref_to_worktree_map.worktrees = get_worktrees(0);
	hashmap_init(&(ref_to_worktree_map.map), ref_to_worktree_map_cmpfnc, NULL, 0);
	populate_worktree_map(&(ref_to_worktree_map.map), ref_to_worktree_map.worktrees);
}

static char *get_worktree_path(const struct used_atom *atom, const struct ref_array_item *ref)
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

	ref->value = xcalloc(used_atom_cnt, sizeof(struct atom_value));

	if (need_symref && (ref->flag & REF_ISSYMREF) && !ref->symref) {
		ref->symref = resolve_refdup(ref->refname, RESOLVE_REF_READING,
					     NULL, NULL);
		if (!ref->symref)
			ref->symref = xstrdup("");
	}

	/* Fill in specials first */
	for (i = 0; i < used_atom_cnt; i++) {
		struct used_atom *atom = &used_atom[i];
		const char *name = used_atom[i].name;
		struct atom_value *v = &ref->value[i];
		int deref = 0;
		const char *refname;
		struct branch *branch = NULL;

		v->handler = append_atom;
		v->atom = atom;

		if (*name == '*') {
			deref = 1;
			name++;
		}

		if (starts_with(name, "refname"))
			refname = get_refname(atom, ref);
		else if (!strcmp(name, "worktreepath")) {
			if (ref->kind == FILTER_REFS_BRANCHES)
				v->s = get_worktree_path(atom, ref);
			else
				v->s = xstrdup("");
			continue;
		}
		else if (starts_with(name, "symref"))
			refname = get_symref(atom, ref);
		else if (starts_with(name, "upstream")) {
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
		} else if (atom->u.remote_ref.push) {
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
		} else if (starts_with(name, "color:")) {
			v->s = xstrdup(atom->u.color);
			continue;
		} else if (!strcmp(name, "flag")) {
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
		} else if (!deref && grab_objectname(name, &ref->objectname, v, atom)) {
			continue;
		} else if (!strcmp(name, "HEAD")) {
			if (atom->u.head && !strcmp(ref->refname, atom->u.head))
				v->s = xstrdup("*");
			else
				v->s = xstrdup(" ");
			continue;
		} else if (starts_with(name, "align")) {
			v->handler = align_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (!strcmp(name, "end")) {
			v->handler = end_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (starts_with(name, "if")) {
			const char *s;
			if (skip_prefix(name, "if:", &s))
				v->s = xstrdup(s);
			else
				v->s = xstrdup("");
			v->handler = if_atom_handler;
			continue;
		} else if (!strcmp(name, "then")) {
			v->handler = then_atom_handler;
			v->s = xstrdup("");
			continue;
		} else if (!strcmp(name, "else")) {
			v->handler = else_atom_handler;
			v->s = xstrdup("");
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
static int match_pattern(const struct ref_filter *filter, const char *refname)
{
	const char **patterns = filter->name_patterns;
	unsigned flags = 0;

	if (filter->ignore_case)
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
static int match_name_as_path(const struct ref_filter *filter, const char *refname)
{
	const char **pattern = filter->name_patterns;
	int namelen = strlen(refname);
	unsigned flags = WM_PATHNAME;

	if (filter->ignore_case)
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
		return match_name_as_path(filter, refname);
	return match_pattern(filter, refname);
}

static int qsort_strcmp(const void *va, const void *vb)
{
	const char *a = *(const char **)va;
	const char *b = *(const char **)vb;

	return strcmp(a, b);
}

static void find_longest_prefixes_1(struct string_list *out,
				  struct strbuf *prefix,
				  const char **patterns, size_t nr)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		char c = patterns[i][prefix->len];
		if (!c || is_glob_special(c)) {
			string_list_append(out, prefix->buf);
			return;
		}
	}

	i = 0;
	while (i < nr) {
		size_t end;

		/*
		* Set "end" to the index of the element _after_ the last one
		* in our group.
		*/
		for (end = i + 1; end < nr; end++) {
			if (patterns[i][prefix->len] != patterns[end][prefix->len])
				break;
		}

		strbuf_addch(prefix, patterns[i][prefix->len]);
		find_longest_prefixes_1(out, prefix, patterns + i, end - i);
		strbuf_setlen(prefix, prefix->len - 1);

		i = end;
	}
}

static void find_longest_prefixes(struct string_list *out,
				  const char **patterns)
{
	struct argv_array sorted = ARGV_ARRAY_INIT;
	struct strbuf prefix = STRBUF_INIT;

	argv_array_pushv(&sorted, patterns);
	QSORT(sorted.argv, sorted.argc, qsort_strcmp);

	find_longest_prefixes_1(out, &prefix, sorted.argv, sorted.argc);

	argv_array_clear(&sorted);
	strbuf_release(&prefix);
}

/*
 * This is the same as for_each_fullref_in(), but it tries to iterate
 * only over the patterns we'll care about. Note that it _doesn't_ do a full
 * pattern match, so the callback still has to match each ref individually.
 */
static int for_each_fullref_in_pattern(struct ref_filter *filter,
				       each_ref_fn cb,
				       void *cb_data,
				       int broken)
{
	struct string_list prefixes = STRING_LIST_INIT_DUP;
	struct string_list_item *prefix;
	int ret;

	if (!filter->match_as_path) {
		/*
		 * in this case, the patterns are applied after
		 * prefixes like "refs/heads/" etc. are stripped off,
		 * so we have to look at everything:
		 */
		return for_each_fullref_in("", cb, cb_data, broken);
	}

	if (filter->ignore_case) {
		/*
		 * we can't handle case-insensitive comparisons,
		 * so just return everything and let the caller
		 * sort it out.
		 */
		return for_each_fullref_in("", cb, cb_data, broken);
	}

	if (!filter->name_patterns[0]) {
		/* no patterns; we have to look at everything */
		return for_each_fullref_in("", cb, cb_data, broken);
	}

	find_longest_prefixes(&prefixes, filter->name_patterns);

	for_each_string_list_item(prefix, &prefixes) {
		ret = for_each_fullref_in(prefix->string, cb, cb_data, broken);
		if (ret)
			break;
	}

	string_list_clear(&prefixes, 0);
	return ret;
}

/*
 * Given a ref (sha1, refname), check if the ref belongs to the array
 * of sha1s. If the given ref is a tag, check if the given tag points
 * at one of the sha1s in the given sha1 array.
 * the given sha1_array.
 * NEEDSWORK:
 * 1. Only a single level of inderection is obtained, we might want to
 * change this to account for multiple levels (e.g. annotated tags
 * pointing to annotated tags pointing to a commit.)
 * 2. As the refs are cached we might know what refname peels to without
 * the need to parse the object via parse_object(). peel_ref() might be a
 * more efficient alternative to obtain the pointee.
 */
static const struct object_id *match_points_at(struct oid_array *points_at,
					       const struct object_id *oid,
					       const char *refname)
{
	const struct object_id *tagged_oid = NULL;
	struct object *obj;

	if (oid_array_lookup(points_at, oid) >= 0)
		return oid;
	obj = parse_object(the_repository, oid);
	if (!obj)
		die(_("malformed object at '%s'"), refname);
	if (obj->type == OBJ_TAG)
		tagged_oid = get_tagged_oid((struct tag *)obj);
	if (tagged_oid && oid_array_lookup(points_at, tagged_oid) >= 0)
		return tagged_oid;
	return NULL;
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

	if (filter->points_at.nr && !match_points_at(&filter->points_at, oid, refname))
		return 0;

	/*
	 * A merge filter is applied on refs pointing to commits. Hence
	 * obtain the commit using the 'oid' available and discard all
	 * non-commits early. The actual filtering is done later.
	 */
	if (filter->merge_commit || filter->with_commit || filter->no_commit || filter->verbose) {
		commit = lookup_commit_reference_gently(the_repository, oid,
							1);
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

	for (i = 0; i < used_atom_cnt; i++)
		free((char *)used_atom[i].name);
	FREE_AND_NULL(used_atom);
	used_atom_cnt = 0;

	if (ref_to_worktree_map.worktrees) {
		hashmap_free_entries(&(ref_to_worktree_map.map),
					struct ref_to_worktree_entry, ent);
		free_worktrees(ref_to_worktree_map.worktrees);
		ref_to_worktree_map.worktrees = NULL;
	}
}

static void do_merge_filter(struct ref_filter_cbdata *ref_cbdata)
{
	struct rev_info revs;
	int i, old_nr;
	struct ref_filter *filter = ref_cbdata->filter;
	struct ref_array *array = ref_cbdata->array;
	struct commit **to_clear = xcalloc(sizeof(struct commit *), array->nr);

	repo_init_revisions(the_repository, &revs, NULL);

	for (i = 0; i < array->nr; i++) {
		struct ref_array_item *item = array->items[i];
		add_pending_object(&revs, &item->commit->object, item->refname);
		to_clear[i] = item->commit;
	}

	filter->merge_commit->object.flags |= UNINTERESTING;
	add_pending_object(&revs, &filter->merge_commit->object, "");

	revs.limited = 1;
	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	old_nr = array->nr;
	array->nr = 0;

	for (i = 0; i < old_nr; i++) {
		struct ref_array_item *item = array->items[i];
		struct commit *commit = item->commit;

		int is_merged = !!(commit->object.flags & UNINTERESTING);

		if (is_merged == (filter->merge == REF_FILTER_MERGED_INCLUDE))
			array->items[array->nr++] = array->items[i];
		else
			free_array_item(item);
	}

	clear_commit_marks_many(old_nr, to_clear, ALL_REV_FLAGS);
	clear_commit_marks(filter->merge_commit, ALL_REV_FLAGS);
	free(to_clear);
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
	int ret = 0;
	unsigned int broken = 0;

	ref_cbdata.array = array;
	ref_cbdata.filter = filter;

	if (type & FILTER_REFS_INCLUDE_BROKEN)
		broken = 1;
	filter->kind = type & FILTER_REFS_KIND_MASK;

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
			ret = for_each_fullref_in("refs/heads/", ref_filter_handler, &ref_cbdata, broken);
		else if (filter->kind == FILTER_REFS_REMOTES)
			ret = for_each_fullref_in("refs/remotes/", ref_filter_handler, &ref_cbdata, broken);
		else if (filter->kind == FILTER_REFS_TAGS)
			ret = for_each_fullref_in("refs/tags/", ref_filter_handler, &ref_cbdata, broken);
		else if (filter->kind & FILTER_REFS_ALL)
			ret = for_each_fullref_in_pattern(filter, ref_filter_handler, &ref_cbdata, broken);
		if (!ret && (filter->kind & FILTER_REFS_DETACHED_HEAD))
			head_ref(ref_filter_handler, &ref_cbdata);
	}

	clear_contains_cache(&ref_cbdata.contains_cache);
	clear_contains_cache(&ref_cbdata.no_contains_cache);

	/*  Filters that need revision walking */
	if (filter->merge_commit)
		do_merge_filter(&ref_cbdata);

	return ret;
}

static int cmp_ref_sorting(struct ref_sorting *s, struct ref_array_item *a, struct ref_array_item *b)
{
	struct atom_value *va, *vb;
	int cmp;
	cmp_type cmp_type = used_atom[s->atom].type;
	int (*cmp_fn)(const char *, const char *);
	struct strbuf err = STRBUF_INIT;

	if (get_ref_atom_value(a, s->atom, &va, &err))
		die("%s", err.buf);
	if (get_ref_atom_value(b, s->atom, &vb, &err))
		die("%s", err.buf);
	strbuf_release(&err);
	cmp_fn = s->ignore_case ? strcasecmp : strcmp;
	if (s->version)
		cmp = versioncmp(va->s, vb->s);
	else if (cmp_type == FIELD_STR)
		cmp = cmp_fn(va->s, vb->s);
	else {
		if (va->value < vb->value)
			cmp = -1;
		else if (va->value == vb->value)
			cmp = cmp_fn(a->refname, b->refname);
		else
			cmp = 1;
	}

	return (s->reverse) ? -cmp : cmp;
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
	return 0;
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
			   const struct ref_format *format,
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
		struct atom_value resetv;
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

void show_ref_array_item(struct ref_array_item *info,
			 const struct ref_format *format)
{
	struct strbuf final_buf = STRBUF_INIT;
	struct strbuf error_buf = STRBUF_INIT;

	if (format_ref_array_item(info, format, &final_buf, &error_buf))
		die("%s", error_buf.buf);
	fwrite(final_buf.buf, 1, final_buf.len, stdout);
	strbuf_release(&error_buf);
	strbuf_release(&final_buf);
	putchar('\n');
}

void pretty_print_ref(const char *name, const struct object_id *oid,
		      const struct ref_format *format)
{
	struct ref_array_item *ref_item;
	ref_item = new_ref_array_item(name, oid);
	ref_item->kind = ref_kind_from_refname(name);
	show_ref_array_item(ref_item, format);
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
struct ref_sorting *ref_default_sorting(void)
{
	static const char cstr_name[] = "refname";

	struct ref_sorting *sorting = xcalloc(1, sizeof(*sorting));

	sorting->next = NULL;
	sorting->atom = parse_sorting_atom(cstr_name);
	return sorting;
}

void parse_ref_sorting(struct ref_sorting **sorting_tail, const char *arg)
{
	struct ref_sorting *s;

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
	s->atom = parse_sorting_atom(arg);
}

int parse_opt_ref_sorting(const struct option *opt, const char *arg, int unset)
{
	/*
	 * NEEDSWORK: We should probably clear the list in this case, but we've
	 * already munged the global used_atoms list, which would need to be
	 * undone.
	 */
	BUG_ON_OPT_NEG(unset);

	parse_ref_sorting(opt->value, arg);
	return 0;
}

int parse_opt_merge_filter(const struct option *opt, const char *arg, int unset)
{
	struct ref_filter *rf = opt->value;
	struct object_id oid;
	int no_merged = starts_with(opt->long_name, "no");

	BUG_ON_OPT_NEG(unset);

	if (rf->merge) {
		if (no_merged) {
			return error(_("option `%s' is incompatible with --merged"),
				     opt->long_name);
		} else {
			return error(_("option `%s' is incompatible with --no-merged"),
				     opt->long_name);
		}
	}

	rf->merge = no_merged
		? REF_FILTER_MERGED_OMIT
		: REF_FILTER_MERGED_INCLUDE;

	if (get_oid(arg, &oid))
		die(_("malformed object name %s"), arg);

	rf->merge_commit = lookup_commit_reference_gently(the_repository,
							  &oid, 0);
	if (!rf->merge_commit)
		return error(_("option `%s' must point to a commit"), opt->long_name);

	return 0;
}
