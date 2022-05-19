#include "cache.h"
#include "cummit.h"
#include "cummit-graph.h"
#include "decorate.h"
#include "prio-queue.h"
#include "tree.h"
#include "ref-filter.h"
#include "revision.h"
#include "tag.h"
#include "cummit-reach.h"

/* Remember to update object flag allocation in object.h */
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static int compare_cummits_by_gen(const void *_a, const void *_b)
{
	const struct cummit *a = *(const struct cummit * const *)_a;
	const struct cummit *b = *(const struct cummit * const *)_b;

	timestamp_t generation_a = cummit_graph_generation(a);
	timestamp_t generation_b = cummit_graph_generation(b);

	if (generation_a < generation_b)
		return -1;
	if (generation_a > generation_b)
		return 1;
	if (a->date < b->date)
		return -1;
	if (a->date > b->date)
		return 1;
	return 0;
}

static int queue_has_nonstale(struct prio_queue *queue)
{
	int i;
	for (i = 0; i < queue->nr; i++) {
		struct cummit *cummit = queue->array[i].data;
		if (!(cummit->object.flags & STALE))
			return 1;
	}
	return 0;
}

/* all input cummits in one and twos[] must have been parsed! */
static struct cummit_list *paint_down_to_common(struct repository *r,
						struct cummit *one, int n,
						struct cummit **twos,
						timestamp_t min_generation)
{
	struct prio_queue queue = { compare_cummits_by_gen_then_cummit_date };
	struct cummit_list *result = NULL;
	int i;
	timestamp_t last_gen = GENERATION_NUMBER_INFINITY;

	if (!min_generation && !corrected_cummit_dates_enabled(r))
		queue.compare = compare_cummits_by_cummit_date;

	one->object.flags |= PARENT1;
	if (!n) {
		cummit_list_append(one, &result);
		return result;
	}
	prio_queue_put(&queue, one);

	for (i = 0; i < n; i++) {
		twos[i]->object.flags |= PARENT2;
		prio_queue_put(&queue, twos[i]);
	}

	while (queue_has_nonstale(&queue)) {
		struct cummit *cummit = prio_queue_get(&queue);
		struct cummit_list *parents;
		int flags;
		timestamp_t generation = cummit_graph_generation(cummit);

		if (min_generation && generation > last_gen)
			BUG("bad generation skip %"PRItime" > %"PRItime" at %s",
			    generation, last_gen,
			    oid_to_hex(&cummit->object.oid));
		last_gen = generation;

		if (generation < min_generation)
			break;

		flags = cummit->object.flags & (PARENT1 | PARENT2 | STALE);
		if (flags == (PARENT1 | PARENT2)) {
			if (!(cummit->object.flags & RESULT)) {
				cummit->object.flags |= RESULT;
				cummit_list_insert_by_date(cummit, &result);
			}
			/* Mark parents of a found merge stale */
			flags |= STALE;
		}
		parents = cummit->parents;
		while (parents) {
			struct cummit *p = parents->item;
			parents = parents->next;
			if ((p->object.flags & flags) == flags)
				continue;
			if (repo_parse_cummit(r, p))
				return NULL;
			p->object.flags |= flags;
			prio_queue_put(&queue, p);
		}
	}

	clear_prio_queue(&queue);
	return result;
}

static struct cummit_list *merge_bases_many(struct repository *r,
					    struct cummit *one, int n,
					    struct cummit **twos)
{
	struct cummit_list *list = NULL;
	struct cummit_list *result = NULL;
	int i;

	for (i = 0; i < n; i++) {
		if (one == twos[i])
			/*
			 * We do not mark this even with RESULT so we do not
			 * have to clean it up.
			 */
			return cummit_list_insert(one, &result);
	}

	if (repo_parse_cummit(r, one))
		return NULL;
	for (i = 0; i < n; i++) {
		if (repo_parse_cummit(r, twos[i]))
			return NULL;
	}

	list = paint_down_to_common(r, one, n, twos, 0);

