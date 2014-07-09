#include "cache.h"
#include "remote.h"
#include "refs.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "dir.h"
#include "tag.h"
#include "string-list.h"
#include "mergesort.h"

enum map_direction { FROM_SRC, FROM_DST };

static struct refspec s_tag_refspec = {
	0,
	1,
	0,
	0,
	"refs/tags/*",
	"refs/tags/*"
};

const struct refspec *tag_refspec = &s_tag_refspec;

struct counted_string {
	size_t len;
	const char *s;
};
struct rewrite {
	const char *base;
	size_t baselen;
	struct counted_string *instead_of;
	int instead_of_nr;
	int instead_of_alloc;
};
struct rewrites {
	struct rewrite **rewrite;
	int rewrite_alloc;
	int rewrite_nr;
};

static struct remote **remotes;
static int remotes_alloc;
static int remotes_nr;

static struct branch **branches;
static int branches_alloc;
static int branches_nr;

static struct branch *current_branch;
static const char *default_remote_name;
static const char *branch_pushremote_name;
static const char *pushremote_name;
static int explicit_default_remote_name;

static struct rewrites rewrites;
static struct rewrites rewrites_push;

#define BUF_SIZE (2048)
static char buffer[BUF_SIZE];

static int valid_remote(const struct remote *remote)
{
	return (!!remote->url) || (!!remote->foreign_vcs);
}

static const char *alias_url(const char *url, struct rewrites *r)
{
	int i, j;
	char *ret;
	struct counted_string *longest;
	int longest_i;

	longest = NULL;
	longest_i = -1;
	for (i = 0; i < r->rewrite_nr; i++) {
		if (!r->rewrite[i])
			continue;
		for (j = 0; j < r->rewrite[i]->instead_of_nr; j++) {
			if (starts_with(url, r->rewrite[i]->instead_of[j].s) &&
			    (!longest ||
			     longest->len < r->rewrite[i]->instead_of[j].len)) {
				longest = &(r->rewrite[i]->instead_of[j]);
				longest_i = i;
			}
		}
	}
	if (!longest)
		return url;

	ret = xmalloc(r->rewrite[longest_i]->baselen +
		     (strlen(url) - longest->len) + 1);
	strcpy(ret, r->rewrite[longest_i]->base);
	strcpy(ret + r->rewrite[longest_i]->baselen, url + longest->len);
	return ret;
}

static void add_push_refspec(struct remote *remote, const char *ref)
{
	ALLOC_GROW(remote->push_refspec,
		   remote->push_refspec_nr + 1,
		   remote->push_refspec_alloc);
	remote->push_refspec[remote->push_refspec_nr++] = ref;
}

static void add_fetch_refspec(struct remote *remote, const char *ref)
{
	ALLOC_GROW(remote->fetch_refspec,
		   remote->fetch_refspec_nr + 1,
		   remote->fetch_refspec_alloc);
	remote->fetch_refspec[remote->fetch_refspec_nr++] = ref;
}

static void add_url(struct remote *remote, const char *url)
{
	ALLOC_GROW(remote->url, remote->url_nr + 1, remote->url_alloc);
	remote->url[remote->url_nr++] = url;
}

static void add_pushurl(struct remote *remote, const char *pushurl)
{
	ALLOC_GROW(remote->pushurl, remote->pushurl_nr + 1, remote->pushurl_alloc);
	remote->pushurl[remote->pushurl_nr++] = pushurl;
}

static void add_pushurl_alias(struct remote *remote, const char *url)
{
	const char *pushurl = alias_url(url, &rewrites_push);
	if (pushurl != url)
		add_pushurl(remote, pushurl);
}

static void add_url_alias(struct remote *remote, const char *url)
{
	add_url(remote, alias_url(url, &rewrites));
	add_pushurl_alias(remote, url);
}

static struct remote *make_remote(const char *name, int len)
{
	struct remote *ret;
	int i;

	for (i = 0; i < remotes_nr; i++) {
		if (len ? (!strncmp(name, remotes[i]->name, len) &&
			   !remotes[i]->name[len]) :
		    !strcmp(name, remotes[i]->name))
			return remotes[i];
	}

	ret = xcalloc(1, sizeof(struct remote));
	ret->prune = -1;  /* unspecified */
	ALLOC_GROW(remotes, remotes_nr + 1, remotes_alloc);
	remotes[remotes_nr++] = ret;
	if (len)
		ret->name = xstrndup(name, len);
	else
		ret->name = xstrdup(name);
	return ret;
}

static void add_merge(struct branch *branch, const char *name)
{
	ALLOC_GROW(branch->merge_name, branch->merge_nr + 1,
		   branch->merge_alloc);
	branch->merge_name[branch->merge_nr++] = name;
}

static struct branch *make_branch(const char *name, int len)
{
	struct branch *ret;
	int i;

	for (i = 0; i < branches_nr; i++) {
		if (len ? (!strncmp(name, branches[i]->name, len) &&
			   !branches[i]->name[len]) :
		    !strcmp(name, branches[i]->name))
			return branches[i];
	}

	ALLOC_GROW(branches, branches_nr + 1, branches_alloc);
	ret = xcalloc(1, sizeof(struct branch));
	branches[branches_nr++] = ret;
	if (len)
		ret->name = xstrndup(name, len);
	else
		ret->name = xstrdup(name);
	ret->refname = xstrfmt("refs/heads/%s", ret->name);

	return ret;
}

static struct rewrite *make_rewrite(struct rewrites *r, const char *base, int len)
{
	struct rewrite *ret;
	int i;

	for (i = 0; i < r->rewrite_nr; i++) {
		if (len
		    ? (len == r->rewrite[i]->baselen &&
		       !strncmp(base, r->rewrite[i]->base, len))
		    : !strcmp(base, r->rewrite[i]->base))
			return r->rewrite[i];
	}

	ALLOC_GROW(r->rewrite, r->rewrite_nr + 1, r->rewrite_alloc);
	ret = xcalloc(1, sizeof(struct rewrite));
	r->rewrite[r->rewrite_nr++] = ret;
	if (len) {
		ret->base = xstrndup(base, len);
		ret->baselen = len;
	}
	else {
		ret->base = xstrdup(base);
		ret->baselen = strlen(base);
	}
	return ret;
}

static void add_instead_of(struct rewrite *rewrite, const char *instead_of)
{
	ALLOC_GROW(rewrite->instead_of, rewrite->instead_of_nr + 1, rewrite->instead_of_alloc);
	rewrite->instead_of[rewrite->instead_of_nr].s = instead_of;
	rewrite->instead_of[rewrite->instead_of_nr].len = strlen(instead_of);
	rewrite->instead_of_nr++;
}

static void read_remotes_file(struct remote *remote)
{
	FILE *f = fopen(git_path("remotes/%s", remote->name), "r");

	if (!f)
		return;
	remote->origin = REMOTE_REMOTES;
	while (fgets(buffer, BUF_SIZE, f)) {
		int value_list;
		char *s, *p;

		if (starts_with(buffer, "URL:")) {
			value_list = 0;
			s = buffer + 4;
		} else if (starts_with(buffer, "Push:")) {
			value_list = 1;
			s = buffer + 5;
		} else if (starts_with(buffer, "Pull:")) {
			value_list = 2;
			s = buffer + 5;
		} else
			continue;

		while (isspace(*s))
			s++;
		if (!*s)
			continue;

		p = s + strlen(s);
		while (isspace(p[-1]))
			*--p = 0;

		switch (value_list) {
		case 0:
			add_url_alias(remote, xstrdup(s));
			break;
		case 1:
			add_push_refspec(remote, xstrdup(s));
			break;
		case 2:
			add_fetch_refspec(remote, xstrdup(s));
			break;
		}
	}
	fclose(f);
}

