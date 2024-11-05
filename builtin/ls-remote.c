#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "gettext.h"
#include "hex.h"
#include "transport.h"
#include "pkt-line.h"
#include "ref-filter.h"
#include "remote.h"
#include "parse-options.h"
#include "wildmatch.h"

static const char * const ls_remote_usage[] = {
	N_("git ls-remote [--branches] [--tags] [--refs] [--upload-pack=<exec>]\n"
	   "              [-q | --quiet] [--exit-code] [--get-url] [--sort=<key>]\n"
	   "              [--symref] [<repository> [<patterns>...]]"),
	NULL
};

/*
 * Is there one among the list of patterns that match the tail part
 * of the path?
 */
static int tail_match(const struct strvec *pattern, const char *path)
{
	char *pathbuf;

	if (!pattern->nr)
		return 1; /* no restriction */

	pathbuf = xstrfmt("/%s", path);
	for (size_t i = 0; i < pattern->nr; i++) {
		if (!wildmatch(pattern->v[i], pathbuf, 0)) {
			free(pathbuf);
			return 1;
		}
	}
	free(pathbuf);
	return 0;
}

int cmd_ls_remote(int argc,
		  const char **argv,
		  const char *prefix,
		  struct repository *repo UNUSED)
{
	const char *dest = NULL;
	unsigned flags = 0;
	int get_url = 0;
	int quiet = 0;
	int status = 0;
	int show_symref_target = 0;
	const char *uploadpack = NULL;
	struct strvec pattern = STRVEC_INIT;
	struct transport_ls_refs_options transport_options =
		TRANSPORT_LS_REFS_OPTIONS_INIT;
	int i;
	struct string_list server_options = STRING_LIST_INIT_DUP;

	struct remote *remote;
	struct transport *transport;
	const struct ref *ref;
	struct ref_array ref_array;
	struct ref_sorting *sorting;
	struct string_list sorting_options = STRING_LIST_INIT_DUP;

	struct option options[] = {
		OPT__QUIET(&quiet, N_("do not print remote URL")),
		OPT_STRING(0, "upload-pack", &uploadpack, N_("exec"),
			   N_("path of git-upload-pack on the remote host")),
		{ OPTION_STRING, 0, "exec", &uploadpack, N_("exec"),
			   N_("path of git-upload-pack on the remote host"),
			   PARSE_OPT_HIDDEN },
		OPT_BIT('t', "tags", &flags, N_("limit to tags"), REF_TAGS),
		OPT_BIT('b', "branches", &flags, N_("limit to branches"), REF_BRANCHES),
		OPT_BIT_F('h', "heads", &flags,
			  N_("deprecated synonym for --branches"), REF_BRANCHES,
			  PARSE_OPT_HIDDEN),
		OPT_BIT(0, "refs", &flags, N_("do not show peeled tags"), REF_NORMAL),
		OPT_BOOL(0, "get-url", &get_url,
			 N_("take url.<base>.insteadOf into account")),
		OPT_REF_SORT(&sorting_options),
		OPT_SET_INT_F(0, "exit-code", &status,
			      N_("exit with exit code 2 if no matching refs are found"),
			      2, PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "symref", &show_symref_target,
			 N_("show underlying ref in addition to the object pointed by it")),
		OPT_STRING_LIST('o', "server-option", &server_options, N_("server-specific"), N_("option to transmit")),
		OPT_END()
	};

	memset(&ref_array, 0, sizeof(ref_array));

	argc = parse_options(argc, argv, prefix, options, ls_remote_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	dest = argv[0];

	/*
	 * TODO: This is buggy, but required for transport helpers. When a
	 * transport helper advertises a "refspec", then we'd add that to a
	 * list of refspecs via `refspec_append()`, which transitively depends
	 * on `the_hash_algo`. Thus, when the hash algorithm isn't properly set
	 * up, this would lead to a segfault.
	 *
	 * We really should fix this in the transport helper logic such that we
	 * lazily parse refspec capabilities _after_ we have learned about the
	 * remote's object format. Otherwise, we may end up misparsing refspecs
	 * depending on what object hash the remote uses.
	 */
	if (!the_repository->hash_algo)
		repo_set_hash_algo(the_repository, GIT_HASH_SHA1);

	packet_trace_identity("ls-remote");

	for (int i = 1; i < argc; i++)
		strvec_pushf(&pattern, "*/%s", argv[i]);

	if (flags & REF_TAGS)
		strvec_push(&transport_options.ref_prefixes, "refs/tags/");
	if (flags & REF_BRANCHES)
		strvec_push(&transport_options.ref_prefixes, "refs/heads/");

	remote = remote_get(dest);
	if (!remote) {
		if (dest)
			die("bad repository '%s'", dest);
		die("No remote configured to list refs from.");
	}

	if (get_url) {
		printf("%s\n", remote->url.v[0]);
		return 0;
	}

	transport = transport_get(remote, NULL);
	if (uploadpack)
		transport_set_option(transport, TRANS_OPT_UPLOADPACK, uploadpack);
	if (server_options.nr)
		transport->server_options = &server_options;

	ref = transport_get_remote_refs(transport, &transport_options);
	if (ref) {
		int hash_algo = hash_algo_by_ptr(transport_get_hash_algo(transport));
		repo_set_hash_algo(the_repository, hash_algo);
	}

	if (!dest && !quiet)
		fprintf(stderr, "From %s\n", remote->url.v[0]);
	for ( ; ref; ref = ref->next) {
		struct ref_array_item *item;
		if (!check_ref_type(ref, flags))
			continue;
		if (!tail_match(&pattern, ref->name))
			continue;
		item = ref_array_push(&ref_array, ref->name, &ref->old_oid);
		item->symref = xstrdup_or_null(ref->symref);
	}

	sorting = ref_sorting_options(&sorting_options);
	ref_array_sort(sorting, &ref_array);

	for (i = 0; i < ref_array.nr; i++) {
		const struct ref_array_item *ref = ref_array.items[i];
		if (show_symref_target && ref->symref)
			printf("ref: %s\t%s\n", ref->symref, ref->refname);
		printf("%s\t%s\n", oid_to_hex(&ref->objectname), ref->refname);
		status = 0; /* we found something */
	}

	string_list_clear(&server_options, 0);
	ref_sorting_release(sorting);
	ref_array_clear(&ref_array);
	if (transport_disconnect(transport))
		status = 1;
	transport_ls_refs_options_release(&transport_options);

	strvec_clear(&pattern);
	string_list_clear(&server_options, 0);
	return status;
}
