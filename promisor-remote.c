#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "object-store-ll.h"
#include "promisor-remote.h"
#include "config.h"
#include "trace2.h"
#include "transport.h"
#include "strvec.h"
#include "packfile.h"
#include "environment.h"
#include "url.h"

struct promisor_remote_config {
	struct promisor_remote *promisors;
	struct promisor_remote **promisors_tail;
};

static int fetch_objects(struct repository *repo,
			 const char *remote_name,
			 const struct object_id *oids,
			 int oid_nr)
{
	struct child_process child = CHILD_PROCESS_INIT;
	int i;
	FILE *child_in;
	int quiet;

	if (git_env_bool(NO_LAZY_FETCH_ENVIRONMENT, 0)) {
		static int warning_shown;
		if (!warning_shown) {
			warning_shown = 1;
			warning(_("lazy fetching disabled; some objects may not be available"));
		}
		return -1;
	}

	child.git_cmd = 1;
	child.in = -1;
	if (repo != the_repository)
		prepare_other_repo_env(&child.env, repo->gitdir);
	strvec_pushl(&child.args, "-c", "fetch.negotiationAlgorithm=noop",
		     "fetch", remote_name, "--no-tags",
		     "--no-write-fetch-head", "--recurse-submodules=no",
		     "--filter=blob:none", "--stdin", NULL);
	if (!git_config_get_bool("promisor.quiet", &quiet) && quiet)
		strvec_push(&child.args, "--quiet");
	if (start_command(&child))
		die(_("promisor-remote: unable to fork off fetch subprocess"));
	child_in = xfdopen(child.in, "w");

	trace2_data_intmax("promisor", repo, "fetch_count", oid_nr);

	for (i = 0; i < oid_nr; i++) {
		if (fputs(oid_to_hex(&oids[i]), child_in) < 0)
			die_errno(_("promisor-remote: could not write to fetch subprocess"));
		if (fputc('\n', child_in) < 0)
			die_errno(_("promisor-remote: could not write to fetch subprocess"));
	}

	if (fclose(child_in) < 0)
		die_errno(_("promisor-remote: could not close stdin to fetch subprocess"));
	return finish_command(&child) ? -1 : 0;
}

static struct promisor_remote *promisor_remote_new(struct promisor_remote_config *config,
						   const char *remote_name)
{
	struct promisor_remote *r;

	if (*remote_name == '/') {
		warning(_("promisor remote name cannot begin with '/': %s"),
			remote_name);
		return NULL;
	}

	FLEX_ALLOC_STR(r, name, remote_name);

	*config->promisors_tail = r;
	config->promisors_tail = &r->next;

	return r;
}

static struct promisor_remote *promisor_remote_lookup(struct promisor_remote_config *config,
						      const char *remote_name,
						      struct promisor_remote **previous)
{
	struct promisor_remote *r, *p;

	for (p = NULL, r = config->promisors; r; p = r, r = r->next)
		if (!strcmp(r->name, remote_name)) {
			if (previous)
				*previous = p;
			return r;
		}

	return NULL;
}

static void promisor_remote_move_to_tail(struct promisor_remote_config *config,
					 struct promisor_remote *r,
					 struct promisor_remote *previous)
{
	if (!r->next)
		return;

	if (previous)
		previous->next = r->next;
	else
		config->promisors = r->next ? r->next : r;
	r->next = NULL;
	*config->promisors_tail = r;
	config->promisors_tail = &r->next;
}