static void read_branches_file(struct remote *remote)
{
	char *frag;
	struct strbuf branch = STRBUF_INIT;
	int n = 1000;
	FILE *f = fopen(git_path("branches/%.*s", n, remote->name), "r");
	char *s, *p;
	int len;

	if (!f)
		return;
	s = fgets(buffer, BUF_SIZE, f);
	fclose(f);
	if (!s)
		return;
	while (isspace(*s))
		s++;
	if (!*s)
		return;
	remote->origin = REMOTE_BRANCHES;
	p = s + strlen(s);
	while (isspace(p[-1]))
		*--p = 0;
	len = p - s;
	p = xmalloc(len + 1);
	strcpy(p, s);

	/*
	 * The branches file would have URL and optionally
	 * #branch specified.  The "master" (or specified) branch is
	 * fetched and stored in the local branch of the same name.
	 */
	frag = strchr(p, '#');
	if (frag) {
		*(frag++) = '\0';
		strbuf_addf(&branch, "refs/heads/%s", frag);
	} else
		strbuf_addstr(&branch, "refs/heads/master");

	strbuf_addf(&branch, ":refs/heads/%s", remote->name);
	add_url_alias(remote, p);
	add_fetch_refspec(remote, strbuf_detach(&branch, NULL));
	/*
	 * Cogito compatible push: push current HEAD to remote #branch
	 * (master if missing)
	 */
	strbuf_init(&branch, 0);
	strbuf_addstr(&branch, "HEAD");
	if (frag)
		strbuf_addf(&branch, ":refs/heads/%s", frag);
	else
		strbuf_addstr(&branch, ":refs/heads/master");
	add_push_refspec(remote, strbuf_detach(&branch, NULL));
	remote->fetch_tags = 1; /* always auto-follow */
}

static int handle_config(const char *key, const char *value, void *cb)
{
	const char *name;
	const char *subkey;
	struct remote *remote;
	struct branch *branch;
	if (starts_with(key, "branch.")) {
		name = key + 7;
		subkey = strrchr(name, '.');
		if (!subkey)
			return 0;
		branch = make_branch(name, subkey - name);
		if (!strcmp(subkey, ".remote")) {
			if (git_config_string(&branch->remote_name, key, value))
				return -1;
			if (branch == current_branch) {
				default_remote_name = branch->remote_name;
				explicit_default_remote_name = 1;
			}
		} else if (!strcmp(subkey, ".pushremote")) {
			if (branch == current_branch)
				if (git_config_string(&branch_pushremote_name, key, value))
					return -1;
		} else if (!strcmp(subkey, ".merge")) {
			if (!value)
				return config_error_nonbool(key);
			add_merge(branch, xstrdup(value));
		}
		return 0;
	}
	if (starts_with(key, "url.")) {
		struct rewrite *rewrite;
		name = key + 4;
		subkey = strrchr(name, '.');
		if (!subkey)
			return 0;
		if (!strcmp(subkey, ".insteadof")) {
			rewrite = make_rewrite(&rewrites, name, subkey - name);
			if (!value)
				return config_error_nonbool(key);
			add_instead_of(rewrite, xstrdup(value));
		} else if (!strcmp(subkey, ".pushinsteadof")) {
			rewrite = make_rewrite(&rewrites_push, name, subkey - name);
			if (!value)
				return config_error_nonbool(key);
			add_instead_of(rewrite, xstrdup(value));
		}
	}

	if (!starts_with(key,  "remote."))
		return 0;
	name = key + 7;

	/* Handle remote.* variables */
	if (!strcmp(name, "pushdefault"))
		return git_config_string(&pushremote_name, key, value);

	/* Handle remote.<name>.* variables */
	if (*name == '/') {
		warning("Config remote shorthand cannot begin with '/': %s",
			name);
		return 0;
	}
	subkey = strrchr(name, '.');
	if (!subkey)
		return 0;
	remote = make_remote(name, subkey - name);
	remote->origin = REMOTE_CONFIG;
	if (!strcmp(subkey, ".mirror"))
		remote->mirror = git_config_bool(key, value);
	else if (!strcmp(subkey, ".skipdefaultupdate"))
		remote->skip_default_update = git_config_bool(key, value);
	else if (!strcmp(subkey, ".skipfetchall"))
		remote->skip_default_update = git_config_bool(key, value);
	else if (!strcmp(subkey, ".prune"))
		remote->prune = git_config_bool(key, value);
	else if (!strcmp(subkey, ".url")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		add_url(remote, v);
	} else if (!strcmp(subkey, ".pushurl")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		add_pushurl(remote, v);
	} else if (!strcmp(subkey, ".push")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		add_push_refspec(remote, v);
	} else if (!strcmp(subkey, ".fetch")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		add_fetch_refspec(remote, v);
	} else if (!strcmp(subkey, ".receivepack")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		if (!remote->receivepack)
			remote->receivepack = v;
		else
			error("more than one receivepack given, using the first");
	} else if (!strcmp(subkey, ".uploadpack")) {
		const char *v;
		if (git_config_string(&v, key, value))
			return -1;
		if (!remote->uploadpack)
			remote->uploadpack = v;
		else
			error("more than one uploadpack given, using the first");
	} else if (!strcmp(subkey, ".tagopt")) {
		if (!strcmp(value, "--no-tags"))
			remote->fetch_tags = -1;
		else if (!strcmp(value, "--tags"))
			remote->fetch_tags = 2;
	} else if (!strcmp(subkey, ".proxy")) {
		return git_config_string((const char **)&remote->http_proxy,
					 key, value);
	} else if (!strcmp(subkey, ".vcs")) {
		return git_config_string(&remote->foreign_vcs, key, value);
	}
	return 0;
}

static void alias_all_urls(void)
{
	int i, j;
	for (i = 0; i < remotes_nr; i++) {
		int add_pushurl_aliases;
		if (!remotes[i])
			continue;
		for (j = 0; j < remotes[i]->pushurl_nr; j++) {
			remotes[i]->pushurl[j] = alias_url(remotes[i]->pushurl[j], &rewrites);
		}
		add_pushurl_aliases = remotes[i]->pushurl_nr == 0;
		for (j = 0; j < remotes[i]->url_nr; j++) {
			if (add_pushurl_aliases)
				add_pushurl_alias(remotes[i], remotes[i]->url[j]);
			remotes[i]->url[j] = alias_url(remotes[i]->url[j], &rewrites);
		}
	}
}

static void read_config(void)
{
	unsigned char sha1[20];
	const char *head_ref;
	int flag;
	if (default_remote_name) /* did this already */
		return;
	default_remote_name = "origin";
	current_branch = NULL;
	head_ref = resolve_ref_unsafe("HEAD", sha1, 0, &flag);
	if (head_ref && (flag & REF_ISSYMREF) &&
	    skip_prefix(head_ref, "refs/heads/", &head_ref)) {
		current_branch = make_branch(head_ref, 0);
	}
	git_config(handle_config, NULL);
	if (branch_pushremote_name) {
		free((char *)pushremote_name);
		pushremote_name = branch_pushremote_name;
	}
	alias_all_urls();
}

/*
 * This function frees a refspec array.
 * Warning: code paths should be checked to ensure that the src
 *          and dst pointers are always freeable pointers as well
 *          as the refspec pointer itself.
 */
static void free_refspecs(struct refspec *refspec, int nr_refspec)
{
	int i;

	if (!refspec)
		return;

	for (i = 0; i < nr_refspec; i++) {
		free(refspec[i].src);
		free(refspec[i].dst);
	}
	free(refspec);
}

static struct refspec *parse_refspec_internal(int nr_refspec, const char **refspec, int fetch, int verify)
{
	int i;
	struct refspec *rs = xcalloc(nr_refspec, sizeof(*rs));

