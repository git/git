#include "cache.h"
#include "remote.h"
#include "strbuf.h"
#include "walker.h"
#include "http.h"
#include "exec_cmd.h"

static struct ref *get_refs(struct walker *walker, const char *url)
{
	struct strbuf buffer = STRBUF_INIT;
	char *data, *start, *mid;
	char *ref_name;
	char *refs_url;
	int i = 0;
	int http_ret;

	struct ref *refs = NULL;
	struct ref *ref = NULL;
	struct ref *last_ref = NULL;

	refs_url = xmalloc(strlen(url) + 11);
	sprintf(refs_url, "%s/info/refs", url);

	http_ret = http_get_strbuf(refs_url, &buffer, HTTP_NO_CACHE);
	switch (http_ret) {
	case HTTP_OK:
		break;
	case HTTP_MISSING_TARGET:
		die("%s not found: did you run git update-server-info on the"
		    " server?", refs_url);
	default:
		http_error(refs_url, http_ret);
		die("HTTP request failed");
	}

	data = buffer.buf;
	start = NULL;
	mid = data;
	while (i < buffer.len) {
		if (!start) {
			start = &data[i];
		}
		if (data[i] == '\t')
			mid = &data[i];
		if (data[i] == '\n') {
			data[i] = 0;
			ref_name = mid + 1;
			ref = xmalloc(sizeof(struct ref) +
				      strlen(ref_name) + 1);
			memset(ref, 0, sizeof(struct ref));
			strcpy(ref->name, ref_name);
			get_sha1_hex(start, ref->old_sha1);
			if (!refs)
				refs = ref;
			if (last_ref)
				last_ref->next = ref;
			last_ref = ref;
			start = NULL;
		}
		i++;
	}

	strbuf_release(&buffer);

	ref = alloc_ref("HEAD");
	if (!walker->fetch_ref(walker, ref) &&
	    !resolve_remote_symref(ref, refs)) {
		ref->next = refs;
		refs = ref;
	} else {
		free(ref);
	}

	strbuf_release(&buffer);
	free(refs_url);
	return refs;
}

int main(int argc, const char **argv)
{
	struct remote *remote;
	struct strbuf buf = STRBUF_INIT;
	const char *url;
	struct walker *walker = NULL;
	int nongit;

	git_extract_argv0_path(argv[0]);
	setup_git_directory_gently(&nongit);
	if (argc < 2) {
		fprintf(stderr, "Remote needed\n");
		return 1;
	}

	remote = remote_get(argv[1]);

	if (argc > 2) {
		url = argv[2];
	} else {
		url = remote->url[0];
	}

	do {
		if (strbuf_getline(&buf, stdin, '\n') == EOF)
			break;
		if (!prefixcmp(buf.buf, "fetch ")) {
			char *obj = buf.buf + strlen("fetch ");
			if (nongit)
				die("Fetch attempted without a local repo");
			if (!walker)
				walker = get_http_walker(url, remote);
			walker->get_all = 1;
			walker->get_tree = 1;
			walker->get_history = 1;
			walker->get_verbosely = 0;
			walker->get_recover = 0;
			if (walker_fetch(walker, 1, &obj, NULL, NULL))
				die("Fetch failed.");
			printf("\n");
			fflush(stdout);
		} else if (!strcmp(buf.buf, "list")) {
			struct ref *refs;
			struct ref *posn;
			if (!walker)
				walker = get_http_walker(url, remote);
			refs = get_refs(walker, url);
			for (posn = refs; posn; posn = posn->next) {
				if (posn->symref)
					printf("@%s %s\n", posn->symref, posn->name);
				else
					printf("%s %s\n", sha1_to_hex(posn->old_sha1), posn->name);
			}
			printf("\n");
			fflush(stdout);
		} else if (!strcmp(buf.buf, "capabilities")) {
			printf("fetch\n");
			printf("\n");
			fflush(stdout);
		} else {
			return 1;
		}
		strbuf_reset(&buf);
	} while (1);
	return 0;
}