static int promisor_remote_config(const char *var, const char *value,
				  const struct config_context *ctx UNUSED,
				  void *data)
{
	struct promisor_remote_config *config = data;
	const char *name;
	size_t namelen;
	const char *subkey;

	if (parse_config_key(var, "remote", &name, &namelen, &subkey) < 0)
		return 0;

	if (!strcmp(subkey, "promisor")) {
		char *remote_name;

		if (!git_config_bool(var, value))
			return 0;

		remote_name = xmemdupz(name, namelen);

		if (!promisor_remote_lookup(config, remote_name, NULL))
			promisor_remote_new(config, remote_name);

		free(remote_name);
		return 0;
	}
	if (!strcmp(subkey, "partialclonefilter")) {
		struct promisor_remote *r;
		char *remote_name = xmemdupz(name, namelen);

		r = promisor_remote_lookup(config, remote_name, NULL);
		if (!r)
			r = promisor_remote_new(config, remote_name);

		free(remote_name);

		if (!r)
			return 0;

		FREE_AND_NULL(r->partial_clone_filter);
		return git_config_string(&r->partial_clone_filter, var, value);
	}

	return 0;
}

static void promisor_remote_init(struct repository *r)
{
	struct promisor_remote_config *config;

	if (r->promisor_remote_config)
		return;
	config = r->promisor_remote_config =
		xcalloc(1, sizeof(*r->promisor_remote_config));
	config->promisors_tail = &config->promisors;

	repo_config(r, promisor_remote_config, config);

	if (r->repository_format_partial_clone) {
		struct promisor_remote *o, *previous;

		o = promisor_remote_lookup(config,
					   r->repository_format_partial_clone,
					   &previous);
		if (o)
			promisor_remote_move_to_tail(config, o, previous);
		else
			promisor_remote_new(config, r->repository_format_partial_clone);
	}
}

void promisor_remote_clear(struct promisor_remote_config *config)
{
	while (config->promisors) {
		struct promisor_remote *r = config->promisors;
		free(r->partial_clone_filter);
		config->promisors = config->promisors->next;
		free(r);
	}

	config->promisors_tail = &config->promisors;
}

void repo_promisor_remote_reinit(struct repository *r)
{
	promisor_remote_clear(r->promisor_remote_config);
	FREE_AND_NULL(r->promisor_remote_config);
	promisor_remote_init(r);
}

struct promisor_remote *repo_promisor_remote_find(struct repository *r,
						  const char *remote_name)
{
	promisor_remote_init(r);

	if (!remote_name)
		return r->promisor_remote_config->promisors;

	return promisor_remote_lookup(r->promisor_remote_config, remote_name, NULL);
}

int repo_has_promisor_remote(struct repository *r)
{
	return !!repo_promisor_remote_find(r, NULL);
}

int repo_has_accepted_promisor_remote(struct repository *r)
{
	struct promisor_remote *p;

	promisor_remote_init(r);

	for (p = r->promisor_remote_config->promisors; p; p = p->next)
		if (p->accepted)
			return 1;
	return 0;
}

static int remove_fetched_oids(struct repository *repo,
			       struct object_id **oids,
			       int oid_nr, int to_free)
{
	int i, remaining_nr = 0;
	int *remaining = xcalloc(oid_nr, sizeof(*remaining));
	struct object_id *old_oids = *oids;
	struct object_id *new_oids;

	for (i = 0; i < oid_nr; i++)
		if (oid_object_info_extended(repo, &old_oids[i], NULL,
					     OBJECT_INFO_SKIP_FETCH_OBJECT)) {
			remaining[i] = 1;
			remaining_nr++;
		}

	if (remaining_nr) {
		int j = 0;
		CALLOC_ARRAY(new_oids, remaining_nr);
		for (i = 0; i < oid_nr; i++)
			if (remaining[i])
				oidcpy(&new_oids[j++], &old_oids[i]);
		*oids = new_oids;
		if (to_free)
			free(old_oids);
	}

	free(remaining);

	return remaining_nr;
}