	for (i = 0; i < nr_refspec; i++) {
		size_t llen;
		int is_glob;
		const char *lhs, *rhs;
		int flags;

		is_glob = 0;

		lhs = refspec[i];
		if (*lhs == '+') {
			rs[i].force = 1;
			lhs++;
		}

		rhs = strrchr(lhs, ':');

		/*
		 * Before going on, special case ":" (or "+:") as a refspec
		 * for pushing matching refs.
		 */
		if (!fetch && rhs == lhs && rhs[1] == '\0') {
			rs[i].matching = 1;
			continue;
		}

		if (rhs) {
			size_t rlen = strlen(++rhs);
			is_glob = (1 <= rlen && strchr(rhs, '*'));
			rs[i].dst = xstrndup(rhs, rlen);
		}

		llen = (rhs ? (rhs - lhs - 1) : strlen(lhs));
		if (1 <= llen && memchr(lhs, '*', llen)) {
			if ((rhs && !is_glob) || (!rhs && fetch))
				goto invalid;
			is_glob = 1;
		} else if (rhs && is_glob) {
			goto invalid;
		}

		rs[i].pattern = is_glob;
		rs[i].src = xstrndup(lhs, llen);
		flags = REFNAME_ALLOW_ONELEVEL | (is_glob ? REFNAME_REFSPEC_PATTERN : 0);

		if (fetch) {
			unsigned char unused[40];

			/* LHS */
			if (!*rs[i].src)
				; /* empty is ok; it means "HEAD" */
			else if (llen == 40 && !get_sha1_hex(rs[i].src, unused))
				rs[i].exact_sha1 = 1; /* ok */
			else if (!check_refname_format(rs[i].src, flags))
				; /* valid looking ref is ok */
			else
				goto invalid;
			/* RHS */
			if (!rs[i].dst)
				; /* missing is ok; it is the same as empty */
			else if (!*rs[i].dst)
				; /* empty is ok; it means "do not store" */
			else if (!check_refname_format(rs[i].dst, flags))
				; /* valid looking ref is ok */
			else
				goto invalid;
		} else {
			/*
			 * LHS
			 * - empty is allowed; it means delete.
			 * - when wildcarded, it must be a valid looking ref.
			 * - otherwise, it must be an extended SHA-1, but
			 *   there is no existing way to validate this.
			 */
			if (!*rs[i].src)
				; /* empty is ok */
			else if (is_glob) {
				if (check_refname_format(rs[i].src, flags))
					goto invalid;
			}
			else
				; /* anything goes, for now */
			/*
			 * RHS
			 * - missing is allowed, but LHS then must be a
			 *   valid looking ref.
			 * - empty is not allowed.
			 * - otherwise it must be a valid looking ref.
			 */
			if (!rs[i].dst) {
				if (check_refname_format(rs[i].src, flags))
					goto invalid;
			} else if (!*rs[i].dst) {
				goto invalid;
			} else {
				if (check_refname_format(rs[i].dst, flags))
					goto invalid;
			}
		}
	}
	return rs;

 invalid:
	if (verify) {
		/*
		 * nr_refspec must be greater than zero and i must be valid
		 * since it is only possible to reach this point from within
		 * the for loop above.
		 */
		free_refspecs(rs, i+1);
		return NULL;
	}
	die("Invalid refspec '%s'", refspec[i]);
}

int valid_fetch_refspec(const char *fetch_refspec_str)
{
	struct refspec *refspec;

	refspec = parse_refspec_internal(1, &fetch_refspec_str, 1, 1);
	free_refspecs(refspec, 1);
	return !!refspec;
}

struct refspec *parse_fetch_refspec(int nr_refspec, const char **refspec)
{
	return parse_refspec_internal(nr_refspec, refspec, 1, 0);
}

static struct refspec *parse_push_refspec(int nr_refspec, const char **refspec)
{
	return parse_refspec_internal(nr_refspec, refspec, 0, 0);
}

void free_refspec(int nr_refspec, struct refspec *refspec)
{
	int i;
	for (i = 0; i < nr_refspec; i++) {
		free(refspec[i].src);
		free(refspec[i].dst);
	}
	free(refspec);
}

static int valid_remote_nick(const char *name)
{
	if (!name[0] || is_dot_or_dotdot(name))
		return 0;
	return !strchr(name, '/'); /* no slash */
}

static struct remote *remote_get_1(const char *name, const char *pushremote_name)
{
	struct remote *ret;
	int name_given = 0;

	if (name)
		name_given = 1;
	else {
		if (pushremote_name) {
			name = pushremote_name;
			name_given = 1;
		} else {
			name = default_remote_name;
			name_given = explicit_default_remote_name;
		}
	}

	ret = make_remote(name, 0);
	if (valid_remote_nick(name)) {
		if (!valid_remote(ret))
			read_remotes_file(ret);
		if (!valid_remote(ret))
			read_branches_file(ret);
	}
	if (name_given && !valid_remote(ret))
		add_url_alias(ret, name);
	if (!valid_remote(ret))
		return NULL;
	ret->fetch = parse_fetch_refspec(ret->fetch_refspec_nr, ret->fetch_refspec);
	ret->push = parse_push_refspec(ret->push_refspec_nr, ret->push_refspec);
	return ret;
}

struct remote *remote_get(const char *name)
{
	read_config();
	return remote_get_1(name, NULL);
}

struct remote *pushremote_get(const char *name)
{
	read_config();
	return remote_get_1(name, pushremote_name);
}

int remote_is_configured(const char *name)
{
	int i;
	read_config();

	for (i = 0; i < remotes_nr; i++)
		if (!strcmp(name, remotes[i]->name))
			return 1;
	return 0;
}

int for_each_remote(each_remote_fn fn, void *priv)
{
	int i, result = 0;
	read_config();
	for (i = 0; i < remotes_nr && !result; i++) {
		struct remote *r = remotes[i];
		if (!r)
			continue;
		if (!r->fetch)
			r->fetch = parse_fetch_refspec(r->fetch_refspec_nr,
						       r->fetch_refspec);
		if (!r->push)
			r->push = parse_push_refspec(r->push_refspec_nr,
						     r->push_refspec);
		result = fn(r, priv);
	}
	return result;
}

static void handle_duplicate(struct ref *ref1, struct ref *ref2)
{
	if (strcmp(ref1->name, ref2->name)) {
		if (ref1->fetch_head_status != FETCH_HEAD_IGNORE &&
		    ref2->fetch_head_status != FETCH_HEAD_IGNORE) {
			die(_("Cannot fetch both %s and %s to %s"),
			    ref1->name, ref2->name, ref2->peer_ref->name);
		} else if (ref1->fetch_head_status != FETCH_HEAD_IGNORE &&
			   ref2->fetch_head_status == FETCH_HEAD_IGNORE) {
			warning(_("%s usually tracks %s, not %s"),
				ref2->peer_ref->name, ref2->name, ref1->name);
		} else if (ref1->fetch_head_status == FETCH_HEAD_IGNORE &&
			   ref2->fetch_head_status == FETCH_HEAD_IGNORE) {
			die(_("%s tracks both %s and %s"),
			    ref2->peer_ref->name, ref1->name, ref2->name);
		} else {
			/*
			 * This last possibility doesn't occur because
			 * FETCH_HEAD_IGNORE entries always appear at
			 * the end of the list.
			 */
			die(_("Internal error"));
		}
	}
	free(ref2->peer_ref);
	free(ref2);
}

struct ref *ref_remove_duplicates(struct ref *ref_map)
{
	struct string_list refs = STRING_LIST_INIT_NODUP;
	struct ref *retval = NULL;
	struct ref **p = &retval;

	while (ref_map) {
		struct ref *ref = ref_map;

		ref_map = ref_map->next;
		ref->next = NULL;

		if (!ref->peer_ref) {
			*p = ref;
			p = &ref->next;
		} else {
			struct string_list_item *item =
				string_list_insert(&refs, ref->peer_ref->name);

			if (item->util) {
				/* Entry already existed */
				handle_duplicate((struct ref *)item->util, ref);
			} else {
				*p = ref;
				p = &ref->next;
				item->util = ref;
			}
		}
	}

	string_list_clear(&refs, 0);
	return retval;
}

int remote_has_url(struct remote *remote, const char *url)
{
	int i;
	for (i = 0; i < remote->url_nr; i++) {
		if (!strcmp(remote->url[i], url))
			return 1;
	}
	return 0;
}

static int match_name_with_pattern(const char *key, const char *name,
				   const char *value, char **result)
{
	const char *kstar = strchr(key, '*');
	size_t klen;
	size_t ksuffixlen;
	size_t namelen;
	int ret;
	if (!kstar)
		die("Key '%s' of pattern had no '*'", key);
	klen = kstar - key;
	ksuffixlen = strlen(kstar + 1);
	namelen = strlen(name);
	ret = !strncmp(name, key, klen) && namelen >= klen + ksuffixlen &&
		!memcmp(name + namelen - ksuffixlen, kstar + 1, ksuffixlen);
	if (ret && value) {
		const char *vstar = strchr(value, '*');
		size_t vlen;
		size_t vsuffixlen;
		if (!vstar)
			die("Value '%s' of pattern has no '*'", value);
		vlen = vstar - value;
		vsuffixlen = strlen(vstar + 1);
		*result = xmalloc(vlen + vsuffixlen +
				  strlen(name) -
				  klen - ksuffixlen + 1);
		strncpy(*result, value, vlen);
		strncpy(*result + vlen,
			name + klen, namelen - klen - ksuffixlen);
		strcpy(*result + vlen + namelen - klen - ksuffixlen,
		       vstar + 1);
	}
	return ret;
}

