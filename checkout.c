#include "cache.h"
#include "remote.h"
#include "checkout.h"

struct tracking_name_data {
	/* const */ char *src_ref;
	char *dst_ref;
	struct object_id *dst_oid;
	int unique;
};

static int check_tracking_name(struct remote *remote, void *cb_data)
{
	struct tracking_name_data *cb = cb_data;
	struct refspec query;
	memset(&query, 0, sizeof(struct refspec));
	query.src = cb->src_ref;
	if (remote_find_tracking(remote, &query) ||
	    get_oid(query.dst, cb->dst_oid)) {
		free(query.dst);
		return 0;
	}
	if (cb->dst_ref) {
		free(query.dst);
		cb->unique = 0;
		return 0;
	}
	cb->dst_ref = query.dst;
	return 0;
}

const char *unique_tracking_name(const char *name, struct object_id *oid)
{
	struct tracking_name_data cb_data = { NULL, NULL, NULL, 1 };
	cb_data.src_ref = xstrfmt("refs/heads/%s", name);
	cb_data.dst_oid = oid;
	for_each_remote(check_tracking_name, &cb_data);
	free(cb_data.src_ref);
	if (cb_data.unique)
		return cb_data.dst_ref;
	free(cb_data.dst_ref);
	return NULL;
}
