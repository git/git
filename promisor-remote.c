#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "odb.h"
#include "promisor-remote.h"
#include "config.h"
#include "trace2.h"
#include "transport.h"
#include "strvec.h"
#include "packfile.h"
#include "environment.h"
#include "url.h"
#include "version.h"

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
	if (!repo_config_get_bool(the_repository, "promisor.quiet", &quiet) && quiet)
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
		free(r->advertised_filter);
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
		if (odb_read_object_info_extended(repo->objects, &old_oids[i], NULL,
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
		if (is_promisor_object(repo, &remaining_oids[i]))
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

/*
 * All the fields used in "promisor-remote" protocol capability,
 * including the mandatory "name" and "url" ones.
 */
static const char promisor_field_name[] = "name";
static const char promisor_field_url[] = "url";
static const char promisor_field_filter[] = "partialCloneFilter";
static const char promisor_field_token[] = "token";

/*
 * List of optional field names that can be used in the
 * "promisor-remote" protocol capability (others must be
 * ignored). Each field should correspond to a configurable property
 * of a remote that can be relevant for the client.
 */
static const char *known_fields[] = {
	promisor_field_filter, /* Filter used for partial clone */
	promisor_field_token,  /* Authentication token for the remote */
	NULL
};

/*
 * Check if 'field' is in the list of the known field names for the
 * "promisor-remote" protocol capability.
 */
static int is_known_field(const char *field)
{
	const char **p;

	for (p = known_fields; *p; p++)
		if (!strcasecmp(*p, field))
			return 1;
	return 0;
}

static int is_valid_field(struct string_list_item *item, void *cb_data)
{
	const char *field = item->string;
	const char *config_key = (const char *)cb_data;

	if (!is_known_field(field)) {
		warning(_("unsupported field '%s' in '%s' config"), field, config_key);
		return 0;
	}
	return 1;
}

static char *fields_from_config(struct string_list *fields_list, const char *config_key)
{
	char *fields = NULL;

	if (!repo_config_get_string(the_repository, config_key, &fields) && *fields) {
		string_list_split_in_place_f(fields_list, fields, ",", -1,
					     STRING_LIST_SPLIT_TRIM |
					     STRING_LIST_SPLIT_NONEMPTY);
		filter_string_list(fields_list, 0, is_valid_field, (void *)config_key);
	}

	return fields;
}

static struct string_list *initialize_fields_list(struct string_list *fields_list, int *initialized,
						  const char *config_key)
{
	if (!*initialized) {
		fields_list->cmp = strcasecmp;
		fields_from_config(fields_list, config_key);
		*initialized = 1;
	}

	return fields_list;
}

static struct string_list *fields_sent(void)
{
	static struct string_list fields_list = STRING_LIST_INIT_NODUP;
	static int initialized;

	return initialize_fields_list(&fields_list, &initialized, "promisor.sendFields");
}

static struct string_list *fields_checked(void)
{
	static struct string_list fields_list = STRING_LIST_INIT_NODUP;
	static int initialized;

	return initialize_fields_list(&fields_list, &initialized, "promisor.checkFields");
}

static struct string_list *fields_stored(void)
{
	static struct string_list fields_list = STRING_LIST_INIT_NODUP;
	static int initialized;

	return initialize_fields_list(&fields_list, &initialized, "promisor.storeFields");
}

/*
 * Struct for promisor remotes involved in the "promisor-remote"
 * protocol capability.
 *
 * Except for "name", each <member> in this struct and its <value>
 * should correspond (either on the client side or on the server side)
 * to a "remote.<name>.<member>" config variable set to <value> where
 * "<name>" is a promisor remote name.
 */
struct promisor_info {
	const char *name;
	const char *url;
	const char *filter;
	const char *token;
};

static void promisor_info_free(struct promisor_info *p)
{
	free((char *)p->name);
	free((char *)p->url);
	free((char *)p->filter);
	free((char *)p->token);
	free(p);
}

static void promisor_info_list_clear(struct string_list *list)
{
	for (size_t i = 0; i < list->nr; i++)
		promisor_info_free(list->items[i].util);
	string_list_clear(list, 0);
}

static void set_one_field(struct promisor_info *p,
			  const char *field, const char *value)
{
	if (!strcasecmp(field, promisor_field_filter))
		p->filter = xstrdup(value);
	else if (!strcasecmp(field, promisor_field_token))
		p->token = xstrdup(value);
	else
		BUG("invalid field '%s'", field);
}

static void set_fields(struct promisor_info *p,
		       struct string_list *field_names)
{
	struct string_list_item *item;

	for_each_string_list_item(item, field_names) {
		char *key = xstrfmt("remote.%s.%s", p->name, item->string);
		const char *val;
		if (!repo_config_get_string_tmp(the_repository, key, &val) && *val)
			set_one_field(p, item->string, val);
		free(key);
	}
}

/*
 * Populate 'list' with promisor remote information from the config.
 * The 'util' pointer of each list item will hold a 'struct
 * promisor_info'. Except "name" and "url", only members of that
 * struct specified by the 'field_names' list are set (using values
 * from the configuration).
 */
static void promisor_config_info_list(struct repository *repo,
				      struct string_list *list,
				      struct string_list *field_names)
{
	struct promisor_remote *r;

	promisor_remote_init(repo);

	for (r = repo->promisor_remote_config->promisors; r; r = r->next) {
		const char *url;
		char *url_key = xstrfmt("remote.%s.url", r->name);

		/* Only add remotes with a non empty URL */
		if (!repo_config_get_string_tmp(the_repository, url_key, &url) && *url) {
			struct promisor_info *new_info = xcalloc(1, sizeof(*new_info));
			struct string_list_item *item;

			new_info->name = xstrdup(r->name);
			new_info->url = xstrdup(url);

			if (field_names)
				set_fields(new_info, field_names);

			item = string_list_append(list, new_info->name);
			item->util = new_info;
		}

		free(url_key);
	}
}

char *promisor_remote_info(struct repository *repo)
{
	struct strbuf sb = STRBUF_INIT;
	int advertise_promisors = 0;
	struct string_list config_info = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;

	repo_config_get_bool(the_repository, "promisor.advertise", &advertise_promisors);

	if (!advertise_promisors)
		return NULL;

	promisor_config_info_list(repo, &config_info, fields_sent());

	if (!config_info.nr)
		return NULL;

	for_each_string_list_item(item, &config_info) {
		struct promisor_info *p = item->util;

		if (item != config_info.items)
			strbuf_addch(&sb, ';');

		strbuf_addf(&sb, "%s=", promisor_field_name);
		strbuf_addstr_urlencode(&sb, p->name, allow_unsanitized);
		strbuf_addf(&sb, ",%s=", promisor_field_url);
		strbuf_addstr_urlencode(&sb, p->url, allow_unsanitized);

		if (p->filter) {
			strbuf_addf(&sb, ",%s=", promisor_field_filter);
			strbuf_addstr_urlencode(&sb, p->filter, allow_unsanitized);
		}
		if (p->token) {
			strbuf_addf(&sb, ",%s=", promisor_field_token);
			strbuf_addstr_urlencode(&sb, p->token, allow_unsanitized);
		}
	}

	promisor_info_list_clear(&config_info);

	return strbuf_detach(&sb, NULL);
}

enum accept_promisor {
	ACCEPT_NONE = 0,
	ACCEPT_KNOWN_URL,
	ACCEPT_KNOWN_NAME,
	ACCEPT_ALL
};

static int match_field_against_config(const char *field, const char *value,
				      struct promisor_info *config_info)
{
	if (config_info->filter && !strcasecmp(field, promisor_field_filter))
		return !strcmp(config_info->filter, value);
	else if (config_info->token && !strcasecmp(field, promisor_field_token))
		return !strcmp(config_info->token, value);

	return 0;
}

static int all_fields_match(struct promisor_info *advertised,
			    struct string_list *config_info,
			    int in_list)
{
	struct string_list *fields = fields_checked();
	struct string_list_item *item_checked;

	for_each_string_list_item(item_checked, fields) {
		int match = 0;
		const char *field = item_checked->string;
		const char *value = NULL;
		struct string_list_item *item;

		if (!strcasecmp(field, promisor_field_filter))
			value = advertised->filter;
		else if (!strcasecmp(field, promisor_field_token))
			value = advertised->token;

		if (!value)
			return 0;

		if (in_list) {
			for_each_string_list_item(item, config_info) {
				struct promisor_info *p = item->util;
				if (match_field_against_config(field, value, p)) {
					match = 1;
					break;
				}
			}
		} else {
			item = string_list_lookup(config_info, advertised->name);
			if (item) {
				struct promisor_info *p = item->util;
				match = match_field_against_config(field, value, p);
			}
		}

		if (!match)
			return 0;
	}

	return 1;
}

static int should_accept_remote(enum accept_promisor accept,
				struct promisor_info *advertised,
				struct string_list *config_info)
{
	struct promisor_info *p;
	struct string_list_item *item;
	const char *remote_name = advertised->name;
	const char *remote_url = advertised->url;

	if (accept == ACCEPT_ALL)
		return all_fields_match(advertised, config_info, 1);

	/* Get config info for that promisor remote */
	item = string_list_lookup(config_info, remote_name);

	if (!item)
		/* We don't know about that remote */
		return 0;

	p = item->util;

	if (accept == ACCEPT_KNOWN_NAME)
		return all_fields_match(advertised, config_info, 0);

	if (accept != ACCEPT_KNOWN_URL)
		BUG("Unhandled 'enum accept_promisor' value '%d'", accept);

	if (!remote_url || !*remote_url) {
		warning(_("no or empty URL advertised for remote '%s'"), remote_name);
		return 0;
	}

	if (!strcmp(p->url, remote_url))
		return all_fields_match(advertised, config_info, 0);

	warning(_("known remote named '%s' but with URL '%s' instead of '%s'"),
		remote_name, p->url, remote_url);

	return 0;
}

static int skip_field_name_prefix(const char *elem, const char *field_name, const char **value)
{
	const char *p;
	if (!skip_prefix(elem, field_name, &p) || *p != '=')
		return 0;
	*value = p + 1;
	return 1;
}

static struct promisor_info *parse_one_advertised_remote(const char *remote_info)
{
	struct promisor_info *info = xcalloc(1, sizeof(*info));
	struct string_list elem_list = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&elem_list, remote_info, ",", -1);

	for_each_string_list_item(item, &elem_list) {
		const char *elem = item->string;
		const char *p = strchr(elem, '=');

		if (!p) {
			warning(_("invalid element '%s' from remote info"), elem);
			continue;
		}

		if (skip_field_name_prefix(elem, promisor_field_name, &p))
			info->name = url_percent_decode(p);
		else if (skip_field_name_prefix(elem, promisor_field_url, &p))
			info->url = url_percent_decode(p);
		else if (skip_field_name_prefix(elem, promisor_field_filter, &p))
			info->filter = url_percent_decode(p);
		else if (skip_field_name_prefix(elem, promisor_field_token, &p))
			info->token = url_percent_decode(p);
	}

	string_list_clear(&elem_list, 0);

	if (!info->name || !info->url) {
		warning(_("server advertised a promisor remote without a name or URL: %s"),
			remote_info);
		promisor_info_free(info);
		return NULL;
	}

	return info;
}

static bool store_one_field(struct repository *repo, const char *remote_name,
			    const char *field_name, const char *field_key,
			    const char *advertised, const char *current)
{
	if (advertised && (!current || strcmp(current, advertised))) {
		char *key = xstrfmt("remote.%s.%s", remote_name, field_key);

		fprintf(stderr, _("Storing new %s from server for remote '%s'.\n"
				  "    '%s' -> '%s'\n"),
			field_name, remote_name,
			current ? current : "",
			advertised);

		repo_config_set_worktree_gently(repo, key, advertised);
		free(key);

		return true;
	}

	return false;
}

/* Check that a filter is valid by parsing it */
static bool valid_filter(const char *filter, const char *remote_name)
{
	struct list_objects_filter_options filter_opts = LIST_OBJECTS_FILTER_INIT;
	struct strbuf err = STRBUF_INIT;
	int res = gently_parse_list_objects_filter(&filter_opts, filter, &err);

	if (res)
		warning(_("invalid filter '%s' for remote '%s' "
			  "will not be stored: %s"),
			filter, remote_name, err.buf);

	list_objects_filter_release(&filter_opts);
	strbuf_release(&err);

	return !res;
}

/* Check that a token doesn't contain any control character */
static bool valid_token(const char *token, const char *remote_name)
{
	const char *c = token;

	for (; *c; c++)
		if (iscntrl(*c)) {
			warning(_("invalid token '%s' for remote '%s' "
				  "will not be stored"),
				token, remote_name);
			return false;
		}

	return true;
}

struct store_info {
	struct repository *repo;
	struct string_list config_info;
	bool store_filter;
	bool store_token;
};

static struct store_info *new_store_info(struct repository *repo)
{
	struct string_list *fields_to_store = fields_stored();
	struct store_info *s = xmalloc(sizeof(*s));

	s->repo = repo;

	string_list_init_nodup(&s->config_info);
	promisor_config_info_list(repo, &s->config_info, fields_to_store);
	string_list_sort(&s->config_info);

	s->store_filter = !!string_list_lookup(fields_to_store, promisor_field_filter);
	s->store_token = !!string_list_lookup(fields_to_store, promisor_field_token);

	return s;
}

static void free_store_info(struct store_info *s)
{
	if (s) {
		promisor_info_list_clear(&s->config_info);
		free(s);
	}
}

static bool promisor_store_advertised_fields(struct promisor_info *advertised,
					     struct store_info *store_info)
{
	struct promisor_info *p;
	struct string_list_item *item;
	const char *remote_name = advertised->name;
	bool reload_config = false;

	if (!(store_info->store_filter || store_info->store_token))
		return false;

	/*
	 * Get existing config info for the advertised promisor
	 * remote. This ensures the remote is already configured on
	 * the client side.
	 */
	item = string_list_lookup(&store_info->config_info, remote_name);

	if (!item)
		return false;

	p = item->util;

	if (store_info->store_filter && advertised->filter &&
	    valid_filter(advertised->filter, remote_name))
		reload_config |= store_one_field(store_info->repo, remote_name,
						 "filter", promisor_field_filter,
						 advertised->filter, p->filter);

	if (store_info->store_token && advertised->token &&
	    valid_token(advertised->token, remote_name))
		reload_config |= store_one_field(store_info->repo, remote_name,
						 "token", promisor_field_token,
						 advertised->token, p->token);

	return reload_config;
}

static void filter_promisor_remote(struct repository *repo,
				   struct strvec *accepted,
				   const char *info)
{
	const char *accept_str;
	enum accept_promisor accept = ACCEPT_NONE;
	struct string_list config_info = STRING_LIST_INIT_NODUP;
	struct string_list remote_info = STRING_LIST_INIT_DUP;
	struct store_info *store_info = NULL;
	struct string_list_item *item;
	bool reload_config = false;
	struct string_list captured_filters = STRING_LIST_INIT_DUP;

	if (!repo_config_get_string_tmp(the_repository, "promisor.acceptfromserver", &accept_str)) {
		if (!*accept_str || !strcasecmp("None", accept_str))
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

	/* Parse remote info received */

	string_list_split(&remote_info, info, ";", -1);

	for_each_string_list_item(item, &remote_info) {
		struct promisor_info *advertised;

		advertised = parse_one_advertised_remote(item->string);

		if (!advertised)
			continue;

		if (!config_info.nr) {
			promisor_config_info_list(repo, &config_info, fields_checked());
			string_list_sort(&config_info);
		}

		if (should_accept_remote(accept, advertised, &config_info)) {
			if (!store_info)
				store_info = new_store_info(repo);
			if (promisor_store_advertised_fields(advertised, store_info))
				reload_config = true;

			strvec_push(accepted, advertised->name);

			/* Capture advertised filters for accepted remotes */
			if (advertised->filter) {
				struct string_list_item *i;
				i = string_list_append(&captured_filters, advertised->name);
				i->util = xstrdup(advertised->filter);
			}
		}

		promisor_info_free(advertised);
	}

	promisor_info_list_clear(&config_info);
	string_list_clear(&remote_info, 0);
	free_store_info(store_info);

	if (reload_config)
		repo_promisor_remote_reinit(repo);

	/* Apply captured filters to the stable repo state */
	for_each_string_list_item(item, &captured_filters) {
		struct promisor_remote *r = repo_promisor_remote_find(repo, item->string);
		if (r) {
			free(r->advertised_filter);
			r->advertised_filter = item->util;
			item->util = NULL;
		}
	}

	string_list_clear(&captured_filters, 1);

	/* Mark the remotes as accepted in the repository state */
	for (size_t i = 0; i < accepted->nr; i++) {
		struct promisor_remote *r = repo_promisor_remote_find(repo, accepted->v[i]);
		if (r)
			r->accepted = 1;
	}
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
	struct string_list accepted_remotes = STRING_LIST_INIT_DUP;
	struct string_list_item *item;

	string_list_split(&accepted_remotes, remotes, ";", -1);

	for_each_string_list_item(item, &accepted_remotes) {
		char *decoded_remote = url_percent_decode(item->string);
		struct promisor_remote *p = repo_promisor_remote_find(r, decoded_remote);

		if (p)
			p->accepted = 1;
		else
			warning(_("accepted promisor remote '%s' not found"),
				decoded_remote);

		free(decoded_remote);
	}

	string_list_clear(&accepted_remotes, 0);
}

char *promisor_remote_construct_filter(struct repository *repo)
{
	struct string_list advertised_filters = STRING_LIST_INIT_NODUP;
	struct promisor_remote *r;
	char *result;

	promisor_remote_init(repo);

	for (r = repo->promisor_remote_config->promisors; r; r = r->next) {
		if (r->accepted && r->advertised_filter)
			string_list_append(&advertised_filters, r->advertised_filter);
	}

	result = list_objects_filter_combine(&advertised_filters);

	string_list_clear(&advertised_filters, 0);

	return result;
}