static void query_refspecs_multiple(struct refspec *refs, int ref_count, struct refspec *query, struct string_list *results)
{
	int i;
	int find_src = !query->src;

	if (find_src && !query->dst)
		error("query_refspecs_multiple: need either src or dst");

	for (i = 0; i < ref_count; i++) {
		struct refspec *refspec = &refs[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;
		const char *needle = find_src ? query->dst : query->src;
		char **result = find_src ? &query->src : &query->dst;

		if (!refspec->dst)
			continue;
		if (refspec->pattern) {
			if (match_name_with_pattern(key, needle, value, result))
				string_list_append_nodup(results, *result);
		} else if (!strcmp(needle, key)) {
			string_list_append(results, value);
		}
	}
}

int query_refspecs(struct refspec *refs, int ref_count, struct refspec *query)
{
	int i;
	int find_src = !query->src;
	const char *needle = find_src ? query->dst : query->src;
	char **result = find_src ? &query->src : &query->dst;

	if (find_src && !query->dst)
		return error("query_refspecs: need either src or dst");

	for (i = 0; i < ref_count; i++) {
		struct refspec *refspec = &refs[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;

		if (!refspec->dst)
			continue;
		if (refspec->pattern) {
			if (match_name_with_pattern(key, needle, value, result)) {
				query->force = refspec->force;
				return 0;
			}
		} else if (!strcmp(needle, key)) {
			*result = xstrdup(value);
			query->force = refspec->force;
			return 0;
		}
	}
	return -1;
}

char *apply_refspecs(struct refspec *refspecs, int nr_refspec,
		     const char *name)
{
	struct refspec query;

	memset(&query, 0, sizeof(struct refspec));
	query.src = (char *)name;

	if (query_refspecs(refspecs, nr_refspec, &query))
		return NULL;

	return query.dst;
}

int remote_find_tracking(struct remote *remote, struct refspec *refspec)
{
	return query_refspecs(remote->fetch, remote->fetch_refspec_nr, refspec);
}

static struct ref *alloc_ref_with_prefix(const char *prefix, size_t prefixlen,
		const char *name)
{
	size_t len = strlen(name);
	struct ref *ref = xcalloc(1, sizeof(struct ref) + prefixlen + len + 1);
	memcpy(ref->name, prefix, prefixlen);
	memcpy(ref->name + prefixlen, name, len);
	return ref;
}

struct ref *alloc_ref(const char *name)
{
	return alloc_ref_with_prefix("", 0, name);
}

struct ref *copy_ref(const struct ref *ref)
{
	struct ref *cpy;
	size_t len;
	if (!ref)
		return NULL;
	len = strlen(ref->name);
	cpy = xmalloc(sizeof(struct ref) + len + 1);
	memcpy(cpy, ref, sizeof(struct ref) + len + 1);
	cpy->next = NULL;
	cpy->symref = ref->symref ? xstrdup(ref->symref) : NULL;
	cpy->remote_status = ref->remote_status ? xstrdup(ref->remote_status) : NULL;
	cpy->peer_ref = copy_ref(ref->peer_ref);
	return cpy;
}

struct ref *copy_ref_list(const struct ref *ref)
{
	struct ref *ret = NULL;
	struct ref **tail = &ret;
	while (ref) {
		*tail = copy_ref(ref);
		ref = ref->next;
		tail = &((*tail)->next);
	}
	return ret;
}

static void free_ref(struct ref *ref)
{
	if (!ref)
		return;
	free_ref(ref->peer_ref);
	free(ref->remote_status);
	free(ref->symref);
	free(ref);
}

void free_refs(struct ref *ref)
{
	struct ref *next;
	while (ref) {
		next = ref->next;
		free_ref(ref);
		ref = next;
	}
}

int ref_compare_name(const void *va, const void *vb)
{
	const struct ref *a = va, *b = vb;
	return strcmp(a->name, b->name);
}

static void *ref_list_get_next(const void *a)
{
	return ((const struct ref *)a)->next;
}

static void ref_list_set_next(void *a, void *next)
{
	((struct ref *)a)->next = next;
}

void sort_ref_list(struct ref **l, int (*cmp)(const void *, const void *))
{
	*l = llist_mergesort(*l, ref_list_get_next, ref_list_set_next, cmp);
}

int count_refspec_match(const char *pattern,
			struct ref *refs,
			struct ref **matched_ref)
{
	int patlen = strlen(pattern);
	struct ref *matched_weak = NULL;
	struct ref *matched = NULL;
	int weak_match = 0;
	int match = 0;

	for (weak_match = match = 0; refs; refs = refs->next) {
		char *name = refs->name;
		int namelen = strlen(name);

		if (!refname_match(pattern, name))
			continue;

		/* A match is "weak" if it is with refs outside
		 * heads or tags, and did not specify the pattern
		 * in full (e.g. "refs/remotes/origin/master") or at
		 * least from the toplevel (e.g. "remotes/origin/master");
		 * otherwise "git push $URL master" would result in
		 * ambiguity between remotes/origin/master and heads/master
		 * at the remote site.
		 */
		if (namelen != patlen &&
		    patlen != namelen - 5 &&
		    !starts_with(name, "refs/heads/") &&
		    !starts_with(name, "refs/tags/")) {
			/* We want to catch the case where only weak
			 * matches are found and there are multiple
			 * matches, and where more than one strong
			 * matches are found, as ambiguous.  One
			 * strong match with zero or more weak matches
			 * are acceptable as a unique match.
			 */
			matched_weak = refs;
			weak_match++;
		}
		else {
			matched = refs;
			match++;
		}
	}
	if (!matched) {
		if (matched_ref)
			*matched_ref = matched_weak;
		return weak_match;
	}
	else {
		if (matched_ref)
			*matched_ref = matched;
		return match;
	}
}

static void tail_link_ref(struct ref *ref, struct ref ***tail)
{
	**tail = ref;
	while (ref->next)
		ref = ref->next;
	*tail = &ref->next;
}

static struct ref *alloc_delete_ref(void)
{
	struct ref *ref = alloc_ref("(delete)");
	hashclr(ref->new_sha1);
	return ref;
}

static int try_explicit_object_name(const char *name,
				    struct ref **match)
{
	unsigned char sha1[20];

	if (!*name) {
		if (match)
			*match = alloc_delete_ref();
		return 0;
	}

	if (get_sha1(name, sha1))
		return -1;

	if (match) {
		*match = alloc_ref(name);
		hashcpy((*match)->new_sha1, sha1);
	}
	return 0;
}

static struct ref *make_linked_ref(const char *name, struct ref ***tail)
{
	struct ref *ret = alloc_ref(name);
	tail_link_ref(ret, tail);
	return ret;
}

static char *guess_ref(const char *name, struct ref *peer)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char sha1[20];

	const char *r = resolve_ref_unsafe(peer->name, sha1, 1, NULL);
	if (!r)
		return NULL;

	if (starts_with(r, "refs/heads/"))
		strbuf_addstr(&buf, "refs/heads/");
	else if (starts_with(r, "refs/tags/"))
		strbuf_addstr(&buf, "refs/tags/");
	else
		return NULL;

	strbuf_addstr(&buf, name);
	return strbuf_detach(&buf, NULL);
}

static int match_explicit_lhs(struct ref *src,
			      struct refspec *rs,
			      struct ref **match,
			      int *allocated_match)
{
	switch (count_refspec_match(rs->src, src, match)) {
	case 1:
		if (allocated_match)
			*allocated_match = 0;
		return 0;
	case 0:
		/* The source could be in the get_sha1() format
		 * not a reference name.  :refs/other is a
		 * way to delete 'other' ref at the remote end.
		 */
		if (try_explicit_object_name(rs->src, match) < 0)
			return error("src refspec %s does not match any.", rs->src);
		if (allocated_match)
			*allocated_match = 1;
		return 0;
	default:
		return error("src refspec %s matches more than one.", rs->src);
	}
}

static int match_explicit(struct ref *src, struct ref *dst,
			  struct ref ***dst_tail,
			  struct refspec *rs)
{
	struct ref *matched_src, *matched_dst;
	int allocated_src;

	const char *dst_value = rs->dst;
	char *dst_guess;

	if (rs->pattern || rs->matching)
		return 0;

	matched_src = matched_dst = NULL;
	if (match_explicit_lhs(src, rs, &matched_src, &allocated_src) < 0)
		return -1;

	if (!dst_value) {
		unsigned char sha1[20];
		int flag;

		dst_value = resolve_ref_unsafe(matched_src->name, sha1, 1, &flag);
		if (!dst_value ||
		    ((flag & REF_ISSYMREF) &&
		     !starts_with(dst_value, "refs/heads/")))
			die("%s cannot be resolved to branch.",
			    matched_src->name);
	}

	switch (count_refspec_match(dst_value, dst, &matched_dst)) {
	case 1:
		break;
	case 0:
		if (starts_with(dst_value, "refs/"))
			matched_dst = make_linked_ref(dst_value, dst_tail);
		else if (is_null_sha1(matched_src->new_sha1))
			error("unable to delete '%s': remote ref does not exist",
			      dst_value);
		else if ((dst_guess = guess_ref(dst_value, matched_src)))
			matched_dst = make_linked_ref(dst_guess, dst_tail);
		else
			error("unable to push to unqualified destination: %s\n"
			      "The destination refspec neither matches an "
			      "existing ref on the remote nor\n"
			      "begins with refs/, and we are unable to "
			      "guess a prefix based on the source ref.",
			      dst_value);
		break;
	default:
		matched_dst = NULL;
		error("dst refspec %s matches more than one.",
		      dst_value);
		break;
	}
	if (!matched_dst)
		return -1;
	if (matched_dst->peer_ref)
		return error("dst ref %s receives from more than one src.",
		      matched_dst->name);
	else {
		matched_dst->peer_ref = allocated_src ?
					matched_src :
					copy_ref(matched_src);
		matched_dst->force = rs->force;
	}
	return 0;
}

static int match_explicit_refs(struct ref *src, struct ref *dst,
			       struct ref ***dst_tail, struct refspec *rs,
			       int rs_nr)
{
	int i, errs;
	for (i = errs = 0; i < rs_nr; i++)
		errs += match_explicit(src, dst, dst_tail, &rs[i]);
	return errs;
}

static char *get_ref_match(const struct refspec *rs, int rs_nr, const struct ref *ref,
		int send_mirror, int direction, const struct refspec **ret_pat)
{
	const struct refspec *pat;
	char *name;
	int i;
	int matching_refs = -1;
	for (i = 0; i < rs_nr; i++) {
		if (rs[i].matching &&
		    (matching_refs == -1 || rs[i].force)) {
			matching_refs = i;
			continue;
		}

		if (rs[i].pattern) {
			const char *dst_side = rs[i].dst ? rs[i].dst : rs[i].src;
			int match;
			if (direction == FROM_SRC)
				match = match_name_with_pattern(rs[i].src, ref->name, dst_side, &name);
			else
				match = match_name_with_pattern(dst_side, ref->name, rs[i].src, &name);
			if (match) {
				matching_refs = i;
				break;
			}
		}
	}
	if (matching_refs == -1)
		return NULL;

	pat = rs + matching_refs;
	if (pat->matching) {
		/*
		 * "matching refs"; traditionally we pushed everything
		 * including refs outside refs/heads/ hierarchy, but
		 * that does not make much sense these days.
		 */
		if (!send_mirror && !starts_with(ref->name, "refs/heads/"))
			return NULL;
		name = xstrdup(ref->name);
	}
	if (ret_pat)
		*ret_pat = pat;
	return name;
}

static struct ref **tail_ref(struct ref **head)
{
	struct ref **tail = head;
	while (*tail)
		tail = &((*tail)->next);
	return tail;
}

struct tips {
	struct commit **tip;
	int nr, alloc;
};

static void add_to_tips(struct tips *tips, const unsigned char *sha1)
{
	struct commit *commit;

	if (is_null_sha1(sha1))
		return;
	commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit || (commit->object.flags & TMP_MARK))
		return;
	commit->object.flags |= TMP_MARK;
	ALLOC_GROW(tips->tip, tips->nr + 1, tips->alloc);
	tips->tip[tips->nr++] = commit;
}

