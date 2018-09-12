#include "cache.h"
#include "packfile.h"
#include "pkt-line.h"
#include "strbuf.h"
#include "transport.h"
#include "fetch-object.h"

static void fetch_refs(const char *remote_name, struct ref *ref)
{
	struct remote *remote;
	struct transport *transport;
	int original_fetch_if_missing = fetch_if_missing;

	fetch_if_missing = 0;
	remote = remote_get(remote_name);
	if (!remote->url[0])
		die(_("Remote with no URL"));
	transport = transport_get(remote, remote->url[0]);

	transport_set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
	transport_set_option(transport, TRANS_OPT_NO_DEPENDENTS, "1");
	transport_fetch_refs(transport, ref);
	fetch_if_missing = original_fetch_if_missing;
}

void fetch_objects(const char *remote_name, const struct object_id *oids,
		   int oid_nr)
{
	struct ref *ref = NULL;
	int i;

	for (i = 0; i < oid_nr; i++) {
		struct ref *new_ref = alloc_ref(oid_to_hex(&oids[i]));
		oidcpy(&new_ref->old_oid, &oids[i]);
		new_ref->exact_oid = 1;
		new_ref->next = ref;
		ref = new_ref;
	}
	fetch_refs(remote_name, ref);
}