void promisor_remote_get_direct(struct repository *repo,
				const struct object_id *oids,
				int oid_nr)
{
	struct promisor_remote *r;
	struct object_id *remaining_oids = (struct object_id *)oids;
	int remaining_nr = oid_nr;
	int to_free = 0;
	int i;

	if (oid_nr == 0)
		return;

	promisor_remote_init(repo);

	for (r = repo->promisor_remote_config->promisors; r; r = r->next) {
		if (fetch_objects(repo, r->name, remaining_oids, remaining_nr) < 0) {
			if (remaining_nr == 1)
				continue;
			remaining_nr = remove_fetched_oids(repo, &remaining_oids,
							 remaining_nr, to_free);
			if (remaining_nr) {
				to_free = 1;
				continue;
			}
		}
		goto all_fetched;
	}

	for (i = 0; i < remaining_nr; i++) {
		if (is_promisor_object(&remaining_oids[i]))
			die(_("could not fetch %s from promisor remote"),
			    oid_to_hex(&remaining_oids[i]));
	}

all_fetched:
	if (to_free)
		free(remaining_oids);
}

static int allow_unsanitized(char ch)
{
	if (ch == ',' || ch == ';' || ch == '%')
		return 0;
	return ch > 32 && ch < 127;
}

static void promisor_info_vecs(struct repository *repo,
			       struct strvec *names,
			       struct strvec *urls)
{
	struct promisor_remote *r;

	promisor_remote_init(repo);

	for (r = repo->promisor_remote_config->promisors; r; r = r->next) {
		char *url;
		char *url_key = xstrfmt("remote.%s.url", r->name);

		strvec_push(names, r->name);
		strvec_push(urls, git_config_get_string(url_key, &url) ? NULL : url);

		free(url);
		free(url_key);
	}
}

char *promisor_remote_info(struct repository *repo)
{
	struct strbuf sb = STRBUF_INIT;
	int advertise_promisors = 0;
	struct strvec names = STRVEC_INIT;
	struct strvec urls = STRVEC_INIT;

	git_config_get_bool("promisor.advertise", &advertise_promisors);

	if (!advertise_promisors)
		return NULL;

	promisor_info_vecs(repo, &names, &urls);

	if (!names.nr)
		return NULL;

	for (size_t i = 0; i < names.nr; i++) {
		if (i)
			strbuf_addch(&sb, ';');
		strbuf_addstr(&sb, "name=");
		strbuf_addstr_urlencode(&sb, names.v[i], allow_unsanitized);
		if (urls.v[i]) {
			strbuf_addstr(&sb, ",url=");
			strbuf_addstr_urlencode(&sb, urls.v[i], allow_unsanitized);
		}
	}

	strbuf_sanitize(&sb);

	strvec_clear(&names);
	strvec_clear(&urls);

	return strbuf_detach(&sb, NULL);
}

/*
 * Find first index of 'vec' where there is 'val'. 'val' is compared
 * case insensively to the strings in 'vec'. If not found 'vec->nr' is
 * returned.
 */
static size_t strvec_find_index(struct strvec *vec, const char *val)
{
	for (size_t i = 0; i < vec->nr; i++)
		if (!strcasecmp(vec->v[i], val))
			return i;
	return vec->nr;
}

enum accept_promisor {
	ACCEPT_NONE = 0,
	ACCEPT_KNOWN_URL,
	ACCEPT_KNOWN_NAME,
	ACCEPT_ALL
};

static int should_accept_remote(enum accept_promisor accept,
				const char *remote_name, const char *remote_url,
				struct strvec *names, struct strvec *urls)
{
	size_t i;

	if (accept == ACCEPT_ALL)
		return 1;

	i = strvec_find_index(names, remote_name);

	if (i >= names->nr)
		/* We don't know about that remote */
		return 0;

	if (accept == ACCEPT_KNOWN_NAME)
		return 1;

	if (accept != ACCEPT_KNOWN_URL)
		BUG("Unhandled 'enum accept_promisor' value '%d'", accept);

	if (!strcasecmp(urls->v[i], remote_url))
		return 1;

	warning(_("known remote named '%s' but with url '%s' instead of '%s'"),
		remote_name, urls->v[i], remote_url);

