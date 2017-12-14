#include "cache.h"
#include "packfile.h"
#include "pkt-line.h"
#include "strbuf.h"
#include "transport.h"
#include "fetch-object.h"

void fetch_object(const char *remote_name, const unsigned char *sha1)
{
	struct remote *remote;
	struct transport *transport;
	struct ref *ref;
	int original_fetch_if_missing = fetch_if_missing;

	fetch_if_missing = 0;
	remote = remote_get(remote_name);
	if (!remote->url[0])
		die(_("Remote with no URL"));
	transport = transport_get(remote, remote->url[0]);

	ref = alloc_ref(sha1_to_hex(sha1));
	hashcpy(ref->old_oid.hash, sha1);
	transport_set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
	transport_set_option(transport, TRANS_OPT_NO_DEPENDENTS, "1");
	transport_fetch_refs(transport, ref);
	fetch_if_missing = original_fetch_if_missing;
}
