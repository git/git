#include "cache.h"
#include "commit.h"
#include "commit-graph.h"
#include "decorate.h"
#include "prio-queue.h"
#include "tree.h"
#include "ref-filter.h"
#include "revision.h"
#include "tag.h"
#include "commit-reach.h"

/* Remember to update object flag allocation in object.h */
#define REACHABLE       (1u<<15)
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static int queue_has_nonstale(struct prio_queue *queue)
{
	int i;
	for (i = 0; i < queue->nr; i++) {
		struct commit *commit = queue->array[i].data;
		if (!(commit->object.flags & STALE))
			return 1;
	}
	return 0;
}

/* all input commits in one and twos[] must have been parsed! */
static struct commit_list *paint_down_to_common(struct repository *r,
						struct commit *one, int n,
						struct commit **twos,
						int min_generation)
{
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
	struct commit_list *result = NULL;
	int i;
	uint32_t last_gen = GENERATION_NUMBER_INFINITY;

	if (!min_generation)
		queue.compare = compare_commits_by_commit_date;

	one->object.flags |= PARENT1;
	if (!n) {
		commit_list_append(one, &result);
		return result;
	}
	prio_queue_put(&queue, one);

	for (i = 0; i < n; i++) {
		twos[i]->object.flags |= PARENT2;
		prio_queue_put(&queue, twos[i]);
	}

	while (queue_has_nonstale(&queue)) {
		struct commit *commit = prio_queue_get(&queue);
		struct commit_list *parents;
		int flags;

		if (min_generation && commit->generation > last_gen)
			BUG("bad generation skip %8x > %8x at %s",
			    commit->generation, last_gen,
			    oid_to_hex(&commit->object.oid));
		last_gen = commit->generation;

		if (commit->generation < min_generation)
			break;

		flags = commit->object.flags & (PARENT1 | PARENT2 | STALE);
		if (flags == (PARENT1 | PARENT2)) {
			if (!(commit->object.flags & RESULT)) {
				commit->object.flags |= RESULT;
				commit_list_insert_by_date(commit, &result);
			}
			/* Mark parents of a found merge stale */
			flags |= STALE;
		}
		parents = commit->parents;
		while (parents) {
			struct commit *p = parents->item;
			parents = parents->next;
			if ((p->object.flags & flags) == flags)
				continue;
			if (repo_parse_commit(r, p))
				return NULL;
			p->object.flags |= flags;
			prio_queue_put(&queue, p);
		}
	}

	clear_prio_queue(&queue);
	return result;
}

static struct commit_list *merge_bases_many(struct repository *r,
					    struct commit *one, int n,
					    struct commit **twos)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;
	int i;

	for (i = 0; i < n; i++) {
		if (one == twos[i])
			/*
			 * We do not mark this even with RESULT so we do not
			 * have to clean it up.
			 */
			return commit_list_insert(one, &result);
	}

	if (repo_parse_commit(r, one))
		return NULL;
	for (i = 0; i < n; i++) {
		if (repo_parse_commit(r, twos[i]))
			return NULL;
	}

	list = paint_down_to_common(r, one, n, twos, 0);

	while (list) {
		struct commit *commit = pop_commit(&list);
		if (!(commit->object.flags & STALE))
			commit_list_insert_by_date(commit, &result);
	}
	return result;
}

struct commit_list *get_octopus_merge_bases(struct commit_list *in)
{
	struct commit_list *i, *j, *k, *ret = NULL;

	if (!in)
		return ret;

	commit_list_insert(in->item, &ret);

	for (i = in->next; i; i = i->next) {
		struct commit_list *new_commits = NULL, *end = NULL;

		for (j = ret; j; j = j->next) {
			struct commit_list *bases;
			bases = get_merge_bases(i->item, j->item);
			if (!new_commits)
				new_commits = bases;
			else
				end->next = bases;
			for (k = bases; k; k = k->next)
				end = k;
		}
		ret = new_commits;
	}
	return ret;
}

