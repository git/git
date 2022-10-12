#include "cache.h"
#include "bundle-uri.h"
#include "bundle.h"
#include "object-store.h"
#include "refs.h"
#include "run-command.h"
#include "hashmap.h"
#include "pkt-line.h"
#include "config.h"

static int compare_bundles(const void *hashmap_cmp_fn_data,
			   const struct hashmap_entry *he1,
			   const struct hashmap_entry *he2,
			   const void *id)
{
	const struct remote_bundle_info *e1 =
		container_of(he1, const struct remote_bundle_info, ent);
	const struct remote_bundle_info *e2 =
		container_of(he2, const struct remote_bundle_info, ent);

	return strcmp(e1->id, id ? (const char *)id : e2->id);
}

void init_bundle_list(struct bundle_list *list)
{
	memset(list, 0, sizeof(*list));

	/* Implied defaults. */
	list->mode = BUNDLE_MODE_ALL;
	list->version = 1;

	hashmap_init(&list->bundles, compare_bundles, NULL, 0);
}

static int clear_remote_bundle_info(struct remote_bundle_info *bundle,
				    void *data)
{
	FREE_AND_NULL(bundle->id);
	FREE_AND_NULL(bundle->uri);
	return 0;
}

void clear_bundle_list(struct bundle_list *list)
{
	if (!list)
		return;

	for_all_bundles_in_list(list, clear_remote_bundle_info, NULL);
	hashmap_clear_and_free(&list->bundles, struct remote_bundle_info, ent);
}

int for_all_bundles_in_list(struct bundle_list *list,
			    bundle_iterator iter,
			    void *data)
{
	struct remote_bundle_info *info;
	struct hashmap_iter i;

	hashmap_for_each_entry(&list->bundles, &i, info, ent) {
		int result = iter(info, data);

		if (result)
			return result;
	}

	return 0;
}

/**
 * Given a key-value pair, update the state of the given bundle list.
 * Returns 0 if the key-value pair is understood. Returns -1 if the key
 * is not understood or the value is malformed.
 */
MAYBE_UNUSED
static int bundle_list_update(const char *key, const char *value,
			      struct bundle_list *list)
{
	struct strbuf id = STRBUF_INIT;
	struct remote_bundle_info lookup = REMOTE_BUNDLE_INFO_INIT;
	struct remote_bundle_info *bundle;
	const char *subsection, *subkey;
	size_t subsection_len;

	if (parse_config_key(key, "bundle", &subsection, &subsection_len, &subkey))
		return -1;

	if (!subsection_len) {
		if (!strcmp(subkey, "version")) {
			int version;
			if (!git_parse_int(value, &version))
				return -1;
			if (version != 1)
				return -1;

			list->version = version;
			return 0;
		}

		if (!strcmp(subkey, "mode")) {
			if (!strcmp(value, "all"))
				list->mode = BUNDLE_MODE_ALL;
			else if (!strcmp(value, "any"))
				list->mode = BUNDLE_MODE_ANY;
			else
				return -1;
			return 0;
		}

		/* Ignore other unknown global keys. */
		return 0;
	}

	strbuf_add(&id, subsection, subsection_len);

	/*
	 * Check for an existing bundle with this <id>, or create one
	 * if necessary.
	 */
	lookup.id = id.buf;
	hashmap_entry_init(&lookup.ent, strhash(lookup.id));
	if (!(bundle = hashmap_get_entry(&list->bundles, &lookup, ent, NULL))) {
		CALLOC_ARRAY(bundle, 1);
		bundle->id = strbuf_detach(&id, NULL);
		hashmap_entry_init(&bundle->ent, strhash(bundle->id));
		hashmap_add(&list->bundles, &bundle->ent);
	}
	strbuf_release(&id);

	if (!strcmp(subkey, "uri")) {
		if (bundle->uri)
			return -1;
		bundle->uri = xstrdup(value);
		return 0;
	}

	/*
	 * At this point, we ignore any information that we don't
	 * understand, assuming it to be hints for a heuristic the client
	 * does not currently understand.
	 */
	return 0;
}

