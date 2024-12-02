#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "list-objects-filter-options.h"
#include "promisor-remote.h"
#include "trace.h"
#include "url.h"
#include "parse-options.h"

static int parse_combine_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf);

const char *list_object_filter_config_name(enum list_objects_filter_choice c)
{
	switch (c) {
	case LOFC_DISABLED:
		/* we have no name for "no filter at all" */
		break;
	case LOFC_BLOB_NONE:
		return "blob:none";
	case LOFC_BLOB_LIMIT:
		return "blob:limit";
	case LOFC_TREE_DEPTH:
		return "tree";
	case LOFC_SPARSE_OID:
		return "sparse:oid";
	case LOFC_OBJECT_TYPE:
		return "object:type";
	case LOFC_COMBINE:
		return "combine";
	case LOFC__COUNT:
		/* not a real filter type; just the count of all filters */
		break;
	}
	BUG("list_object_filter_config_name: invalid argument '%d'", c);
}

int gently_parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf)
{
	const char *v0;

	if (!arg)
		return 0;

	if (filter_options->choice)
		BUG("filter_options already populated");

	if (!strcmp(arg, "blob:none")) {
		filter_options->choice = LOFC_BLOB_NONE;
		return 0;

	} else if (skip_prefix(arg, "blob:limit=", &v0)) {
		if (git_parse_ulong(v0, &filter_options->blob_limit_value)) {
			filter_options->choice = LOFC_BLOB_LIMIT;
			return 0;
		}

	} else if (skip_prefix(arg, "tree:", &v0)) {
		if (!git_parse_ulong(v0, &filter_options->tree_exclude_depth)) {
			strbuf_addstr(errbuf, _("expected 'tree:<depth>'"));
			return 1;
		}
		filter_options->choice = LOFC_TREE_DEPTH;
		return 0;

	} else if (skip_prefix(arg, "sparse:oid=", &v0)) {
		filter_options->sparse_oid_name = xstrdup(v0);
		filter_options->choice = LOFC_SPARSE_OID;
		return 0;

	} else if (skip_prefix(arg, "sparse:path=", &v0)) {
		if (errbuf) {
			strbuf_addstr(
				errbuf,
				_("sparse:path filters support has been dropped"));
		}
		return 1;

	} else if (skip_prefix(arg, "object:type=", &v0)) {
		int type = type_from_string_gently(v0, strlen(v0), 1);
		if (type < 0) {
			strbuf_addf(errbuf, _("'%s' for 'object:type=<type>' is "
					      "not a valid object type"), v0);
			return 1;
		}

		filter_options->object_type = type;
		filter_options->choice = LOFC_OBJECT_TYPE;

		return 0;

	} else if (skip_prefix(arg, "combine:", &v0)) {
		return parse_combine_filter(filter_options, v0, errbuf);

	}
	/*
	 * Please update _git_fetch() in git-completion.bash when you
	 * add new filters
	 */

	strbuf_addf(errbuf, _("invalid filter-spec '%s'"), arg);

	list_objects_filter_init(filter_options);
	return 1;
}

static const char *RESERVED_NON_WS = "~`!@#$^&*()[]{}\\;'\",<>?";

static int has_reserved_character(
	struct strbuf *sub_spec, struct strbuf *errbuf)
{
	const char *c = sub_spec->buf;
	while (*c) {
		if (*c <= ' ' || strchr(RESERVED_NON_WS, *c)) {
			strbuf_addf(
				errbuf,
				_("must escape char in sub-filter-spec: '%c'"),
				*c);
			return 1;
		}
		c++;
	}

	return 0;
}

static int parse_combine_subfilter(
	struct list_objects_filter_options *filter_options,
	struct strbuf *subspec,
	struct strbuf *errbuf)
{
	size_t new_index = filter_options->sub_nr;
	char *decoded;
	int result;

	ALLOC_GROW_BY(filter_options->sub, filter_options->sub_nr, 1,
		      filter_options->sub_alloc);
	list_objects_filter_init(&filter_options->sub[new_index]);

	decoded = url_percent_decode(subspec->buf);

	result = has_reserved_character(subspec, errbuf) ||
		gently_parse_list_objects_filter(
			&filter_options->sub[new_index], decoded, errbuf);

