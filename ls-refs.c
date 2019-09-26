#include "cache.h"
#include "repository.h"
#include "refs.h"
#include "remote.h"
#include "argv-array.h"
#include "ls-refs.h"
#include "pkt-line.h"
#include "config.h"

/*
 * Check if one of the prefixes is a prefix of the ref.
 * If no prefixes were provided, all refs match.
 */
static int ref_match(const struct argv_array *prefixes, const char *refname)
{
	int i;

	if (!prefixes->argc)
		return 1; /* no restriction */

	for (i = 0; i < prefixes->argc; i++) {
		const char *prefix = prefixes->argv[i];

		if (starts_with(refname, prefix))
			return 1;
	}

	return 0;
}

struct ls_refs_data {
	unsigned peel;
	unsigned symrefs;
	struct argv_array prefixes;
};

static int send_ref(const char *refname, const struct object_id *oid,
		    int flag, void *cb_data)
{
	struct ls_refs_data *data = cb_data;
	const char *refname_nons = strip_namespace(refname);
	struct strbuf refline = STRBUF_INIT;

	if (ref_is_hidden(refname_nons, refname))
		return 0;

	if (!ref_match(&data->prefixes, refname_nons))
		return 0;

	strbuf_addf(&refline, "%s %s", oid_to_hex(oid), refname_nons);
	if (data->symrefs && flag & REF_ISSYMREF) {
		struct object_id unused;
		const char *symref_target = resolve_ref_unsafe(refname, 0,
							       &unused,
							       &flag);

		if (!symref_target)
			die("'%s' is a symref but it is not?", refname);

		strbuf_addf(&refline, " symref-target:%s",
			    strip_namespace(symref_target));
	}

	if (data->peel) {
		struct object_id peeled;
		if (!peel_ref(refname, &peeled))
			strbuf_addf(&refline, " peeled:%s", oid_to_hex(&peeled));
	}

	strbuf_addch(&refline, '\n');
	packet_write(1, refline.buf, refline.len);

	strbuf_release(&refline);
	return 0;
}

static int ls_refs_config(const char *var, const char *value, void *data)
{
	/*
	 * We only serve fetches over v2 for now, so respect only "uploadpack"
	 * config. This may need to eventually be expanded to "receive", but we
	 * don't yet know how that information will be passed to ls-refs.
	 */
	return parse_hide_refs_config(var, value, "uploadpack");
}

int ls_refs(struct repository *r, struct argv_array *keys,
	    struct packet_reader *request)
{
	struct ls_refs_data data;

	memset(&data, 0, sizeof(data));

	git_config(ls_refs_config, NULL);

	while (packet_reader_read(request) != PACKET_READ_FLUSH) {
		const char *arg = request->line;
		const char *out;

		if (!strcmp("peel", arg))
			data.peel = 1;
		else if (!strcmp("symrefs", arg))
			data.symrefs = 1;
		else if (skip_prefix(arg, "ref-prefix ", &out))
			argv_array_push(&data.prefixes, out);
	}

	head_ref_namespaced(send_ref, &data);
	for_each_namespaced_ref(send_ref, &data);
	packet_flush(1);
	argv_array_clear(&data.prefixes);
	return 0;
}