static void add_missing_tags(struct ref *src, struct ref **dst, struct ref ***dst_tail)
{
	struct string_list dst_tag = STRING_LIST_INIT_NODUP;
	struct string_list src_tag = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;
	struct ref *ref;
	struct tips sent_tips;

	/*
	 * Collect everything we know they would have at the end of
	 * this push, and collect all tags they have.
	 */
	memset(&sent_tips, 0, sizeof(sent_tips));
	for (ref = *dst; ref; ref = ref->next) {
		if (ref->peer_ref &&
		    !is_null_sha1(ref->peer_ref->new_sha1))
			add_to_tips(&sent_tips, ref->peer_ref->new_sha1);
		else
			add_to_tips(&sent_tips, ref->old_sha1);
		if (starts_with(ref->name, "refs/tags/"))
			string_list_append(&dst_tag, ref->name);
	}
	clear_commit_marks_many(sent_tips.nr, sent_tips.tip, TMP_MARK);

	sort_string_list(&dst_tag);

	/* Collect tags they do not have. */
	for (ref = src; ref; ref = ref->next) {
		if (!starts_with(ref->name, "refs/tags/"))
			continue; /* not a tag */
		if (string_list_has_string(&dst_tag, ref->name))
			continue; /* they already have it */
		if (sha1_object_info(ref->new_sha1, NULL) != OBJ_TAG)
			continue; /* be conservative */
		item = string_list_append(&src_tag, ref->name);
		item->util = ref;
	}
	string_list_clear(&dst_tag, 0);

	/*
	 * At this point, src_tag lists tags that are missing from
	 * dst, and sent_tips lists the tips we are pushing or those
	 * that we know they already have. An element in the src_tag
	 * that is an ancestor of any of the sent_tips needs to be
	 * sent to the other side.
	 */
	if (sent_tips.nr) {
		for_each_string_list_item(item, &src_tag) {
			struct ref *ref = item->util;
			struct ref *dst_ref;
			struct commit *commit;

			if (is_null_sha1(ref->new_sha1))
				continue;
			commit = lookup_commit_reference_gently(ref->new_sha1, 1);
			if (!commit)
				/* not pushing a commit, which is not an error */
				continue;

			/*
			 * Is this tag, which they do not have, reachable from
			 * any of the commits we are sending?
			 */
			if (!in_merge_bases_many(commit, sent_tips.nr, sent_tips.tip))
				continue;

			/* Add it in */
			dst_ref = make_linked_ref(ref->name, dst_tail);
			hashcpy(dst_ref->new_sha1, ref->new_sha1);
			dst_ref->peer_ref = copy_ref(ref);
		}
	}
	string_list_clear(&src_tag, 0);
	free(sent_tips.tip);
}

struct ref *find_ref_by_name(const struct ref *list, const char *name)
{
	for ( ; list; list = list->next)
		if (!strcmp(list->name, name))
			return (struct ref *)list;
	return NULL;
}

static void prepare_ref_index(struct string_list *ref_index, struct ref *ref)
{
	for ( ; ref; ref = ref->next)
		string_list_append_nodup(ref_index, ref->name)->util = ref;

	sort_string_list(ref_index);
}

/*
 * Given only the set of local refs, sanity-check the set of push
 * refspecs. We can't catch all errors that match_push_refs would,
 * but we can catch some errors early before even talking to the
 * remote side.
 */
int check_push_refs(struct ref *src, int nr_refspec, const char **refspec_names)
{
	struct refspec *refspec = parse_push_refspec(nr_refspec, refspec_names);
	int ret = 0;
	int i;

	for (i = 0; i < nr_refspec; i++) {
		struct refspec *rs = refspec + i;

		if (rs->pattern || rs->matching)
			continue;

		ret |= match_explicit_lhs(src, rs, NULL, NULL);
	}

	free_refspec(nr_refspec, refspec);
	return ret;
}

