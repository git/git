#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "abspath.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "remote.h"
#include "urlmatch.h"
#include "refs.h"
#include "refspec.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "path.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "dir.h"
#include "setup.h"
#include "string-list.h"
#include "strvec.h"
#include "commit-reach.h"
#include "advice.h"
#include "connect.h"
#include "parse-options.h"
#include "transport.h"

enum map_direction { FROM_SRC, FROM_DST };

struct counted_string {
	size_t len;
	const char *s;
};

static int valid_remote(const struct remote *remote)
{
	return !!remote->url.nr;
}

static char *alias_url(const char *url, struct rewrites *r)
{
	int i, j;
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
		return NULL;

	return xstrfmt("%s%s", r->rewrite[longest_i]->base, url + longest->len);
}

static void add_url(struct remote *remote, const char *url)
{
	if (*url)
		strvec_push(&remote->url, url);
	else
		strvec_clear(&remote->url);
}

static void add_pushurl(struct remote *remote, const char *pushurl)
{
	if (*pushurl)
		strvec_push(&remote->pushurl, pushurl);
	else
		strvec_clear(&remote->pushurl);
}

static void add_pushurl_alias(struct remote_state *remote_state,
			      struct remote *remote, const char *url)
{
	char *alias = alias_url(url, &remote_state->rewrites_push);
	if (alias)
		add_pushurl(remote, alias);
	free(alias);
}

static void add_url_alias(struct remote_state *remote_state,
			  struct remote *remote, const char *url)
{
	char *alias = alias_url(url, &remote_state->rewrites);
	add_url(remote, alias ? alias : url);
	add_pushurl_alias(remote_state, remote, url);
	free(alias);
}

struct remotes_hash_key {
	const char *str;
	int len;
};

static int remotes_hash_cmp(const void *cmp_data UNUSED,
			    const struct hashmap_entry *eptr,
			    const struct hashmap_entry *entry_or_key,
			    const void *keydata)
{
	const struct remote *a, *b;
	const struct remotes_hash_key *key = keydata;

	a = container_of(eptr, const struct remote, ent);
	b = container_of(entry_or_key, const struct remote, ent);

	if (key)
		return !!xstrncmpz(a->name, key->str, key->len);
	else
		return strcmp(a->name, b->name);
}

static struct remote *make_remote(struct remote_state *remote_state,
				  const char *name, int len)
{
	struct remote *ret;
	struct remotes_hash_key lookup;
	struct hashmap_entry lookup_entry, *e;

	if (!len)
		len = strlen(name);

	lookup.str = name;
	lookup.len = len;
	hashmap_entry_init(&lookup_entry, memhash(name, len));

	e = hashmap_get(&remote_state->remotes_hash, &lookup_entry, &lookup);
	if (e)
		return container_of(e, struct remote, ent);

	CALLOC_ARRAY(ret, 1);
	ret->prune = -1;  /* unspecified */
	ret->prune_tags = -1;  /* unspecified */
	ret->name = xstrndup(name, len);
	refspec_init(&ret->push, REFSPEC_PUSH);
	refspec_init(&ret->fetch, REFSPEC_FETCH);
	string_list_init_dup(&ret->server_options);

	ALLOC_GROW(remote_state->remotes, remote_state->remotes_nr + 1,
		   remote_state->remotes_alloc);
	remote_state->remotes[remote_state->remotes_nr++] = ret;

	hashmap_entry_init(&ret->ent, lookup_entry.hash);
	if (hashmap_put_entry(&remote_state->remotes_hash, ret, ent))
		BUG("hashmap_put overwrote entry after hashmap_get returned NULL");
	return ret;
}

static void remote_clear(struct remote *remote)
{
	free((char *)remote->name);
	free((char *)remote->foreign_vcs);

	strvec_clear(&remote->url);
	strvec_clear(&remote->pushurl);

	free((char *)remote->receivepack);
	free((char *)remote->uploadpack);
	FREE_AND_NULL(remote->http_proxy);
	FREE_AND_NULL(remote->http_proxy_authmethod);
	string_list_clear(&remote->server_options, 0);
}

static void add_merge(struct branch *branch, const char *name)
{
	ALLOC_GROW(branch->merge_name, branch->merge_nr + 1,
		   branch->merge_alloc);
	branch->merge_name[branch->merge_nr++] = name;
}

struct branches_hash_key {
	const char *str;
	int len;
};

static int branches_hash_cmp(const void *cmp_data UNUSED,
			     const struct hashmap_entry *eptr,
			     const struct hashmap_entry *entry_or_key,
			     const void *keydata)
{
	const struct branch *a, *b;
	const struct branches_hash_key *key = keydata;

	a = container_of(eptr, const struct branch, ent);
	b = container_of(entry_or_key, const struct branch, ent);

	if (key)
		return !!xstrncmpz(a->name, key->str, key->len);
	else
		return strcmp(a->name, b->name);
}

static struct branch *find_branch(struct remote_state *remote_state,
				  const char *name, size_t len)
{
	struct branches_hash_key lookup;
	struct hashmap_entry lookup_entry, *e;

	lookup.str = name;
	lookup.len = len;
	hashmap_entry_init(&lookup_entry, memhash(name, len));

	e = hashmap_get(&remote_state->branches_hash, &lookup_entry, &lookup);
	if (e)
		return container_of(e, struct branch, ent);

	return NULL;
}

static void die_on_missing_branch(struct repository *repo,
				  struct branch *branch)
{
	/* branch == NULL is always valid because it represents detached HEAD. */
	if (branch &&
	    branch != find_branch(repo->remote_state, branch->name,
				  strlen(branch->name)))
		die("branch %s was not found in the repository", branch->name);
}

static struct branch *make_branch(struct remote_state *remote_state,
				  const char *name, size_t len)
{
	struct branch *ret;

	ret = find_branch(remote_state, name, len);
	if (ret)
		return ret;

	CALLOC_ARRAY(ret, 1);
	ret->name = xstrndup(name, len);
	ret->refname = xstrfmt("refs/heads/%s", ret->name);

	hashmap_entry_init(&ret->ent, memhash(name, len));
	if (hashmap_put_entry(&remote_state->branches_hash, ret, ent))
		BUG("hashmap_put overwrote entry after hashmap_get returned NULL");
	return ret;
}

static void branch_release(struct branch *branch)
{
	free((char *)branch->name);
	free((char *)branch->refname);
	free(branch->remote_name);
	free(branch->pushremote_name);
	for (int i = 0; i < branch->merge_nr; i++)
		refspec_item_clear(branch->merge[i]);
	free(branch->merge);
}

static struct rewrite *make_rewrite(struct rewrites *r,
				    const char *base, size_t len)
{
	struct rewrite *ret;
	int i;

	for (i = 0; i < r->rewrite_nr; i++) {
		if (len == r->rewrite[i]->baselen &&
		    !strncmp(base, r->rewrite[i]->base, len))
			return r->rewrite[i];
	}

	ALLOC_GROW(r->rewrite, r->rewrite_nr + 1, r->rewrite_alloc);
	CALLOC_ARRAY(ret, 1);
	r->rewrite[r->rewrite_nr++] = ret;
	ret->base = xstrndup(base, len);
	ret->baselen = len;
	return ret;
}

static void rewrites_release(struct rewrites *r)
{
	for (int i = 0; i < r->rewrite_nr; i++)
		free((char *)r->rewrite[i]->base);
	free(r->rewrite);
	memset(r, 0, sizeof(*r));
}

static void add_instead_of(struct rewrite *rewrite, const char *instead_of)
{
	ALLOC_GROW(rewrite->instead_of, rewrite->instead_of_nr + 1, rewrite->instead_of_alloc);
	rewrite->instead_of[rewrite->instead_of_nr].s = instead_of;
	rewrite->instead_of[rewrite->instead_of_nr].len = strlen(instead_of);
	rewrite->instead_of_nr++;
}

static const char *skip_spaces(const char *s)
{
	while (isspace(*s))
		s++;
	return s;
}

static void read_remotes_file(struct remote_state *remote_state,
			      struct remote *remote)
{
	struct strbuf buf = STRBUF_INIT;
	FILE *f = fopen_or_warn(git_path("remotes/%s", remote->name), "r");

	if (!f)
		return;
	remote->configured_in_repo = 1;
	remote->origin = REMOTE_REMOTES;
	while (strbuf_getline(&buf, f) != EOF) {
		const char *v;

		strbuf_rtrim(&buf);

		if (skip_prefix(buf.buf, "URL:", &v))
			add_url_alias(remote_state, remote,
				      skip_spaces(v));
		else if (skip_prefix(buf.buf, "Push:", &v))
			refspec_append(&remote->push, skip_spaces(v));
		else if (skip_prefix(buf.buf, "Pull:", &v))
			refspec_append(&remote->fetch, skip_spaces(v));
	}
	strbuf_release(&buf);
	fclose(f);
}

static void read_branches_file(struct remote_state *remote_state,
			       struct remote *remote)
{
	char *frag, *to_free = NULL;
	struct strbuf buf = STRBUF_INIT;
	FILE *f = fopen_or_warn(git_path("branches/%s", remote->name), "r");