	while (list) {
		struct cummit *cummit = pop_cummit(&list);
		if (!(cummit->object.flags & STALE))
			cummit_list_insert_by_date(cummit, &result);
	}
	return result;
}

struct cummit_list *get_octopus_merge_bases(struct cummit_list *in)
{
	struct cummit_list *i, *j, *k, *ret = NULL;

	if (!in)
		return ret;

	cummit_list_insert(in->item, &ret);

	for (i = in->next; i; i = i->next) {
		struct cummit_list *new_cummits = NULL, *end = NULL;

		for (j = ret; j; j = j->next) {
			struct cummit_list *bases;
			bases = get_merge_bases(i->item, j->item);
			if (!new_cummits)
				new_cummits = bases;
			else
				end->next = bases;
			for (k = bases; k; k = k->next)
				end = k;
		}
		ret = new_cummits;
	}
	return ret;
}

static int remove_redundant_no_gen(struct repository *r,
				   struct cummit **array, int cnt)
{
	struct cummit **work;
	unsigned char *redundant;
	int *filled_index;
	int i, j, filled;

	CALLOC_ARRAY(work, cnt);
	redundant = xcalloc(cnt, 1);
	ALLOC_ARRAY(filled_index, cnt - 1);

	for (i = 0; i < cnt; i++)
		repo_parse_cummit(r, array[i]);
	for (i = 0; i < cnt; i++) {
		struct cummit_list *common;
		timestamp_t min_generation = cummit_graph_generation(array[i]);

		if (redundant[i])
			continue;
		for (j = filled = 0; j < cnt; j++) {
			timestamp_t curr_generation;
			if (i == j || redundant[j])
				continue;
			filled_index[filled] = j;
			work[filled++] = array[j];

			curr_generation = cummit_graph_generation(array[j]);
			if (curr_generation < min_generation)
				min_generation = curr_generation;
		}
		common = paint_down_to_common(r, array[i], filled,
					      work, min_generation);
		if (array[i]->object.flags & PARENT2)
			redundant[i] = 1;
		for (j = 0; j < filled; j++)
			if (work[j]->object.flags & PARENT1)
				redundant[filled_index[j]] = 1;
		clear_cummit_marks(array[i], all_flags);
		clear_cummit_marks_many(filled, work, all_flags);
		free_cummit_list(common);
	}

	/* Now collect the result */
	COPY_ARRAY(work, array, cnt);
	for (i = filled = 0; i < cnt; i++)
		if (!redundant[i])
			array[filled++] = work[i];
	free(work);
	free(redundant);
	free(filled_index);
	return filled;
}

static int remove_redundant_with_gen(struct repository *r,
				     struct cummit **array, int cnt)
{
	int i, count_non_stale = 0, count_still_independent = cnt;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;
	struct cummit **walk_start, **sorted;
	size_t walk_start_nr = 0, walk_start_alloc = cnt;
	int min_gen_pos = 0;

	/*
	 * Sort the input by generation number, ascending. This allows
	 * us to increase the "min_generation" limit when we discover
	 * the cummit with lowest generation is STALE. The index
	 * min_gen_pos points to the current position within 'array'
	 * that is not yet known to be STALE.
	 */
	ALLOC_ARRAY(sorted, cnt);
	COPY_ARRAY(sorted, array, cnt);
	QSORT(sorted, cnt, compare_cummits_by_gen);
	min_generation = cummit_graph_generation(sorted[0]);

	ALLOC_ARRAY(walk_start, walk_start_alloc);

	/* Mark all parents of the input as STALE */
	for (i = 0; i < cnt; i++) {
		struct cummit_list *parents;

		repo_parse_cummit(r, array[i]);
		array[i]->object.flags |= RESULT;
		parents = array[i]->parents;

		while (parents) {
			repo_parse_cummit(r, parents->item);
			if (!(parents->item->object.flags & STALE)) {
				parents->item->object.flags |= STALE;
				ALLOC_GROW(walk_start, walk_start_nr + 1, walk_start_alloc);
				walk_start[walk_start_nr++] = parents->item;
			}
			parents = parents->next;
		}
	}