/*
 * Given the set of refs the local repository has, the set of refs the
 * remote repository has, and the refspec used for push, determine
 * what remote refs we will update and with what value by setting
 * peer_ref (which object is being pushed) and force (if the push is
 * forced) in elements of "dst". The function may add new elements to
 * dst (e.g. pushing to a new branch, done in match_explicit_refs).
 */
int match_push_refs(struct ref *src, struct ref **dst,
		    int nr_refspec, const char **refspec, int flags)
{
	struct refspec *rs;
	int send_all = flags & MATCH_REFS_ALL;
	int send_mirror = flags & MATCH_REFS_MIRROR;
	int send_prune = flags & MATCH_REFS_PRUNE;
	int errs;
	static const char *default_refspec[] = { ":", NULL };
	struct ref *ref, **dst_tail = tail_ref(dst);
	struct string_list dst_ref_index = STRING_LIST_INIT_NODUP;

	if (!nr_refspec) {
		nr_refspec = 1;
		refspec = default_refspec;
	}
	rs = parse_push_refspec(nr_refspec, (const char **) refspec);
	errs = match_explicit_refs(src, *dst, &dst_tail, rs, nr_refspec);

	/* pick the remainder */
	for (ref = src; ref; ref = ref->next) {
		struct string_list_item *dst_item;
		struct ref *dst_peer;
		const struct refspec *pat = NULL;
		char *dst_name;

		dst_name = get_ref_match(rs, nr_refspec, ref, send_mirror, FROM_SRC, &pat);
		if (!dst_name)
			continue;

		if (!dst_ref_index.nr)
			prepare_ref_index(&dst_ref_index, *dst);

		dst_item = string_list_lookup(&dst_ref_index, dst_name);
		dst_peer = dst_item ? dst_item->util : NULL;
		if (dst_peer) {
			if (dst_peer->peer_ref)
				/* We're already sending something to this ref. */
				goto free_name;
		} else {
			if (pat->matching && !(send_all || send_mirror))
				/*
				 * Remote doesn't have it, and we have no
				 * explicit pattern, and we don't have
				 * --all or --mirror.
				 */
				goto free_name;

			/* Create a new one and link it */
			dst_peer = make_linked_ref(dst_name, &dst_tail);
			hashcpy(dst_peer->new_sha1, ref->new_sha1);
			string_list_insert(&dst_ref_index,
				dst_peer->name)->util = dst_peer;
		}
		dst_peer->peer_ref = copy_ref(ref);
		dst_peer->force = pat->force;
	free_name:
		free(dst_name);
	}

	string_list_clear(&dst_ref_index, 0);

	if (flags & MATCH_REFS_FOLLOW_TAGS)
		add_missing_tags(src, dst, &dst_tail);

	if (send_prune) {
		struct string_list src_ref_index = STRING_LIST_INIT_NODUP;
		/* check for missing refs on the remote */
		for (ref = *dst; ref; ref = ref->next) {
			char *src_name;

			if (ref->peer_ref)
				/* We're already sending something to this ref. */
				continue;

			src_name = get_ref_match(rs, nr_refspec, ref, send_mirror, FROM_DST, NULL);
			if (src_name) {
				if (!src_ref_index.nr)
					prepare_ref_index(&src_ref_index, src);
				if (!string_list_has_string(&src_ref_index,
					    src_name))
					ref->peer_ref = alloc_delete_ref();
				free(src_name);
			}
		}
		string_list_clear(&src_ref_index, 0);
	}
	if (errs)
		return -1;
	return 0;
}

void set_ref_status_for_push(struct ref *remote_refs, int send_mirror,
			     int force_update)
{
	struct ref *ref;

	for (ref = remote_refs; ref; ref = ref->next) {
		int force_ref_update = ref->force || force_update;
		int reject_reason = 0;

		if (ref->peer_ref)
			hashcpy(ref->new_sha1, ref->peer_ref->new_sha1);
		else if (!send_mirror)
			continue;

		ref->deletion = is_null_sha1(ref->new_sha1);
		if (!ref->deletion &&
			!hashcmp(ref->old_sha1, ref->new_sha1)) {
			ref->status = REF_STATUS_UPTODATE;
			continue;
		}

		/*
		 * Bypass the usual "must fast-forward" check but
		 * replace it with a weaker "the old value must be
		 * this value we observed".  If the remote ref has
		 * moved and is now different from what we expect,
		 * reject any push.
		 *
		 * It also is an error if the user told us to check
		 * with the remote-tracking branch to find the value
		 * to expect, but we did not have such a tracking
		 * branch.
		 */
		if (ref->expect_old_sha1) {
			if (ref->expect_old_no_trackback ||
			    hashcmp(ref->old_sha1, ref->old_sha1_expect))
				reject_reason = REF_STATUS_REJECT_STALE;
		}

		/*
		 * The usual "must fast-forward" rules.
		 *
		 * Decide whether an individual refspec A:B can be
		 * pushed.  The push will succeed if any of the
		 * following are true:
		 *
		 * (1) the remote reference B does not exist
		 *
		 * (2) the remote reference B is being removed (i.e.,
		 *     pushing :B where no source is specified)
		 *
		 * (3) the destination is not under refs/tags/, and
		 *     if the old and new value is a commit, the new
		 *     is a descendant of the old.
		 *
		 * (4) it is forced using the +A:B notation, or by
		 *     passing the --force argument
		 */

		else if (!ref->deletion && !is_null_sha1(ref->old_sha1)) {
			if (starts_with(ref->name, "refs/tags/"))
				reject_reason = REF_STATUS_REJECT_ALREADY_EXISTS;
			else if (!has_sha1_file(ref->old_sha1))
				reject_reason = REF_STATUS_REJECT_FETCH_FIRST;
			else if (!lookup_commit_reference_gently(ref->old_sha1, 1) ||
				 !lookup_commit_reference_gently(ref->new_sha1, 1))
				reject_reason = REF_STATUS_REJECT_NEEDS_FORCE;
			else if (!ref_newer(ref->new_sha1, ref->old_sha1))
				reject_reason = REF_STATUS_REJECT_NONFASTFORWARD;
		}

		/*
		 * "--force" will defeat any rejection implemented
		 * by the rules above.
		 */
		if (!force_ref_update)
			ref->status = reject_reason;
		else if (reject_reason)
			ref->forced_update = 1;
	}
}

struct branch *branch_get(const char *name)
{
	struct branch *ret;

	read_config();
	if (!name || !*name || !strcmp(name, "HEAD"))
		ret = current_branch;
	else
		ret = make_branch(name, 0);
	if (ret && ret->remote_name) {
		ret->remote = remote_get(ret->remote_name);
		if (ret->merge_nr) {
			int i;
			ret->merge = xcalloc(ret->merge_nr, sizeof(*ret->merge));
			for (i = 0; i < ret->merge_nr; i++) {
				ret->merge[i] = xcalloc(1, sizeof(**ret->merge));
				ret->merge[i]->src = xstrdup(ret->merge_name[i]);
				if (remote_find_tracking(ret->remote, ret->merge[i])
				    && !strcmp(ret->remote_name, "."))
					ret->merge[i]->dst = xstrdup(ret->merge_name[i]);
			}
		}
	}
	return ret;
}

int branch_has_merge_config(struct branch *branch)
{
	return branch && !!branch->merge;
}

int branch_merge_matches(struct branch *branch,
		                 int i,
		                 const char *refname)
{
	if (!branch || i < 0 || i >= branch->merge_nr)
		return 0;
	return refname_match(branch->merge[i]->src, refname);
}

static int ignore_symref_update(const char *refname)
{
	unsigned char sha1[20];
	int flag;

	if (!resolve_ref_unsafe(refname, sha1, 0, &flag))
		return 0; /* non-existing refs are OK */
	return (flag & REF_ISSYMREF);
}

/*
 * Create and return a list of (struct ref) consisting of copies of
 * each remote_ref that matches refspec.  refspec must be a pattern.
 * Fill in the copies' peer_ref to describe the local tracking refs to
 * which they map.  Omit any references that would map to an existing
 * local symbolic ref.
 */