	if (!f)
		return;

	strbuf_getline_lf(&buf, f);
	fclose(f);
	strbuf_trim(&buf);
	if (!buf.len) {
		strbuf_release(&buf);
		return;
	}

	remote->configured_in_repo = 1;
	remote->origin = REMOTE_BRANCHES;

	/*
	 * The branches file would have URL and optionally
	 * #branch specified.  The default (or specified) branch is
	 * fetched and stored in the local branch matching the
	 * remote name.
	 */
	frag = strchr(buf.buf, '#');
	if (frag)
		*(frag++) = '\0';
	else
		frag = to_free = repo_default_branch_name(the_repository, 0);

	add_url_alias(remote_state, remote, buf.buf);
	refspec_appendf(&remote->fetch, "refs/heads/%s:refs/heads/%s",
			frag, remote->name);

	/*
	 * Cogito compatible push: push current HEAD to remote #branch
	 * (master if missing)
	 */
	refspec_appendf(&remote->push, "HEAD:refs/heads/%s", frag);
	remote->fetch_tags = 1; /* always auto-follow */

	strbuf_release(&buf);
	free(to_free);
}

static int handle_config(const char *key, const char *value,
			 const struct config_context *ctx, void *cb)
{
	const char *name;
	size_t namelen;
	const char *subkey;
	struct remote *remote;
	struct branch *branch;
	struct remote_state *remote_state = cb;
	const struct key_value_info *kvi = ctx->kvi;

	if (parse_config_key(key, "branch", &name, &namelen, &subkey) >= 0) {
		/* There is no subsection. */
		if (!name)
			return 0;
		/* There is a subsection, but it is empty. */
		if (!namelen)
			return -1;
		branch = make_branch(remote_state, name, namelen);
		if (!strcmp(subkey, "remote")) {
			FREE_AND_NULL(branch->remote_name);
			return git_config_string(&branch->remote_name, key, value);
		} else if (!strcmp(subkey, "pushremote")) {
			FREE_AND_NULL(branch->pushremote_name);
			return git_config_string(&branch->pushremote_name, key, value);
		} else if (!strcmp(subkey, "merge")) {
			if (!value)
				return config_error_nonbool(key);
			add_merge(branch, xstrdup(value));
		}
		return 0;
	}
	if (parse_config_key(key, "url", &name, &namelen, &subkey) >= 0) {
		struct rewrite *rewrite;
		if (!name)
			return 0;
		if (!strcmp(subkey, "insteadof")) {
			if (!value)
				return config_error_nonbool(key);
			rewrite = make_rewrite(&remote_state->rewrites, name,
					       namelen);
			add_instead_of(rewrite, xstrdup(value));
		} else if (!strcmp(subkey, "pushinsteadof")) {
			if (!value)
				return config_error_nonbool(key);
			rewrite = make_rewrite(&remote_state->rewrites_push,
					       name, namelen);
			add_instead_of(rewrite, xstrdup(value));
		}
	}

	if (parse_config_key(key, "remote", &name, &namelen, &subkey) < 0)
		return 0;

	/* Handle remote.* variables */
	if (!name && !strcmp(subkey, "pushdefault")) {
		FREE_AND_NULL(remote_state->pushremote_name);
		return git_config_string(&remote_state->pushremote_name, key,
					 value);
	}

	if (!name)
		return 0;
	/* Handle remote.<name>.* variables */
	if (*name == '/') {
		warning(_("config remote shorthand cannot begin with '/': %s"),
			name);
		return 0;
	}
	remote = make_remote(remote_state, name, namelen);
	remote->origin = REMOTE_CONFIG;
	if (kvi->scope == CONFIG_SCOPE_LOCAL ||
	    kvi->scope == CONFIG_SCOPE_WORKTREE)
		remote->configured_in_repo = 1;
	if (!strcmp(subkey, "mirror"))
		remote->mirror = git_config_bool(key, value);
	else if (!strcmp(subkey, "skipdefaultupdate"))
		remote->skip_default_update = git_config_bool(key, value);
	else if (!strcmp(subkey, "skipfetchall"))
		remote->skip_default_update = git_config_bool(key, value);
	else if (!strcmp(subkey, "prune"))
		remote->prune = git_config_bool(key, value);
	else if (!strcmp(subkey, "prunetags"))
		remote->prune_tags = git_config_bool(key, value);
	else if (!strcmp(subkey, "url")) {
		if (!value)
			return config_error_nonbool(key);
		add_url(remote, value);
	} else if (!strcmp(subkey, "pushurl")) {
		if (!value)
			return config_error_nonbool(key);
		add_pushurl(remote, value);
	} else if (!strcmp(subkey, "push")) {
		char *v;
		if (git_config_string(&v, key, value))
			return -1;
		refspec_append(&remote->push, v);
		free(v);
	} else if (!strcmp(subkey, "fetch")) {
		char *v;
		if (git_config_string(&v, key, value))
			return -1;
		refspec_append(&remote->fetch, v);
		free(v);
	} else if (!strcmp(subkey, "receivepack")) {
		char *v;
		if (git_config_string(&v, key, value))
			return -1;
		if (!remote->receivepack)
			remote->receivepack = v;
		else
			error(_("more than one receivepack given, using the first"));
	} else if (!strcmp(subkey, "uploadpack")) {
		char *v;
		if (git_config_string(&v, key, value))
			return -1;
		if (!remote->uploadpack)
			remote->uploadpack = v;
		else
			error(_("more than one uploadpack given, using the first"));
	} else if (!strcmp(subkey, "tagopt")) {
		if (!strcmp(value, "--no-tags"))
			remote->fetch_tags = -1;
		else if (!strcmp(value, "--tags"))
			remote->fetch_tags = 2;
	} else if (!strcmp(subkey, "proxy")) {
		FREE_AND_NULL(remote->http_proxy);
		return git_config_string(&remote->http_proxy,
					 key, value);
	} else if (!strcmp(subkey, "proxyauthmethod")) {
		FREE_AND_NULL(remote->http_proxy_authmethod);
		return git_config_string(&remote->http_proxy_authmethod,
					 key, value);
	} else if (!strcmp(subkey, "vcs")) {
		FREE_AND_NULL(remote->foreign_vcs);
		return git_config_string(&remote->foreign_vcs, key, value);
	} else if (!strcmp(subkey, "serveroption")) {
		return parse_transport_option(key, value,
					      &remote->server_options);
	} else if (!strcmp(subkey, "followremotehead")) {
		const char *no_warn_branch;
		if (!strcmp(value, "never"))
			remote->follow_remote_head = FOLLOW_REMOTE_NEVER;
		else if (!strcmp(value, "create"))
			remote->follow_remote_head = FOLLOW_REMOTE_CREATE;
		else if (!strcmp(value, "warn")) {
			remote->follow_remote_head = FOLLOW_REMOTE_WARN;
			remote->no_warn_branch = NULL;
		} else if (skip_prefix(value, "warn-if-not-", &no_warn_branch)) {
			remote->follow_remote_head = FOLLOW_REMOTE_WARN;
			remote->no_warn_branch = no_warn_branch;
		} else if (!strcmp(value, "always")) {
			remote->follow_remote_head = FOLLOW_REMOTE_ALWAYS;
		} else {
			warning(_("unrecognized followRemoteHEAD value '%s' ignored"),
				value);
		}
	}
	return 0;
}

static void alias_all_urls(struct remote_state *remote_state)
{
	int i, j;
	for (i = 0; i < remote_state->remotes_nr; i++) {
		int add_pushurl_aliases;
		if (!remote_state->remotes[i])
			continue;
		for (j = 0; j < remote_state->remotes[i]->pushurl.nr; j++) {
			char *alias = alias_url(remote_state->remotes[i]->pushurl.v[j],
						&remote_state->rewrites);
			if (alias)
				strvec_replace(&remote_state->remotes[i]->pushurl,
					       j, alias);
			free(alias);
		}
		add_pushurl_aliases = remote_state->remotes[i]->pushurl.nr == 0;
		for (j = 0; j < remote_state->remotes[i]->url.nr; j++) {
			char *alias;
			if (add_pushurl_aliases)
				add_pushurl_alias(
					remote_state, remote_state->remotes[i],
					remote_state->remotes[i]->url.v[j]);
			alias = alias_url(remote_state->remotes[i]->url.v[j],
					  &remote_state->rewrites);
			if (alias)
				strvec_replace(&remote_state->remotes[i]->url,
					       j, alias);
			free(alias);
		}
	}
}

static void read_config(struct repository *repo, int early)
{
	int flag;

	if (repo->remote_state->initialized)
		return;
	repo->remote_state->initialized = 1;

	repo->remote_state->current_branch = NULL;
	if (startup_info->have_repository && !early) {
		const char *head_ref = refs_resolve_ref_unsafe(
			get_main_ref_store(repo), "HEAD", 0, NULL, &flag);
		if (head_ref && (flag & REF_ISSYMREF) &&
		    skip_prefix(head_ref, "refs/heads/", &head_ref)) {
			repo->remote_state->current_branch = make_branch(
				repo->remote_state, head_ref, strlen(head_ref));
		}
	}
	repo_config(repo, handle_config, repo->remote_state);
	alias_all_urls(repo->remote_state);
}

