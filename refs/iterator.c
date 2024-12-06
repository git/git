/*
 * Generic reference iterator infrastructure. See refs-internal.h for
 * documentation about the design and use of reference iterators.
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "refs.h"
#include "refs/refs-internal.h"
#include "iterator.h"

int ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	return ref_iterator->vtable->advance(ref_iterator);
}

int ref_iterator_peel(struct ref_iterator *ref_iterator,
		      struct object_id *peeled)
{
	return ref_iterator->vtable->peel(ref_iterator, peeled);
}

int ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	return ref_iterator->vtable->abort(ref_iterator);
}

void base_ref_iterator_init(struct ref_iterator *iter,
			    struct ref_iterator_vtable *vtable)
{
	iter->vtable = vtable;
	iter->refname = NULL;
	iter->referent = NULL;
	iter->oid = NULL;
	iter->flags = 0;
}

void base_ref_iterator_free(struct ref_iterator *iter)
{
	/* Help make use-after-free bugs fail quickly: */
	iter->vtable = NULL;
	free(iter);
}

struct empty_ref_iterator {
	struct ref_iterator base;
};

static int empty_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	return ref_iterator_abort(ref_iterator);
}

static int empty_ref_iterator_peel(struct ref_iterator *ref_iterator UNUSED,
				   struct object_id *peeled UNUSED)
{
	BUG("peel called for empty iterator");
}

static int empty_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	base_ref_iterator_free(ref_iterator);
	return ITER_DONE;
}

static struct ref_iterator_vtable empty_ref_iterator_vtable = {
	.advance = empty_ref_iterator_advance,
	.peel = empty_ref_iterator_peel,
	.abort = empty_ref_iterator_abort,
};

struct ref_iterator *empty_ref_iterator_begin(void)
{
	struct empty_ref_iterator *iter = xcalloc(1, sizeof(*iter));
	struct ref_iterator *ref_iterator = &iter->base;

	base_ref_iterator_init(ref_iterator, &empty_ref_iterator_vtable);
	return ref_iterator;
}

int is_empty_ref_iterator(struct ref_iterator *ref_iterator)
{
	return ref_iterator->vtable == &empty_ref_iterator_vtable;
}

struct merge_ref_iterator {
	struct ref_iterator base;

	struct ref_iterator *iter0, *iter1;

	ref_iterator_select_fn *select;
	void *cb_data;

	/*
	 * A pointer to iter0 or iter1 (whichever is supplying the
	 * current value), or NULL if advance has not yet been called.
	 */
	struct ref_iterator **current;
};

enum iterator_selection ref_iterator_select(struct ref_iterator *iter_worktree,
					    struct ref_iterator *iter_common,
					    void *cb_data UNUSED)
{
	if (iter_worktree && !iter_common) {
		/*
		 * Return the worktree ref if there are no more common refs.
		 */
		return ITER_SELECT_0;
	} else if (iter_common) {
		/*
		 * In case we have pending worktree and common refs we need to
		 * yield them based on their lexicographical order. Worktree
		 * refs that have the same name as common refs shadow the
		 * latter.
		 */
		if (iter_worktree) {
			int cmp = strcmp(iter_worktree->refname,
					 iter_common->refname);
			if (cmp < 0)
				return ITER_SELECT_0;
			else if (!cmp)
				return ITER_SELECT_0_SKIP_1;
		}

		 /*
		  * We now know that the lexicographically-next ref is a common
		  * ref. When the common ref is a shared one we return it.
		  */
		if (parse_worktree_ref(iter_common->refname, NULL, NULL,
				       NULL) == REF_WORKTREE_SHARED)
			return ITER_SELECT_1;

		/*
		 * Otherwise, if the common ref is a per-worktree ref we skip
		 * it because it would belong to the main worktree, not ours.
		 */
		return ITER_SKIP_1;
	} else {
		return ITER_DONE;
	}
}