	free(decoded);
	return result;
}

static int parse_combine_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf)
{
	struct strbuf **subspecs = strbuf_split_str(arg, '+', 0);
	size_t sub;
	int result = 0;

	if (!subspecs[0]) {
		strbuf_addstr(errbuf, _("expected something after combine:"));
		result = 1;
		goto cleanup;
	}

	for (sub = 0; subspecs[sub] && !result; sub++) {
		if (subspecs[sub + 1]) {
			/*
			 * This is not the last subspec. Remove trailing "+" so
			 * we can parse it.
			 */
			size_t last = subspecs[sub]->len - 1;
			assert(subspecs[sub]->buf[last] == '+');
			strbuf_remove(subspecs[sub], last, 1);
		}
		result = parse_combine_subfilter(
			filter_options, subspecs[sub], errbuf);
	}

	filter_options->choice = LOFC_COMBINE;

cleanup:
	strbuf_list_free(subspecs);
	if (result)
		list_objects_filter_release(filter_options);
	return result;
}

static int allow_unencoded(char ch)
{
	if (ch <= ' ' || ch == '%' || ch == '+')
		return 0;
	return !strchr(RESERVED_NON_WS, ch);
}

static void filter_spec_append_urlencode(
	struct list_objects_filter_options *filter, const char *raw)
{
	size_t orig_len = filter->filter_spec.len;
	strbuf_addstr_urlencode(&filter->filter_spec, raw, allow_unencoded);
	trace_printf("Add to combine filter-spec: %s\n",
		     filter->filter_spec.buf + orig_len);
}

/*
 * Changes filter_options into an equivalent LOFC_COMBINE filter options
 * instance. Does not do anything if filter_options is already LOFC_COMBINE.
 */
static void transform_to_combine_type(
	struct list_objects_filter_options *filter_options)
{
	assert(filter_options->choice);
	if (filter_options->choice == LOFC_COMBINE)
		return;
	{
		const int initial_sub_alloc = 2;
		struct list_objects_filter_options *sub_array =
			xcalloc(initial_sub_alloc, sizeof(*sub_array));
		sub_array[0] = *filter_options;
		list_objects_filter_init(filter_options);
		filter_options->sub = sub_array;
		filter_options->sub_alloc = initial_sub_alloc;
	}
	filter_options->sub_nr = 1;
	filter_options->choice = LOFC_COMBINE;
	strbuf_addstr(&filter_options->filter_spec, "combine:");
	filter_spec_append_urlencode(
		filter_options,
		list_objects_filter_spec(&filter_options->sub[0]));
	/*
	 * We don't need the filter_spec strings for subfilter specs, only the
	 * top level.
	 */
	strbuf_release(&filter_options->sub[0].filter_spec);
}

void list_objects_filter_die_if_populated(
	struct list_objects_filter_options *filter_options)
{
	if (filter_options->choice)
		die(_("multiple filter-specs cannot be combined"));
}

void parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg)
{
	struct strbuf errbuf = STRBUF_INIT;

	if (!filter_options->filter_spec.buf)
		BUG("filter_options not properly initialized");

	if (!filter_options->choice) {
		if (gently_parse_list_objects_filter(filter_options, arg, &errbuf))
			die("%s", errbuf.buf);
		strbuf_addstr(&filter_options->filter_spec, arg);
	} else {
		struct list_objects_filter_options *sub;

		/*
		 * Make filter_options an LOFC_COMBINE spec so we can trivially
		 * add subspecs to it.
		 */
		transform_to_combine_type(filter_options);

		ALLOC_GROW_BY(filter_options->sub, filter_options->sub_nr, 1,
			      filter_options->sub_alloc);
		sub = &filter_options->sub[filter_options->sub_nr - 1];

		list_objects_filter_init(sub);
		if (gently_parse_list_objects_filter(sub, arg, &errbuf))
			die("%s", errbuf.buf);

		strbuf_addch(&filter_options->filter_spec, '+');
		filter_spec_append_urlencode(filter_options, arg);
	}
}