static int valid_remote_nick(const char *name)
{
	if (!name[0] || is_dot_or_dotdot(name))
		return 0;

	/* remote nicknames cannot contain slashes */
	while (*name)
		if (is_dir_sep(*name++))
			return 0;
	return 1;
}

static const char *remotes_remote_for_branch(struct remote_state *remote_state,
					     struct branch *branch,
					     int *explicit)
{
	if (branch && branch->remote_name) {
		if (explicit)
			*explicit = 1;
		return branch->remote_name;
	}
	if (explicit)
		*explicit = 0;
	if (remote_state->remotes_nr == 1)
		return remote_state->remotes[0]->name;
	return "origin";
}

const char *remote_for_branch(struct branch *branch, int *explicit)
{
	read_config(the_repository, 0);
	die_on_missing_branch(the_repository, branch);

	return remotes_remote_for_branch(the_repository->remote_state, branch,
					 explicit);
}

static const char *
remotes_pushremote_for_branch(struct remote_state *remote_state,
			      struct branch *branch, int *explicit)
{
	if (branch && branch->pushremote_name) {
		if (explicit)
			*explicit = 1;
		return branch->pushremote_name;
	}
	if (remote_state->pushremote_name) {
		if (explicit)
			*explicit = 1;
		return remote_state->pushremote_name;
	}
	return remotes_remote_for_branch(remote_state, branch, explicit);
}

const char *pushremote_for_branch(struct branch *branch, int *explicit)
{
	read_config(the_repository, 0);
	die_on_missing_branch(the_repository, branch);

	return remotes_pushremote_for_branch(the_repository->remote_state,
					     branch, explicit);
}

static struct remote *remotes_remote_get(struct remote_state *remote_state,
					 const char *name);

char *remote_ref_for_branch(struct branch *branch, int for_push)
{
	read_config(the_repository, 0);
	die_on_missing_branch(the_repository, branch);

	if (branch) {
		if (!for_push) {
			if (branch->merge_nr) {
				return xstrdup(branch->merge_name[0]);
			}
		} else {
			char *dst;
			const char *remote_name = remotes_pushremote_for_branch(
					the_repository->remote_state, branch,
					NULL);
			struct remote *remote = remotes_remote_get(
				the_repository->remote_state, remote_name);

			if (remote && remote->push.nr &&
			    (dst = apply_refspecs(&remote->push,
						  branch->refname))) {
				return dst;
			}
		}
	}
	return NULL;
}

static void validate_remote_url(struct remote *remote)
{
	int i;
	const char *value;
	struct strbuf redacted = STRBUF_INIT;
	int warn_not_die;

	if (git_config_get_string_tmp("transfer.credentialsinurl", &value))
		return;

	if (!strcmp("warn", value))
		warn_not_die = 1;
	else if (!strcmp("die", value))
		warn_not_die = 0;
	else if (!strcmp("allow", value))
		return;
	else
		die(_("unrecognized value transfer.credentialsInUrl: '%s'"), value);

	for (i = 0; i < remote->url.nr; i++) {
		struct url_info url_info = { 0 };

		if (!url_normalize(remote->url.v[i], &url_info) ||
		    !url_info.passwd_off)
			goto loop_cleanup;

		strbuf_reset(&redacted);
		strbuf_add(&redacted, url_info.url, url_info.passwd_off);
		strbuf_addstr(&redacted, "<redacted>");
		strbuf_addstr(&redacted,
			      url_info.url + url_info.passwd_off + url_info.passwd_len);

		if (warn_not_die)
			warning(_("URL '%s' uses plaintext credentials"), redacted.buf);
		else
			die(_("URL '%s' uses plaintext credentials"), redacted.buf);

loop_cleanup:
		free(url_info.url);
	}

	strbuf_release(&redacted);
}

static struct remote *
remotes_remote_get_1(struct remote_state *remote_state, const char *name,
		     const char *(*get_default)(struct remote_state *,
						struct branch *, int *))
{
	struct remote *ret;
	int name_given = 0;

	if (name)
		name_given = 1;
	else
		name = get_default(remote_state, remote_state->current_branch,
				   &name_given);

	ret = make_remote(remote_state, name, 0);
	if (valid_remote_nick(name) && have_git_dir()) {
		if (!valid_remote(ret))
			read_remotes_file(remote_state, ret);
		if (!valid_remote(ret))
			read_branches_file(remote_state, ret);
	}
	if (name_given && !valid_remote(ret))
		add_url_alias(remote_state, ret, name);
	if (!valid_remote(ret))
		return NULL;

	validate_remote_url(ret);

	return ret;
}

static inline struct remote *
remotes_remote_get(struct remote_state *remote_state, const char *name)
{
	return remotes_remote_get_1(remote_state, name,
				    remotes_remote_for_branch);
}

struct remote *remote_get(const char *name)
{
	read_config(the_repository, 0);
	return remotes_remote_get(the_repository->remote_state, name);
}

struct remote *remote_get_early(const char *name)
{
	read_config(the_repository, 1);
	return remotes_remote_get(the_repository->remote_state, name);
}

static inline struct remote *
remotes_pushremote_get(struct remote_state *remote_state, const char *name)
{
	return remotes_remote_get_1(remote_state, name,
				    remotes_pushremote_for_branch);
}

struct remote *pushremote_get(const char *name)
{
	read_config(the_repository, 0);
	return remotes_pushremote_get(the_repository->remote_state, name);
}

int remote_is_configured(struct remote *remote, int in_repo)
{
	if (!remote)
		return 0;
	if (in_repo)
		return remote->configured_in_repo;
	return !!remote->origin;
}

int for_each_remote(each_remote_fn fn, void *priv)
{
	int i, result = 0;
	read_config(the_repository, 0);
	for (i = 0; i < the_repository->remote_state->remotes_nr && !result;
	     i++) {
		struct remote *remote =
			the_repository->remote_state->remotes[i];
		if (!remote)
			continue;
		result = fn(remote, priv);
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
			BUG("Internal error");
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
	for (i = 0; i < remote->url.nr; i++) {
		if (!strcmp(remote->url.v[i], url))
			return 1;
	}
	return 0;
}

struct strvec *push_url_of_remote(struct remote *remote)
{
	return remote->pushurl.nr ? &remote->pushurl : &remote->url;
}

void ref_push_report_free(struct ref_push_report *report)
{
	while (report) {
		struct ref_push_report *next = report->next;

		free(report->ref_name);
		free(report->old_oid);
		free(report->new_oid);
		free(report);

		report = next;
	}
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
		die(_("key '%s' of pattern had no '*'"), key);
	klen = kstar - key;
	ksuffixlen = strlen(kstar + 1);
	namelen = strlen(name);
	ret = !strncmp(name, key, klen) && namelen >= klen + ksuffixlen &&
		!memcmp(name + namelen - ksuffixlen, kstar + 1, ksuffixlen);
	if (ret && value) {
		struct strbuf sb = STRBUF_INIT;
		const char *vstar = strchr(value, '*');
		if (!vstar)
			die(_("value '%s' of pattern has no '*'"), value);
		strbuf_add(&sb, value, vstar - value);
		strbuf_add(&sb, name + klen, namelen - klen - ksuffixlen);
		strbuf_addstr(&sb, vstar + 1);
		*result = strbuf_detach(&sb, NULL);
	}
	return ret;
}

static int refspec_match(const struct refspec_item *refspec,
			 const char *name)
{
	if (refspec->pattern)
		return match_name_with_pattern(refspec->src, name, NULL, NULL);

	return !strcmp(refspec->src, name);
}

int omit_name_by_refspec(const char *name, struct refspec *rs)
{
	int i;

	for (i = 0; i < rs->nr; i++) {
		if (rs->items[i].negative && refspec_match(&rs->items[i], name))
			return 1;
	}
	return 0;
}

struct ref *apply_negative_refspecs(struct ref *ref_map, struct refspec *rs)
{
	struct ref **tail;

	for (tail = &ref_map; *tail; ) {
		struct ref *ref = *tail;

		if (omit_name_by_refspec(ref->name, rs)) {
			*tail = ref->next;
			free(ref->peer_ref);
			free(ref);
		} else
			tail = &ref->next;
	}

	return ref_map;
}

static int query_matches_negative_refspec(struct refspec *rs, struct refspec_item *query)
{
	int i, matched_negative = 0;
	int find_src = !query->src;
	struct string_list reversed = STRING_LIST_INIT_DUP;
	const char *needle = find_src ? query->dst : query->src;

	/*
	 * Check whether the queried ref matches any negative refpsec. If so,
	 * then we should ultimately treat this as not matching the query at
	 * all.
	 *
	 * Note that negative refspecs always match the source, but the query
	 * item uses the destination. To handle this, we apply pattern
	 * refspecs in reverse to figure out if the query source matches any
	 * of the negative refspecs.
	 *
	 * The first loop finds and expands all positive refspecs
	 * matched by the queried ref.
	 *
	 * The second loop checks if any of the results of the first loop
	 * match any negative refspec.
	 */
	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		char *expn_name;

		if (refspec->negative)
			continue;

		/* Note the reversal of src and dst */
		if (refspec->pattern) {
			const char *key = refspec->dst ? refspec->dst : refspec->src;
			const char *value = refspec->src;

			if (match_name_with_pattern(key, needle, value, &expn_name))
				string_list_append_nodup(&reversed, expn_name);
		} else if (refspec->matching) {
			/* For the special matching refspec, any query should match */
			string_list_append(&reversed, needle);
		} else if (!refspec->src) {
			BUG("refspec->src should not be null here");
		} else if (!strcmp(needle, refspec->src)) {
			string_list_append(&reversed, refspec->src);
		}
	}

	for (i = 0; !matched_negative && i < reversed.nr; i++) {
		if (omit_name_by_refspec(reversed.items[i].string, rs))
			matched_negative = 1;
	}

	string_list_clear(&reversed, 0);

	return matched_negative;
}

