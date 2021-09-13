#include "cache.h"
#include "repository.h"
#include "refs.h"
#include "remote.h"
#include "strvec.h"
#include "ls-refs.h"
#include "pkt-line.h"
#include "config.h"

static int config_read;
static int advertise_unborn;
static int allow_unborn;

static void ensure_config_read(void)
{
	const char *str = NULL;

	if (config_read)
		return;

	if (repo_config_get_string_tmp(the_repository, "lsrefs.unborn", &str)) {
		/*
		 * If there is no such config, advertise and allow it by
		 * default.
		 */
		advertise_unborn = 1;
		allow_unborn = 1;
	} else {
		if (!strcmp(str, "advertise")) {
			advertise_unborn = 1;
			allow_unborn = 1;
		} else if (!strcmp(str, "allow")) {
			allow_unborn = 1;
		} else if (!strcmp(str, "ignore")) {
			/* do nothing */
		} else {
			die(_("invalid value '%s' for lsrefs.unborn"), str);
		}
	}
	config_read = 1;
}

/*
 * Check if one of the prefixes is a prefix of the ref.
 * If no prefixes were provided, all refs match.
 */
static int ref_match(const struct strvec *prefixes, const char *refname)
{
	int i;

	if (!prefixes->nr)
		return 1; /* no restriction */

	for (i = 0; i < prefixes->nr; i++) {
		const char *prefix = prefixes->v[i];

		if (starts_with(refname, prefix))
			return 1;
	}

	return 0;
}

struct ls_refs_data {
	unsigned peel;
	unsigned symrefs;
	struct strvec prefixes;
	struct strbuf buf;
	unsigned unborn : 1;
};

static int send_ref(const char *refname, const struct object_id *oid,
		    int flag, void *cb_data)
{
	struct ls_refs_data *data = cb_data;
	const char *refname_nons = strip_namespace(refname);

	strbuf_reset(&data->buf);

	if (ref_is_hidden(refname_nons, refname))
		return 0;

	if (!ref_match(&data->prefixes, refname_nons))
		return 0;

	if (oid)
		strbuf_addf(&data->buf, "%s %s", oid_to_hex(oid), refname_nons);
	else
		strbuf_addf(&data->buf, "unborn %s", refname_nons);
	if (data->symrefs && flag & REF_ISSYMREF) {
		struct object_id unused;
		const char *symref_target = resolve_ref_unsafe(refname, 0,
							       &unused,
							       &flag);

		if (!symref_target)
			die("'%s' is a symref but it is not?", refname);

		strbuf_addf(&data->buf, " symref-target:%s",
			    strip_namespace(symref_target));
	}

	if (data->peel && oid) {
		struct object_id peeled;
		if (!peel_iterated_oid(oid, &peeled))
			strbuf_addf(&data->buf, " peeled:%s", oid_to_hex(&peeled));
	}

	strbuf_addch(&data->buf, '\n');
	packet_fwrite(stdout, data->buf.buf, data->buf.len);

	return 0;
}

static void send_possibly_unborn_head(struct ls_refs_data *data)
{
	struct strbuf namespaced = STRBUF_INIT;
	struct object_id oid;
	int flag;
	int oid_is_null;

	strbuf_addf(&namespaced, "%sHEAD", get_git_namespace());
	if (!resolve_ref_unsafe(namespaced.buf, 0, &oid, &flag))
		return; /* bad ref */
	oid_is_null = is_null_oid(&oid);
	if (!oid_is_null ||
	    (data->unborn && data->symrefs && (flag & REF_ISSYMREF)))
		send_ref(namespaced.buf, oid_is_null ? NULL : &oid, flag, data);
	strbuf_release(&namespaced);
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

int ls_refs(struct repository *r, struct packet_reader *request)
{
	struct ls_refs_data data;

	memset(&data, 0, sizeof(data));
	strvec_init(&data.prefixes);
	strbuf_init(&data.buf, 0);

	ensure_config_read();
	git_config(ls_refs_config, NULL);

	while (packet_reader_read(request) == PACKET_READ_NORMAL) {
		const char *arg = request->line;
		const char *out;

		if (!strcmp("peel", arg))
			data.peel = 1;
		else if (!strcmp("symrefs", arg))
			data.symrefs = 1;
		else if (skip_prefix(arg, "ref-prefix ", &out))
			strvec_push(&data.prefixes, out);
		else if (!strcmp("unborn", arg))
			data.unborn = allow_unborn;
	}

	if (request->status != PACKET_READ_FLUSH)
		die(_("expected flush after ls-refs arguments"));

	send_possibly_unborn_head(&data);
	if (!data.prefixes.nr)
		strvec_push(&data.prefixes, "");
	for_each_fullref_in_prefixes(get_git_namespace(), data.prefixes.v,
				     send_ref, &data, 0);
	packet_fflush(stdout);
	strvec_clear(&data.prefixes);
	strbuf_release(&data.buf);
	return 0;
}

int ls_refs_advertise(struct repository *r, struct strbuf *value)
{
	if (value) {
		ensure_config_read();
		if (advertise_unborn)
			strbuf_addstr(value, "unborn");
	}

	return 1;
}