	QSORT(walk_start, walk_start_nr, compare_cummits_by_gen);

	/* remove STALE bit for now to allow walking through parents */
	for (i = 0; i < walk_start_nr; i++)
		walk_start[i]->object.flags &= ~STALE;

	/*
	 * Start walking from the highest generation. Hopefully, it will
	 * find all other items during the first-parent walk, and we can
	 * terminate early. Otherwise, we will do the same amount of work
	 * as before.
	 */
	for (i = walk_start_nr - 1; i >= 0 && count_still_independent > 1; i--) {
		/* push the STALE bits up to min generation */
		struct cummit_list *stack = NULL;

		cummit_list_insert(walk_start[i], &stack);
		walk_start[i]->object.flags |= STALE;

		while (stack) {
			struct cummit_list *parents;
			struct cummit *c = stack->item;

			repo_parse_cummit(r, c);

			if (c->object.flags & RESULT) {
				c->object.flags &= ~RESULT;
				if (--count_still_independent <= 1)
					break;
				if (oideq(&c->object.oid, &sorted[min_gen_pos]->object.oid)) {
					while (min_gen_pos < cnt - 1 &&
					       (sorted[min_gen_pos]->object.flags & STALE))
						min_gen_pos++;
					min_generation = cummit_graph_generation(sorted[min_gen_pos]);
				}
			}

			if (cummit_graph_generation(c) < min_generation) {
				pop_cummit(&stack);
				continue;
			}

			parents = c->parents;
			while (parents) {
				if (!(parents->item->object.flags & STALE)) {
					parents->item->object.flags |= STALE;
					cummit_list_insert(parents->item, &stack);
					break;
				}
				parents = parents->next;
			}

			/* pop if all parents have been visited already */
			if (!parents)
				pop_cummit(&stack);
		}
		free_cummit_list(stack);
	}
	free(sorted);

	/* clear result */
	for (i = 0; i < cnt; i++)
		array[i]->object.flags &= ~RESULT;

	/* rearrange array */
	for (i = count_non_stale = 0; i < cnt; i++) {
		if (!(array[i]->object.flags & STALE))
			array[count_non_stale++] = array[i];
	}

	/* clear marks */
	clear_cummit_marks_many(walk_start_nr, walk_start, STALE);
	free(walk_start);

	return count_non_stale;
}

static int remove_redundant(struct repository *r, struct cummit **array, int cnt)
{
	/*
	 * Some cummit in the array may be an ancestor of
	 * another cummit.  Move the independent cummits to the
	 * beginning of 'array' and return their number. Callers
	 * should not rely upon the contents of 'array' after
	 * that number.
	 */
	if (generation_numbers_enabled(r)) {
		int i;

		/*
		 * If we have a single cummit with finite generation
		 * number, then the _with_gen algorithm is preferred.
		 */
		for (i = 0; i < cnt; i++) {
			if (cummit_graph_generation(array[i]) < GENERATION_NUMBER_INFINITY)
				return remove_redundant_with_gen(r, array, cnt);
		}
	}

	return remove_redundant_no_gen(r, array, cnt);
}