static void query_refspecs_multiple(struct refspec *rs,
				    struct refspec_item *query,
				    struct string_list *results)
{
	int i;
	int find_src = !query->src;

	if (find_src && !query->dst)
		BUG("query_refspecs_multiple: need either src or dst");

	if (query_matches_negative_refspec(rs, query))
		return;

	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;
		const char *needle = find_src ? query->dst : query->src;
		char **result = find_src ? &query->src : &query->dst;

		if (!refspec->dst || refspec->negative)
			continue;
		if (refspec->pattern) {
			if (match_name_with_pattern(key, needle, value, result))
				string_list_append_nodup(results, *result);
		} else if (!strcmp(needle, key)) {
			string_list_append(results, value);
		}
	}
}

int query_refspecs(struct refspec *rs, struct refspec_item *query)
{
	int i;
	int find_src = !query->src;
	const char *needle = find_src ? query->dst : query->src;
	char **result = find_src ? &query->src : &query->dst;

	if (find_src && !query->dst)
		BUG("query_refspecs: need either src or dst");

	if (query_matches_negative_refspec(rs, query))
		return -1;

	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *refspec = &rs->items[i];
		const char *key = find_src ? refspec->dst : refspec->src;
		const char *value = find_src ? refspec->src : refspec->dst;

		if (!refspec->dst || refspec->negative)
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

char *apply_refspecs(struct refspec *rs, const char *name)
{
	struct refspec_item query;

	memset(&query, 0, sizeof(struct refspec_item));
	query.src = (char *)name;

	if (query_refspecs(rs, &query))
		return NULL;

	return query.dst;
}

int remote_find_tracking(struct remote *remote, struct refspec_item *refspec)
{
	return query_refspecs(&remote->fetch, refspec);
}

static struct ref *alloc_ref_with_prefix(const char *prefix, size_t prefixlen,
		const char *name)
{
	size_t len = strlen(name);
	struct ref *ref = xcalloc(1, st_add4(sizeof(*ref), prefixlen, len, 1));
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
	len = st_add3(sizeof(struct ref), strlen(ref->name), 1);
	cpy = xmalloc(len);
	memcpy(cpy, ref, len);
	cpy->next = NULL;
	cpy->symref = xstrdup_or_null(ref->symref);
	cpy->remote_status = xstrdup_or_null(ref->remote_status);
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

void free_one_ref(struct ref *ref)
{
	if (!ref)
		return;
	free_one_ref(ref->peer_ref);
	ref_push_report_free(ref->report);
	free(ref->remote_status);
	free(ref->tracking_ref);
	free(ref->symref);
	free(ref);
}

void free_refs(struct ref *ref)
{
	struct ref *next;
	while (ref) {
		next = ref->next;
		free_one_ref(ref);
		ref = next;
	}
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
	oidclr(&ref->new_oid, the_repository->hash_algo);
	return ref;
}

static int try_explicit_object_name(const char *name,
				    struct ref **match)
{
	struct object_id oid;

	if (!*name) {
		if (match)
			*match = alloc_delete_ref();
		return 0;
	}

	if (repo_get_oid(the_repository, name, &oid))
		return -1;

	if (match) {
		*match = alloc_ref(name);
		oidcpy(&(*match)->new_oid, &oid);
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

	const char *r = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
						peer->name,
						RESOLVE_REF_READING,
						NULL, NULL);
	if (!r)
		return NULL;

	if (starts_with(r, "refs/heads/")) {
		strbuf_addstr(&buf, "refs/heads/");
	} else if (starts_with(r, "refs/tags/")) {
		strbuf_addstr(&buf, "refs/tags/");
	} else {
		return NULL;
	}

	strbuf_addstr(&buf, name);
	return strbuf_detach(&buf, NULL);
}

static int match_explicit_lhs(struct ref *src,
			      struct refspec_item *rs,
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
			return error(_("src refspec %s does not match any"), rs->src);
		if (allocated_match)
			*allocated_match = 1;
		return 0;
	default:
		return error(_("src refspec %s matches more than one"), rs->src);
	}
}

static void show_push_unqualified_ref_name_error(const char *dst_value,
						 const char *matched_src_name)
{
	struct object_id oid;
	enum object_type type;

	/*
	 * TRANSLATORS: "matches '%s'%" is the <dst> part of "git push
	 * <remote> <src>:<dst>" push, and "being pushed ('%s')" is
	 * the <src>.
	 */
	error(_("The destination you provided is not a full refname (i.e.,\n"
		"starting with \"refs/\"). We tried to guess what you meant by:\n"
		"\n"
		"- Looking for a ref that matches '%s' on the remote side.\n"
		"- Checking if the <src> being pushed ('%s')\n"
		"  is a ref in \"refs/{heads,tags}/\". If so we add a corresponding\n"
		"  refs/{heads,tags}/ prefix on the remote side.\n"
		"\n"
		"Neither worked, so we gave up. You must fully qualify the ref."),
	      dst_value, matched_src_name);

	if (!advice_enabled(ADVICE_PUSH_UNQUALIFIED_REF_NAME))
		return;

	if (repo_get_oid(the_repository, matched_src_name, &oid))
		BUG("'%s' is not a valid object, "
		    "match_explicit_lhs() should catch this!",
		    matched_src_name);
	type = oid_object_info(the_repository, &oid, NULL);
	if (type == OBJ_COMMIT) {
		advise(_("The <src> part of the refspec is a commit object.\n"
			 "Did you mean to create a new branch by pushing to\n"
			 "'%s:refs/heads/%s'?"),
		       matched_src_name, dst_value);
	} else if (type == OBJ_TAG) {
		advise(_("The <src> part of the refspec is a tag object.\n"
			 "Did you mean to create a new tag by pushing to\n"
			 "'%s:refs/tags/%s'?"),
		       matched_src_name, dst_value);
	} else if (type == OBJ_TREE) {
		advise(_("The <src> part of the refspec is a tree object.\n"
			 "Did you mean to tag a new tree by pushing to\n"
			 "'%s:refs/tags/%s'?"),
		       matched_src_name, dst_value);
	} else if (type == OBJ_BLOB) {
		advise(_("The <src> part of the refspec is a blob object.\n"
			 "Did you mean to tag a new blob by pushing to\n"
			 "'%s:refs/tags/%s'?"),
		       matched_src_name, dst_value);
	} else {
		BUG("'%s' should be commit/tag/tree/blob, is '%d'",
		    matched_src_name, type);
	}
}

static int match_explicit(struct ref *src, struct ref *dst,
			  struct ref ***dst_tail,
			  struct refspec_item *rs)
{
	struct ref *matched_src = NULL, *matched_dst = NULL;
	int allocated_src = 0, ret;

	const char *dst_value = rs->dst;
	char *dst_guess;

	if (rs->pattern || rs->matching || rs->negative) {
		ret = 0;
		goto out;
	}

	if (match_explicit_lhs(src, rs, &matched_src, &allocated_src) < 0) {
		ret = -1;
		goto out;
	}

	if (!dst_value) {
		int flag;

		dst_value = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
						    matched_src->name,
						    RESOLVE_REF_READING,
						    NULL, &flag);
		if (!dst_value ||
		    ((flag & REF_ISSYMREF) &&
		     !starts_with(dst_value, "refs/heads/")))
			die(_("%s cannot be resolved to branch"),
			    matched_src->name);
	}

	switch (count_refspec_match(dst_value, dst, &matched_dst)) {
	case 1:
		break;
	case 0:
		if (starts_with(dst_value, "refs/")) {
			matched_dst = make_linked_ref(dst_value, dst_tail);
		} else if (is_null_oid(&matched_src->new_oid)) {
			error(_("unable to delete '%s': remote ref does not exist"),
			      dst_value);
		} else if ((dst_guess = guess_ref(dst_value, matched_src))) {
			matched_dst = make_linked_ref(dst_guess, dst_tail);
			free(dst_guess);
		} else {
			show_push_unqualified_ref_name_error(dst_value,
							     matched_src->name);
		}
		break;
	default:
		matched_dst = NULL;
		error(_("dst refspec %s matches more than one"),
		      dst_value);
		break;
	}

	if (!matched_dst) {
		ret = -1;
		goto out;
	}

	if (matched_dst->peer_ref) {
		ret = error(_("dst ref %s receives from more than one src"),
			    matched_dst->name);
		goto out;
	} else {
		matched_dst->peer_ref = allocated_src ?
					matched_src :
					copy_ref(matched_src);
		matched_dst->force = rs->force;
		matched_src = NULL;
	}

	ret = 0;

out:
	if (allocated_src)
		free_one_ref(matched_src);
	return ret;
}

