#include "cache.h"
#include "bundle-uri.h"
#include "bundle.h"
#include "object-store.h"
#include "refs.h"
#include "run-command.h"

static int find_temp_filename(struct strbuf *name)
{
	int fd;
	/*
	 * Find a temporary filename that is available. This is briefly
	 * racy, but unlikely to collide.
	 */
	fd = odb_mkstemp(name, "bundles/tmp_uri_XXXXXX");
	if (fd < 0) {
		warning(_("failed to create temporary file"));
		return -1;
	}

	close(fd);
	unlink(name->buf);
	return 0;
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
	struct strbuf filename = STRBUF_INIT;

	if ((result = find_temp_filename(&filename)))
		goto cleanup;

	if ((result = copy_uri_to_file(filename.buf, uri))) {
		warning(_("failed to download bundle from URI '%s'"), uri);
		goto cleanup;
	}

	if ((result = !is_bundle(filename.buf, 0))) {
		warning(_("file at URI '%s' is not a bundle"), uri);
		goto cleanup;
	}

	if ((result = unbundle_from_file(r, filename.buf))) {
		warning(_("failed to unbundle bundle from URI '%s'"), uri);
		goto cleanup;
	}

cleanup:
	unlink(filename.buf);
	strbuf_release(&filename);
	return result;
}