static int remove_redundant(struct repository *r, struct commit **array, int cnt)
{
	/*
	 * Some commit in the array may be an ancestor of
	 * another commit.  Move such commit to the end of
	 * the array, and return the number of commits that
	 * are independent from each other.
	 */
	struct commit **work;
	unsigned char *redundant;
	int *filled_index;
	int i, j, filled;

	work = xcalloc(cnt, sizeof(*work));
	redundant = xcalloc(cnt, 1);
	ALLOC_ARRAY(filled_index, cnt - 1);

	for (i = 0; i < cnt; i++)
		repo_parse_commit(r, array[i]);
	for (i = 0; i < cnt; i++) {
		struct commit_list *common;
		uint32_t min_generation = array[i]->generation;

		if (redundant[i])
			continue;
		for (j = filled = 0; j < cnt; j++) {
			if (i == j || redundant[j])
				continue;
			filled_index[filled] = j;
			work[filled++] = array[j];

			if (array[j]->generation < min_generation)
				min_generation = array[j]->generation;
		}
		common = paint_down_to_common(r, array[i], filled,
					      work, min_generation);
		if (array[i]->object.flags & PARENT2)
			redundant[i] = 1;
		for (j = 0; j < filled; j++)
			if (work[j]->object.flags & PARENT1)
				redundant[filled_index[j]] = 1;
		clear_commit_marks(array[i], all_flags);
		clear_commit_marks_many(filled, work, all_flags);
		free_commit_list(common);
	}

	/* Now collect the result */
	COPY_ARRAY(work, array, cnt);
	for (i = filled = 0; i < cnt; i++)
		if (!redundant[i])
			array[filled++] = work[i];
	for (j = filled, i = 0; i < cnt; i++)
		if (redundant[i])
			array[j++] = work[i];
	free(work);
	free(redundant);
	free(filled_index);
	return filled;
}