static int match_explicit_refs(struct ref *src, struct ref *dst,
			       struct ref ***dst_tail, struct refspec *rs)
{
	int i, errs;
	for (i = errs = 0; i < rs->nr; i++)
		errs += match_explicit(src, dst, dst_tail, &rs->items[i]);
	return errs;
}

static char *get_ref_match(const struct refspec *rs, const struct ref *ref,
			   int send_mirror, int direction,
			   const struct refspec_item **ret_pat)
{
	const struct refspec_item *pat;
	char *name;
	int i;
	int matching_refs = -1;
	for (i = 0; i < rs->nr; i++) {
		const struct refspec_item *item = &rs->items[i];

		if (item->negative)
			continue;

		if (item->matching &&
		    (matching_refs == -1 || item->force)) {
			matching_refs = i;
			continue;
		}

		if (item->pattern) {
			const char *dst_side = item->dst ? item->dst : item->src;
			int match;
			if (direction == FROM_SRC)
				match = match_name_with_pattern(item->src, ref->name, dst_side, &name);
			else
				match = match_name_with_pattern(dst_side, ref->name, item->src, &name);
			if (match) {
				matching_refs = i;
				break;
			}
		}
	}
	if (matching_refs == -1)
		return NULL;

	pat = &rs->items[matching_refs];
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

static void add_to_tips(struct tips *tips, const struct object_id *oid)
{
	struct commit *commit;

	if (is_null_oid(oid))
		return;
	commit = lookup_commit_reference_gently(the_repository, oid, 1);
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
		    !is_null_oid(&ref->peer_ref->new_oid))
			add_to_tips(&sent_tips, &ref->peer_ref->new_oid);
		else
			add_to_tips(&sent_tips, &ref->old_oid);
		if (starts_with(ref->name, "refs/tags/"))
			string_list_append(&dst_tag, ref->name);
	}
	clear_commit_marks_many(sent_tips.nr, sent_tips.tip, TMP_MARK);

	string_list_sort(&dst_tag);

	/* Collect tags they do not have. */
	for (ref = src; ref; ref = ref->next) {
		if (!starts_with(ref->name, "refs/tags/"))
			continue; /* not a tag */
		if (string_list_has_string(&dst_tag, ref->name))
			continue; /* they already have it */
		if (oid_object_info(the_repository, &ref->new_oid, NULL) != OBJ_TAG)
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
		const int reachable_flag = 1;
		struct commit_list *found_commits;
		struct commit **src_commits;
		int nr_src_commits = 0, alloc_src_commits = 16;
		ALLOC_ARRAY(src_commits, alloc_src_commits);

		for_each_string_list_item(item, &src_tag) {
			struct ref *ref = item->util;
			struct commit *commit;

			if (is_null_oid(&ref->new_oid))
				continue;
			commit = lookup_commit_reference_gently(the_repository,
								&ref->new_oid,
								1);
			if (!commit)
				/* not pushing a commit, which is not an error */
				continue;

			ALLOC_GROW(src_commits, nr_src_commits + 1, alloc_src_commits);
			src_commits[nr_src_commits++] = commit;
		}

		found_commits = get_reachable_subset(sent_tips.tip, sent_tips.nr,
						     src_commits, nr_src_commits,
						     reachable_flag);

		for_each_string_list_item(item, &src_tag) {
			struct ref *dst_ref;
			struct ref *ref = item->util;
			struct commit *commit;

			if (is_null_oid(&ref->new_oid))
				continue;
			commit = lookup_commit_reference_gently(the_repository,
								&ref->new_oid,
								1);
			if (!commit)
				/* not pushing a commit, which is not an error */
				continue;

			/*
			 * Is this tag, which they do not have, reachable from
			 * any of the commits we are sending?
			 */
			if (!(commit->object.flags & reachable_flag))
				continue;

			/* Add it in */
			dst_ref = make_linked_ref(ref->name, dst_tail);
			oidcpy(&dst_ref->new_oid, &ref->new_oid);
			dst_ref->peer_ref = copy_ref(ref);
		}

		clear_commit_marks_many(nr_src_commits, src_commits, reachable_flag);
		free(src_commits);
		free_commit_list(found_commits);
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

	string_list_sort(ref_index);
}

/*
 * Given only the set of local refs, sanity-check the set of push
 * refspecs. We can't catch all errors that match_push_refs would,
 * but we can catch some errors early before even talking to the
 * remote side.
 */
int check_push_refs(struct ref *src, struct refspec *rs)
{
	int ret = 0;
	int i;

	for (i = 0; i < rs->nr; i++) {
		struct refspec_item *item = &rs->items[i];

		if (item->pattern || item->matching || item->negative)
			continue;

		ret |= match_explicit_lhs(src, item, NULL, NULL);
	}

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
		    struct refspec *rs, int flags)
{
	int send_all = flags & MATCH_REFS_ALL;
	int send_mirror = flags & MATCH_REFS_MIRROR;
	int send_prune = flags & MATCH_REFS_PRUNE;
	int errs;
	struct ref *ref, **dst_tail = tail_ref(dst);
	struct string_list dst_ref_index = STRING_LIST_INIT_NODUP;

	/* If no refspec is provided, use the default ":" */
	if (!rs->nr)
		refspec_append(rs, ":");

	errs = match_explicit_refs(src, *dst, &dst_tail, rs);

	/* pick the remainder */
	for (ref = src; ref; ref = ref->next) {
		struct string_list_item *dst_item;
		struct ref *dst_peer;
		const struct refspec_item *pat = NULL;
		char *dst_name;

		dst_name = get_ref_match(rs, ref, send_mirror, FROM_SRC, &pat);
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
			oidcpy(&dst_peer->new_oid, &ref->new_oid);
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

			src_name = get_ref_match(rs, ref, send_mirror, FROM_DST, NULL);
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

	*dst = apply_negative_refspecs(*dst, rs);

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
			oidcpy(&ref->new_oid, &ref->peer_ref->new_oid);
		else if (!send_mirror)
			continue;

		ref->deletion = is_null_oid(&ref->new_oid);
		if (!ref->deletion &&
			oideq(&ref->old_oid, &ref->new_oid)) {
			ref->status = REF_STATUS_UPTODATE;
			continue;
		}

		/*
		 * If the remote ref has moved and is now different
		 * from what we expect, reject any push.
		 *
		 * It also is an error if the user told us to check
		 * with the remote-tracking branch to find the value
		 * to expect, but we did not have such a tracking
		 * branch.
		 *
		 * If the tip of the remote-tracking ref is unreachable
		 * from any reflog entry of its local ref indicating a
		 * possible update since checkout; reject the push.
		 */
		if (ref->expect_old_sha1) {
			if (!oideq(&ref->old_oid, &ref->old_oid_expect))
				reject_reason = REF_STATUS_REJECT_STALE;
			else if (ref->check_reachable && ref->unreachable)
				reject_reason =
					REF_STATUS_REJECT_REMOTE_UPDATED;
			else
				/*
				 * If the ref isn't stale, and is reachable
				 * from one of the reflog entries of
				 * the local branch, force the update.
				 */
				force_ref_update = 1;
		}

		/*
		 * If the update isn't already rejected then check
		 * the usual "must fast-forward" rules.
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

		if (!reject_reason && !ref->deletion && !is_null_oid(&ref->old_oid)) {
			if (starts_with(ref->name, "refs/tags/"))
				reject_reason = REF_STATUS_REJECT_ALREADY_EXISTS;
			else if (!repo_has_object_file_with_flags(the_repository, &ref->old_oid, OBJECT_INFO_SKIP_FETCH_OBJECT))
				reject_reason = REF_STATUS_REJECT_FETCH_FIRST;
			else if (!lookup_commit_reference_gently(the_repository, &ref->old_oid, 1) ||
				 !lookup_commit_reference_gently(the_repository, &ref->new_oid, 1))
				reject_reason = REF_STATUS_REJECT_NEEDS_FORCE;
			else if (!ref_newer(&ref->new_oid, &ref->old_oid))
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

static void set_merge(struct remote_state *remote_state, struct branch *ret)
{
	struct remote *remote;
	char *ref;
	struct object_id oid;
	int i;

	if (!ret)
		return; /* no branch */
	if (ret->merge)
		return; /* already run */
	if (!ret->remote_name || !ret->merge_nr) {
		/*
		 * no merge config; let's make sure we don't confuse callers
		 * with a non-zero merge_nr but a NULL merge
		 */
		ret->merge_nr = 0;
		return;
	}

	remote = remotes_remote_get(remote_state, ret->remote_name);

	CALLOC_ARRAY(ret->merge, ret->merge_nr);
	for (i = 0; i < ret->merge_nr; i++) {
		ret->merge[i] = xcalloc(1, sizeof(**ret->merge));
		ret->merge[i]->src = xstrdup(ret->merge_name[i]);
		if (!remote_find_tracking(remote, ret->merge[i]) ||
		    strcmp(ret->remote_name, "."))
			continue;
		if (repo_dwim_ref(the_repository, ret->merge_name[i],
				  strlen(ret->merge_name[i]), &oid, &ref,
				  0) == 1)
			ret->merge[i]->dst = ref;
		else
			ret->merge[i]->dst = xstrdup(ret->merge_name[i]);
	}
}

struct branch *branch_get(const char *name)
{
	struct branch *ret;

