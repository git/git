#include "cache.h"
#include "remote.h"
#include "refs.h"

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

static struct remote **remotes;
static int remotes_alloc;
static int remotes_nr;

static struct branch **branches;
static int branches_alloc;
static int branches_nr;

static struct branch *current_branch;
static const char *default_remote_name;

static struct rewrite **rewrite;
static int rewrite_alloc;
static int rewrite_nr;

#define BUF_SIZE (2048)
static char buffer[BUF_SIZE];

static const char *alias_url(const char *url)
{
	int i, j;
	char *ret;
	struct counted_string *longest;
	int longest_i;

	longest = NULL;
	longest_i = -1;
	for (i = 0; i < rewrite_nr; i++) {
		if (!rewrite[i])
			continue;
		for (j = 0; j < rewrite[i]->instead_of_nr; j++) {
			if (!prefixcmp(url, rewrite[i]->instead_of[j].s) &&
			    (!longest ||
			     longest->len < rewrite[i]->instead_of[j].len)) {
				longest = &(rewrite[i]->instead_of[j]);
				longest_i = i;
			}
		}
	}
	if (!longest)
		return url;

	ret = malloc(rewrite[longest_i]->baselen +
		     (strlen(url) - longest->len) + 1);
	strcpy(ret, rewrite[longest_i]->base);
	strcpy(ret + rewrite[longest_i]->baselen, url + longest->len);
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

static void add_url_alias(struct remote *remote, const char *url)
{
	add_url(remote, alias_url(url));
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
	refname = malloc(strlen(name) + strlen("refs/heads/") + 1);
	strcpy(refname, "refs/heads/");
	strcpy(refname + strlen("refs/heads/"), ret->name);
	ret->refname = refname;

	return ret;
}

static struct rewrite *make_rewrite(const char *base, int len)
{
	struct rewrite *ret;
	int i;

	for (i = 0; i < rewrite_nr; i++) {
		if (len
		    ? (len == rewrite[i]->baselen &&
		       !strncmp(base, rewrite[i]->base, len))
		    : !strcmp(base, rewrite[i]->base))
			return rewrite[i];
	}

	ALLOC_GROW(rewrite, rewrite_nr + 1, rewrite_alloc);
	ret = xcalloc(1, sizeof(struct rewrite));
	rewrite[rewrite_nr++] = ret;
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
	char *branch;
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
	frag = strchr(p, '#');
	if (frag) {
		*(frag++) = '\0';
		branch = xmalloc(strlen(frag) + 12);
		strcpy(branch, "refs/heads/");
		strcat(branch, frag);
	} else {
		branch = "refs/heads/master";
	}
	add_url_alias(remote, p);
	add_fetch_refspec(remote, branch);
	remote->fetch_tags = 1; /* always auto-follow */
}

static int handle_config(const char *key, const char *value)
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
			if (branch == current_branch)
				default_remote_name = branch->remote_name;
		} else if (!strcmp(subkey, ".merge")) {
			if (!value)
				return config_error_nonbool(key);
			add_merge(branch, xstrdup(value));
		}
		return 0;
	}
	if (!prefixcmp(key, "url.")) {
		struct rewrite *rewrite;
		name = key + 5;
		subkey = strrchr(name, '.');
		if (!subkey)
			return 0;
		rewrite = make_rewrite(name, subkey - name);
		if (!strcmp(subkey, ".insteadof")) {
			if (!value)
				return config_error_nonbool(key);
			add_instead_of(rewrite, xstrdup(value));
		}
	}
	if (prefixcmp(key,  "remote."))
		return 0;
	name = key + 7;
	subkey = strrchr(name, '.');
	if (!subkey)
		return error("Config with no key for remote %s", name);
	if (*subkey == '/') {
		warning("Config remote shorthand cannot begin with '/': %s", name);
		return 0;
	}
	remote = make_remote(name, subkey - name);
	if (!value) {
		/* if we ever have a boolean variable, e.g. "remote.*.disabled"
		 * [remote "frotz"]
		 *      disabled
		 * is a valid way to set it to true; we get NULL in value so
		 * we need to handle it here.
		 *
		 * if (!strcmp(subkey, ".disabled")) {
		 *      val = git_config_bool(key, value);
		 *      return 0;
		 * } else
		 *
		 */
		return 0; /* ignore unknown booleans */
	}
	if (!strcmp(subkey, ".url")) {
		add_url(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".push")) {
		add_push_refspec(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".fetch")) {
		add_fetch_refspec(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".receivepack")) {
		if (!remote->receivepack)
			remote->receivepack = xstrdup(value);
		else
			error("more than one receivepack given, using the first");
	} else if (!strcmp(subkey, ".uploadpack")) {
		if (!remote->uploadpack)
			remote->uploadpack = xstrdup(value);
		else
			error("more than one uploadpack given, using the first");
	} else if (!strcmp(subkey, ".tagopt")) {
		if (!strcmp(value, "--no-tags"))
			remote->fetch_tags = -1;
	} else if (!strcmp(subkey, ".proxy")) {
		remote->http_proxy = xstrdup(value);
	} else if (!strcmp(subkey, ".skipdefaultupdate"))
		remote->skip_default_update = 1;
	return 0;
}