static int merge_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct merge_ref_iterator *iter =
		(struct merge_ref_iterator *)ref_iterator;
	int ok;

	if (!iter->current) {
		/* Initialize: advance both iterators to their first entries */
		if ((ok = ref_iterator_advance(iter->iter0)) != ITER_OK) {
			iter->iter0 = NULL;
			if (ok == ITER_ERROR)
				goto error;
		}
		if ((ok = ref_iterator_advance(iter->iter1)) != ITER_OK) {
			iter->iter1 = NULL;
			if (ok == ITER_ERROR)
				goto error;
		}
	} else {
		/*
		 * Advance the current iterator past the just-used
		 * entry:
		 */
		if ((ok = ref_iterator_advance(*iter->current)) != ITER_OK) {
			*iter->current = NULL;
			if (ok == ITER_ERROR)
				goto error;
		}
	}

	/* Loop until we find an entry that we can yield. */
	while (1) {
		struct ref_iterator **secondary;
		enum iterator_selection selection =
			iter->select(iter->iter0, iter->iter1, iter->cb_data);

		if (selection == ITER_SELECT_DONE) {
			return ref_iterator_abort(ref_iterator);
		} else if (selection == ITER_SELECT_ERROR) {
			ref_iterator_abort(ref_iterator);
			return ITER_ERROR;
		}

		if ((selection & ITER_CURRENT_SELECTION_MASK) == 0) {
			iter->current = &iter->iter0;
			secondary = &iter->iter1;
		} else {
			iter->current = &iter->iter1;
			secondary = &iter->iter0;
		}

		if (selection & ITER_SKIP_SECONDARY) {
			if ((ok = ref_iterator_advance(*secondary)) != ITER_OK) {
				*secondary = NULL;
				if (ok == ITER_ERROR)
					goto error;
			}
		}

		if (selection & ITER_YIELD_CURRENT) {
			iter->base.referent = (*iter->current)->referent;
			iter->base.refname = (*iter->current)->refname;
			iter->base.oid = (*iter->current)->oid;
			iter->base.flags = (*iter->current)->flags;
			return ITER_OK;
		}
	}

error:
	ref_iterator_abort(ref_iterator);
	return ITER_ERROR;
}

static int merge_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct merge_ref_iterator *iter =
		(struct merge_ref_iterator *)ref_iterator;

	if (!iter->current) {
		BUG("peel called before advance for merge iterator");
	}
	return ref_iterator_peel(*iter->current, peeled);
}

static int merge_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct merge_ref_iterator *iter =
		(struct merge_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	if (iter->iter0) {
		if (ref_iterator_abort(iter->iter0) != ITER_DONE)
			ok = ITER_ERROR;
	}
	if (iter->iter1) {
		if (ref_iterator_abort(iter->iter1) != ITER_DONE)
			ok = ITER_ERROR;
	}
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable merge_ref_iterator_vtable = {
	.advance = merge_ref_iterator_advance,
	.peel = merge_ref_iterator_peel,
	.abort = merge_ref_iterator_abort,
};

struct ref_iterator *merge_ref_iterator_begin(
		struct ref_iterator *iter0, struct ref_iterator *iter1,
		ref_iterator_select_fn *select, void *cb_data)
{
	struct merge_ref_iterator *iter = xcalloc(1, sizeof(*iter));
	struct ref_iterator *ref_iterator = &iter->base;

	/*
	 * We can't do the same kind of is_empty_ref_iterator()-style
	 * optimization here as overlay_ref_iterator_begin() does,
	 * because we don't know the semantics of the select function.
	 * It might, for example, implement "intersect" by passing
	 * references through only if they exist in both iterators.
	 */

	base_ref_iterator_init(ref_iterator, &merge_ref_iterator_vtable);
	iter->iter0 = iter0;
	iter->iter1 = iter1;
	iter->select = select;
	iter->cb_data = cb_data;
	iter->current = NULL;
	return ref_iterator;
}

/*
 * A ref_iterator_select_fn that overlays the items from front on top
 * of those from back (like loose refs over packed refs). See
 * overlay_ref_iterator_begin().
 */
static enum iterator_selection overlay_iterator_select(
		struct ref_iterator *front, struct ref_iterator *back,
		void *cb_data UNUSED)
{
	int cmp;

	if (!back)
		return front ? ITER_SELECT_0 : ITER_SELECT_DONE;
	else if (!front)
		return ITER_SELECT_1;

	cmp = strcmp(front->refname, back->refname);

	if (cmp < 0)
		return ITER_SELECT_0;
	else if (cmp > 0)
		return ITER_SELECT_1;
	else
		return ITER_SELECT_0_SKIP_1;
}

struct ref_iterator *overlay_ref_iterator_begin(
		struct ref_iterator *front, struct ref_iterator *back)
{
	/*
	 * Optimization: if one of the iterators is empty, return the
	 * other one rather than incurring the overhead of wrapping
	 * them.
	 */
	if (is_empty_ref_iterator(front)) {
		ref_iterator_abort(front);
		return back;
	} else if (is_empty_ref_iterator(back)) {
		ref_iterator_abort(back);
		return front;
	}

	return merge_ref_iterator_begin(front, back, overlay_iterator_select, NULL);
}

struct prefix_ref_iterator {
	struct ref_iterator base;

	struct ref_iterator *iter0;
	char *prefix;
	int trim;
};

/* Return -1, 0, 1 if refname is before, inside, or after the prefix. */
static int compare_prefix(const char *refname, const char *prefix)
{
	while (*prefix) {
		if (*refname != *prefix)
			return ((unsigned char)*refname < (unsigned char)*prefix) ? -1 : +1;

		refname++;
		prefix++;
	}

	return 0;
}

static int prefix_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct prefix_ref_iterator *iter =
		(struct prefix_ref_iterator *)ref_iterator;
	int ok;

	while ((ok = ref_iterator_advance(iter->iter0)) == ITER_OK) {
		int cmp = compare_prefix(iter->iter0->refname, iter->prefix);

		if (cmp < 0)
			continue;

		if (cmp > 0) {
			/*
			 * As the source iterator is ordered, we
			 * can stop the iteration as soon as we see a
			 * refname that comes after the prefix:
			 */
			ok = ref_iterator_abort(iter->iter0);
			break;
		}

		if (iter->trim) {
			/*
			 * It is nonsense to trim off characters that
			 * you haven't already checked for via a
			 * prefix check, whether via this
			 * `prefix_ref_iterator` or upstream in
			 * `iter0`). So if there wouldn't be at least
			 * one character left in the refname after
			 * trimming, report it as a bug:
			 */
			if (strlen(iter->iter0->refname) <= iter->trim)
				BUG("attempt to trim too many characters");
			iter->base.refname = iter->iter0->refname + iter->trim;
		} else {
			iter->base.refname = iter->iter0->refname;
		}

		iter->base.oid = iter->iter0->oid;
		iter->base.flags = iter->iter0->flags;
		return ITER_OK;
	}

	iter->iter0 = NULL;
	if (ref_iterator_abort(ref_iterator) != ITER_DONE)
		return ITER_ERROR;
	return ok;
}