	read_config(the_repository, 0);
	if (!name || !*name || !strcmp(name, "HEAD"))
		ret = the_repository->remote_state->current_branch;
	else
		ret = make_branch(the_repository->remote_state, name,
				  strlen(name));
	set_merge(the_repository->remote_state, ret);
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

__attribute__((format (printf,2,3)))
static const char *error_buf(struct strbuf *err, const char *fmt, ...)
{
	if (err) {
		va_list ap;
		va_start(ap, fmt);
		strbuf_vaddf(err, fmt, ap);
		va_end(ap);
	}
	return NULL;
}

const char *branch_get_upstream(struct branch *branch, struct strbuf *err)
{
	if (!branch)
		return error_buf(err, _("HEAD does not point to a branch"));

	if (!branch->merge || !branch->merge[0]) {
		/*
		 * no merge config; is it because the user didn't define any,
		 * or because it is not a real branch, and get_branch
		 * auto-vivified it?
		 */
		if (!refs_ref_exists(get_main_ref_store(the_repository), branch->refname))
			return error_buf(err, _("no such branch: '%s'"),
					 branch->name);
		return error_buf(err,
				 _("no upstream configured for branch '%s'"),
				 branch->name);
	}

	if (!branch->merge[0]->dst)
		return error_buf(err,
				 _("upstream branch '%s' not stored as a remote-tracking branch"),
				 branch->merge[0]->src);

	return branch->merge[0]->dst;
}

static const char *tracking_for_push_dest(struct remote *remote,
					  const char *refname,
					  struct strbuf *err)
{
	char *ret;

	ret = apply_refspecs(&remote->fetch, refname);
	if (!ret)
		return error_buf(err,
				 _("push destination '%s' on remote '%s' has no local tracking branch"),
				 refname, remote->name);
	return ret;
}

static const char *branch_get_push_1(struct remote_state *remote_state,
				     struct branch *branch, struct strbuf *err)
{
	struct remote *remote;

	remote = remotes_remote_get(
		remote_state,
		remotes_pushremote_for_branch(remote_state, branch, NULL));
	if (!remote)
		return error_buf(err,
				 _("branch '%s' has no remote for pushing"),
				 branch->name);

	if (remote->push.nr) {
		char *dst;
		const char *ret;

		dst = apply_refspecs(&remote->push, branch->refname);
		if (!dst)
			return error_buf(err,
					 _("push refspecs for '%s' do not include '%s'"),
					 remote->name, branch->name);

		ret = tracking_for_push_dest(remote, dst, err);
		free(dst);
		return ret;
	}

	if (remote->mirror)
		return tracking_for_push_dest(remote, branch->refname, err);

	switch (push_default) {
	case PUSH_DEFAULT_NOTHING:
		return error_buf(err, _("push has no destination (push.default is 'nothing')"));

	case PUSH_DEFAULT_MATCHING:
	case PUSH_DEFAULT_CURRENT:
		return tracking_for_push_dest(remote, branch->refname, err);

	case PUSH_DEFAULT_UPSTREAM:
		return branch_get_upstream(branch, err);

	case PUSH_DEFAULT_UNSPECIFIED:
	case PUSH_DEFAULT_SIMPLE:
		{
			const char *up, *cur;

			up = branch_get_upstream(branch, err);
			if (!up)
				return NULL;
			cur = tracking_for_push_dest(remote, branch->refname, err);
			if (!cur)
				return NULL;
			if (strcmp(cur, up))
				return error_buf(err,
						 _("cannot resolve 'simple' push to a single destination"));
			return cur;
		}
	}

	BUG("unhandled push situation");
}

const char *branch_get_push(struct branch *branch, struct strbuf *err)
{
	read_config(the_repository, 0);
	die_on_missing_branch(the_repository, branch);

	if (!branch)
		return error_buf(err, _("HEAD does not point to a branch"));

	if (!branch->push_tracking_ref)
		branch->push_tracking_ref = branch_get_push_1(
			the_repository->remote_state, branch, err);
	return branch->push_tracking_ref;
}

static int ignore_symref_update(const char *refname, struct strbuf *scratch)
{
	return !refs_read_symbolic_ref(get_main_ref_store(the_repository), refname, scratch);
}

/*
 * Create and return a list of (struct ref) consisting of copies of
 * each remote_ref that matches refspec.  refspec must be a pattern.
 * Fill in the copies' peer_ref to describe the local tracking refs to
 * which they map.  Omit any references that would map to an existing
 * local symbolic ref.
 */
static struct ref *get_expanded_map(const struct ref *remote_refs,
				    const struct refspec_item *refspec)
{
	struct strbuf scratch = STRBUF_INIT;
	const struct ref *ref;
	struct ref *ret = NULL;
	struct ref **tail = &ret;

	for (ref = remote_refs; ref; ref = ref->next) {
		char *expn_name = NULL;

		strbuf_reset(&scratch);

		if (strchr(ref->name, '^'))
			continue; /* a dereference item */
		if (match_name_with_pattern(refspec->src, ref->name,
					    refspec->dst, &expn_name) &&
		    !ignore_symref_update(expn_name, &scratch)) {
			struct ref *cpy = copy_ref(ref);

			if (cpy->peer_ref)
				free_one_ref(cpy->peer_ref);
			cpy->peer_ref = alloc_ref(expn_name);
			if (refspec->force)
				cpy->peer_ref->force = 1;
			*tail = cpy;
			tail = &cpy->next;
		}
		free(expn_name);
	}

	strbuf_release(&scratch);
	return ret;
}

static const struct ref *find_ref_by_name_abbrev(const struct ref *refs, const char *name)
{
	const struct ref *ref;
	const struct ref *best_match = NULL;
	int best_score = 0;

	for (ref = refs; ref; ref = ref->next) {
		int score = refname_match(name, ref->name);

		if (best_score < score) {
			best_match = ref;
			best_score = score;
		}
	}
	return best_match;
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
		  const struct refspec_item *refspec,
		  struct ref ***tail,
		  int missing_ok)
{
	struct ref *ref_map, **rmp;

	if (refspec->negative)
		return 0;

	if (refspec->pattern) {
		ref_map = get_expanded_map(remote_refs, refspec);
	} else {
		const char *name = refspec->src[0] ? refspec->src : "HEAD";

		if (refspec->exact_sha1) {
			ref_map = alloc_ref(name);
			get_oid_hex(name, &ref_map->old_oid);
			ref_map->exact_oid = 1;
		} else {
			ref_map = get_remote_ref(remote_refs, name);
		}
		if (!missing_ok && !ref_map)
			die(_("couldn't find remote ref %s"), name);
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
				error(_("* Ignoring funny ref '%s' locally"),
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
			oidcpy(&ref->old_oid, &list->old_oid);
			return 0;
		}
	return 1;
}

/*
 * Compute the commit ahead/behind values for the pair branch_name, base.
 *
 * If abf is AHEAD_BEHIND_FULL, compute the full ahead/behind and return the
 * counts in *num_ours and *num_theirs.  If abf is AHEAD_BEHIND_QUICK, skip
 * the (potentially expensive) a/b computation (*num_ours and *num_theirs are
 * set to zero).
 *
 * Returns -1 if num_ours and num_theirs could not be filled in (e.g., ref
 * does not exist).  Returns 0 if the commits are identical.  Returns 1 if
 * commits are different.
 */

static int stat_branch_pair(const char *branch_name, const char *base,
			     int *num_ours, int *num_theirs,
			     enum ahead_behind_flags abf)
{
	struct object_id oid;
	struct commit *ours, *theirs;
	struct rev_info revs;
	struct setup_revision_opt opt = {
		.free_removed_argv_elements = 1,
	};
	struct strvec argv = STRVEC_INIT;

	/* Cannot stat if what we used to build on no longer exists */
	if (refs_read_ref(get_main_ref_store(the_repository), base, &oid))
		return -1;
	theirs = lookup_commit_reference(the_repository, &oid);
	if (!theirs)
		return -1;

	if (refs_read_ref(get_main_ref_store(the_repository), branch_name, &oid))
		return -1;
	ours = lookup_commit_reference(the_repository, &oid);
	if (!ours)
		return -1;

	*num_theirs = *num_ours = 0;

	/* are we the same? */
	if (theirs == ours)
		return 0;
	if (abf == AHEAD_BEHIND_QUICK)
		return 1;
	if (abf != AHEAD_BEHIND_FULL)
		BUG("stat_branch_pair: invalid abf '%d'", abf);

	/* Run "rev-list --left-right ours...theirs" internally... */
	strvec_push(&argv, ""); /* ignored */
	strvec_push(&argv, "--left-right");
	strvec_pushf(&argv, "%s...%s",
		     oid_to_hex(&ours->object.oid),
		     oid_to_hex(&theirs->object.oid));
	strvec_push(&argv, "--");

	repo_init_revisions(the_repository, &revs, NULL);
	setup_revisions(argv.nr, argv.v, &revs, &opt);
	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	/* ... and count the commits on each side. */
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

	strvec_clear(&argv);
	release_revisions(&revs);
	return 1;
}

/*
 * Lookup the tracking branch for the given branch and if present, optionally
 * compute the commit ahead/behind values for the pair.
 *
 * If for_push is true, the tracking branch refers to the push branch,
 * otherwise it refers to the upstream branch.
 *
 * The name of the tracking branch (or NULL if it is not defined) is
 * returned via *tracking_name, if it is not itself NULL.
 *
 * If abf is AHEAD_BEHIND_FULL, compute the full ahead/behind and return the
 * counts in *num_ours and *num_theirs.  If abf is AHEAD_BEHIND_QUICK, skip
 * the (potentially expensive) a/b computation (*num_ours and *num_theirs are
 * set to zero).
 *
 * Returns -1 if num_ours and num_theirs could not be filled in (e.g., no
 * upstream defined, or ref does not exist).  Returns 0 if the commits are
 * identical.  Returns 1 if commits are different.
 */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs,
		       const char **tracking_name, int for_push,
		       enum ahead_behind_flags abf)
{
	const char *base;