static char *find_temp_filename(void)
{
	int fd;
	struct strbuf name = STRBUF_INIT;
	/*
	 * Find a temporary filename that is available. This is briefly
	 * racy, but unlikely to collide.
	 */
	fd = odb_mkstemp(&name, "bundles/tmp_uri_XXXXXX");
	if (fd < 0) {
		warning(_("failed to create temporary file"));
		return NULL;
	}

	close(fd);
	unlink(name.buf);
	return strbuf_detach(&name, NULL);
}

static int download_https_uri_to_file(const char *file, const char *uri)
{
	int result = 0;
	struct child_process cp = CHILD_PROCESS_INIT;
	FILE *child_in = NULL, *child_out = NULL;
	struct strbuf line = STRBUF_INIT;
	int found_get = 0;

	strvec_pushl(&cp.args, "git-remote-https", uri, NULL);
	cp.in = -1;
	cp.out = -1;

	if (start_command(&cp))
		return 1;

	child_in = fdopen(cp.in, "w");
	if (!child_in) {
		result = 1;
		goto cleanup;
	}

	child_out = fdopen(cp.out, "r");
	if (!child_out) {
		result = 1;
		goto cleanup;
	}

	fprintf(child_in, "capabilities\n");
	fflush(child_in);

	while (!strbuf_getline(&line, child_out)) {
		if (!line.len)
			break;
		if (!strcmp(line.buf, "get"))
			found_get = 1;
	}
	strbuf_release(&line);

	if (!found_get) {
		result = error(_("insufficient capabilities"));
		goto cleanup;
	}

	fprintf(child_in, "get %s %s\n\n", uri, file);

cleanup:
	if (child_in)
		fclose(child_in);
	if (finish_command(&cp))
		return 1;
	if (child_out)
		fclose(child_out);
	return result;
}

static int copy_uri_to_file(const char *filename, const char *uri)
{
	const char *out;

	if (starts_with(uri, "https:") ||
	    starts_with(uri, "http:"))
		return download_https_uri_to_file(filename, uri);

	if (skip_prefix(uri, "file://", &out))
		uri = out;

	/* Copy as a file */
	return copy_file(filename, uri, 0);
}

static int unbundle_from_file(struct repository *r, const char *file)
{
	int result = 0;
	int bundle_fd;
	struct bundle_header header = BUNDLE_HEADER_INIT;
	struct string_list_item *refname;
	struct strbuf bundle_ref = STRBUF_INIT;
	size_t bundle_prefix_len;

	if ((bundle_fd = read_bundle_header(file, &header)) < 0)
		return 1;

	if ((result = unbundle(r, &header, bundle_fd, NULL)))
		return 1;

	/*
	 * Convert all refs/heads/ from the bundle into refs/bundles/
	 * in the local repository.
	 */
	strbuf_addstr(&bundle_ref, "refs/bundles/");
	bundle_prefix_len = bundle_ref.len;

	for_each_string_list_item(refname, &header.references) {
		struct object_id *oid = refname->util;
		struct object_id old_oid;
		const char *branch_name;
		int has_old;

		if (!skip_prefix(refname->string, "refs/heads/", &branch_name))
			continue;

		strbuf_setlen(&bundle_ref, bundle_prefix_len);
		strbuf_addstr(&bundle_ref, branch_name);

		has_old = !read_ref(bundle_ref.buf, &old_oid);
		update_ref("fetched bundle", bundle_ref.buf, oid,
			   has_old ? &old_oid : NULL,
			   REF_SKIP_OID_VERIFICATION,
			   UPDATE_REFS_MSG_ON_ERR);
	}

	bundle_header_release(&header);
	return result;
}

int fetch_bundle_uri(struct repository *r, const char *uri)
{
	int result = 0;
	char *filename;

	if (!(filename = find_temp_filename())) {
		result = -1;
		goto cleanup;
	}

	if ((result = copy_uri_to_file(filename, uri))) {
		warning(_("failed to download bundle from URI '%s'"), uri);
		goto cleanup;
	}

	if ((result = !is_bundle(filename, 0))) {
		warning(_("file at URI '%s' is not a bundle"), uri);
		goto cleanup;
	}

	if ((result = unbundle_from_file(r, filename))) {
		warning(_("failed to unbundle bundle from URI '%s'"), uri);
		goto cleanup;
	}

cleanup:
	if (filename)
		unlink(filename);
	free(filename);
	return result;
}