static struct cummit_list *get_merge_bases_many_0(struct repository *r,
						  struct cummit *one,
						  int n,
						  struct cummit **twos,
						  int cleanup)
{
	struct cummit_list *list;
	struct cummit **rslt;
	struct cummit_list *result;
	int cnt, i;

	result = merge_bases_many(r, one, n, twos);
	for (i = 0; i < n; i++) {
		if (one == twos[i])
			return result;
	}
	if (!result || !result->next) {
		if (cleanup) {
			clear_cummit_marks(one, all_flags);
			clear_cummit_marks_many(n, twos, all_flags);
		}
		return result;
	}

	/* There are more than one */
	cnt = cummit_list_count(result);
	CALLOC_ARRAY(rslt, cnt);
	for (list = result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_cummit_list(result);

	clear_cummit_marks(one, all_flags);
	clear_cummit_marks_many(n, twos, all_flags);

	cnt = remove_redundant(r, rslt, cnt);
	result = NULL;
	for (i = 0; i < cnt; i++)
		cummit_list_insert_by_date(rslt[i], &result);
	free(rslt);
	return result;
}

struct cummit_list *repo_get_merge_bases_many(struct repository *r,
					      struct cummit *one,
					      int n,
					      struct cummit **twos)
{
	return get_merge_bases_many_0(r, one, n, twos, 1);
}

struct cummit_list *repo_get_merge_bases_many_dirty(struct repository *r,
						    struct cummit *one,
						    int n,
						    struct cummit **twos)
{
	return get_merge_bases_many_0(r, one, n, twos, 0);
}

struct cummit_list *repo_get_merge_bases(struct repository *r,
					 struct cummit *one,
					 struct cummit *two)
{
	return get_merge_bases_many_0(r, one, 1, &two, 1);
}

/*
 * Is "cummit" a descendant of one of the elements on the "with_cummit" list?
 */
int repo_is_descendant_of(struct repository *r,
			  struct cummit *cummit,
			  struct cummit_list *with_cummit)
{
	if (!with_cummit)
		return 1;

	if (generation_numbers_enabled(the_repository)) {
		struct cummit_list *from_list = NULL;
		int result;
		cummit_list_insert(cummit, &from_list);
		result = can_all_from_reach(from_list, with_cummit, 0);
		free_cummit_list(from_list);
		return result;
	} else {
		while (with_cummit) {
			struct cummit *other;

			other = with_cummit->item;
			with_cummit = with_cummit->next;
			if (repo_in_merge_bases_many(r, other, 1, &cummit))
				return 1;
		}
		return 0;
	}
}

/*
 * Is "cummit" an ancestor of one of the "references"?
 */
int repo_in_merge_bases_many(struct repository *r, struct cummit *cummit,
			     int nr_reference, struct cummit **reference)
{
	struct cummit_list *bases;
	int ret = 0, i;
	timestamp_t generation, max_generation = GENERATION_NUMBER_ZERO;

	if (repo_parse_cummit(r, cummit))
		return ret;
	for (i = 0; i < nr_reference; i++) {
		if (repo_parse_cummit(r, reference[i]))
			return ret;

		generation = cummit_graph_generation(reference[i]);
		if (generation > max_generation)
			max_generation = generation;
	}

	generation = cummit_graph_generation(cummit);
	if (generation > max_generation)
		return ret;

	bases = paint_down_to_common(r, cummit,
				     nr_reference, reference,
				     generation);
	if (cummit->object.flags & PARENT2)
		ret = 1;
	clear_cummit_marks(cummit, all_flags);
	clear_cummit_marks_many(nr_reference, reference, all_flags);
	free_cummit_list(bases);
	return ret;
}

/*
 * Is "cummit" an ancestor of (i.e. reachable from) the "reference"?
 */
int repo_in_merge_bases(struct repository *r,
			struct cummit *cummit,
			struct cummit *reference)
{
	int res;
	struct cummit_list *list = NULL;
	struct cummit_list **next = &list;

	next = cummit_list_append(cummit, next);
	res = repo_is_descendant_of(r, reference, list);
	free_cummit_list(list);

	return res;
}

struct cummit_list *reduce_heads(struct cummit_list *heads)
{
	struct cummit_list *p;
	struct cummit_list *result = NULL, **tail = &result;
	struct cummit **array;
	int num_head, i;

	if (!heads)
		return NULL;

	/* Uniquify */
	for (p = heads; p; p = p->next)
		p->item->object.flags &= ~STALE;
	for (p = heads, num_head = 0; p; p = p->next) {
		if (p->item->object.flags & STALE)
			continue;
		p->item->object.flags |= STALE;
		num_head++;
	}
	CALLOC_ARRAY(array, num_head);
	for (p = heads, i = 0; p; p = p->next) {
		if (p->item->object.flags & STALE) {
			array[i++] = p->item;
			p->item->object.flags &= ~STALE;
		}
	}
	num_head = remove_redundant(the_repository, array, num_head);
	for (i = 0; i < num_head; i++)
		tail = &cummit_list_insert(array[i], tail)->next;
	free(array);
	return result;
}

void reduce_heads_replace(struct cummit_list **heads)
{
	struct cummit_list *result = reduce_heads(*heads);
	free_cummit_list(*heads);
	*heads = result;
}

int ref_newer(const struct object_id *new_oid, const struct object_id *old_oid)
{
	struct object *o;
	struct cummit *old_cummit, *new_cummit;
	struct cummit_list *old_cummit_list = NULL;
	int ret;

	/*
	 * Both new_cummit and old_cummit must be cummit-ish and new_cummit is descendant of
	 * old_cummit.  Otherwise we require --force.
	 */
	o = deref_tag(the_repository, parse_object(the_repository, old_oid),
		      NULL, 0);
	if (!o || o->type != OBJ_CUMMIT)
		return 0;
	old_cummit = (struct cummit *) o;

	o = deref_tag(the_repository, parse_object(the_repository, new_oid),
		      NULL, 0);
	if (!o || o->type != OBJ_CUMMIT)
		return 0;
	new_cummit = (struct cummit *) o;

	if (parse_cummit(new_cummit) < 0)
		return 0;

	cummit_list_insert(old_cummit, &old_cummit_list);
	ret = repo_is_descendant_of(the_repository,
				    new_cummit, old_cummit_list);
	free_cummit_list(old_cummit_list);
	return ret;
}

/*
 * Mimicking the real stack, this stack lives on the heap, avoiding stack
 * overflows.
 *
 * At each recursion step, the stack items points to the cummits whose
 * ancestors are to be inspected.
 */
struct contains_stack {
	int nr, alloc;
	struct contains_stack_entry {
		struct cummit *cummit;
		struct cummit_list *parents;
	} *contains_stack;
};

static int in_cummit_list(const struct cummit_list *want, struct cummit *c)
{
	for (; want; want = want->next)
		if (oideq(&want->item->object.oid, &c->object.oid))
			return 1;
	return 0;
}

/*
 * Test whether the candidate is contained in the list.
 * Do not recurse to find out, though, but return -1 if inconclusive.
 */
static enum contains_result contains_test(struct cummit *candidate,
					  const struct cummit_list *want,
					  struct contains_cache *cache,
					  timestamp_t cutoff)
{
	enum contains_result *cached = contains_cache_at(cache, candidate);

	/* If we already have the answer cached, return that. */
	if (*cached)
		return *cached;

	/* or are we it? */
	if (in_cummit_list(want, candidate)) {
		*cached = CONTAINS_YES;
		return CONTAINS_YES;
	}

	/* Otherwise, we don't know; prepare to recurse */
	parse_cummit_or_die(candidate);

	if (cummit_graph_generation(candidate) < cutoff)
		return CONTAINS_NO;

	return CONTAINS_UNKNOWN;
}

static void push_to_contains_stack(struct cummit *candidate, struct contains_stack *contains_stack)
{
	ALLOC_GROW(contains_stack->contains_stack, contains_stack->nr + 1, contains_stack->alloc);
	contains_stack->contains_stack[contains_stack->nr].cummit = candidate;
	contains_stack->contains_stack[contains_stack->nr++].parents = candidate->parents;
}

static enum contains_result contains_tag_algo(struct cummit *candidate,
					      const struct cummit_list *want,
					      struct contains_cache *cache)
{
	struct contains_stack contains_stack = { 0, 0, NULL };
	enum contains_result result;
	timestamp_t cutoff = GENERATION_NUMBER_INFINITY;
	const struct cummit_list *p;

	for (p = want; p; p = p->next) {
		timestamp_t generation;
		struct cummit *c = p->item;
		load_cummit_graph_info(the_repository, c);
		generation = cummit_graph_generation(c);
		if (generation < cutoff)
			cutoff = generation;
	}

	result = contains_test(candidate, want, cache, cutoff);
	if (result != CONTAINS_UNKNOWN)
		return result;

	push_to_contains_stack(candidate, &contains_stack);
	while (contains_stack.nr) {
		struct contains_stack_entry *entry = &contains_stack.contains_stack[contains_stack.nr - 1];
		struct cummit *cummit = entry->cummit;
		struct cummit_list *parents = entry->parents;

		if (!parents) {
			*contains_cache_at(cache, cummit) = CONTAINS_NO;
			contains_stack.nr--;
		}
		/*
		 * If we just popped the stack, parents->item has been marked,
		 * therefore contains_test will return a meaningful yes/no.
		 */
		else switch (contains_test(parents->item, want, cache, cutoff)) {
		case CONTAINS_YES:
			*contains_cache_at(cache, cummit) = CONTAINS_YES;
			contains_stack.nr--;
			break;
		case CONTAINS_NO:
			entry->parents = parents->next;
			break;
		case CONTAINS_UNKNOWN:
			push_to_contains_stack(parents->item, &contains_stack);
			break;
		}
	}
	free(contains_stack.contains_stack);
	return contains_test(candidate, want, cache, cutoff);
}

int cummit_contains(struct ref_filter *filter, struct cummit *cummit,
		    struct cummit_list *list, struct contains_cache *cache)
{
	if (filter->with_cummit_tag_algo)
		return contains_tag_algo(cummit, list, cache) == CONTAINS_YES;
	return repo_is_descendant_of(the_repository, cummit, list);
}

int can_all_from_reach_with_flag(struct object_array *from,
				 unsigned int with_flag,
				 unsigned int assign_flag,
				 time_t min_cummit_date,
				 timestamp_t min_generation)
{
	struct cummit **list = NULL;
	int i;
	int nr_cummits;
	int result = 1;

	ALLOC_ARRAY(list, from->nr);
	nr_cummits = 0;
	for (i = 0; i < from->nr; i++) {
		struct object *from_one = from->objects[i].item;

		if (!from_one || from_one->flags & assign_flag)
			continue;

		from_one = deref_tag(the_repository, from_one,
				     "a from object", 0);
		if (!from_one || from_one->type != OBJ_CUMMIT) {
			/*
			 * no way to tell if this is reachable by
			 * looking at the ancestry chain alone, so
			 * leave a note to ourselves not to worry about
			 * this object anymore.
			 */
			from->objects[i].item->flags |= assign_flag;
			continue;
		}

		list[nr_cummits] = (struct cummit *)from_one;
		if (parse_cummit(list[nr_cummits]) ||
		    cummit_graph_generation(list[nr_cummits]) < min_generation) {
			result = 0;
			goto cleanup;
		}

		nr_cummits++;
	}

	QSORT(list, nr_cummits, compare_cummits_by_gen);

	for (i = 0; i < nr_cummits; i++) {
		/* DFS from list[i] */
		struct cummit_list *stack = NULL;

		list[i]->object.flags |= assign_flag;
		cummit_list_insert(list[i], &stack);

		while (stack) {
			struct cummit_list *parent;

			if (stack->item->object.flags & (with_flag | RESULT)) {
				pop_cummit(&stack);
				if (stack)
					stack->item->object.flags |= RESULT;
				continue;
			}

			for (parent = stack->item->parents; parent; parent = parent->next) {
				if (parent->item->object.flags & (with_flag | RESULT))
					stack->item->object.flags |= RESULT;

				if (!(parent->item->object.flags & assign_flag)) {
					parent->item->object.flags |= assign_flag;

					if (parse_cummit(parent->item) ||
					    parent->item->date < min_cummit_date ||
					    cummit_graph_generation(parent->item) < min_generation)
						continue;

					cummit_list_insert(parent->item, &stack);
					break;
				}
			}

			if (!parent)
				pop_cummit(&stack);
		}

		if (!(list[i]->object.flags & (with_flag | RESULT))) {
			result = 0;
			goto cleanup;
		}
	}

cleanup:
	clear_cummit_marks_many(nr_cummits, list, RESULT | assign_flag);
	free(list);

	for (i = 0; i < from->nr; i++)
		from->objects[i].item->flags &= ~assign_flag;

	return result;
}

int can_all_from_reach(struct cummit_list *from, struct cummit_list *to,
		       int cutoff_by_min_date)
{
	struct object_array from_objs = OBJECT_ARRAY_INIT;
	time_t min_cummit_date = cutoff_by_min_date ? from->item->date : 0;
	struct cummit_list *from_iter = from, *to_iter = to;
	int result;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;

	while (from_iter) {
		add_object_array(&from_iter->item->object, NULL, &from_objs);

		if (!parse_cummit(from_iter->item)) {
			timestamp_t generation;
			if (from_iter->item->date < min_cummit_date)
				min_cummit_date = from_iter->item->date;

			generation = cummit_graph_generation(from_iter->item);
			if (generation < min_generation)
				min_generation = generation;
		}

		from_iter = from_iter->next;
	}

	while (to_iter) {
		if (!parse_cummit(to_iter->item)) {
			timestamp_t generation;
			if (to_iter->item->date < min_cummit_date)
				min_cummit_date = to_iter->item->date;

			generation = cummit_graph_generation(to_iter->item);
			if (generation < min_generation)
				min_generation = generation;
		}

		to_iter->item->object.flags |= PARENT2;

		to_iter = to_iter->next;
	}

	result = can_all_from_reach_with_flag(&from_objs, PARENT2, PARENT1,
					      min_cummit_date, min_generation);

	while (from) {
		clear_cummit_marks(from->item, PARENT1);
		from = from->next;
	}

	while (to) {
		clear_cummit_marks(to->item, PARENT2);
		to = to->next;
	}

	object_array_clear(&from_objs);
	return result;
}

struct cummit_list *get_reachable_subset(struct cummit **from, int nr_from,
					 struct cummit **to, int nr_to,
					 unsigned int reachable_flag)
{
	struct cummit **item;
	struct cummit *current;
	struct cummit_list *found_cummits = NULL;
	struct cummit **to_last = to + nr_to;
	struct cummit **from_last = from + nr_from;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;
	int num_to_find = 0;

	struct prio_queue queue = { compare_cummits_by_gen_then_cummit_date };

	for (item = to; item < to_last; item++) {
		timestamp_t generation;
		struct cummit *c = *item;

		parse_cummit(c);
		generation = cummit_graph_generation(c);
		if (generation < min_generation)
			min_generation = generation;

		if (!(c->object.flags & PARENT1)) {
			c->object.flags |= PARENT1;
			num_to_find++;
		}
	}

	for (item = from; item < from_last; item++) {
		struct cummit *c = *item;
		if (!(c->object.flags & PARENT2)) {
			c->object.flags |= PARENT2;
			parse_cummit(c);

			prio_queue_put(&queue, *item);
		}
	}

	while (num_to_find && (current = prio_queue_get(&queue)) != NULL) {
		struct cummit_list *parents;

		if (current->object.flags & PARENT1) {
			current->object.flags &= ~PARENT1;
			current->object.flags |= reachable_flag;
			cummit_list_insert(current, &found_cummits);
			num_to_find--;
		}

		for (parents = current->parents; parents; parents = parents->next) {
			struct cummit *p = parents->item;

			parse_cummit(p);

			if (cummit_graph_generation(p) < min_generation)
				continue;

			if (p->object.flags & PARENT2)
				continue;

			p->object.flags |= PARENT2;
			prio_queue_put(&queue, p);
		}
	}

	clear_cummit_marks_many(nr_to, to, PARENT1);
	clear_cummit_marks_many(nr_from, from, PARENT2);

	return found_cummits;
}