int opt_parse_list_objects_filter(const struct option *opt,
				  const char *arg, int unset)
{
	struct list_objects_filter_options *filter_options = opt->value;

	if (unset || !arg)
		list_objects_filter_set_no_filter(filter_options);
	else
		parse_list_objects_filter(filter_options, arg);
	return 0;
}

const char *list_objects_filter_spec(struct list_objects_filter_options *filter)
{
	if (!filter->filter_spec.len)
		BUG("no filter_spec available for this filter");
	return filter->filter_spec.buf;
}

const char *expand_list_objects_filter_spec(
	struct list_objects_filter_options *filter)
{
	if (filter->choice == LOFC_BLOB_LIMIT) {
		strbuf_release(&filter->filter_spec);
		strbuf_addf(&filter->filter_spec, "blob:limit=%lu",
			    filter->blob_limit_value);
	}

	return list_objects_filter_spec(filter);
}

void list_objects_filter_release(
	struct list_objects_filter_options *filter_options)
{
	size_t sub;

	if (!filter_options)
		return;
	strbuf_release(&filter_options->filter_spec);
	free(filter_options->sparse_oid_name);
	for (sub = 0; sub < filter_options->sub_nr; sub++)
		list_objects_filter_release(&filter_options->sub[sub]);
	free(filter_options->sub);
	list_objects_filter_init(filter_options);
}

void partial_clone_register(
	const char *remote,
	struct list_objects_filter_options *filter_options)
{
	struct promisor_remote *promisor_remote;
	char *cfg_name;
	char *filter_name;

	/* Check if it is already registered */
	if ((promisor_remote = repo_promisor_remote_find(the_repository, remote))) {
		if (promisor_remote->partial_clone_filter)
			/*
			 * Remote is already registered and a filter is already
			 * set, so we don't need to do anything here.
			 */
			return;
	} else {
		if (upgrade_repository_format(1) < 0)
			die(_("unable to upgrade repository format to support partial clone"));

		/* Add promisor config for the remote */
		cfg_name = xstrfmt("remote.%s.promisor", remote);
		git_config_set(cfg_name, "true");
		free(cfg_name);
	}

	/*
	 * Record the initial filter-spec in the config as
	 * the default for subsequent fetches from this remote.
	 */
	filter_name = xstrfmt("remote.%s.partialclonefilter", remote);
	/* NEEDSWORK: 'expand' result leaking??? */
	git_config_set(filter_name,
		       expand_list_objects_filter_spec(filter_options));
	free(filter_name);

	/* Make sure the config info are reset */
	repo_promisor_remote_reinit(the_repository);
}

void partial_clone_get_default_filter_spec(
	struct list_objects_filter_options *filter_options,
	const char *remote)
{
	struct promisor_remote *promisor = repo_promisor_remote_find(the_repository,
								     remote);
	struct strbuf errbuf = STRBUF_INIT;

	/*
	 * Parse default value, but silently ignore it if it is invalid.
	 */
	if (!promisor || !promisor->partial_clone_filter)
		return;

	strbuf_addstr(&filter_options->filter_spec,
		      promisor->partial_clone_filter);
	gently_parse_list_objects_filter(filter_options,
					 promisor->partial_clone_filter,
					 &errbuf);
	strbuf_release(&errbuf);
}

void list_objects_filter_copy(
	struct list_objects_filter_options *dest,
	const struct list_objects_filter_options *src)
{
	int i;

	/* Copy everything. We will overwrite the pointers shortly. */
	memcpy(dest, src, sizeof(struct list_objects_filter_options));

	strbuf_init(&dest->filter_spec, 0);
	strbuf_addbuf(&dest->filter_spec, &src->filter_spec);
	dest->sparse_oid_name = xstrdup_or_null(src->sparse_oid_name);

	ALLOC_ARRAY(dest->sub, dest->sub_alloc);
	for (i = 0; i < src->sub_nr; i++)
		list_objects_filter_copy(&dest->sub[i], &src->sub[i]);
}

void list_objects_filter_init(struct list_objects_filter_options *filter_options)
{
	struct list_objects_filter_options blank = LIST_OBJECTS_FILTER_INIT;
	memcpy(filter_options, &blank, sizeof(*filter_options));
}