static void alias_all_urls(void)
{
	int i, j;
	for (i = 0; i < remotes_nr; i++) {
		if (!remotes[i])
			continue;
		for (j = 0; j < remotes[i]->url_nr; j++) {
			remotes[i]->url[j] = alias_url(remotes[i]->url[j]);
		}
	}
}

static void read_config(void)
{
	unsigned char sha1[20];
	const char *head_ref;
	int flag;
	if (default_remote_name) // did this already
		return;
	default_remote_name = xstrdup("origin");
	current_branch = NULL;
	head_ref = resolve_ref("HEAD", sha1, 0, &flag);
	if (head_ref && (flag & REF_ISSYMREF) &&
	    !prefixcmp(head_ref, "refs/heads/")) {
		current_branch =
			make_branch(head_ref + strlen("refs/heads/"), 0);
	}
	git_config(handle_config);
	alias_all_urls();
}

struct refspec *parse_ref_spec(int nr_refspec, const char **refspec)
{
	int i;
	struct refspec *rs = xcalloc(sizeof(*rs), nr_refspec);
	for (i = 0; i < nr_refspec; i++) {
		const char *sp, *ep, *gp;
		sp = refspec[i];
		if (*sp == '+') {
			rs[i].force = 1;
			sp++;
		}
		gp = strchr(sp, '*');
		ep = strchr(sp, ':');
		if (gp && ep && gp > ep)
			gp = NULL;
		if (ep) {
			if (ep[1]) {
				const char *glob = strchr(ep + 1, '*');
				if (!glob)
					gp = NULL;
				if (gp)
					rs[i].dst = xstrndup(ep + 1,
							     glob - ep - 1);
				else
					rs[i].dst = xstrdup(ep + 1);
			}
		} else {
			ep = sp + strlen(sp);
		}
		if (gp) {
			rs[i].pattern = 1;
			ep = gp;
		}
		rs[i].src = xstrndup(sp, ep - sp);
	}
	return rs;
}

static int valid_remote_nick(const char *name)
{
	if (!name[0] || /* not empty */
	    (name[0] == '.' && /* not "." */
	     (!name[1] || /* not ".." */
	      (name[1] == '.' && !name[2]))))
		return 0;
	return !strchr(name, '/'); /* no slash */
}

struct remote *remote_get(const char *name)
{
	struct remote *ret;

	read_config();
	if (!name)
		name = default_remote_name;
	ret = make_remote(name, 0);
	if (valid_remote_nick(name)) {
		if (!ret->url)
			read_remotes_file(ret);
		if (!ret->url)
			read_branches_file(ret);
	}
	if (!ret->url)
		add_url_alias(ret, name);
	if (!ret->url)
		return NULL;
	ret->fetch = parse_ref_spec(ret->fetch_refspec_nr, ret->fetch_refspec);
	ret->push = parse_ref_spec(ret->push_refspec_nr, ret->push_refspec);
	return ret;
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
			r->fetch = parse_ref_spec(r->fetch_refspec_nr,
					r->fetch_refspec);
		if (!r->push)
			r->push = parse_ref_spec(r->push_refspec_nr,
					r->push_refspec);
		result = fn(r, priv);
	}
	return result;
}

