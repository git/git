#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "object.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "quote.h"
#include "parse-options.h"
#include "remote.h"

/* Quoting styles */
#define QUOTE_NONE 0
#define QUOTE_SHELL 1
#define QUOTE_PERL 2
#define QUOTE_PYTHON 4
#define QUOTE_TCL 8

typedef enum { FIELD_STR, FIELD_ULONG, FIELD_TIME } cmp_type;

struct atom_value {
	const char *s;
	unsigned long ul; /* used for sorting when not FIELD_STR */
};

struct ref_sort {
	struct ref_sort *next;
	int atom; /* index into used_atom array */
	unsigned reverse : 1;
};

struct refinfo {
	char *refname;
	unsigned char objectname[20];
	int flag;
	const char *symref;
	struct atom_value *value;
};

static struct {
	const char *name;
	cmp_type cmp_type;
} valid_atom[] = {
	{ "refname" },
	{ "objecttype" },
	{ "objectsize", FIELD_ULONG },
	{ "objectname" },
	{ "tree" },
	{ "parent" },
	{ "numparent", FIELD_ULONG },
	{ "object" },
	{ "type" },
	{ "tag" },
	{ "author" },
	{ "authorname" },
	{ "authoremail" },
	{ "authordate", FIELD_TIME },
	{ "committer" },
	{ "committername" },
	{ "committeremail" },
	{ "committerdate", FIELD_TIME },
	{ "tagger" },
	{ "taggername" },
	{ "taggeremail" },
	{ "taggerdate", FIELD_TIME },
	{ "creator" },
	{ "creatordate", FIELD_TIME },
	{ "subject" },
	{ "body" },
	{ "contents" },
	{ "contents:subject" },
	{ "contents:body" },
	{ "contents:signature" },
	{ "upstream" },
	{ "symref" },
	{ "flag" },
};

/*
 * An atom is a valid field atom listed above, possibly prefixed with
 * a "*" to denote deref_tag().
 *
 * We parse given format string and sort specifiers, and make a list
 * of properties that we need to extract out of objects.  refinfo
 * structure will hold an array of values extracted that can be
 * indexed with the "atom number", which is an index into this
 * array.
 */
static const char **used_atom;
static cmp_type *used_atom_type;
static int used_atom_cnt, sort_atom_limit, need_tagged, need_symref;

/*
 * Used to parse format string and sort specifiers
 */
static int parse_atom(const char *atom, const char *ep)
{
	const char *sp;
	int i, at;

	sp = atom;
	if (*sp == '*' && sp < ep)
		sp++; /* deref */
	if (ep <= sp)
		die("malformed field name: %.*s", (int)(ep-atom), atom);

	/* Do we have the atom already used elsewhere? */
	for (i = 0; i < used_atom_cnt; i++) {
		int len = strlen(used_atom[i]);
		if (len == ep - atom && !memcmp(used_atom[i], atom, len))
			return i;
	}

	/* Is the atom a valid one? */
	for (i = 0; i < ARRAY_SIZE(valid_atom); i++) {
		int len = strlen(valid_atom[i].name);
		/*
		 * If the atom name has a colon, strip it and everything after
		 * it off - it specifies the format for this entry, and
		 * shouldn't be used for checking against the valid_atom
		 * table.
		 */
		const char *formatp = strchr(sp, ':');
		if (!formatp || ep < formatp)
			formatp = ep;
		if (len == formatp - sp && !memcmp(valid_atom[i].name, sp, len))
			break;
	}

	if (ARRAY_SIZE(valid_atom) <= i)
		die("unknown field name: %.*s", (int)(ep-atom), atom);

	/* Add it in, including the deref prefix */
	at = used_atom_cnt;
	used_atom_cnt++;
	used_atom = xrealloc(used_atom,
			     (sizeof *used_atom) * used_atom_cnt);
	used_atom_type = xrealloc(used_atom_type,
				  (sizeof(*used_atom_type) * used_atom_cnt));
	used_atom[at] = xmemdupz(atom, ep - atom);
	used_atom_type[at] = valid_atom[i].cmp_type;
	if (*atom == '*')
		need_tagged = 1;
	if (!strcmp(used_atom[at], "symref"))
		need_symref = 1;
	return at;
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
static int verify_format(const char *format)
{
	const char *cp, *sp;
	for (cp = format; *cp && (sp = find_next(cp)); ) {
		const char *ep = strchr(sp, ')');
		if (!ep)
			return error("malformed format string %s", sp);
		/* sp points at "%(" and ep points at the closing ")" */
		parse_atom(sp + 2, ep);
		cp = ep + 1;
	}
	return 0;
}

/*
 * Given an object name, read the object data and size, and return a
 * "struct object".  If the object data we are returning is also borrowed
 * by the "struct object" representation, set *eaten as well---it is a
 * signal from parse_object_buffer to us not to free the buffer.
 */
static void *get_obj(const unsigned char *sha1, struct object **obj, unsigned long *sz, int *eaten)
{
	enum object_type type;
	void *buf = read_sha1_file(sha1, &type, sz);

	if (buf)
		*obj = parse_object_buffer(sha1, type, *sz, buf, eaten);
	else
		*obj = NULL;
	return buf;
}

/* See grab_values */
static void grab_common_values(struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	int i;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "objecttype"))
			v->s = typename(obj->type);
		else if (!strcmp(name, "objectsize")) {
			char *s = xmalloc(40);
			sprintf(s, "%lu", sz);
			v->ul = sz;
			v->s = s;
		}
		else if (!strcmp(name, "objectname")) {
			char *s = xmalloc(41);
			strcpy(s, sha1_to_hex(obj->sha1));
			v->s = s;
		}
		else if (!strcmp(name, "objectname:short")) {
			v->s = xstrdup(find_unique_abbrev(obj->sha1,
							  DEFAULT_ABBREV));
		}
	}
}