static int prefix_ref_iterator_peel(struct ref_iterator *ref_iterator,
				    struct object_id *peeled)
{
	struct prefix_ref_iterator *iter =
		(struct prefix_ref_iterator *)ref_iterator;

	return ref_iterator_peel(iter->iter0, peeled);
}

static int prefix_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct prefix_ref_iterator *iter =
		(struct prefix_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	if (iter->iter0)
		ok = ref_iterator_abort(iter->iter0);
	free(iter->prefix);
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable prefix_ref_iterator_vtable = {
	.advance = prefix_ref_iterator_advance,
	.peel = prefix_ref_iterator_peel,
	.abort = prefix_ref_iterator_abort,
};

struct ref_iterator *prefix_ref_iterator_begin(struct ref_iterator *iter0,
					       const char *prefix,
					       int trim)
{
	struct prefix_ref_iterator *iter;
	struct ref_iterator *ref_iterator;

	if (!*prefix && !trim)
		return iter0; /* optimization: no need to wrap iterator */

	CALLOC_ARRAY(iter, 1);
	ref_iterator = &iter->base;

	base_ref_iterator_init(ref_iterator, &prefix_ref_iterator_vtable);

	iter->iter0 = iter0;
	iter->prefix = xstrdup(prefix);
	iter->trim = trim;

	return ref_iterator;
}

struct ref_iterator *current_ref_iter = NULL;

int do_for_each_ref_iterator(struct ref_iterator *iter,
			     each_ref_fn fn, void *cb_data)
{
	int retval = 0, ok;
	struct ref_iterator *old_ref_iter = current_ref_iter;

	current_ref_iter = iter;
	while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
		retval = fn(iter->refname, iter->referent, iter->oid, iter->flags, cb_data);
		if (retval) {
			/*
			 * If ref_iterator_abort() returns ITER_ERROR,
			 * we ignore that error in deference to the
			 * callback function's return value.
			 */
			ref_iterator_abort(iter);
			goto out;
		}
	}

out:
	current_ref_iter = old_ref_iter;
	if (ok == ITER_ERROR)
		return -1;
	return retval;
}