void ref_remove_duplicates(struct ref *ref_map)
{
	struct ref **posn;
	struct ref *next;
	for (; ref_map; ref_map = ref_map->next) {
		if (!ref_map->peer_ref)
			continue;
		posn = &ref_map->next;
		while (*posn) {
			if ((*posn)->peer_ref &&
			    !strcmp((*posn)->peer_ref->name,
				    ref_map->peer_ref->name)) {
				if (strcmp((*posn)->name, ref_map->name))
					die("%s tracks both %s and %s",
					    ref_map->peer_ref->name,
					    (*posn)->name, ref_map->name);
				next = (*posn)->next;
				free((*posn)->peer_ref);
				free(*posn);
				*posn = next;
			} else {
				posn = &(*posn)->next;
			}
		}
	}
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
			if (!prefixcmp(needle, key)) {
				*result = xmalloc(strlen(value) +
						  strlen(needle) -
						  strlen(key) + 1);
				strcpy(*result, value);
				strcpy(*result + strlen(value),
				       needle + strlen(key));
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

struct ref *alloc_ref(unsigned namelen)
{
	struct ref *ret = xmalloc(sizeof(struct ref) + namelen);
	memset(ret, 0, sizeof(struct ref) + namelen);
	return ret;
}

static struct ref *copy_ref(const struct ref *ref)
{
	struct ref *ret = xmalloc(sizeof(struct ref) + strlen(ref->name) + 1);
	memcpy(ret, ref, sizeof(struct ref) + strlen(ref->name) + 1);
	ret->next = NULL;
	return ret;
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

void free_refs(struct ref *ref)
{
	struct ref *next;
	while (ref) {
		next = ref->next;
		free(ref->peer_ref);
		free(ref);
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
	int len;

	if (!*name) {
		ref = alloc_ref(20);
		strcpy(ref->name, "(delete)");
		hashclr(ref->new_sha1);
		return ref;
	}
	if (get_sha1(name, sha1))
		return NULL;
	len = strlen(name) + 1;
	ref = alloc_ref(len);
	memcpy(ref->name, name, len);
	hashcpy(ref->new_sha1, sha1);
	return ref;
}

static struct ref *make_linked_ref(const char *name, struct ref ***tail)
{
	struct ref *ret;
	size_t len;

	len = strlen(name) + 1;
	ret = alloc_ref(len);
	memcpy(ret->name, name, len);
	tail_link_ref(ret, tail);
	return ret;
}

static int match_explicit(struct ref *src, struct ref *dst,
			  struct ref ***dst_tail,
			  struct refspec *rs,
			  int errs)
{
	struct ref *matched_src, *matched_dst;

	const char *dst_value = rs->dst;

	if (rs->pattern)
		return errs;

	matched_src = matched_dst = NULL;
	switch (count_refspec_match(rs->src, src, &matched_src)) {
	case 1:
		break;
	case 0:
		/* The source could be in the get_sha1() format
		 * not a reference name.  :refs/other is a
		 * way to delete 'other' ref at the remote end.
		 */
		matched_src = try_explicit_object_name(rs->src);
		if (!matched_src)
			error("src refspec %s does not match any.", rs->src);
		break;
	default:
		matched_src = NULL;
		error("src refspec %s matches more than one.", rs->src);
		break;
	}

	if (!matched_src)
		errs = 1;

	if (!dst_value) {
		unsigned char sha1[20];
		int flag;

		if (!matched_src)
			return errs;
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
		else
			error("dst refspec %s does not match any "
			      "existing ref on the remote and does "
			      "not start with refs/.", dst_value);
		break;
	default:
		matched_dst = NULL;
		error("dst refspec %s matches more than one.",
		      dst_value);
		break;
	}
	if (errs || !matched_dst)
		return 1;
	if (matched_dst->peer_ref) {
		errs = 1;
		error("dst ref %s receives from more than one src.",
		      matched_dst->name);
	}
	else {
		matched_dst->peer_ref = matched_src;
		matched_dst->force = rs->force;
	}
	return errs;
}

static int match_explicit_refs(struct ref *src, struct ref *dst,
			       struct ref ***dst_tail, struct refspec *rs,
			       int rs_nr)
{
	int i, errs;
	for (i = errs = 0; i < rs_nr; i++)
		errs |= match_explicit(src, dst, dst_tail, &rs[i], errs);
	return -errs;
}

static const struct refspec *check_pattern_match(const struct refspec *rs,
						 int rs_nr,
						 const struct ref *src)
{
	int i;
	for (i = 0; i < rs_nr; i++) {
		if (rs[i].pattern && !prefixcmp(src->name, rs[i].src))
			return rs + i;
	}
	return NULL;
}

/*
 * Note. This is used only by "push"; refspec matching rules for
 * push and fetch are subtly different, so do not try to reuse it
 * without thinking.
 */
int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
	       int nr_refspec, const char **refspec, int flags)
{
	struct refspec *rs =
		parse_ref_spec(nr_refspec, (const char **) refspec);
	int send_all = flags & MATCH_REFS_ALL;
	int send_mirror = flags & MATCH_REFS_MIRROR;

	if (match_explicit_refs(src, dst, dst_tail, rs, nr_refspec))
		return -1;

	/* pick the remainder */
	for ( ; src; src = src->next) {
		struct ref *dst_peer;
		const struct refspec *pat = NULL;
		char *dst_name;
		if (src->peer_ref)
			continue;
		if (nr_refspec) {
			pat = check_pattern_match(rs, nr_refspec, src);
			if (!pat)
				continue;
		}
		else if (!send_mirror && prefixcmp(src->name, "refs/heads/"))
			/*
			 * "matching refs"; traditionally we pushed everything
			 * including refs outside refs/heads/ hierarchy, but
			 * that does not make much sense these days.
			 */
			continue;

		if (pat) {
			const char *dst_side = pat->dst ? pat->dst : pat->src;
			dst_name = xmalloc(strlen(dst_side) +
					   strlen(src->name) -
					   strlen(pat->src) + 2);
			strcpy(dst_name, dst_side);
			strcat(dst_name, src->name + strlen(pat->src));
		} else
			dst_name = xstrdup(src->name);
		dst_peer = find_ref_by_name(dst, dst_name);
		if (dst_peer && dst_peer->peer_ref)
			/* We're already sending something to this ref. */
			goto free_name;

		if (!dst_peer && !nr_refspec && !(send_all || send_mirror))
			/*
			 * Remote doesn't have it, and we have no
			 * explicit pattern, and we don't have
			 * --all nor --mirror.
			 */
			goto free_name;
		if (!dst_peer) {
			/* Create a new one and link it */
			dst_peer = make_linked_ref(dst_name, dst_tail);
			hashcpy(dst_peer->new_sha1, src->new_sha1);
		}
		dst_peer->peer_ref = src;
		if (pat)
			dst_peer->force = pat->force;
	free_name:
		free(dst_name);
	}
	return 0;
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
				remote_find_tracking(ret->remote,
						     ret->merge[i]);
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

	int remote_prefix_len = strlen(refspec->src);
	int local_prefix_len = strlen(refspec->dst);

	for (ref = remote_refs; ref; ref = ref->next) {
		if (strchr(ref->name, '^'))
			continue; /* a dereference item */
		if (!prefixcmp(ref->name, refspec->src)) {
			const char *match;
			struct ref *cpy = copy_ref(ref);
			match = ref->name + remote_prefix_len;

			cpy->peer_ref = alloc_ref(local_prefix_len +
						  strlen(match) + 1);
			sprintf(cpy->peer_ref->name, "%s%s",
				refspec->dst, match);
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
	struct ref *ret;
	if (!name)
		return NULL;

	if (!prefixcmp(name, "refs/")) {
		ret = alloc_ref(strlen(name) + 1);
		strcpy(ret->name, name);
		return ret;
	}

	if (!prefixcmp(name, "heads/") ||
	    !prefixcmp(name, "tags/") ||
	    !prefixcmp(name, "remotes/")) {
		ret = alloc_ref(strlen(name) + 6);
		sprintf(ret->name, "refs/%s", name);
		return ret;
	}

	ret = alloc_ref(strlen(name) + 12);
	sprintf(ret->name, "refs/heads/%s", name);
	return ret;
}

int get_fetch_map(const struct ref *remote_refs,
		  const struct refspec *refspec,
		  struct ref ***tail,
		  int missing_ok)
{
	struct ref *ref_map, *rm;

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

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref && check_ref_format(rm->peer_ref->name + 5))
			die("* refusing to create funny ref '%s' locally",
			    rm->peer_ref->name);
	}

	if (ref_map)
		tail_link_ref(ref_map, tail);

	return 0;
}