	/* Cannot stat unless we are marked to build on top of somebody else. */
	base = for_push ? branch_get_push(branch, NULL) :
		branch_get_upstream(branch, NULL);
	if (tracking_name)
		*tracking_name = base;
	if (!base)
		return -1;

	return stat_branch_pair(branch->refname, base, num_ours, num_theirs, abf);
}

/*
 * Return true when there is anything to report, otherwise false.
 */
int format_tracking_info(struct branch *branch, struct strbuf *sb,
			 enum ahead_behind_flags abf,
			 int show_divergence_advice)
{
	int ours, theirs, sti;
	const char *full_base;
	char *base;
	int upstream_is_gone = 0;

	sti = stat_tracking_info(branch, &ours, &theirs, &full_base, 0, abf);
	if (sti < 0) {
		if (!full_base)
			return 0;
		upstream_is_gone = 1;
	}

	base = refs_shorten_unambiguous_ref(get_main_ref_store(the_repository),
					    full_base, 0);
	if (upstream_is_gone) {
		strbuf_addf(sb,
			_("Your branch is based on '%s', but the upstream is gone.\n"),
			base);
		if (advice_enabled(ADVICE_STATUS_HINTS))
			strbuf_addstr(sb,
				_("  (use \"git branch --unset-upstream\" to fixup)\n"));
	} else if (!sti) {
		strbuf_addf(sb,
			_("Your branch is up to date with '%s'.\n"),
			base);
	} else if (abf == AHEAD_BEHIND_QUICK) {
		strbuf_addf(sb,
			    _("Your branch and '%s' refer to different commits.\n"),
			    base);
		if (advice_enabled(ADVICE_STATUS_HINTS))
			strbuf_addf(sb, _("  (use \"%s\" for details)\n"),
				    "git status --ahead-behind");
	} else if (!theirs) {
		strbuf_addf(sb,
			Q_("Your branch is ahead of '%s' by %d commit.\n",
			   "Your branch is ahead of '%s' by %d commits.\n",
			   ours),
			base, ours);
		if (advice_enabled(ADVICE_STATUS_HINTS))
			strbuf_addstr(sb,
				_("  (use \"git push\" to publish your local commits)\n"));
	} else if (!ours) {
		strbuf_addf(sb,
			Q_("Your branch is behind '%s' by %d commit, "
			       "and can be fast-forwarded.\n",
			   "Your branch is behind '%s' by %d commits, "
			       "and can be fast-forwarded.\n",
			   theirs),
			base, theirs);
		if (advice_enabled(ADVICE_STATUS_HINTS))
			strbuf_addstr(sb,
				_("  (use \"git pull\" to update your local branch)\n"));
	} else {
		strbuf_addf(sb,
			Q_("Your branch and '%s' have diverged,\n"
			       "and have %d and %d different commit each, "
			       "respectively.\n",
			   "Your branch and '%s' have diverged,\n"
			       "and have %d and %d different commits each, "
			       "respectively.\n",
			   ours + theirs),
			base, ours, theirs);
		if (show_divergence_advice &&
		    advice_enabled(ADVICE_STATUS_HINTS))
			strbuf_addstr(sb,
				_("  (use \"git pull\" if you want to integrate the remote branch with yours)\n"));
	}
	free(base);
	return 1;
}

static int one_local_ref(const char *refname, const char *referent UNUSED, const struct object_id *oid,
			 int flag UNUSED,
			 void *cb_data)
{
	struct ref ***local_tail = cb_data;
	struct ref *ref;

	/* we already know it starts with refs/ to get here */
	if (check_refname_format(refname + 5, 0))
		return 0;

	ref = alloc_ref(refname);
	oidcpy(&ref->new_oid, oid);
	**local_tail = ref;
	*local_tail = &ref->next;
	return 0;
}

struct ref *get_local_heads(void)
{
	struct ref *local_refs = NULL, **local_tail = &local_refs;

	refs_for_each_ref(get_main_ref_store(the_repository), one_local_ref,
			  &local_tail);
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

	/* If a remote branch exists with the default branch name, let's use it. */
	if (!all) {
		char *default_branch = repo_default_branch_name(the_repository, 0);
		char *ref = xstrfmt("refs/heads/%s", default_branch);

		r = find_ref_by_name(refs, ref);
		free(ref);
		free(default_branch);

		if (r && oideq(&r->old_oid, &head->old_oid))
			return copy_ref(r);

		/* Fall back to the hard-coded historical default */
		r = find_ref_by_name(refs, "refs/heads/master");
		if (r && oideq(&r->old_oid, &head->old_oid))
			return copy_ref(r);
	}

	/* Look for another ref that points there */
	for (r = refs; r; r = r->next) {
		if (r != head &&
		    starts_with(r->name, "refs/heads/") &&
		    oideq(&r->old_oid, &head->old_oid)) {
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
	struct refspec *rs;
};

static int get_stale_heads_cb(const char *refname, const char *referent UNUSED, const struct object_id *oid,
			      int flags, void *cb_data)
{
	struct stale_heads_info *info = cb_data;
	struct string_list matches = STRING_LIST_INIT_DUP;
	struct refspec_item query;
	int i, stale = 1;
	memset(&query, 0, sizeof(struct refspec_item));
	query.dst = (char *)refname;

	query_refspecs_multiple(info->rs, &query, &matches);
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
		oidcpy(&ref->new_oid, oid);
	}

clean_exit:
	string_list_clear(&matches, 0);
	return 0;
}

struct ref *get_stale_heads(struct refspec *rs, struct ref *fetch_map)
{
	struct ref *ref, *stale_refs = NULL;
	struct string_list ref_names = STRING_LIST_INIT_NODUP;
	struct stale_heads_info info;

	info.ref_names = &ref_names;
	info.stale_refs_tail = &stale_refs;
	info.rs = rs;
	for (ref = fetch_map; ref; ref = ref->next)
		string_list_append(&ref_names, ref->name);
	string_list_sort(&ref_names);
	refs_for_each_ref(get_main_ref_store(the_repository),
			  get_stale_heads_cb, &info);
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

static int parse_push_cas_option(struct push_cas_option *cas, const char *arg, int unset)
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
	else if (!colon[1])
		oidclr(&entry->expect, the_repository->hash_algo);
	else if (repo_get_oid(the_repository, colon + 1, &entry->expect))
		return error(_("cannot parse expected object name '%s'"),
			     colon + 1);
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
 * tracking branch for the refname there. Fill the name of
 * the remote-tracking branch in *dst_refname, and the name
 * of the commit object at its tip in oid[].
 * If we cannot do so, return negative to signal an error.
 */
static int remote_tracking(struct remote *remote, const char *refname,
			   struct object_id *oid, char **dst_refname)
{
	char *dst;

	dst = apply_refspecs(&remote->fetch, refname);
	if (!dst)
		return -1; /* no tracking ref for refname at remote */
	if (refs_read_ref(get_main_ref_store(the_repository), dst, oid)) {
		free(dst);
		return -1; /* we know what the tracking ref is but we cannot read it */
	}

