#include "cache.h"
#include "remote.h"
#include "refs.h"

static struct remote **remotes;
static int allocated_remotes;

#define BUF_SIZE (2048)
static char buffer[BUF_SIZE];

static void add_push_refspec(struct remote *remote, const char *ref)
{
	int nr = remote->push_refspec_nr + 1;
	remote->push_refspec =
		xrealloc(remote->push_refspec, nr * sizeof(char *));
	remote->push_refspec[nr-1] = ref;
	remote->push_refspec_nr = nr;
}

static void add_fetch_refspec(struct remote *remote, const char *ref)
{
	int nr = remote->fetch_refspec_nr + 1;
	remote->fetch_refspec =
		xrealloc(remote->fetch_refspec, nr * sizeof(char *));
	remote->fetch_refspec[nr-1] = ref;
	remote->fetch_refspec_nr = nr;
}

static void add_uri(struct remote *remote, const char *uri)
{
	int nr = remote->uri_nr + 1;
	remote->uri =
		xrealloc(remote->uri, nr * sizeof(char *));
	remote->uri[nr-1] = uri;
	remote->uri_nr = nr;
}

static struct remote *make_remote(const char *name, int len)
{
	int i, empty = -1;

	for (i = 0; i < allocated_remotes; i++) {
		if (!remotes[i]) {
			if (empty < 0)
				empty = i;
		} else {
			if (len ? (!strncmp(name, remotes[i]->name, len) &&
				   !remotes[i]->name[len]) :
			    !strcmp(name, remotes[i]->name))
				return remotes[i];
		}
	}

	if (empty < 0) {
		empty = allocated_remotes;
		allocated_remotes += allocated_remotes ? allocated_remotes : 1;
		remotes = xrealloc(remotes,
				   sizeof(*remotes) * allocated_remotes);
		memset(remotes + empty, 0,
		       (allocated_remotes - empty) * sizeof(*remotes));
	}
	remotes[empty] = xcalloc(1, sizeof(struct remote));
	if (len)
		remotes[empty]->name = xstrndup(name, len);
	else
		remotes[empty]->name = xstrdup(name);
	return remotes[empty];
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
			add_uri(remote, xstrdup(s));
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
	add_uri(remote, p);
}

static char *default_remote_name = NULL;
static const char *current_branch = NULL;
static int current_branch_len = 0;

static int handle_config(const char *key, const char *value)
{
	const char *name;
	const char *subkey;
	struct remote *remote;
	if (!prefixcmp(key, "branch.") && current_branch &&
	    !strncmp(key + 7, current_branch, current_branch_len) &&
	    !strcmp(key + 7 + current_branch_len, ".remote")) {
		free(default_remote_name);
		default_remote_name = xstrdup(value);
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
		add_uri(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".push")) {
		add_push_refspec(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".fetch")) {
		add_fetch_refspec(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".receivepack")) {
		if (!remote->receivepack)
			remote->receivepack = xstrdup(value);
		else
			error("more than one receivepack given, using the first");
	}
	return 0;
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
		current_branch = head_ref + strlen("refs/heads/");
		current_branch_len = strlen(current_branch);
	}
	git_config(handle_config);
}

static struct refspec *parse_ref_spec(int nr_refspec, const char **refspec)
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

struct remote *remote_get(const char *name)
{
	struct remote *ret;

	read_config();
	if (!name)
		name = default_remote_name;
	ret = make_remote(name, 0);
	if (name[0] != '/') {
		if (!ret->uri)
			read_remotes_file(ret);
		if (!ret->uri)
			read_branches_file(ret);
	}
	if (!ret->uri)
		add_uri(ret, name);
	if (!ret->uri)
		return NULL;
	ret->fetch = parse_ref_spec(ret->fetch_refspec_nr, ret->fetch_refspec);
	ret->push = parse_ref_spec(ret->push_refspec_nr, ret->push_refspec);
	return ret;
}

int remote_has_uri(struct remote *remote, const char *uri)
{
	int i;
	for (i = 0; i < remote->uri_nr; i++) {
		if (!strcmp(remote->uri[i], uri))
			return 1;
	}
	return 0;
}

int remote_find_tracking(struct remote *remote, struct refspec *refspec)
{
	int i;
	for (i = 0; i < remote->fetch_refspec_nr; i++) {
		struct refspec *fetch = &remote->fetch[i];
		if (!fetch->dst)
			continue;
		if (fetch->pattern) {
			if (!prefixcmp(refspec->src, fetch->src)) {
				refspec->dst =
					xmalloc(strlen(fetch->dst) +
						strlen(refspec->src) -
						strlen(fetch->src) + 1);
				strcpy(refspec->dst, fetch->dst);
				strcpy(refspec->dst + strlen(fetch->dst),
				       refspec->src + strlen(fetch->src));
				refspec->force = fetch->force;
				return 0;
			}
		} else {
			if (!strcmp(refspec->src, fetch->src)) {
				refspec->dst = xstrdup(fetch->dst);
				refspec->force = fetch->force;
				return 0;
			}
		}
	}
	refspec->dst = NULL;
	return -1;
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
		int weak_match;

		if (namelen < patlen ||
		    memcmp(name + namelen - patlen, pattern, patlen))
			continue;
		if (namelen != patlen && name[namelen - patlen - 1] != '/')
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

static void link_dst_tail(struct ref *ref, struct ref ***tail)
{
	**tail = ref;
	*tail = &ref->next;
	**tail = NULL;
}

static struct ref *try_explicit_object_name(const char *name)
{
	unsigned char sha1[20];
	struct ref *ref;
	int len;

	if (!*name) {
		ref = xcalloc(1, sizeof(*ref) + 20);
		strcpy(ref->name, "(delete)");
		hashclr(ref->new_sha1);
		return ref;
	}
	if (get_sha1(name, sha1))
		return NULL;
	len = strlen(name) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->name, name, len);
	hashcpy(ref->new_sha1, sha1);
	return ref;
}

