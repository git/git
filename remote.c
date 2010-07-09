#include "cache.h"
#include "remote.h"
#include "refs.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "dir.h"
#include "tag.h"
#include "string-list.h"

static struct refspec s_tag_refspec = {
	0,
	1,
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
			if (!prefixcmp(url, r->rewrite[i]->instead_of[j].s) &&
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
	char *refname;

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
	refname = xmalloc(strlen(name) + strlen("refs/heads/") + 1);
	strcpy(refname, "refs/heads/");
	strcpy(refname + strlen("refs/heads/"), ret->name);
	ret->refname = refname;

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

		if (!prefixcmp(buffer, "URL:")) {
			value_list = 0;
			s = buffer + 4;
		} else if (!prefixcmp(buffer, "Push:")) {
			value_list = 1;
			s = buffer + 5;
		} else if (!prefixcmp(buffer, "Pull:")) {
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
	const char *slash = strchr(remote->name, '/');
	char *frag;
	struct strbuf branch = STRBUF_INIT;
	int n = slash ? slash - remote->name : 1000;
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
	if (slash)
		len += strlen(slash);
	p = xmalloc(len + 1);
	strcpy(p, s);
	if (slash)
		strcat(p, slash);

	/*
	 * With "slash", e.g. "git fetch jgarzik/netdev-2.6" when
	 * reading from $GIT_DIR/branches/jgarzik fetches "HEAD" from
	 * the partial URL obtained from the branches file plus
	 * "/netdev-2.6" and does not store it in any tracking ref.
	 * #branch specifier in the file is ignored.
	 *
	 * Otherwise, the branches file would have URL and optionally
	 * #branch specified.  The "master" (or specified) branch is
	 * fetched and stored in the local branch of the same name.
	 */
	frag = strchr(p, '#');
	if (frag) {
		*(frag++) = '\0';
		strbuf_addf(&branch, "refs/heads/%s", frag);
	} else
		strbuf_addstr(&branch, "refs/heads/master");
	if (!slash) {
		strbuf_addf(&branch, ":refs/heads/%s", remote->name);
	} else {
		strbuf_reset(&branch);
		strbuf_addstr(&branch, "HEAD:");
	}
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
	if (!prefixcmp(key, "branch.")) {
		name = key + 7;
		subkey = strrchr(name, '.');
		if (!subkey)
			return 0;
		branch = make_branch(name, subkey - name);
		if (!strcmp(subkey, ".remote")) {
			if (!value)
				return config_error_nonbool(key);
			branch->remote_name = xstrdup(value);
			if (branch == current_branch) {
				default_remote_name = branch->remote_name;
				explicit_default_remote_name = 1;
			}
		} else if (!strcmp(subkey, ".merge")) {
			if (!value)
				return config_error_nonbool(key);
			add_merge(branch, xstrdup(value));
		}
		return 0;
	}
	if (!prefixcmp(key, "url.")) {
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
	if (prefixcmp(key,  "remote."))
		return 0;
	name = key + 7;
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
	default_remote_name = xstrdup("origin");
	current_branch = NULL;
	head_ref = resolve_ref("HEAD", sha1, 0, &flag);
	if (head_ref && (flag & REF_ISSYMREF) &&
	    !prefixcmp(head_ref, "refs/heads/")) {
		current_branch =
			make_branch(head_ref + strlen("refs/heads/"), 0);
	}
	git_config(handle_config, NULL);
	alias_all_urls();
}

/*
 * We need to make sure the tracking branches are well formed, but a
 * wildcard refspec in "struct refspec" must have a trailing slash. We
 * temporarily drop the trailing '/' while calling check_ref_format(),
 * and put it back.  The caller knows that a CHECK_REF_FORMAT_ONELEVEL
 * error return is Ok for a wildcard refspec.
 */
static int verify_refname(char *name, int is_glob)
{
	int result;

	result = check_ref_format(name);
	if (is_glob && result == CHECK_REF_FORMAT_WILDCARD)
		result = CHECK_REF_FORMAT_OK;
	return result;
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
	int st;
	struct refspec *rs = xcalloc(sizeof(*rs), nr_refspec);

	for (i = 0; i < nr_refspec; i++) {
		size_t llen;
		int is_glob;
		const char *lhs, *rhs;

		is_glob = 0;

		lhs = refspec[i];
		if (*lhs == '+') {
			rs[i].force = 1;
			lhs++;
		}

		rhs = strrchr(lhs, ':');

		/*
		 * Before going on, special case ":" (or "+:") as a refspec
		 * for matching refs.
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

		if (fetch) {
			/*
			 * LHS
			 * - empty is allowed; it means HEAD.
			 * - otherwise it must be a valid looking ref.
			 */
			if (!*rs[i].src)
				; /* empty is ok */
			else {
				st = verify_refname(rs[i].src, is_glob);
				if (st && st != CHECK_REF_FORMAT_ONELEVEL)
					goto invalid;
			}
			/*
			 * RHS
			 * - missing is ok, and is same as empty.
			 * - empty is ok; it means not to store.
			 * - otherwise it must be a valid looking ref.
			 */
			if (!rs[i].dst) {
				; /* ok */
			} else if (!*rs[i].dst) {
				; /* ok */
			} else {
				st = verify_refname(rs[i].dst, is_glob);
				if (st && st != CHECK_REF_FORMAT_ONELEVEL)
					goto invalid;
			}
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
				st = verify_refname(rs[i].src, is_glob);
				if (st && st != CHECK_REF_FORMAT_ONELEVEL)
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
				st = verify_refname(rs[i].src, is_glob);
				if (st && st != CHECK_REF_FORMAT_ONELEVEL)
					goto invalid;
			} else if (!*rs[i].dst) {
				goto invalid;
			} else {
				st = verify_refname(rs[i].dst, is_glob);
				if (st && st != CHECK_REF_FORMAT_ONELEVEL)
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

struct remote *remote_get(const char *name)
{
	struct remote *ret;
	int name_given = 0;

	read_config();
	if (name)
		name_given = 1;
	else {
		name = default_remote_name;
		name_given = explicit_default_remote_name;
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

void ref_remove_duplicates(struct ref *ref_map)
{
	struct string_list refs = { NULL, 0, 0, 0 };
	struct string_list_item *item = NULL;
	struct ref *prev = NULL, *next = NULL;
	for (; ref_map; prev = ref_map, ref_map = next) {
		next = ref_map->next;
		if (!ref_map->peer_ref)
			continue;

		item = string_list_lookup(&refs, ref_map->peer_ref->name);
		if (item) {
			if (strcmp(((struct ref *)item->util)->name,
				   ref_map->name))
				die("%s tracks both %s and %s",
				    ref_map->peer_ref->name,
				    ((struct ref *)item->util)->name,
				    ref_map->name);
			prev->next = ref_map->next;
			free(ref_map->peer_ref);
			free(ref_map);
			ref_map = prev; /* skip this; we freed it */
			continue;
		}

		item = string_list_insert(&refs, ref_map->peer_ref->name);
		item->util = ref_map;
	}
	string_list_clear(&refs, 0);
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

char *apply_refspecs(struct refspec *refspecs, int nr_refspec,
		     const char *name)
{
	int i;
	char *ret = NULL;
	for (i = 0; i < nr_refspec; i++) {
		struct refspec *refspec = refspecs + i;
		if (refspec->pattern) {
			if (match_name_with_pattern(refspec->src, name,
						    refspec->dst, &ret))
				return ret;
		} else if (!strcmp(refspec->src, name))
			return strdup(refspec->dst);
	}
	return NULL;
}

int remote_find_tracking(struct remote *remote, struct refspec *refspec)
{
	int find_src = refspec->src == NULL;
	char *needle, **result;
	int i;

	if (find_src) {
		if (!refspec->dst)
			return error("find_tracking: need either src or dst");
		needle = refspec->dst;
		result = &refspec->src;
	} else {
		needle = refspec->src;
		result = &refspec->dst;
	}

	for (i = 0; i < remote->fetch_refspec_nr; i++) {
		struct refspec *fetch = &remote->fetch[i];
		const char *key = find_src ? fetch->dst : fetch->src;
		const char *value = find_src ? fetch->src : fetch->dst;
		if (!fetch->dst)
			continue;
		if (fetch->pattern) {
			if (match_name_with_pattern(key, needle, value, result)) {
				refspec->force = fetch->force;
				return 0;
			}
		} else if (!strcmp(needle, key)) {
			*result = xstrdup(value);
			refspec->force = fetch->force;
			return 0;
		}
	}
	return -1;
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

static struct ref *copy_ref(const struct ref *ref)
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

static int count_refspec_match(const char *pattern,
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

		if (!refname_match(pattern, name, ref_rev_parse_rules))
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
		    prefixcmp(name, "refs/heads/") &&
		    prefixcmp(name, "refs/tags/")) {
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
		*matched_ref = matched_weak;
		return weak_match;
	}
	else {
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

static struct ref *try_explicit_object_name(const char *name)
{
	unsigned char sha1[20];
	struct ref *ref;

	if (!*name) {
		ref = alloc_ref("(delete)");
		hashclr(ref->new_sha1);
		return ref;
	}
	if (get_sha1(name, sha1))
		return NULL;
	ref = alloc_ref(name);
	hashcpy(ref->new_sha1, sha1);
	return ref;
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

	const char *r = resolve_ref(peer->name, sha1, 1, NULL);
	if (!r)
		return NULL;

	if (!prefixcmp(r, "refs/heads/"))
		strbuf_addstr(&buf, "refs/heads/");
	else if (!prefixcmp(r, "refs/tags/"))
		strbuf_addstr(&buf, "refs/tags/");
	else
		return NULL;

	strbuf_addstr(&buf, name);
	return strbuf_detach(&buf, NULL);
}

static int match_explicit(struct ref *src, struct ref *dst,
			  struct ref ***dst_tail,
			  struct refspec *rs)
{
	struct ref *matched_src, *matched_dst;
	int copy_src;

	const char *dst_value = rs->dst;
	char *dst_guess;

	if (rs->pattern || rs->matching)
		return 0;

	matched_src = matched_dst = NULL;
	switch (count_refspec_match(rs->src, src, &matched_src)) {
	case 1:
		copy_src = 1;
		break;
	case 0:
		/* The source could be in the get_sha1() format
		 * not a reference name.  :refs/other is a
		 * way to delete 'other' ref at the remote end.
		 */
		matched_src = try_explicit_object_name(rs->src);
		if (!matched_src)
			return error("src refspec %s does not match any.", rs->src);
		copy_src = 0;
		break;
	default:
		return error("src refspec %s matches more than one.", rs->src);
	}

	if (!dst_value) {
		unsigned char sha1[20];
		int flag;

		dst_value = resolve_ref(matched_src->name, sha1, 1, &flag);
		if (!dst_value ||
		    ((flag & REF_ISSYMREF) &&
		     prefixcmp(dst_value, "refs/heads/")))
			die("%s cannot be resolved to branch.",
			    matched_src->name);
	}

	switch (count_refspec_match(dst_value, dst, &matched_dst)) {
	case 1:
		break;
	case 0:
		if (!memcmp(dst_value, "refs/", 5))
			matched_dst = make_linked_ref(dst_value, dst_tail);
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
		matched_dst->peer_ref = copy_src ? copy_ref(matched_src) : matched_src;
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

static const struct refspec *check_pattern_match(const struct refspec *rs,
						 int rs_nr,
						 const struct ref *src)
{
	int i;
	int matching_refs = -1;
	for (i = 0; i < rs_nr; i++) {
		if (rs[i].matching &&
		    (matching_refs == -1 || rs[i].force)) {
			matching_refs = i;
			continue;
		}

		if (rs[i].pattern && match_name_with_pattern(rs[i].src, src->name,
							     NULL, NULL))
			return rs + i;
	}
	if (matching_refs != -1)
		return rs + matching_refs;
	else
		return NULL;
}

static struct ref **tail_ref(struct ref **head)
{
	struct ref **tail = head;
	while (*tail)
		tail = &((*tail)->next);
	return tail;
}

/*
 * Note. This is used only by "push"; refspec matching rules for
 * push and fetch are subtly different, so do not try to reuse it
 * without thinking.
 */
int match_refs(struct ref *src, struct ref **dst,
	       int nr_refspec, const char **refspec, int flags)
{
	struct refspec *rs;
	int send_all = flags & MATCH_REFS_ALL;
	int send_mirror = flags & MATCH_REFS_MIRROR;
	int errs;
	static const char *default_refspec[] = { ":", NULL };
	struct ref **dst_tail = tail_ref(dst);

	if (!nr_refspec) {
		nr_refspec = 1;
		refspec = default_refspec;
	}
	rs = parse_push_refspec(nr_refspec, (const char **) refspec);
	errs = match_explicit_refs(src, *dst, &dst_tail, rs, nr_refspec);

	/* pick the remainder */
	for ( ; src; src = src->next) {
		struct ref *dst_peer;
		const struct refspec *pat = NULL;
		char *dst_name;
		if (src->peer_ref)
			continue;

		pat = check_pattern_match(rs, nr_refspec, src);
		if (!pat)
			continue;

		if (pat->matching) {
			/*
			 * "matching refs"; traditionally we pushed everything
			 * including refs outside refs/heads/ hierarchy, but
			 * that does not make much sense these days.
			 */
			if (!send_mirror && prefixcmp(src->name, "refs/heads/"))
				continue;
			dst_name = xstrdup(src->name);

		} else {
			const char *dst_side = pat->dst ? pat->dst : pat->src;
			if (!match_name_with_pattern(pat->src, src->name,
						     dst_side, &dst_name))
				die("Didn't think it matches any more");
		}
		dst_peer = find_ref_by_name(*dst, dst_name);
		if (dst_peer) {
			if (dst_peer->peer_ref)
				/* We're already sending something to this ref. */
				goto free_name;

		} else {
			if (pat->matching && !(send_all || send_mirror))
				/*
				 * Remote doesn't have it, and we have no
				 * explicit pattern, and we don't have
				 * --all nor --mirror.
				 */
				goto free_name;

			/* Create a new one and link it */
			dst_peer = make_linked_ref(dst_name, &dst_tail);
			hashcpy(dst_peer->new_sha1, src->new_sha1);
		}
		dst_peer->peer_ref = copy_ref(src);
		dst_peer->force = pat->force;
	free_name:
		free(dst_name);
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

		/* This part determines what can overwrite what.
		 * The rules are:
		 *
		 * (0) you can always use --force or +A:B notation to
		 *     selectively force individual ref pairs.
		 *
		 * (1) if the old thing does not exist, it is OK.
		 *
		 * (2) if you do not have the old thing, you are not allowed
		 *     to overwrite it; you would not know what you are losing
		 *     otherwise.
		 *
		 * (3) if both new and old are commit-ish, and new is a
		 *     descendant of old, it is OK.
		 *
		 * (4) regardless of all of the above, removing :B is
		 *     always allowed.
		 */

		ref->nonfastforward =
			!ref->deletion &&
			!is_null_sha1(ref->old_sha1) &&
			(!has_sha1_file(ref->old_sha1)
			  || !ref_newer(ref->new_sha1, ref->old_sha1));

		if (ref->nonfastforward && !ref->force && !force_update) {
			ref->status = REF_STATUS_REJECT_NONFASTFORWARD;
			continue;
		}
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
			ret->merge = xcalloc(sizeof(*ret->merge),
					     ret->merge_nr);
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
	return refname_match(branch->merge[i]->src, refname, ref_fetch_rules);
}

static struct ref *get_expanded_map(const struct ref *remote_refs,
				    const struct refspec *refspec)
{
	const struct ref *ref;
	struct ref *ret = NULL;
	struct ref **tail = &ret;

	char *expn_name;

	for (ref = remote_refs; ref; ref = ref->next) {
		if (strchr(ref->name, '^'))
			continue; /* a dereference item */
		if (match_name_with_pattern(refspec->src, ref->name,
					    refspec->dst, &expn_name)) {
			struct ref *cpy = copy_ref(ref);

			cpy->peer_ref = alloc_ref(expn_name);
			free(expn_name);
			if (refspec->force)
				cpy->peer_ref->force = 1;
			*tail = cpy;
			tail = &cpy->next;
		}
	}

	return ret;
}

static const struct ref *find_ref_by_name_abbrev(const struct ref *refs, const char *name)
{
	const struct ref *ref;
	for (ref = refs; ref; ref = ref->next) {
		if (refname_match(name, ref->name, ref_fetch_rules))
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

	if (!prefixcmp(name, "refs/"))
		return alloc_ref(name);

	if (!prefixcmp(name, "heads/") ||
	    !prefixcmp(name, "tags/") ||
	    !prefixcmp(name, "remotes/"))
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

		ref_map = get_remote_ref(remote_refs, name);
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
			int st = check_ref_format((*rmp)->peer_ref->name + 5);
			if (st && st != CHECK_REF_FORMAT_ONELEVEL) {
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

	/* Both new and old must be commit-ish and new is descendant of
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
 * Return true if there is anything to report, otherwise false.
 */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs)
{
	unsigned char sha1[20];
	struct commit *ours, *theirs;
	char symmetric[84];
	struct rev_info revs;
	const char *rev_argv[10], *base;
	int rev_argc;

	/*
	 * Nothing to report unless we are marked to build on top of
	 * somebody else.
	 */
	if (!branch ||
	    !branch->merge || !branch->merge[0] || !branch->merge[0]->dst)
		return 0;

	/*
	 * If what we used to build on no longer exists, there is
	 * nothing to report.
	 */
	base = branch->merge[0]->dst;
	if (!resolve_ref(base, sha1, 1, NULL))
		return 0;
	theirs = lookup_commit_reference(sha1);
	if (!theirs)
		return 0;

	if (!resolve_ref(branch->refname, sha1, 1, NULL))
		return 0;
	ours = lookup_commit_reference(sha1);
	if (!ours)
		return 0;

	/* are we the same? */
	if (theirs == ours)
		return 0;

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
	int num_ours, num_theirs;
	const char *base;

	if (!stat_tracking_info(branch, &num_ours, &num_theirs))
		return 0;

	base = branch->merge[0]->dst;
	base = shorten_unambiguous_ref(base, 0);
	if (!num_theirs)
		strbuf_addf(sb, "Your branch is ahead of '%s' "
			    "by %d commit%s.\n",
			    base, num_ours, (num_ours == 1) ? "" : "s");
	else if (!num_ours)
		strbuf_addf(sb, "Your branch is behind '%s' "
			    "by %d commit%s, "
			    "and can be fast-forwarded.\n",
			    base, num_theirs, (num_theirs == 1) ? "" : "s");
	else
		strbuf_addf(sb, "Your branch and '%s' have diverged,\n"
			    "and have %d and %d different commit(s) each, "
			    "respectively.\n",
			    base, num_ours, num_theirs);
	return 1;
}

static int one_local_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct ref ***local_tail = cb_data;
	struct ref *ref;
	int len;

	/* we already know it starts with refs/ to get here */
	if (check_ref_format(refname + 5))
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
		if (r != head && !hashcmp(r->old_sha1, head->old_sha1)) {
			*tail = copy_ref(r);
			tail = &((*tail)->next);
			if (!all)
				break;
		}
	}

	return list;
}

struct stale_heads_info {
	struct remote *remote;
	struct string_list *ref_names;
	struct ref **stale_refs_tail;
};

static int get_stale_heads_cb(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct stale_heads_info *info = cb_data;
	struct refspec refspec;
	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (!remote_find_tracking(info->remote, &refspec)) {
		if (!((flags & REF_ISSYMREF) ||
		    string_list_has_string(info->ref_names, refspec.src))) {
			struct ref *ref = make_linked_ref(refname, &info->stale_refs_tail);
			hashcpy(ref->new_sha1, sha1);
		}
	}
	return 0;
}

struct ref *get_stale_heads(struct remote *remote, struct ref *fetch_map)
{
	struct ref *ref, *stale_refs = NULL;
	struct string_list ref_names = { NULL, 0, 0, 0 };
	struct stale_heads_info info;
	info.remote = remote;
	info.ref_names = &ref_names;
	info.stale_refs_tail = &stale_refs;
	for (ref = fetch_map; ref; ref = ref->next)
		string_list_append(&ref_names, ref->name);
	sort_string_list(&ref_names);
	for_each_ref(get_stale_heads_cb, &info);
	string_list_clear(&ref_names, 0);
	return stale_refs;
}