/* See grab_values */
static void grab_tag_values(struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	int i;
	struct tag *tag = (struct tag *) obj;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "tag"))
			v->s = tag->tag;
		else if (!strcmp(name, "type") && tag->tagged)
			v->s = typename(tag->tagged->type);
		else if (!strcmp(name, "object") && tag->tagged) {
			char *s = xmalloc(41);
			strcpy(s, sha1_to_hex(tag->tagged->sha1));
			v->s = s;
		}
	}
}

static int num_parents(struct commit *commit)
{
	struct commit_list *parents;
	int i;

	for (i = 0, parents = commit->parents;
	     parents;
	     parents = parents->next)
		i++;
	return i;
}

/* See grab_values */
static void grab_commit_values(struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	int i;
	struct commit *commit = (struct commit *) obj;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (!strcmp(name, "tree")) {
			char *s = xmalloc(41);
			strcpy(s, sha1_to_hex(commit->tree->object.sha1));
			v->s = s;
		}
		if (!strcmp(name, "numparent")) {
			char *s = xmalloc(40);
			v->ul = num_parents(commit);
			sprintf(s, "%lu", v->ul);
			v->s = s;
		}
		else if (!strcmp(name, "parent")) {
			int num = num_parents(commit);
			int i;
			struct commit_list *parents;
			char *s = xmalloc(41 * num + 1);
			v->s = s;
			for (i = 0, parents = commit->parents;
			     parents;
			     parents = parents->next, i = i + 41) {
				struct commit *parent = parents->item;
				strcpy(s+i, sha1_to_hex(parent->object.sha1));
				if (parents->next)
					s[i+40] = ' ';
			}
			if (!i)
				*s = '\0';
		}
	}
}

static const char *find_wholine(const char *who, int wholen, const char *buf, unsigned long sz)
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
	return "";
}

static const char *copy_email(const char *buf)
{
	const char *email = strchr(buf, '<');
	const char *eoemail;
	if (!email)
		return "";
	eoemail = strchr(email, '>');
	if (!eoemail)
		return "";
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
	unsigned long timestamp;
	long tz;
	enum date_mode date_mode = DATE_NORMAL;
	const char *formatp;

	/*
	 * We got here because atomname ends in "date" or "date<something>";
	 * it's not possible that <something> is not ":<format>" because
	 * parse_atom() wouldn't have allowed it, so we can assume that no
	 * ":" means no format is specified, and use the default.
	 */
	formatp = strchr(atomname, ':');
	if (formatp != NULL) {
		formatp++;
		date_mode = parse_date_format(formatp);
	}

	if (!eoemail)
		goto bad;
	timestamp = strtoul(eoemail + 2, &zone, 10);
	if (timestamp == ULONG_MAX)
		goto bad;
	tz = strtol(zone, NULL, 10);
	if ((tz == LONG_MIN || tz == LONG_MAX) && errno == ERANGE)
		goto bad;
	v->s = xstrdup(show_date(timestamp, tz, date_mode));
	v->ul = timestamp;
	return;
 bad:
	v->s = "";
	v->ul = 0;
}

