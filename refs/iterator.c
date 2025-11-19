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

int ref_iterator_seek(struct ref_iterator *ref_iterator, const char *refname,
		      unsigned int flags)
{
	return ref_iterator->vtable->seek(ref_iterator, refname, flags);
}

void ref_iterator_free(struct ref_iterator *ref_iterator)
{
	if (ref_iterator) {
		ref_iterator->vtable->release(ref_iterator);
		/* Help make use-after-free bugs fail quickly: */
		ref_iterator->vtable = NULL;
		free(ref_iterator);
	}
}

void base_ref_iterator_init(struct ref_iterator *iter,
			    struct ref_iterator_vtable *vtable)
{
	iter->vtable = vtable;
	memset(&iter->ref, 0, sizeof(iter->ref));
}

struct empty_ref_iterator {
	struct ref_iterator base;
};

static int empty_ref_iterator_advance(struct ref_iterator *ref_iterator UNUSED)
{
	return ITER_DONE;
}

static int empty_ref_iterator_seek(struct ref_iterator *ref_iterator UNUSED,
				   const char *refname UNUSED,
				   unsigned int flags UNUSED)
{
	return 0;
}

static void empty_ref_iterator_release(struct ref_iterator *ref_iterator UNUSED)
{
}

static struct ref_iterator_vtable empty_ref_iterator_vtable = {
	.advance = empty_ref_iterator_advance,
	.seek = empty_ref_iterator_seek,
	.release = empty_ref_iterator_release,
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

	struct ref_iterator *iter0, *iter0_owned;
	struct ref_iterator *iter1, *iter1_owned;

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
			int cmp = strcmp(iter_worktree->ref.name,
					 iter_common->ref.name);
			if (cmp < 0)
				return ITER_SELECT_0;
			else if (!cmp)
				return ITER_SELECT_0_SKIP_1;
		}

		 /*
		  * We now know that the lexicographically-next ref is a common
		  * ref. When the common ref is a shared one we return it.
		  */
		if (parse_worktree_ref(iter_common->ref.name, NULL, NULL,
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
			return ITER_DONE;
		} else if (selection == ITER_SELECT_ERROR) {
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
			iter->base.ref = (*iter->current)->ref;
			return ITER_OK;
		}
	}

error:
	return ITER_ERROR;
}

static int merge_ref_iterator_seek(struct ref_iterator *ref_iterator,
				   const char *refname, unsigned int flags)
{
	struct merge_ref_iterator *iter =
		(struct merge_ref_iterator *)ref_iterator;
	int ret;

	iter->current = NULL;
	iter->iter0 = iter->iter0_owned;
	iter->iter1 = iter->iter1_owned;

	ret = ref_iterator_seek(iter->iter0, refname, flags);
	if (ret < 0)
		return ret;

	ret = ref_iterator_seek(iter->iter1, refname, flags);
	if (ret < 0)
		return ret;

	return 0;
}

static void merge_ref_iterator_release(struct ref_iterator *ref_iterator)
{
	struct merge_ref_iterator *iter =
		(struct merge_ref_iterator *)ref_iterator;
	ref_iterator_free(iter->iter0_owned);
	ref_iterator_free(iter->iter1_owned);
}

static struct ref_iterator_vtable merge_ref_iterator_vtable = {
	.advance = merge_ref_iterator_advance,
	.seek = merge_ref_iterator_seek,
	.release = merge_ref_iterator_release,
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
	iter->iter0 = iter->iter0_owned = iter0;
	iter->iter1 = iter->iter1_owned = iter1;
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

	cmp = strcmp(front->ref.name, back->ref.name);

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
		ref_iterator_free(front);
		return back;
	} else if (is_empty_ref_iterator(back)) {
		ref_iterator_free(back);
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
		int cmp = compare_prefix(iter->iter0->ref.name, iter->prefix);
		if (cmp < 0)
			continue;
		/*
		 * As the source iterator is ordered, we
		 * can stop the iteration as soon as we see a
		 * refname that comes after the prefix:
		 */
		if (cmp > 0)
			return ITER_DONE;

		iter->base.ref = iter->iter0->ref;

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
			if (strlen(iter->base.ref.name) <= iter->trim)
				BUG("attempt to trim too many characters");
			iter->base.ref.name += iter->trim;
		}

		return ITER_OK;
	}

	return ok;
}

static int prefix_ref_iterator_seek(struct ref_iterator *ref_iterator,
				    const char *refname, unsigned int flags)
{
	struct prefix_ref_iterator *iter =
		(struct prefix_ref_iterator *)ref_iterator;

	if (flags & REF_ITERATOR_SEEK_SET_PREFIX) {
		free(iter->prefix);
		iter->prefix = xstrdup_or_null(refname);
	}
	return ref_iterator_seek(iter->iter0, refname, flags);
}

static void prefix_ref_iterator_release(struct ref_iterator *ref_iterator)
{
	struct prefix_ref_iterator *iter =
		(struct prefix_ref_iterator *)ref_iterator;
	ref_iterator_free(iter->iter0);
	free(iter->prefix);
}

static struct ref_iterator_vtable prefix_ref_iterator_vtable = {
	.advance = prefix_ref_iterator_advance,
	.seek = prefix_ref_iterator_seek,
	.release = prefix_ref_iterator_release,
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

int do_for_each_ref_iterator(struct ref_iterator *iter,
			     each_ref_fn fn, void *cb_data)
{
	int retval = 0, ok;

	while ((ok = ref_iterator_advance(iter)) == ITER_OK) {
		retval = fn(&iter->ref, cb_data);
		if (retval)
			goto out;
	}

out:
	if (ok == ITER_ERROR)
		retval = -1;
	ref_iterator_free(iter);
	return retval;
}
