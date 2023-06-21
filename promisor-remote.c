#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "object-store.h"
#include "promisor-remote.h"
#include "config.h"
#include "trace2.h"
#include "transport.h"
#include "strvec.h"
#include "packfile.h"

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

	child.git_cmd = 1;
	child.in = -1;
	if (repo != the_repository)
		prepare_other_repo_env(&child.env, repo->gitdir);
	strvec_pushl(&child.args, "-c", "fetch.negotiationAlgorithm=noop",
		     "fetch", remote_name, "--no-tags",
		     "--no-write-fetch-head", "--recurse-submodules=no",
		     "--filter=blob:none", "--stdin", NULL);
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

static int promisor_remote_config(const char *var, const char *value, void *data)
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