static struct commit_list *get_merge_bases_many_0(struct repository *r,
						  struct commit *one,
						  int n,
						  struct commit **twos,
						  int cleanup)
{
	struct commit_list *list;
	struct commit **rslt;
	struct commit_list *result;
	int cnt, i;

	result = merge_bases_many(r, one, n, twos);
	for (i = 0; i < n; i++) {
		if (one == twos[i])
			return result;
	}
	if (!result || !result->next) {
		if (cleanup) {
			clear_commit_marks(one, all_flags);
			clear_commit_marks_many(n, twos, all_flags);
		}
		return result;
	}

	/* There are more than one */
	cnt = commit_list_count(result);
	rslt = xcalloc(cnt, sizeof(*rslt));
	for (list = result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_commit_list(result);

	clear_commit_marks(one, all_flags);
	clear_commit_marks_many(n, twos, all_flags);

	cnt = remove_redundant(r, rslt, cnt);
	result = NULL;
	for (i = 0; i < cnt; i++)
		commit_list_insert_by_date(rslt[i], &result);
	free(rslt);
	return result;
}

struct commit_list *repo_get_merge_bases_many(struct repository *r,
					      struct commit *one,
					      int n,
					      struct commit **twos)
{
	return get_merge_bases_many_0(r, one, n, twos, 1);
}

struct commit_list *repo_get_merge_bases_many_dirty(struct repository *r,
						    struct commit *one,
						    int n,
						    struct commit **twos)
{
	return get_merge_bases_many_0(r, one, n, twos, 0);
}

struct commit_list *repo_get_merge_bases(struct repository *r,
					 struct commit *one,
					 struct commit *two)
{
	return get_merge_bases_many_0(r, one, 1, &two, 1);
}

/*
 * Is "commit" a descendant of one of the elements on the "with_commit" list?
 */
int is_descendant_of(struct commit *commit, struct commit_list *with_commit)
{
	if (!with_commit)
		return 1;

	if (generation_numbers_enabled(the_repository)) {
		struct commit_list *from_list = NULL;
		int result;
		commit_list_insert(commit, &from_list);
		result = can_all_from_reach(from_list, with_commit, 0);
		free_commit_list(from_list);
		return result;
	} else {
		while (with_commit) {
			struct commit *other;

			other = with_commit->item;
			with_commit = with_commit->next;
			if (in_merge_bases(other, commit))
				return 1;
		}
		return 0;
	}
}

/*
 * Is "commit" an ancestor of one of the "references"?
 */
int repo_in_merge_bases_many(struct repository *r, struct commit *commit,
			     int nr_reference, struct commit **reference)
{
	struct commit_list *bases;
	int ret = 0, i;
	uint32_t min_generation = GENERATION_NUMBER_INFINITY;

	if (repo_parse_commit(r, commit))
		return ret;
	for (i = 0; i < nr_reference; i++) {
		if (repo_parse_commit(r, reference[i]))
			return ret;
		if (reference[i]->generation < min_generation)
			min_generation = reference[i]->generation;
	}

	if (commit->generation > min_generation)
		return ret;

	bases = paint_down_to_common(r, commit,
				     nr_reference, reference,
				     commit->generation);
	if (commit->object.flags & PARENT2)
		ret = 1;
	clear_commit_marks(commit, all_flags);
	clear_commit_marks_many(nr_reference, reference, all_flags);
	free_commit_list(bases);
	return ret;
}

/*
 * Is "commit" an ancestor of (i.e. reachable from) the "reference"?
 */
int repo_in_merge_bases(struct repository *r,
			struct commit *commit,
			struct commit *reference)
{
	return repo_in_merge_bases_many(r, commit, 1, &reference);
}

struct commit_list *reduce_heads(struct commit_list *heads)
{
	struct commit_list *p;
	struct commit_list *result = NULL, **tail = &result;
	struct commit **array;
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
	array = xcalloc(num_head, sizeof(*array));
	for (p = heads, i = 0; p; p = p->next) {
		if (p->item->object.flags & STALE) {
			array[i++] = p->item;
			p->item->object.flags &= ~STALE;
		}
	}
	num_head = remove_redundant(the_repository, array, num_head);
	for (i = 0; i < num_head; i++)
		tail = &commit_list_insert(array[i], tail)->next;
	free(array);
	return result;
}

void reduce_heads_replace(struct commit_list **heads)
{
	struct commit_list *result = reduce_heads(*heads);
	free_commit_list(*heads);
	*heads = result;
}

int ref_newer(const struct object_id *new_oid, const struct object_id *old_oid)
{
	struct object *o;
	struct commit *old_commit, *new_commit;
	struct commit_list *old_commit_list = NULL;

	/*
	 * Both new_commit and old_commit must be commit-ish and new_commit is descendant of
	 * old_commit.  Otherwise we require --force.
	 */
	o = deref_tag(the_repository, parse_object(the_repository, old_oid),
		      NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	old_commit = (struct commit *) o;

	o = deref_tag(the_repository, parse_object(the_repository, new_oid),
		      NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	new_commit = (struct commit *) o;

	if (parse_commit(new_commit) < 0)
		return 0;

	commit_list_insert(old_commit, &old_commit_list);
	return is_descendant_of(new_commit, old_commit_list);
}

/*
 * Mimicking the real stack, this stack lives on the heap, avoiding stack
 * overflows.
 *
 * At each recursion step, the stack items points to the commits whose
 * ancestors are to be inspected.
 */
struct contains_stack {
	int nr, alloc;
	struct contains_stack_entry {
		struct commit *commit;
		struct commit_list *parents;
	} *contains_stack;
};

static int in_commit_list(const struct commit_list *want, struct commit *c)
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
static enum contains_result contains_test(struct commit *candidate,
					  const struct commit_list *want,
					  struct contains_cache *cache,
					  uint32_t cutoff)
{
	enum contains_result *cached = contains_cache_at(cache, candidate);

	/* If we already have the answer cached, return that. */
	if (*cached)
		return *cached;

	/* or are we it? */
	if (in_commit_list(want, candidate)) {
		*cached = CONTAINS_YES;
		return CONTAINS_YES;
	}

	/* Otherwise, we don't know; prepare to recurse */
	parse_commit_or_die(candidate);

	if (candidate->generation < cutoff)
		return CONTAINS_NO;

	return CONTAINS_UNKNOWN;
}

static void push_to_contains_stack(struct commit *candidate, struct contains_stack *contains_stack)
{
	ALLOC_GROW(contains_stack->contains_stack, contains_stack->nr + 1, contains_stack->alloc);
	contains_stack->contains_stack[contains_stack->nr].commit = candidate;
	contains_stack->contains_stack[contains_stack->nr++].parents = candidate->parents;
}

static enum contains_result contains_tag_algo(struct commit *candidate,
					      const struct commit_list *want,
					      struct contains_cache *cache)
{
	struct contains_stack contains_stack = { 0, 0, NULL };
	enum contains_result result;
	uint32_t cutoff = GENERATION_NUMBER_INFINITY;
	const struct commit_list *p;

	for (p = want; p; p = p->next) {
		struct commit *c = p->item;
		load_commit_graph_info(the_repository, c);
		if (c->generation < cutoff)
			cutoff = c->generation;
	}

	result = contains_test(candidate, want, cache, cutoff);
	if (result != CONTAINS_UNKNOWN)
		return result;

	push_to_contains_stack(candidate, &contains_stack);
	while (contains_stack.nr) {
		struct contains_stack_entry *entry = &contains_stack.contains_stack[contains_stack.nr - 1];
		struct commit *commit = entry->commit;
		struct commit_list *parents = entry->parents;

		if (!parents) {
			*contains_cache_at(cache, commit) = CONTAINS_NO;
			contains_stack.nr--;
		}
		/*
		 * If we just popped the stack, parents->item has been marked,
		 * therefore contains_test will return a meaningful yes/no.
		 */
		else switch (contains_test(parents->item, want, cache, cutoff)) {
		case CONTAINS_YES:
			*contains_cache_at(cache, commit) = CONTAINS_YES;
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

int commit_contains(struct ref_filter *filter, struct commit *commit,
		    struct commit_list *list, struct contains_cache *cache)
{
	if (filter->with_commit_tag_algo)
		return contains_tag_algo(commit, list, cache) == CONTAINS_YES;
	return is_descendant_of(commit, list);
}

static int compare_commits_by_gen(const void *_a, const void *_b)
{
	const struct commit *a = *(const struct commit * const *)_a;
	const struct commit *b = *(const struct commit * const *)_b;

	if (a->generation < b->generation)
		return -1;
	if (a->generation > b->generation)
		return 1;
	return 0;
}

int can_all_from_reach_with_flag(struct object_array *from,
				 unsigned int with_flag,
				 unsigned int assign_flag,
				 time_t min_commit_date,
				 uint32_t min_generation)
{
	struct commit **list = NULL;
	int i;
	int nr_commits;
	int result = 1;

	ALLOC_ARRAY(list, from->nr);
	nr_commits = 0;
	for (i = 0; i < from->nr; i++) {
		struct object *from_one = from->objects[i].item;

		if (!from_one || from_one->flags & assign_flag)
			continue;

		from_one = deref_tag(the_repository, from_one,
				     "a from object", 0);
		if (!from_one || from_one->type != OBJ_COMMIT) {
			/*
			 * no way to tell if this is reachable by
			 * looking at the ancestry chain alone, so
			 * leave a note to ourselves not to worry about
			 * this object anymore.
			 */
			from->objects[i].item->flags |= assign_flag;
			continue;
		}

		list[nr_commits] = (struct commit *)from_one;
		if (parse_commit(list[nr_commits]) ||
		    list[nr_commits]->generation < min_generation) {
			result = 0;
			goto cleanup;
		}

		nr_commits++;
	}

	QSORT(list, nr_commits, compare_commits_by_gen);

	for (i = 0; i < nr_commits; i++) {
		/* DFS from list[i] */
		struct commit_list *stack = NULL;

		list[i]->object.flags |= assign_flag;
		commit_list_insert(list[i], &stack);

		while (stack) {
			struct commit_list *parent;

			if (stack->item->object.flags & (with_flag | RESULT)) {
				pop_commit(&stack);
				if (stack)
					stack->item->object.flags |= RESULT;
				continue;
			}

			for (parent = stack->item->parents; parent; parent = parent->next) {
				if (parent->item->object.flags & (with_flag | RESULT))
					stack->item->object.flags |= RESULT;

				if (!(parent->item->object.flags & assign_flag)) {
					parent->item->object.flags |= assign_flag;

					if (parse_commit(parent->item) ||
					    parent->item->date < min_commit_date ||
					    parent->item->generation < min_generation)
						continue;

					commit_list_insert(parent->item, &stack);
					break;
				}
			}

			if (!parent)
				pop_commit(&stack);
		}

		if (!(list[i]->object.flags & (with_flag | RESULT))) {
			result = 0;
			goto cleanup;
		}
	}

cleanup:
	clear_commit_marks_many(nr_commits, list, RESULT | assign_flag);
	free(list);

	for (i = 0; i < from->nr; i++)
		from->objects[i].item->flags &= ~assign_flag;

	return result;
}

int can_all_from_reach(struct commit_list *from, struct commit_list *to,
		       int cutoff_by_min_date)
{
	struct object_array from_objs = OBJECT_ARRAY_INIT;
	time_t min_commit_date = cutoff_by_min_date ? from->item->date : 0;
	struct commit_list *from_iter = from, *to_iter = to;
	int result;
	uint32_t min_generation = GENERATION_NUMBER_INFINITY;

	while (from_iter) {
		add_object_array(&from_iter->item->object, NULL, &from_objs);

		if (!parse_commit(from_iter->item)) {
			if (from_iter->item->date < min_commit_date)
				min_commit_date = from_iter->item->date;

			if (from_iter->item->generation < min_generation)
				min_generation = from_iter->item->generation;
		}

		from_iter = from_iter->next;
	}

	while (to_iter) {
		if (!parse_commit(to_iter->item)) {
			if (to_iter->item->date < min_commit_date)
				min_commit_date = to_iter->item->date;

			if (to_iter->item->generation < min_generation)
				min_generation = to_iter->item->generation;
		}

		to_iter->item->object.flags |= PARENT2;

		to_iter = to_iter->next;
	}

	result = can_all_from_reach_with_flag(&from_objs, PARENT2, PARENT1,
					      min_commit_date, min_generation);

	while (from) {
		clear_commit_marks(from->item, PARENT1);
		from = from->next;
	}

	while (to) {
		clear_commit_marks(to->item, PARENT2);
		to = to->next;
	}

	object_array_clear(&from_objs);
	return result;
}

struct commit_list *get_reachable_subset(struct commit **from, int nr_from,
					 struct commit **to, int nr_to,
					 unsigned int reachable_flag)
{
	struct commit **item;
	struct commit *current;
	struct commit_list *found_commits = NULL;
	struct commit **to_last = to + nr_to;
	struct commit **from_last = from + nr_from;
	uint32_t min_generation = GENERATION_NUMBER_INFINITY;
	int num_to_find = 0;

	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };

	for (item = to; item < to_last; item++) {
		struct commit *c = *item;

		parse_commit(c);
		if (c->generation < min_generation)
			min_generation = c->generation;

		if (!(c->object.flags & PARENT1)) {
			c->object.flags |= PARENT1;
			num_to_find++;
		}
	}

	for (item = from; item < from_last; item++) {
		struct commit *c = *item;
		if (!(c->object.flags & PARENT2)) {
			c->object.flags |= PARENT2;
			parse_commit(c);

			prio_queue_put(&queue, *item);
		}
	}

	while (num_to_find && (current = prio_queue_get(&queue)) != NULL) {
		struct commit_list *parents;

		if (current->object.flags & PARENT1) {
			current->object.flags &= ~PARENT1;
			current->object.flags |= reachable_flag;
			commit_list_insert(current, &found_commits);
			num_to_find--;
		}

		for (parents = current->parents; parents; parents = parents->next) {
			struct commit *p = parents->item;

			parse_commit(p);

			if (p->generation < min_generation)
				continue;

			if (p->object.flags & PARENT2)
				continue;

			p->object.flags |= PARENT2;
			prio_queue_put(&queue, p);
		}
	}

	clear_commit_marks_many(nr_to, to, PARENT1);
	clear_commit_marks_many(nr_from, from, PARENT2);

	return found_commits;
}