static struct ref *get_expanded_map(const struct ref *remote_refs,
				    const struct refspec *refspec)
{
	const struct ref *ref;
	struct ref *ret = NULL;
	struct ref **tail = &ret;

	for (ref = remote_refs; ref; ref = ref->next) {
		char *expn_name = NULL;

		if (strchr(ref->name, '^'))
			continue; /* a dereference item */
		if (match_name_with_pattern(refspec->src, ref->name,
					    refspec->dst, &expn_name) &&
		    !ignore_symref_update(expn_name)) {
			struct ref *cpy = copy_ref(ref);

			cpy->peer_ref = alloc_ref(expn_name);
			if (refspec->force)
				cpy->peer_ref->force = 1;
			*tail = cpy;
			tail = &cpy->next;
		}
		free(expn_name);
	}

	return ret;
}

static const struct ref *find_ref_by_name_abbrev(const struct ref *refs, const char *name)
{
	const struct ref *ref;
	for (ref = refs; ref; ref = ref->next) {
		if (refname_match(name, ref->name))
			return ref;
	}
	return NULL;
}

struct ref *get_remote_ref(const struct ref *remote_refs, const char *name)
{
	const struct ref *ref = find_ref_by_name_abbrev(remote_refs, name);

	if (!ref)
		return NULL;

	return copy_ref(ref);
}

static struct ref *get_local_ref(const char *name)
{
	if (!name || name[0] == '\0')
		return NULL;

	if (starts_with(name, "refs/"))
		return alloc_ref(name);

	if (starts_with(name, "heads/") ||
	    starts_with(name, "tags/") ||
	    starts_with(name, "remotes/"))
		return alloc_ref_with_prefix("refs/", 5, name);

	return alloc_ref_with_prefix("refs/heads/", 11, name);
}

int get_fetch_map(const struct ref *remote_refs,
		  const struct refspec *refspec,
		  struct ref ***tail,
		  int missing_ok)
{
	struct ref *ref_map, **rmp;

	if (refspec->pattern) {
		ref_map = get_expanded_map(remote_refs, refspec);
	} else {
		const char *name = refspec->src[0] ? refspec->src : "HEAD";

		if (refspec->exact_sha1) {
			ref_map = alloc_ref(name);
			get_sha1_hex(name, ref_map->old_sha1);
		} else {
			ref_map = get_remote_ref(remote_refs, name);
		}
		if (!missing_ok && !ref_map)
			die("Couldn't find remote ref %s", name);
		if (ref_map) {
			ref_map->peer_ref = get_local_ref(refspec->dst);
			if (ref_map->peer_ref && refspec->force)
				ref_map->peer_ref->force = 1;
		}
	}

	for (rmp = &ref_map; *rmp; ) {
		if ((*rmp)->peer_ref) {
			if (!starts_with((*rmp)->peer_ref->name, "refs/") ||
			    check_refname_format((*rmp)->peer_ref->name, 0)) {
				struct ref *ignore = *rmp;
				error("* Ignoring funny ref '%s' locally",
				      (*rmp)->peer_ref->name);
				*rmp = (*rmp)->next;
				free(ignore->peer_ref);
				free(ignore);
				continue;
			}
		}
		rmp = &((*rmp)->next);
	}

	if (ref_map)
		tail_link_ref(ref_map, tail);

	return 0;
}

int resolve_remote_symref(struct ref *ref, struct ref *list)
{
	if (!ref->symref)
		return 0;
	for (; list; list = list->next)
		if (!strcmp(ref->symref, list->name)) {
			hashcpy(ref->old_sha1, list->old_sha1);
			return 0;
		}
	return 1;
}

static void unmark_and_free(struct commit_list *list, unsigned int mark)
{
	while (list) {
		struct commit_list *temp = list;
		temp->item->object.flags &= ~mark;
		list = temp->next;
		free(temp);
	}
}

