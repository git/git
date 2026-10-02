#include "git-compat-util.h"
#include "diff.h"
#include "diff-provider-internal.h"
#include "replace-object.h"
#include "repository.h"

/*
 * The terminal provider: the builtin computation.  A request that
 * carries a fill callback is answered by loading the pair's content
 * and running xdiff, so a walk that reaches it never falls through
 * to the consumer.  On a consult-only walk it passes, and the walk's
 * fall-through outcome tells the consumer to compute.
 */
static enum diff_provider_disposition
builtin_consult(struct diff_provider *provider UNUSED,
		const struct diff_provider_request *req,
		diff_provider_fill_fn fill, void *fill_data,
		xdl_emit_hunk_consume_func_t hunk_cb, void *cb_data)
{
	xdemitconf_t xecfg = { .hunk_func = hunk_cb };
	xdemitcb_t ecb = { .priv = cb_data };
	mmfile_t old_file, new_file;

	if (!fill)
		return DIFF_PROVIDER_DISP_PASS;
	if (fill(fill_data, &old_file, &new_file) < 0)
		return DIFF_PROVIDER_DISP_ERROR;
	if (xdi_diff(&old_file, &new_file, req->xpp, &xecfg, &ecb) < 0)
		return DIFF_PROVIDER_DISP_ERROR;
	return DIFF_PROVIDER_DISP_ANSWERED;
}

static struct diff_provider *builtin_provider_new(void)
{
	struct diff_provider *p = xcalloc(1, sizeof(*p));

	p->consult = builtin_consult;
	p->computes = 1;
	return p;
}

/*
 * The repository's chain, assembled on first walk.  The composition
 * is fixed, and the order is the authority resolution: the process
 * outranks the store, and the builtin computation is the terminal
 * provider, so the chain always ends in an implementor that can
 * answer.  Nothing is decided per repository here; each provider
 * gates itself per request.
 */
static struct diff_provider *provider_chain(struct repository *r)
{
	struct diff_provider **tail = &r->diff_providers;

	if (*tail)
		return *tail;
	*tail = diff_process_provider_new();
	tail = &(*tail)->next;
	*tail = diff_hunks_store_provider_new();
	tail = &(*tail)->next;
	*tail = builtin_provider_new();
	return r->diff_providers;
}

void diff_providers_clear(struct repository *r)
{
	struct diff_provider *p = r->diff_providers;

	while (p) {
		struct diff_provider *next = p->next;

		if (p->release)
			p->release(p);
		free(p);
		p = next;
	}
	r->diff_providers = NULL;
}

/*
 * The walk shared by diff_provider_consult() and
 * diff_provider_emit_hunks(): consult the chain in order and map its
 * dispositions onto the outcome set.  The first answer ends the
 * walk.  A stop-no-record disposition (diff-provider-internal.h)
 * is a refusal, not a pass: the provider does not answer, but rules
 * the pair out of identity service and out of recording, so from
 * then on the walk consults only the computing provider, and a walk
 * that ends unanswered carries the no-record verdict.  With a fill
 * callback the terminal provider computes instead of passing, so an
 * emit walk returns only answered or error.
 */
static enum diff_provider_outcome
walk_providers(const struct diff_provider_request *req,
	       diff_provider_fill_fn fill, void *fill_data,
	       xdl_emit_hunk_consume_func_t hunk_cb, void *cb_data)
{
	struct diff_provider *p;
	int no_record = 0;

	if (req->diffopt && req->diffopt->repo != req->repo)
		BUG("diff provider request walks one repository's chain "
		    "with another repository's diff options");

	/*
	 * An object replacement redirects a blob's content
	 * (OBJECT_INFO_LOOKUP_REPLACE) while leaving the id that names it
	 * unchanged, so an answer keyed on the raw id would be the
	 * pre-replacement diff.  A replacement is therefore a parameter
	 * outside the recording key: no provider may serve a replaced pair
	 * from its identity, and a result computed for it must not be
	 * recorded under the raw id.  Mark the walk no-record so the
	 * identity providers step aside and the builtin computes from the
	 * replaced content.  The check is a no-op when the repository has
	 * no replace refs.
	 */
	if ((req->old_oid &&
	     lookup_replace_object(req->repo, req->old_oid) != req->old_oid) ||
	    (req->new_oid &&
	     lookup_replace_object(req->repo, req->new_oid) != req->new_oid))
		no_record = 1;

	for (p = provider_chain(req->repo); p; p = p->next) {
		enum diff_provider_disposition disp;

		if (no_record && !p->computes)
			continue;
		disp = p->consult(p, req, fill, fill_data,
				  hunk_cb, cb_data);
		if (disp == DIFF_PROVIDER_DISP_ERROR && !p->computes)
			BUG("only the computing provider may return the "
			    "error disposition");
		if (p->computes && !fill && disp != DIFF_PROVIDER_DISP_PASS)
			BUG("the computing provider must pass on a "
			    "fill-less walk");
		switch (disp) {
		case DIFF_PROVIDER_DISP_ANSWERED:
			return DIFF_PROVIDER_ANSWERED;
		case DIFF_PROVIDER_DISP_PASS:
			continue;
		case DIFF_PROVIDER_DISP_STOP_NO_RECORD:
			no_record = 1;
			continue;
		case DIFF_PROVIDER_DISP_ERROR:
			return DIFF_PROVIDER_ERROR;
		}
	}
	return no_record ? DIFF_PROVIDER_UNANSWERED_NO_RECORD :
		DIFF_PROVIDER_UNANSWERED;
}

enum diff_provider_outcome
diff_provider_consult(const struct diff_provider_request *req,
		      xdl_emit_hunk_consume_func_t hunk_cb, void *cb_data)
{
	return walk_providers(req, NULL, NULL, hunk_cb, cb_data);
}

enum diff_provider_hunks_error
diff_provider_check_hunk(struct diff_provider_hunks_check *c,
			  long old_start, long old_count,
			  long new_start, long new_count)
{
	if (old_start < 0 || old_count < 0 ||
	    new_start < 0 || new_count < 0 ||
	    old_start > INT32_MAX || old_count > INT32_MAX ||
	    new_start > INT32_MAX || new_count > INT32_MAX ||
	    (int64_t)old_start + old_count > INT32_MAX ||
	    (int64_t)new_start + new_count > INT32_MAX)
		return DIFF_PROVIDER_HUNKS_RANGE;
	if (old_start < c->prev_old_end || new_start < c->prev_new_end)
		return DIFF_PROVIDER_HUNKS_OVERLAP;
	if (old_start - c->prev_old_end != new_start - c->prev_new_end)
		return DIFF_PROVIDER_HUNKS_MISALIGNED;
	/*
	 * With each field bounded to int32 above, the int64 sums cannot
	 * overflow even where long is 32-bit, and the range rule has
	 * already capped them at INT32_MAX.
	 */
	c->prev_old_end = (int64_t)old_start + old_count;
	c->prev_new_end = (int64_t)new_start + new_count;
	return DIFF_PROVIDER_HUNKS_OK;
}

enum diff_provider_outcome
diff_provider_emit_hunks(const struct diff_provider_request *req,
			 diff_provider_fill_fn fill, void *fill_data,
			 xdl_emit_hunk_consume_func_t hunk_cb,
			 void *cb_data)
{
	return walk_providers(req, fill, fill_data, hunk_cb, cb_data);
}