	*dst_refname = dst;
	return 0;
}

/*
 * The struct "reflog_commit_array" and related helper functions
 * are used for collecting commits into an array during reflog
 * traversals in "check_and_collect_until()".
 */
struct reflog_commit_array {
	struct commit **item;
	size_t nr, alloc;
};

#define REFLOG_COMMIT_ARRAY_INIT { 0 }

/* Append a commit to the array. */
static void append_commit(struct reflog_commit_array *arr,
			  struct commit *commit)
{
	ALLOC_GROW(arr->item, arr->nr + 1, arr->alloc);
	arr->item[arr->nr++] = commit;
}

/* Free and reset the array. */
static void free_commit_array(struct reflog_commit_array *arr)
{
	FREE_AND_NULL(arr->item);
	arr->nr = arr->alloc = 0;
}

struct check_and_collect_until_cb_data {
	struct commit *remote_commit;
	struct reflog_commit_array *local_commits;
	timestamp_t remote_reflog_timestamp;
};

/* Get the timestamp of the latest entry. */
static int peek_reflog(struct object_id *o_oid UNUSED,
		       struct object_id *n_oid UNUSED,
		       const char *ident UNUSED,
		       timestamp_t timestamp, int tz UNUSED,
		       const char *message UNUSED, void *cb_data)
{
	timestamp_t *ts = cb_data;
	*ts = timestamp;
	return 1;
}

static int check_and_collect_until(struct object_id *o_oid UNUSED,
				   struct object_id *n_oid,
				   const char *ident UNUSED,
				   timestamp_t timestamp, int tz UNUSED,
				   const char *message UNUSED, void *cb_data)
{
	struct commit *commit;
	struct check_and_collect_until_cb_data *cb = cb_data;

	/* An entry was found. */
	if (oideq(n_oid, &cb->remote_commit->object.oid))
		return 1;

	if ((commit = lookup_commit_reference(the_repository, n_oid)))
		append_commit(cb->local_commits, commit);

	/*
	 * If the reflog entry timestamp is older than the remote ref's
	 * latest reflog entry, there is no need to check or collect
	 * entries older than this one.
	 */
	if (timestamp < cb->remote_reflog_timestamp)
		return -1;

	return 0;
}

#define MERGE_BASES_BATCH_SIZE 8

/*
 * Iterate through the reflog of the local ref to check if there is an entry
 * for the given remote-tracking ref; runs until the timestamp of an entry is
 * older than latest timestamp of remote-tracking ref's reflog. Any commits
 * are that seen along the way are collected into an array to check if the
 * remote-tracking ref is reachable from any of them.
 */
static int is_reachable_in_reflog(const char *local, const struct ref *remote)
{
	timestamp_t date;
	struct commit *commit;
	struct commit **chunk;
	struct check_and_collect_until_cb_data cb;
	struct reflog_commit_array arr = REFLOG_COMMIT_ARRAY_INIT;
	size_t size = 0;
	int ret = 0;

	commit = lookup_commit_reference(the_repository, &remote->old_oid);
	if (!commit)
		goto cleanup_return;

	/*
	 * Get the timestamp from the latest entry
	 * of the remote-tracking ref's reflog.
	 */
	refs_for_each_reflog_ent_reverse(get_main_ref_store(the_repository),
					 remote->tracking_ref, peek_reflog,
					 &date);

	cb.remote_commit = commit;
	cb.local_commits = &arr;
	cb.remote_reflog_timestamp = date;
	ret = refs_for_each_reflog_ent_reverse(get_main_ref_store(the_repository),
					       local, check_and_collect_until,
					       &cb);

	/* We found an entry in the reflog. */
	if (ret > 0)
		goto cleanup_return;

	/*
	 * Check if the remote commit is reachable from any
	 * of the commits in the collected array, in batches.
	 */
	for (chunk = arr.item; chunk < arr.item + arr.nr; chunk += size) {
		size = arr.item + arr.nr - chunk;
		if (MERGE_BASES_BATCH_SIZE < size)
			size = MERGE_BASES_BATCH_SIZE;

		if ((ret = repo_in_merge_bases_many(the_repository, commit, size, chunk, 0)))
			break;
	}

cleanup_return:
	free_commit_array(&arr);
	return ret;
}

/*
 * Check for reachability of a remote-tracking
 * ref in the reflog entries of its local ref.
 */
static void check_if_includes_upstream(struct ref *remote)
{
	struct ref *local = get_local_ref(remote->name);
	if (!local)
		return;

	if (is_reachable_in_reflog(local->name, remote) <= 0)
		remote->unreachable = 1;
	free_one_ref(local);
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
			oidcpy(&ref->old_oid_expect, &entry->expect);
		else if (remote_tracking(remote, ref->name,
					 &ref->old_oid_expect,
					 &ref->tracking_ref))
			oidclr(&ref->old_oid_expect, the_repository->hash_algo);
		else
			ref->check_reachable = cas->use_force_if_includes;
		return;
	}

	/* Are we using "--<option>" to cover all? */
	if (!cas->use_tracking_for_rest)
		return;

	ref->expect_old_sha1 = 1;
	if (remote_tracking(remote, ref->name,
			    &ref->old_oid_expect,
			    &ref->tracking_ref))
		oidclr(&ref->old_oid_expect, the_repository->hash_algo);
	else
		ref->check_reachable = cas->use_force_if_includes;
}

void apply_push_cas(struct push_cas_option *cas,
		    struct remote *remote,
		    struct ref *remote_refs)
{
	struct ref *ref;
	for (ref = remote_refs; ref; ref = ref->next) {
		apply_cas(cas, remote, ref);

		/*
		 * If "compare-and-swap" is in "use_tracking[_for_rest]"
		 * mode, and if "--force-if-includes" was specified, run
		 * the check.
		 */
		if (ref->check_reachable)
			check_if_includes_upstream(ref);
	}
}

struct remote_state *remote_state_new(void)
{
	struct remote_state *r;

	CALLOC_ARRAY(r, 1);

	hashmap_init(&r->remotes_hash, remotes_hash_cmp, NULL, 0);
	hashmap_init(&r->branches_hash, branches_hash_cmp, NULL, 0);
	return r;
}

void remote_state_clear(struct remote_state *remote_state)
{
	struct hashmap_iter iter;
	struct branch *b;
	int i;

	for (i = 0; i < remote_state->remotes_nr; i++)
		remote_clear(remote_state->remotes[i]);
	FREE_AND_NULL(remote_state->remotes);
	FREE_AND_NULL(remote_state->pushremote_name);
	remote_state->remotes_alloc = 0;
	remote_state->remotes_nr = 0;

	rewrites_release(&remote_state->rewrites);
	rewrites_release(&remote_state->rewrites_push);

	hashmap_clear_and_free(&remote_state->remotes_hash, struct remote, ent);
	hashmap_for_each_entry(&remote_state->branches_hash, &iter, b, ent) {
		branch_release(b);
		free(b);
	}
	hashmap_clear(&remote_state->branches_hash);
}

/*
 * Returns 1 if it was the last chop before ':'.
 */
static int chop_last_dir(char **remoteurl, int is_relative)
{
	char *rfind = find_last_dir_sep(*remoteurl);
	if (rfind) {
		*rfind = '\0';
		return 0;
	}

	rfind = strrchr(*remoteurl, ':');
	if (rfind) {
		*rfind = '\0';
		return 1;
	}

	if (is_relative || !strcmp(".", *remoteurl))
		die(_("cannot strip one component off url '%s'"),
			*remoteurl);

	free(*remoteurl);
	*remoteurl = xstrdup(".");
	return 0;
}

char *relative_url(const char *remote_url, const char *url,
		   const char *up_path)
{
	int is_relative = 0;
	int colonsep = 0;
	char *out;
	char *remoteurl;
	struct strbuf sb = STRBUF_INIT;
	size_t len;

	if (!url_is_local_not_ssh(url) || is_absolute_path(url))
		return xstrdup(url);

	len = strlen(remote_url);
	if (!len)
		BUG("invalid empty remote_url");

	remoteurl = xstrdup(remote_url);
	if (is_dir_sep(remoteurl[len-1]))
		remoteurl[len-1] = '\0';

	if (!url_is_local_not_ssh(remoteurl) || is_absolute_path(remoteurl))
		is_relative = 0;
	else {
		is_relative = 1;
		/*
		 * Prepend a './' to ensure all relative
		 * remoteurls start with './' or '../'
		 */
		if (!starts_with_dot_slash_native(remoteurl) &&
		    !starts_with_dot_dot_slash_native(remoteurl)) {
			strbuf_reset(&sb);
			strbuf_addf(&sb, "./%s", remoteurl);
			free(remoteurl);
			remoteurl = strbuf_detach(&sb, NULL);
		}
	}
	/*
	 * When the url starts with '../', remove that and the
	 * last directory in remoteurl.
	 */
	while (*url) {
		if (starts_with_dot_dot_slash_native(url)) {
			url += 3;
			colonsep |= chop_last_dir(&remoteurl, is_relative);
		} else if (starts_with_dot_slash_native(url))
			url += 2;
		else
			break;
	}
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s%s%s", remoteurl, colonsep ? ":" : "/", url);
	if (ends_with(url, "/"))
		strbuf_setlen(&sb, sb.len - 1);
	free(remoteurl);

	if (starts_with_dot_slash_native(sb.buf))
		out = xstrdup(sb.buf + 2);
	else
		out = xstrdup(sb.buf);

	if (!up_path || !is_relative) {
		strbuf_release(&sb);
		return out;
	}

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s%s", up_path, out);
	free(out);
	return strbuf_detach(&sb, NULL);
}