int ref_newer(const unsigned char *new_sha1, const unsigned char *old_sha1)
{
	struct object *o;
	struct commit *old, *new;
	struct commit_list *list, *used;
	int found = 0;

	/*
	 * Both new and old must be commit-ish and new is descendant of
	 * old.  Otherwise we require --force.
	 */
	o = deref_tag(parse_object(old_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	old = (struct commit *) o;

	o = deref_tag(parse_object(new_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	new = (struct commit *) o;

	if (parse_commit(new) < 0)
		return 0;

	used = list = NULL;
	commit_list_insert(new, &list);
	while (list) {
		new = pop_most_recent_commit(&list, TMP_MARK);
		commit_list_insert(new, &used);
		if (new == old) {
			found = 1;
			break;
		}
	}
	unmark_and_free(list, TMP_MARK);
	unmark_and_free(used, TMP_MARK);
	return found;
}

/*
 * Compare a branch with its upstream, and save their differences (number
 * of commits) in *num_ours and *num_theirs.
 *
 * Return 0 if branch has no upstream (no base), -1 if upstream is missing
 * (with "gone" base), otherwise 1 (with base).
 */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs)
{
	unsigned char sha1[20];
	struct commit *ours, *theirs;
	char symmetric[84];
	struct rev_info revs;
	const char *rev_argv[10], *base;
	int rev_argc;

	/* Cannot stat unless we are marked to build on top of somebody else. */
	if (!branch ||
	    !branch->merge || !branch->merge[0] || !branch->merge[0]->dst)
		return 0;

	/* Cannot stat if what we used to build on no longer exists */
	base = branch->merge[0]->dst;
	if (read_ref(base, sha1))
		return -1;
	theirs = lookup_commit_reference(sha1);
	if (!theirs)
		return -1;

	if (read_ref(branch->refname, sha1))
		return -1;
	ours = lookup_commit_reference(sha1);
	if (!ours)
		return -1;

	/* are we the same? */
	if (theirs == ours) {
		*num_theirs = *num_ours = 0;
		return 1;
	}

	/* Run "rev-list --left-right ours...theirs" internally... */
	rev_argc = 0;
	rev_argv[rev_argc++] = NULL;
	rev_argv[rev_argc++] = "--left-right";
	rev_argv[rev_argc++] = symmetric;
	rev_argv[rev_argc++] = "--";
	rev_argv[rev_argc] = NULL;

	strcpy(symmetric, sha1_to_hex(ours->object.sha1));
	strcpy(symmetric + 40, "...");
	strcpy(symmetric + 43, sha1_to_hex(theirs->object.sha1));

	init_revisions(&revs, NULL);
	setup_revisions(rev_argc, rev_argv, &revs, NULL);
	prepare_revision_walk(&revs);

	/* ... and count the commits on each side. */
	*num_ours = 0;
	*num_theirs = 0;
	while (1) {
		struct commit *c = get_revision(&revs);
		if (!c)
			break;
		if (c->object.flags & SYMMETRIC_LEFT)
			(*num_ours)++;
		else
			(*num_theirs)++;
	}

	/* clear object flags smudged by the above traversal */
	clear_commit_marks(ours, ALL_REV_FLAGS);
	clear_commit_marks(theirs, ALL_REV_FLAGS);
	return 1;
}

/*
 * Return true when there is anything to report, otherwise false.
 */
int format_tracking_info(struct branch *branch, struct strbuf *sb)
{
	int ours, theirs;
	const char *base;
	int upstream_is_gone = 0;

	switch (stat_tracking_info(branch, &ours, &theirs)) {
	case 0:
		/* no base */
		return 0;
	case -1:
		/* with "gone" base */
		upstream_is_gone = 1;
		break;
	default:
		/* with base */
		break;
	}

	base = branch->merge[0]->dst;
	base = shorten_unambiguous_ref(base, 0);
	if (upstream_is_gone) {
		strbuf_addf(sb,
			_("Your branch is based on '%s', but the upstream is gone.\n"),
			base);
		if (advice_status_hints)
			strbuf_addf(sb,
				_("  (use \"git branch --unset-upstream\" to fixup)\n"));
	} else if (!ours && !theirs) {
		strbuf_addf(sb,
			_("Your branch is up-to-date with '%s'.\n"),
			base);
	} else if (!theirs) {
		strbuf_addf(sb,
			Q_("Your branch is ahead of '%s' by %d commit.\n",
			   "Your branch is ahead of '%s' by %d commits.\n",
			   ours),
			base, ours);
		if (advice_status_hints)
			strbuf_addf(sb,
				_("  (use \"git push\" to publish your local commits)\n"));
	} else if (!ours) {
		strbuf_addf(sb,
			Q_("Your branch is behind '%s' by %d commit, "
			       "and can be fast-forwarded.\n",
			   "Your branch is behind '%s' by %d commits, "
			       "and can be fast-forwarded.\n",
			   theirs),
			base, theirs);
		if (advice_status_hints)
			strbuf_addf(sb,
				_("  (use \"git pull\" to update your local branch)\n"));
	} else {
		strbuf_addf(sb,
			Q_("Your branch and '%s' have diverged,\n"
			       "and have %d and %d different commit each, "
			       "respectively.\n",
			   "Your branch and '%s' have diverged,\n"
			       "and have %d and %d different commits each, "
			       "respectively.\n",
			   theirs),
			base, ours, theirs);
		if (advice_status_hints)
			strbuf_addf(sb,
				_("  (use \"git pull\" to merge the remote branch into yours)\n"));
	}
	return 1;
}

static int one_local_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct ref ***local_tail = cb_data;
	struct ref *ref;
	int len;

	/* we already know it starts with refs/ to get here */
	if (check_refname_format(refname + 5, 0))
		return 0;

	len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	hashcpy(ref->new_sha1, sha1);
	memcpy(ref->name, refname, len);
	**local_tail = ref;
	*local_tail = &ref->next;
	return 0;
}

struct ref *get_local_heads(void)
{
	struct ref *local_refs = NULL, **local_tail = &local_refs;
	for_each_ref(one_local_ref, &local_tail);
	return local_refs;
}

struct ref *guess_remote_head(const struct ref *head,
			      const struct ref *refs,
			      int all)
{
	const struct ref *r;
	struct ref *list = NULL;
	struct ref **tail = &list;

	if (!head)
		return NULL;

	/*
	 * Some transports support directly peeking at
	 * where HEAD points; if that is the case, then
	 * we don't have to guess.
	 */
	if (head->symref)
		return copy_ref(find_ref_by_name(refs, head->symref));

	/* If refs/heads/master could be right, it is. */
	if (!all) {
		r = find_ref_by_name(refs, "refs/heads/master");
		if (r && !hashcmp(r->old_sha1, head->old_sha1))
			return copy_ref(r);
	}

	/* Look for another ref that points there */
	for (r = refs; r; r = r->next) {
		if (r != head &&
		    starts_with(r->name, "refs/heads/") &&
		    !hashcmp(r->old_sha1, head->old_sha1)) {
			*tail = copy_ref(r);
			tail = &((*tail)->next);
			if (!all)
				break;
		}
	}

	return list;
}

struct stale_heads_info {
	struct string_list *ref_names;
	struct ref **stale_refs_tail;
	struct refspec *refs;
	int ref_count;
};

static int get_stale_heads_cb(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct stale_heads_info *info = cb_data;
	struct string_list matches = STRING_LIST_INIT_DUP;
	struct refspec query;
	int i, stale = 1;
	memset(&query, 0, sizeof(struct refspec));
	query.dst = (char *)refname;

	query_refspecs_multiple(info->refs, info->ref_count, &query, &matches);
	if (matches.nr == 0)
		goto clean_exit; /* No matches */

	/*
	 * If we did find a suitable refspec and it's not a symref and
	 * it's not in the list of refs that currently exist in that
	 * remote, we consider it to be stale. In order to deal with
	 * overlapping refspecs, we need to go over all of the
	 * matching refs.
	 */
	if (flags & REF_ISSYMREF)
		goto clean_exit;

	for (i = 0; stale && i < matches.nr; i++)
		if (string_list_has_string(info->ref_names, matches.items[i].string))
			stale = 0;

	if (stale) {
		struct ref *ref = make_linked_ref(refname, &info->stale_refs_tail);
		hashcpy(ref->new_sha1, sha1);
	}

clean_exit:
	string_list_clear(&matches, 0);
	return 0;
}

struct ref *get_stale_heads(struct refspec *refs, int ref_count, struct ref *fetch_map)
{
	struct ref *ref, *stale_refs = NULL;
	struct string_list ref_names = STRING_LIST_INIT_NODUP;
	struct stale_heads_info info;
	info.ref_names = &ref_names;
	info.stale_refs_tail = &stale_refs;
	info.refs = refs;
	info.ref_count = ref_count;
	for (ref = fetch_map; ref; ref = ref->next)
		string_list_append(&ref_names, ref->name);
	sort_string_list(&ref_names);
	for_each_ref(get_stale_heads_cb, &info);
	string_list_clear(&ref_names, 0);
	return stale_refs;
}

/*
 * Compare-and-swap
 */
void clear_cas_option(struct push_cas_option *cas)
{
	int i;

	for (i = 0; i < cas->nr; i++)
		free(cas->entry[i].refname);
	free(cas->entry);
	memset(cas, 0, sizeof(*cas));
}

static struct push_cas *add_cas_entry(struct push_cas_option *cas,
				      const char *refname,
				      size_t refnamelen)
{
	struct push_cas *entry;
	ALLOC_GROW(cas->entry, cas->nr + 1, cas->alloc);
	entry = &cas->entry[cas->nr++];
	memset(entry, 0, sizeof(*entry));
	entry->refname = xmemdupz(refname, refnamelen);
	return entry;
}

int parse_push_cas_option(struct push_cas_option *cas, const char *arg, int unset)
{
	const char *colon;
	struct push_cas *entry;

	if (unset) {
		/* "--no-<option>" */
		clear_cas_option(cas);
		return 0;
	}

	if (!arg) {
		/* just "--<option>" */
		cas->use_tracking_for_rest = 1;
		return 0;
	}

	/* "--<option>=refname" or "--<option>=refname:value" */
	colon = strchrnul(arg, ':');
	entry = add_cas_entry(cas, arg, colon - arg);
	if (!*colon)
		entry->use_tracking = 1;
	else if (get_sha1(colon + 1, entry->expect))
		return error("cannot parse expected object name '%s'", colon + 1);
	return 0;
}

int parseopt_push_cas_option(const struct option *opt, const char *arg, int unset)
{
	return parse_push_cas_option(opt->value, arg, unset);
}

int is_empty_cas(const struct push_cas_option *cas)
{
	return !cas->use_tracking_for_rest && !cas->nr;
}

/*
 * Look at remote.fetch refspec and see if we have a remote
 * tracking branch for the refname there.  Fill its current
 * value in sha1[].
 * If we cannot do so, return negative to signal an error.
 */
static int remote_tracking(struct remote *remote, const char *refname,
			   unsigned char sha1[20])
{
	char *dst;

	dst = apply_refspecs(remote->fetch, remote->fetch_refspec_nr, refname);
	if (!dst)
		return -1; /* no tracking ref for refname at remote */
	if (read_ref(dst, sha1))
		return -1; /* we know what the tracking ref is but we cannot read it */
	return 0;
}

static void apply_cas(struct push_cas_option *cas,
		      struct remote *remote,
		      struct ref *ref)
{
	int i;

	/* Find an explicit --<option>=<name>[:<value>] entry */
	for (i = 0; i < cas->nr; i++) {
		struct push_cas *entry = &cas->entry[i];
		if (!refname_match(entry->refname, ref->name))
			continue;
		ref->expect_old_sha1 = 1;
		if (!entry->use_tracking)
			hashcpy(ref->old_sha1_expect, cas->entry[i].expect);
		else if (remote_tracking(remote, ref->name, ref->old_sha1_expect))
			ref->expect_old_no_trackback = 1;
		return;
	}

	/* Are we using "--<option>" to cover all? */
	if (!cas->use_tracking_for_rest)
		return;

	ref->expect_old_sha1 = 1;
	if (remote_tracking(remote, ref->name, ref->old_sha1_expect))
		ref->expect_old_no_trackback = 1;
}

void apply_push_cas(struct push_cas_option *cas,
		    struct remote *remote,
		    struct ref *remote_refs)
{
	struct ref *ref;
	for (ref = remote_refs; ref; ref = ref->next)
		apply_cas(cas, remote, ref);
}