/* See grab_values */
static void grab_person(const char *who, struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	int i;
	int wholen = strlen(who);
	const char *wholine = NULL;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
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
		    prefixcmp(name + wholen, "date"))
			continue;
		if (!wholine)
			wholine = find_wholine(who, wholen, buf, sz);
		if (!wholine)
			return; /* no point looking for it */
		if (name[wholen] == 0)
			v->s = copy_line(wholine);
		else if (!strcmp(name + wholen, "name"))
			v->s = copy_name(wholine);
		else if (!strcmp(name + wholen, "email"))
			v->s = copy_email(wholine);
		else if (!prefixcmp(name + wholen, "date"))
			grab_date(wholine, v, name);
	}

	/*
	 * For a tag or a commit object, if "creator" or "creatordate" is
	 * requested, do something special.
	 */
	if (strcmp(who, "tagger") && strcmp(who, "committer"))
		return; /* "author" for commit object is not wanted */
	if (!wholine)
		wholine = find_wholine(who, wholen, buf, sz);
	if (!wholine)
		return;
	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;

		if (!prefixcmp(name, "creatordate"))
			grab_date(wholine, v, name);
		else if (!strcmp(name, "creator"))
			v->s = copy_line(wholine);
	}
}

static void find_subpos(const char *buf, unsigned long sz,
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

/* See grab_values */
static void grab_sub_body_contents(struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	int i;
	const char *subpos = NULL, *bodypos = NULL, *sigpos = NULL;
	unsigned long sublen = 0, bodylen = 0, nonsiglen = 0, siglen = 0;

	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &val[i];
		if (!!deref != (*name == '*'))
			continue;
		if (deref)
			name++;
		if (strcmp(name, "subject") &&
		    strcmp(name, "body") &&
		    strcmp(name, "contents") &&
		    strcmp(name, "contents:subject") &&
		    strcmp(name, "contents:body") &&
		    strcmp(name, "contents:signature"))
			continue;
		if (!subpos)
			find_subpos(buf, sz,
				    &subpos, &sublen,
				    &bodypos, &bodylen, &nonsiglen,
				    &sigpos, &siglen);

		if (!strcmp(name, "subject"))
			v->s = copy_subject(subpos, sublen);
		else if (!strcmp(name, "contents:subject"))
			v->s = copy_subject(subpos, sublen);
		else if (!strcmp(name, "body"))
			v->s = xmemdupz(bodypos, bodylen);
		else if (!strcmp(name, "contents:body"))
			v->s = xmemdupz(bodypos, nonsiglen);
		else if (!strcmp(name, "contents:signature"))
			v->s = xmemdupz(sigpos, siglen);
		else if (!strcmp(name, "contents"))
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
			v->s = "";
	}
}

/*
 * val is a list of atom_value to hold returned values.  Extract
 * the values for atoms in used_atom array out of (obj, buf, sz).
 * when deref is false, (obj, buf, sz) is the object that is
 * pointed at by the ref itself; otherwise it is the object the
 * ref (which is a tag) refers to.
 */
