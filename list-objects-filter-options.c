#include "cache.h"
#include "commit.h"
#include "config.h"
#include "revision.h"
#include "argv-array.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "promisor-remote.h"
#include "trace.h"
#include "url.h"

static int parse_combine_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf);

/*
 * Parse value of the argument to the "filter" keyword.
 * On the command line this looks like:
 *       --filter=<arg>
 * and in the pack protocol as:
 *       "filter" SP <arg>
 *
 * The filter keyword will be used by many commands.
 * See Documentation/rev-list-options.txt for allowed values for <arg>.
 *
 * Capture the given arg as the "filter_spec".  This can be forwarded to
 * subordinate commands when necessary (although it's better to pass it through
 * expand_list_objects_filter_spec() first).  We also "intern" the arg for the
 * convenience of the current command.
 */
static int gently_parse_list_objects_filter(
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

	} else if (skip_prefix(arg, "combine:", &v0)) {
		return parse_combine_filter(filter_options, v0, errbuf);

	}
	/*
	 * Please update _git_fetch() in git-completion.bash when you
	 * add new filters
	 */

	strbuf_addf(errbuf, _("invalid filter-spec '%s'"), arg);

	memset(filter_options, 0, sizeof(*filter_options));
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
	if (result) {
		list_objects_filter_release(filter_options);
		memset(filter_options, 0, sizeof(*filter_options));
	}
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
	struct strbuf buf = STRBUF_INIT;
	strbuf_addstr_urlencode(&buf, raw, allow_unencoded);
	trace_printf("Add to combine filter-spec: %s\n", buf.buf);
	string_list_append(&filter->filter_spec, strbuf_detach(&buf, NULL));
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
		memset(filter_options, 0, sizeof(*filter_options));
		filter_options->sub = sub_array;
		filter_options->sub_alloc = initial_sub_alloc;
	}
	filter_options->sub_nr = 1;
	filter_options->choice = LOFC_COMBINE;
	string_list_append(&filter_options->filter_spec, xstrdup("combine:"));
	filter_spec_append_urlencode(
		filter_options,
		list_objects_filter_spec(&filter_options->sub[0]));
	/*
	 * We don't need the filter_spec strings for subfilter specs, only the
	 * top level.
	 */
	string_list_clear(&filter_options->sub[0].filter_spec, /*free_util=*/0);
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
	int parse_error;

	if (!filter_options->choice) {
		string_list_append(&filter_options->filter_spec, xstrdup(arg));

		parse_error = gently_parse_list_objects_filter(
			filter_options, arg, &errbuf);
	} else {
		/*
		 * Make filter_options an LOFC_COMBINE spec so we can trivially
		 * add subspecs to it.
		 */
		transform_to_combine_type(filter_options);

		string_list_append(&filter_options->filter_spec, xstrdup("+"));
		filter_spec_append_urlencode(filter_options, arg);
		ALLOC_GROW_BY(filter_options->sub, filter_options->sub_nr, 1,
			      filter_options->sub_alloc);

		parse_error = gently_parse_list_objects_filter(
			&filter_options->sub[filter_options->sub_nr - 1], arg,
			&errbuf);
	}
	if (parse_error)
		die("%s", errbuf.buf);
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
	if (!filter->filter_spec.nr)
		BUG("no filter_spec available for this filter");
	if (filter->filter_spec.nr != 1) {
		struct strbuf concatted = STRBUF_INIT;
		strbuf_add_separated_string_list(
			&concatted, "", &filter->filter_spec);
		string_list_clear(&filter->filter_spec, /*free_util=*/0);
		string_list_append(
			&filter->filter_spec, strbuf_detach(&concatted, NULL));
	}

	return filter->filter_spec.items[0].string;
}

const char *expand_list_objects_filter_spec(
	struct list_objects_filter_options *filter)
{
	if (filter->choice == LOFC_BLOB_LIMIT) {
		struct strbuf expanded_spec = STRBUF_INIT;
		strbuf_addf(&expanded_spec, "blob:limit=%lu",
			    filter->blob_limit_value);
		string_list_clear(&filter->filter_spec, /*free_util=*/0);
		string_list_append(
			&filter->filter_spec,
			strbuf_detach(&expanded_spec, NULL));
	}

	return list_objects_filter_spec(filter);
}

void list_objects_filter_release(
	struct list_objects_filter_options *filter_options)
{
	size_t sub;

	if (!filter_options)
		return;
	string_list_clear(&filter_options->filter_spec, /*free_util=*/0);
	free(filter_options->sparse_oid_name);
	for (sub = 0; sub < filter_options->sub_nr; sub++)
		list_objects_filter_release(&filter_options->sub[sub]);
	free(filter_options->sub);
	memset(filter_options, 0, sizeof(*filter_options));
}

void partial_clone_register(
	const char *remote,
	struct list_objects_filter_options *filter_options)
{
	char *cfg_name;
	char *filter_name;

	/* Check if it is already registered */
	if (!promisor_remote_find(remote)) {
		git_config_set("core.repositoryformatversion", "1");

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
	promisor_remote_reinit();
}

void partial_clone_get_default_filter_spec(
	struct list_objects_filter_options *filter_options,
	const char *remote)
{
	struct promisor_remote *promisor = promisor_remote_find(remote);
	struct strbuf errbuf = STRBUF_INIT;

	/*
	 * Parse default value, but silently ignore it if it is invalid.
	 */
	if (!promisor)
		return;

	string_list_append(&filter_options->filter_spec,
			   promisor->partial_clone_filter);
	gently_parse_list_objects_filter(filter_options,
					 promisor->partial_clone_filter,
					 &errbuf);
	strbuf_release(&errbuf);
}