	return 0;
}

static void filter_promisor_remote(struct repository *repo,
				   struct strvec *accepted,
				   const char *info)
{
	struct strbuf **remotes;
	char *accept_str;
	enum accept_promisor accept = ACCEPT_NONE;
	struct strvec names = STRVEC_INIT;
	struct strvec urls = STRVEC_INIT;

	if (!git_config_get_string("promisor.acceptfromserver", &accept_str)) {
		if (!accept_str || !*accept_str || !strcasecmp("None", accept_str))
			accept = ACCEPT_NONE;
		else if (!strcasecmp("KnownUrl", accept_str))
			accept = ACCEPT_KNOWN_URL;
		else if (!strcasecmp("KnownName", accept_str))
			accept = ACCEPT_KNOWN_NAME;
		else if (!strcasecmp("All", accept_str))
			accept = ACCEPT_ALL;
		else
			warning(_("unknown '%s' value for '%s' config option"),
				accept_str, "promisor.acceptfromserver");
	}

	if (accept == ACCEPT_NONE)
		return;

	if (accept != ACCEPT_ALL)
		promisor_info_vecs(repo, &names, &urls);

	/* Parse remote info received */

	remotes = strbuf_split_str(info, ';', 0);

	for (size_t i = 0; remotes[i]; i++) {
		struct strbuf **elems;
		const char *remote_name = NULL;
		const char *remote_url = NULL;
		char *decoded_name = NULL;
		char *decoded_url = NULL;

		strbuf_trim_trailing_ch(remotes[i], ';');
		elems = strbuf_split_str(remotes[i]->buf, ',', 0);

		for (size_t j = 0; elems[j]; j++) {
			int res;
			strbuf_trim_trailing_ch(elems[j], ',');
			res = skip_prefix(elems[j]->buf, "name=", &remote_name) ||
				skip_prefix(elems[j]->buf, "url=", &remote_url);
			if (!res)
				warning(_("unknown element '%s' from remote info"),
					elems[j]->buf);
		}

		if (remote_name)
			decoded_name = url_percent_decode(remote_name);
		if (remote_url)
			decoded_url = url_percent_decode(remote_url);

		if (decoded_name && should_accept_remote(accept, decoded_name, decoded_url, &names, &urls))
			strvec_push(accepted, decoded_name);

		strbuf_list_free(elems);
		free(decoded_name);
		free(decoded_url);
	}

	free(accept_str);
	strvec_clear(&names);
	strvec_clear(&urls);
	strbuf_list_free(remotes);
}

char *promisor_remote_reply(const char *info)
{
	struct strvec accepted = STRVEC_INIT;
	struct strbuf reply = STRBUF_INIT;

	filter_promisor_remote(the_repository, &accepted, info);

	if (!accepted.nr)
		return NULL;

	for (size_t i = 0; i < accepted.nr; i++) {
		if (i)
			strbuf_addch(&reply, ';');
		strbuf_addstr_urlencode(&reply, accepted.v[i], allow_unsanitized);
	}

	strvec_clear(&accepted);

	return strbuf_detach(&reply, NULL);
}

void mark_promisor_remotes_as_accepted(struct repository *r, const char *remotes)
{
	struct strbuf **accepted_remotes = strbuf_split_str(remotes, ';', 0);

	for (size_t i = 0; accepted_remotes[i]; i++) {
		struct promisor_remote *p;
		char *decoded_remote;

		strbuf_trim_trailing_ch(accepted_remotes[i], ';');
		decoded_remote = url_percent_decode(accepted_remotes[i]->buf);

		p = repo_promisor_remote_find(r, decoded_remote);
		if (p)
			p->accepted = 1;
		else
			warning(_("accepted promisor remote '%s' not found"),
				decoded_remote);

		free(decoded_remote);
	}

	strbuf_list_free(accepted_remotes);
}