static int match_explicit_refs(struct ref *src, struct ref *dst,
			       struct ref ***dst_tail, struct refspec *rs,
			       int rs_nr)
{
	int i, errs;
	for (i = errs = 0; i < rs_nr; i++) {
		struct ref *matched_src, *matched_dst;

		const char *dst_value = rs[i].dst;

		if (rs[i].pattern)
			continue;

		if (dst_value == NULL)
			dst_value = rs[i].src;

		matched_src = matched_dst = NULL;
		switch (count_refspec_match(rs[i].src, src, &matched_src)) {
		case 1:
			break;
		case 0:
			/* The source could be in the get_sha1() format
			 * not a reference name.  :refs/other is a
			 * way to delete 'other' ref at the remote end.
			 */
			matched_src = try_explicit_object_name(rs[i].src);
			if (matched_src)
				break;
			errs = 1;
			error("src refspec %s does not match any.",
			      rs[i].src);
			break;
		default:
			errs = 1;
			error("src refspec %s matches more than one.",
			      rs[i].src);
			break;
		}
		switch (count_refspec_match(dst_value, dst, &matched_dst)) {
		case 1:
			break;
		case 0:
			if (!memcmp(dst_value, "refs/", 5)) {
				int len = strlen(dst_value) + 1;
				matched_dst = xcalloc(1, sizeof(*dst) + len);
				memcpy(matched_dst->name, dst_value, len);
				link_dst_tail(matched_dst, dst_tail);
			}
			else if (!strcmp(rs[i].src, dst_value) &&
				 matched_src) {
				/* pushing "master:master" when
				 * remote does not have master yet.
				 */
				int len = strlen(matched_src->name) + 1;
				matched_dst = xcalloc(1, sizeof(*dst) + len);
				memcpy(matched_dst->name, matched_src->name,
				       len);
				link_dst_tail(matched_dst, dst_tail);
			}
			else {
				errs = 1;
				error("dst refspec %s does not match any "
				      "existing ref on the remote and does "
				      "not start with refs/.", dst_value);
			}
			break;
		default:
			errs = 1;
			error("dst refspec %s matches more than one.",
			      dst_value);
			break;
		}
		if (errs)
			continue;
		if (matched_dst->peer_ref) {
			errs = 1;
			error("dst ref %s receives from more than one src.",
			      matched_dst->name);
		}
		else {
			matched_dst->peer_ref = matched_src;
			matched_dst->force = rs[i].force;
		}
	}
	return -errs;
}

static struct ref *find_ref_by_name(struct ref *list, const char *name)
{
	for ( ; list; list = list->next)
		if (!strcmp(list->name, name))
			return list;
	return NULL;
}

static int check_pattern_match(struct refspec *rs, int rs_nr, struct ref *src)
{
	int i;
	if (!rs_nr)
		return 1;
	for (i = 0; i < rs_nr; i++) {
		if (rs[i].pattern && !prefixcmp(src->name, rs[i].src))
			return 1;
	}
	return 0;
}

int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
	       int nr_refspec, char **refspec, int all)
{
	struct refspec *rs =
		parse_ref_spec(nr_refspec, (const char **) refspec);

	if (match_explicit_refs(src, dst, dst_tail, rs, nr_refspec))
		return -1;

	/* pick the remainder */
	for ( ; src; src = src->next) {
		struct ref *dst_peer;
		if (src->peer_ref)
			continue;
		if (!check_pattern_match(rs, nr_refspec, src))
			continue;

		dst_peer = find_ref_by_name(dst, src->name);
		if (dst_peer && dst_peer->peer_ref)
			/* We're already sending something to this ref. */
			continue;
		if (!dst_peer && !nr_refspec && !all)
			/* Remote doesn't have it, and we have no
			 * explicit pattern, and we don't have
			 * --all. */
			continue;
		if (!dst_peer) {
			/* Create a new one and link it */
			int len = strlen(src->name) + 1;
			dst_peer = xcalloc(1, sizeof(*dst_peer) + len);
			memcpy(dst_peer->name, src->name, len);
			hashcpy(dst_peer->new_sha1, src->new_sha1);
			link_dst_tail(dst_peer, dst_tail);
		}
		dst_peer->peer_ref = src;
	}
	return 0;
}