static void grab_values(struct atom_value *val, int deref, struct object *obj, void *buf, unsigned long sz)
{
	grab_common_values(val, deref, obj, buf, sz);
	switch (obj->type) {
	case OBJ_TAG:
		grab_tag_values(val, deref, obj, buf, sz);
		grab_sub_body_contents(val, deref, obj, buf, sz);
		grab_person("tagger", val, deref, obj, buf, sz);
		break;
	case OBJ_COMMIT:
		grab_commit_values(val, deref, obj, buf, sz);
		grab_sub_body_contents(val, deref, obj, buf, sz);
		grab_person("author", val, deref, obj, buf, sz);
		grab_person("committer", val, deref, obj, buf, sz);
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

/*
 * Parse the object referred by ref, and grab needed value.
 */
static void populate_value(struct refinfo *ref)
{
	void *buf;
	struct object *obj;
	int eaten, i;
	unsigned long size;
	const unsigned char *tagged;

	ref->value = xcalloc(sizeof(struct atom_value), used_atom_cnt);

	if (need_symref && (ref->flag & REF_ISSYMREF) && !ref->symref) {
		unsigned char unused1[20];
		const char *symref;
		symref = resolve_ref(ref->refname, unused1, 1, NULL);
		if (symref)
			ref->symref = xstrdup(symref);
		else
			ref->symref = "";
	}

	/* Fill in specials first */
	for (i = 0; i < used_atom_cnt; i++) {
		const char *name = used_atom[i];
		struct atom_value *v = &ref->value[i];
		int deref = 0;
		const char *refname;
		const char *formatp;

		if (*name == '*') {
			deref = 1;
			name++;
		}

		if (!prefixcmp(name, "refname"))
			refname = ref->refname;
		else if (!prefixcmp(name, "symref"))
			refname = ref->symref ? ref->symref : "";
		else if (!prefixcmp(name, "upstream")) {
			struct branch *branch;
			/* only local branches may have an upstream */
			if (prefixcmp(ref->refname, "refs/heads/"))
				continue;
			branch = branch_get(ref->refname + 11);

			if (!branch || !branch->merge || !branch->merge[0] ||
			    !branch->merge[0]->dst)
				continue;
			refname = branch->merge[0]->dst;
		}
		else if (!strcmp(name, "flag")) {
			char buf[256], *cp = buf;
			if (ref->flag & REF_ISSYMREF)
				cp = copy_advance(cp, ",symref");
			if (ref->flag & REF_ISPACKED)
				cp = copy_advance(cp, ",packed");
			if (cp == buf)
				v->s = "";
			else {
				*cp = '\0';
				v->s = xstrdup(buf + 1);
			}
			continue;
		}
		else
			continue;

		formatp = strchr(name, ':');
		/* look for "short" refname format */
		if (formatp) {
			formatp++;
			if (!strcmp(formatp, "short"))
				refname = shorten_unambiguous_ref(refname,
						      warn_ambiguous_refs);
			else
				die("unknown %.*s format %s",
				    (int)(formatp - name), name, formatp);
		}

		if (!deref)
			v->s = refname;
		else {
			int len = strlen(refname);
			char *s = xmalloc(len + 4);
			sprintf(s, "%s^{}", refname);
			v->s = s;
		}
	}

	for (i = 0; i < used_atom_cnt; i++) {
		struct atom_value *v = &ref->value[i];
		if (v->s == NULL)
			goto need_obj;
	}
	return;

 need_obj:
	buf = get_obj(ref->objectname, &obj, &size, &eaten);
	if (!buf)
		die("missing object %s for %s",
		    sha1_to_hex(ref->objectname), ref->refname);
	if (!obj)
		die("parse_object_buffer failed on %s for %s",
		    sha1_to_hex(ref->objectname), ref->refname);

	grab_values(ref->value, 0, obj, buf, size);
	if (!eaten)
		free(buf);

	/*
	 * If there is no atom that wants to know about tagged
	 * object, we are done.
	 */
	if (!need_tagged || (obj->type != OBJ_TAG))
		return;

	/*
	 * If it is a tag object, see if we use a value that derefs
	 * the object, and if we do grab the object it refers to.
	 */
	tagged = ((struct tag *)obj)->tagged->sha1;

	/*
	 * NEEDSWORK: This derefs tag only once, which
	 * is good to deal with chains of trust, but
	 * is not consistent with what deref_tag() does
	 * which peels the onion to the core.
	 */
	buf = get_obj(tagged, &obj, &size, &eaten);
	if (!buf)
		die("missing object %s for %s",
		    sha1_to_hex(tagged), ref->refname);
	if (!obj)
		die("parse_object_buffer failed on %s for %s",
		    sha1_to_hex(tagged), ref->refname);
	grab_values(ref->value, 1, obj, buf, size);
	if (!eaten)
		free(buf);
}

/*
 * Given a ref, return the value for the atom.  This lazily gets value
 * out of the object by calling populate value.
 */
static void get_value(struct refinfo *ref, int atom, struct atom_value **v)
{
	if (!ref->value) {
		populate_value(ref);
		fill_missing_values(ref->value);
	}
	*v = &ref->value[atom];
}

struct grab_ref_cbdata {
	struct refinfo **grab_array;
	const char **grab_pattern;
	int grab_cnt;
};

/*
 * A call-back given to for_each_ref().  Filter refs and keep them for
 * later object processing.
 */
static int grab_single_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct grab_ref_cbdata *cb = cb_data;
	struct refinfo *ref;
	int cnt;

	if (*cb->grab_pattern) {
		const char **pattern;
		int namelen = strlen(refname);
		for (pattern = cb->grab_pattern; *pattern; pattern++) {
			const char *p = *pattern;
			int plen = strlen(p);

			if ((plen <= namelen) &&
			    !strncmp(refname, p, plen) &&
			    (refname[plen] == '\0' ||
			     refname[plen] == '/' ||
			     p[plen-1] == '/'))
				break;
			if (!fnmatch(p, refname, FNM_PATHNAME))
				break;
		}
		if (!*pattern)
			return 0;
	}

	/*
	 * We do not open the object yet; sort may only need refname
	 * to do its job and the resulting list may yet to be pruned
	 * by maxcount logic.
	 */
	ref = xcalloc(1, sizeof(*ref));
	ref->refname = xstrdup(refname);
	hashcpy(ref->objectname, sha1);
	ref->flag = flag;

	cnt = cb->grab_cnt;
	cb->grab_array = xrealloc(cb->grab_array,
				  sizeof(*cb->grab_array) * (cnt + 1));
	cb->grab_array[cnt++] = ref;
	cb->grab_cnt = cnt;
	return 0;
}

static int cmp_ref_sort(struct ref_sort *s, struct refinfo *a, struct refinfo *b)
{
	struct atom_value *va, *vb;
	int cmp;
	cmp_type cmp_type = used_atom_type[s->atom];

	get_value(a, s->atom, &va);
	get_value(b, s->atom, &vb);
	switch (cmp_type) {
	case FIELD_STR:
		cmp = strcmp(va->s, vb->s);
		break;
	default:
		if (va->ul < vb->ul)
			cmp = -1;
		else if (va->ul == vb->ul)
			cmp = 0;
		else
			cmp = 1;
		break;
	}
	return (s->reverse) ? -cmp : cmp;
}

static struct ref_sort *ref_sort;
static int compare_refs(const void *a_, const void *b_)
{
	struct refinfo *a = *((struct refinfo **)a_);
	struct refinfo *b = *((struct refinfo **)b_);
	struct ref_sort *s;

	for (s = ref_sort; s; s = s->next) {
		int cmp = cmp_ref_sort(s, a, b);
		if (cmp)
			return cmp;
	}
	return 0;
}

static void sort_refs(struct ref_sort *sort, struct refinfo **refs, int num_refs)
{
	ref_sort = sort;
	qsort(refs, num_refs, sizeof(struct refinfo *), compare_refs);
}

static void print_value(struct refinfo *ref, int atom, int quote_style)
{
	struct atom_value *v;
	get_value(ref, atom, &v);
	switch (quote_style) {
	case QUOTE_NONE:
		fputs(v->s, stdout);
		break;
	case QUOTE_SHELL:
		sq_quote_print(stdout, v->s);
		break;
	case QUOTE_PERL:
		perl_quote_print(stdout, v->s);
		break;
	case QUOTE_PYTHON:
		python_quote_print(stdout, v->s);
		break;
	case QUOTE_TCL:
		tcl_quote_print(stdout, v->s);
		break;
	}
}

static int hex1(char ch)
{
	if ('0' <= ch && ch <= '9')
		return ch - '0';
	else if ('a' <= ch && ch <= 'f')
		return ch - 'a' + 10;
	else if ('A' <= ch && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}
static int hex2(const char *cp)
{
	if (cp[0] && cp[1])
		return (hex1(cp[0]) << 4) | hex1(cp[1]);
	else
		return -1;
}

static void emit(const char *cp, const char *ep)
{
	while (*cp && (!ep || cp < ep)) {
		if (*cp == '%') {
			if (cp[1] == '%')
				cp++;
			else {
				int ch = hex2(cp + 1);
				if (0 <= ch) {
					putchar(ch);
					cp += 3;
					continue;
				}
			}
		}
		putchar(*cp);
		cp++;
	}
}

static void show_ref(struct refinfo *info, const char *format, int quote_style)
{
	const char *cp, *sp, *ep;

	for (cp = format; *cp && (sp = find_next(cp)); cp = ep + 1) {
		ep = strchr(sp, ')');
		if (cp < sp)
			emit(cp, sp);
		print_value(info, parse_atom(sp + 2, ep), quote_style);
	}
	if (*cp) {
		sp = cp + strlen(cp);
		emit(cp, sp);
	}
	putchar('\n');
}

static struct ref_sort *default_sort(void)
{
	static const char cstr_name[] = "refname";

	struct ref_sort *sort = xcalloc(1, sizeof(*sort));

	sort->next = NULL;
	sort->atom = parse_atom(cstr_name, cstr_name + strlen(cstr_name));
	return sort;
}

static int opt_parse_sort(const struct option *opt, const char *arg, int unset)
{
	struct ref_sort **sort_tail = opt->value;
	struct ref_sort *s;
	int len;

	if (!arg) /* should --no-sort void the list ? */
		return -1;

	*sort_tail = s = xcalloc(1, sizeof(*s));

	if (*arg == '-') {
		s->reverse = 1;
		arg++;
	}
	len = strlen(arg);
	s->atom = parse_atom(arg, arg+len);
	return 0;
}

static char const * const for_each_ref_usage[] = {
	"git for-each-ref [options] [<pattern>]",
	NULL
};

int cmd_for_each_ref(int argc, const char **argv, const char *prefix)
{
	int i, num_refs;
	const char *format = "%(objectname) %(objecttype)\t%(refname)";
	struct ref_sort *sort = NULL, **sort_tail = &sort;
	int maxcount = 0, quote_style = 0;
	struct refinfo **refs;
	struct grab_ref_cbdata cbdata;

	struct option opts[] = {
		OPT_BIT('s', "shell", &quote_style,
		        "quote placeholders suitably for shells", QUOTE_SHELL),
		OPT_BIT('p', "perl",  &quote_style,
		        "quote placeholders suitably for perl", QUOTE_PERL),
		OPT_BIT(0 , "python", &quote_style,
		        "quote placeholders suitably for python", QUOTE_PYTHON),
		OPT_BIT(0 , "tcl",  &quote_style,
		        "quote placeholders suitably for tcl", QUOTE_TCL),

		OPT_GROUP(""),
		OPT_INTEGER( 0 , "count", &maxcount, "show only <n> matched refs"),
		OPT_STRING(  0 , "format", &format, "format", "format to use for the output"),
		OPT_CALLBACK(0 , "sort", sort_tail, "key",
		            "field name to sort on", &opt_parse_sort),
		OPT_END(),
	};

	parse_options(argc, argv, prefix, opts, for_each_ref_usage, 0);
	if (maxcount < 0) {
		error("invalid --count argument: `%d'", maxcount);
		usage_with_options(for_each_ref_usage, opts);
	}
	if (HAS_MULTI_BITS(quote_style)) {
		error("more than one quoting style?");
		usage_with_options(for_each_ref_usage, opts);
	}
	if (verify_format(format))
		usage_with_options(for_each_ref_usage, opts);

	if (!sort)
		sort = default_sort();
	sort_atom_limit = used_atom_cnt;

	/* for warn_ambiguous_refs */
	git_config(git_default_config, NULL);

	memset(&cbdata, 0, sizeof(cbdata));
	cbdata.grab_pattern = argv;
	for_each_rawref(grab_single_ref, &cbdata);
	refs = cbdata.grab_array;
	num_refs = cbdata.grab_cnt;

	sort_refs(sort, refs, num_refs);

	if (!maxcount || num_refs < maxcount)
		maxcount = num_refs;
	for (i = 0; i < maxcount; i++)
		show_ref(refs[i], format, quote_style);
	return 0;
}
