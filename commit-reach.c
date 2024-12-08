#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "commit.h"
#include "commit-graph.h"
#include "decorate.h"
#include "hex.h"
#include "prio-queue.h"
#include "ref-filter.h"
#include "revision.h"
#include "tag.h"
#include "commit-reach.h"
#include "ewah/ewok.h"

/* Remember to update object flag allocation in object.h */
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static int compare_commits_by_gen(const void *_a, const void *_b)
{
	const struct commit *a = *(const struct commit * const *)_a;
	const struct commit *b = *(const struct commit * const *)_b;

	timestamp_t generation_a = commit_graph_generation(a);
	timestamp_t generation_b = commit_graph_generation(b);

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
		struct commit *commit = queue->array[i].data;
		if (!(commit->object.flags & STALE))
			return 1;
	}
	return 0;
}

/* all input commits in one and twos[] must have been parsed! */
static int paint_down_to_common(struct repository *r,
				struct commit *one, int n,
				struct commit **twos,
				timestamp_t min_generation,
				int ignore_missing_commits,
				struct commit_list **result)
{
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
	int i;
	timestamp_t last_gen = GENERATION_NUMBER_INFINITY;

	if (!min_generation && !corrected_commit_dates_enabled(r))
		queue.compare = compare_commits_by_commit_date;

	one->object.flags |= PARENT1;
	if (!n) {
		commit_list_append(one, result);
		return 0;
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
		timestamp_t generation = commit_graph_generation(commit);

		if (min_generation && generation > last_gen)
			BUG("bad generation skip %"PRItime" > %"PRItime" at %s",
			    generation, last_gen,
			    oid_to_hex(&commit->object.oid));
		last_gen = generation;

		if (generation < min_generation)
			break;

		flags = commit->object.flags & (PARENT1 | PARENT2 | STALE);
		if (flags == (PARENT1 | PARENT2)) {
			if (!(commit->object.flags & RESULT)) {
				commit->object.flags |= RESULT;
				commit_list_insert_by_date(commit, result);
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
			if (repo_parse_commit(r, p)) {
				clear_prio_queue(&queue);
				free_commit_list(*result);
				*result = NULL;
				/*
				 * At this stage, we know that the commit is
				 * missing: `repo_parse_commit()` uses
				 * `OBJECT_INFO_DIE_IF_CORRUPT` and therefore
				 * corrupt commits would already have been
				 * dispatched with a `die()`.
				 */
				if (ignore_missing_commits)
					return 0;
				return error(_("could not parse commit %s"),
					     oid_to_hex(&p->object.oid));
			}
			p->object.flags |= flags;
			prio_queue_put(&queue, p);
		}
	}

	clear_prio_queue(&queue);
	return 0;
}

static int merge_bases_many(struct repository *r,
			    struct commit *one, int n,
			    struct commit **twos,
			    struct commit_list **result)
{
	struct commit_list *list = NULL;
	int i;

	for (i = 0; i < n; i++) {
		if (one == twos[i]) {
			/*
			 * We do not mark this even with RESULT so we do not
			 * have to clean it up.
			 */
			*result = commit_list_insert(one, result);
			return 0;
		}
	}

	if (!one)
		return 0;
	if (repo_parse_commit(r, one))
		return error(_("could not parse commit %s"),
			     oid_to_hex(&one->object.oid));
	for (i = 0; i < n; i++) {
		if (!twos[i])
			return 0;
		if (repo_parse_commit(r, twos[i]))
			return error(_("could not parse commit %s"),
				     oid_to_hex(&twos[i]->object.oid));
	}

	if (paint_down_to_common(r, one, n, twos, 0, 0, &list)) {
		free_commit_list(list);
		return -1;
	}

	while (list) {
		struct commit *commit = pop_commit(&list);
		if (!(commit->object.flags & STALE))
			commit_list_insert_by_date(commit, result);
	}
	return 0;
}

int get_octopus_merge_bases(struct commit_list *in, struct commit_list **result)
{
	struct commit_list *i, *j, *k;

	if (!in)
		return 0;

	commit_list_insert(in->item, result);

	for (i = in->next; i; i = i->next) {
		struct commit_list *new_commits = NULL, *end = NULL;

		for (j = *result; j; j = j->next) {
			struct commit_list *bases = NULL;
			if (repo_get_merge_bases(the_repository, i->item,
						 j->item, &bases) < 0) {
				free_commit_list(bases);
				free_commit_list(*result);
				*result = NULL;
				return -1;
			}
			if (!new_commits)
				new_commits = bases;
			else
				end->next = bases;
			for (k = bases; k; k = k->next)
				end = k;
		}
		free_commit_list(*result);
		*result = new_commits;
	}
	return 0;
}

static int remove_redundant_no_gen(struct repository *r,
				   struct commit **array, int cnt)
{
	struct commit **work;
	unsigned char *redundant;
	int *filled_index;
	int i, j, filled;

	CALLOC_ARRAY(work, cnt);
	redundant = xcalloc(cnt, 1);
	ALLOC_ARRAY(filled_index, cnt - 1);

	for (i = 0; i < cnt; i++)
		repo_parse_commit(r, array[i]);
	for (i = 0; i < cnt; i++) {
		struct commit_list *common = NULL;
		timestamp_t min_generation = commit_graph_generation(array[i]);

		if (redundant[i])
			continue;
		for (j = filled = 0; j < cnt; j++) {
			timestamp_t curr_generation;
			if (i == j || redundant[j])
				continue;
			filled_index[filled] = j;
			work[filled++] = array[j];

			curr_generation = commit_graph_generation(array[j]);
			if (curr_generation < min_generation)
				min_generation = curr_generation;
		}
		if (paint_down_to_common(r, array[i], filled,
					 work, min_generation, 0, &common)) {
			clear_commit_marks(array[i], all_flags);
			clear_commit_marks_many(filled, work, all_flags);
			free_commit_list(common);
			free(work);
			free(redundant);
			free(filled_index);
			return -1;
		}
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
	free(work);
	free(redundant);
	free(filled_index);
	return filled;
}

static int remove_redundant_with_gen(struct repository *r,
				     struct commit **array, int cnt)
{
	int i, count_non_stale = 0, count_still_independent = cnt;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;
	struct commit **walk_start, **sorted;
	size_t walk_start_nr = 0, walk_start_alloc = cnt;
	int min_gen_pos = 0;

	/*
	 * Sort the input by generation number, ascending. This allows
	 * us to increase the "min_generation" limit when we discover
	 * the commit with lowest generation is STALE. The index
	 * min_gen_pos points to the current position within 'array'
	 * that is not yet known to be STALE.
	 */
	DUP_ARRAY(sorted, array, cnt);
	QSORT(sorted, cnt, compare_commits_by_gen);
	min_generation = commit_graph_generation(sorted[0]);

	ALLOC_ARRAY(walk_start, walk_start_alloc);

	/* Mark all parents of the input as STALE */
	for (i = 0; i < cnt; i++) {
		struct commit_list *parents;

		repo_parse_commit(r, array[i]);
		array[i]->object.flags |= RESULT;
		parents = array[i]->parents;

		while (parents) {
			repo_parse_commit(r, parents->item);
			if (!(parents->item->object.flags & STALE)) {
				parents->item->object.flags |= STALE;
				ALLOC_GROW(walk_start, walk_start_nr + 1, walk_start_alloc);
				walk_start[walk_start_nr++] = parents->item;
			}
			parents = parents->next;
		}
	}

	QSORT(walk_start, walk_start_nr, compare_commits_by_gen);

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
		struct commit_list *stack = NULL;

		commit_list_insert(walk_start[i], &stack);
		walk_start[i]->object.flags |= STALE;

		while (stack) {
			struct commit_list *parents;
			struct commit *c = stack->item;

			repo_parse_commit(r, c);

			if (c->object.flags & RESULT) {
				c->object.flags &= ~RESULT;
				if (--count_still_independent <= 1)
					break;
				if (oideq(&c->object.oid, &sorted[min_gen_pos]->object.oid)) {
					while (min_gen_pos < cnt - 1 &&
					       (sorted[min_gen_pos]->object.flags & STALE))
						min_gen_pos++;
					min_generation = commit_graph_generation(sorted[min_gen_pos]);
				}
			}

			if (commit_graph_generation(c) < min_generation) {
				pop_commit(&stack);
				continue;
			}

			parents = c->parents;
			while (parents) {
				if (!(parents->item->object.flags & STALE)) {
					parents->item->object.flags |= STALE;
					commit_list_insert(parents->item, &stack);
					break;
				}
				parents = parents->next;
			}

			/* pop if all parents have been visited already */
			if (!parents)
				pop_commit(&stack);
		}
		free_commit_list(stack);
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
	clear_commit_marks_many(walk_start_nr, walk_start, STALE);
	free(walk_start);

	return count_non_stale;
}

static int remove_redundant(struct repository *r, struct commit **array, int cnt)
{
	/*
	 * Some commit in the array may be an ancestor of
	 * another commit.  Move the independent commits to the
	 * beginning of 'array' and return their number. Callers
	 * should not rely upon the contents of 'array' after
	 * that number.
	 */
	if (generation_numbers_enabled(r)) {
		int i;

		/*
		 * If we have a single commit with finite generation
		 * number, then the _with_gen algorithm is preferred.
		 */
		for (i = 0; i < cnt; i++) {
			if (commit_graph_generation(array[i]) < GENERATION_NUMBER_INFINITY)
				return remove_redundant_with_gen(r, array, cnt);
		}
	}

	return remove_redundant_no_gen(r, array, cnt);
}

static int get_merge_bases_many_0(struct repository *r,
				  struct commit *one,
				  int n,
				  struct commit **twos,
				  int cleanup,
				  struct commit_list **result)
{
	struct commit_list *list;
	struct commit **rslt;
	int cnt, i;

	if (merge_bases_many(r, one, n, twos, result) < 0)
		return -1;
	for (i = 0; i < n; i++) {
		if (one == twos[i])
			return 0;
	}
	if (!*result || !(*result)->next) {
		if (cleanup) {
			clear_commit_marks(one, all_flags);
			clear_commit_marks_many(n, twos, all_flags);
		}
		return 0;
	}

	/* There are more than one */
	cnt = commit_list_count(*result);
	CALLOC_ARRAY(rslt, cnt);
	for (list = *result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_commit_list(*result);
	*result = NULL;

	clear_commit_marks(one, all_flags);
	clear_commit_marks_many(n, twos, all_flags);

	cnt = remove_redundant(r, rslt, cnt);
	if (cnt < 0) {
		free(rslt);
		return -1;
	}
	for (i = 0; i < cnt; i++)
		commit_list_insert_by_date(rslt[i], result);
	free(rslt);
	return 0;
}

int repo_get_merge_bases_many(struct repository *r,
			      struct commit *one,
			      int n,
			      struct commit **twos,
			      struct commit_list **result)
{
	return get_merge_bases_many_0(r, one, n, twos, 1, result);
}

int repo_get_merge_bases_many_dirty(struct repository *r,
				    struct commit *one,
				    int n,
				    struct commit **twos,
				    struct commit_list **result)
{
	return get_merge_bases_many_0(r, one, n, twos, 0, result);
}

int repo_get_merge_bases(struct repository *r,
			 struct commit *one,
			 struct commit *two,
			 struct commit_list **result)
{
	return get_merge_bases_many_0(r, one, 1, &two, 1, result);
}

/*
 * Is "commit" a descendant of one of the elements on the "with_commit" list?
 */
int repo_is_descendant_of(struct repository *r,
			  struct commit *commit,
			  struct commit_list *with_commit)
{
	if (!with_commit)
		return 1;

	if (generation_numbers_enabled(r)) {
		struct commit_list *from_list = NULL;
		int result;
		commit_list_insert(commit, &from_list);
		result = can_all_from_reach(from_list, with_commit, 0);
		free_commit_list(from_list);
		return result;
	} else {
		while (with_commit) {
			struct commit *other;
			int ret;

			other = with_commit->item;
			with_commit = with_commit->next;
			ret = repo_in_merge_bases_many(r, other, 1, &commit, 0);
			if (ret)
				return ret;
		}
		return 0;
	}
}

/*
 * Is "commit" an ancestor of one of the "references"?
 */
int repo_in_merge_bases_many(struct repository *r, struct commit *commit,
			     int nr_reference, struct commit **reference,
			     int ignore_missing_commits)
{
	struct commit_list *bases = NULL;
	int ret = 0, i;
	timestamp_t generation, max_generation = GENERATION_NUMBER_ZERO;

	if (repo_parse_commit(r, commit))
		return ignore_missing_commits ? 0 : -1;
	for (i = 0; i < nr_reference; i++) {
		if (repo_parse_commit(r, reference[i]))
			return ignore_missing_commits ? 0 : -1;

		generation = commit_graph_generation(reference[i]);
		if (generation > max_generation)
			max_generation = generation;
	}

	generation = commit_graph_generation(commit);
	if (generation > max_generation)
		return ret;

	if (paint_down_to_common(r, commit,
				 nr_reference, reference,
				 generation, ignore_missing_commits, &bases))
		ret = -1;
	else if (commit->object.flags & PARENT2)
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
	int res;
	struct commit_list *list = NULL;
	struct commit_list **next = &list;

	next = commit_list_append(commit, next);
	res = repo_is_descendant_of(r, reference, list);
	free_commit_list(list);

	return res;
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
	CALLOC_ARRAY(array, num_head);
	for (p = heads, i = 0; p; p = p->next) {
		if (p->item->object.flags & STALE) {
			array[i++] = p->item;
			p->item->object.flags &= ~STALE;
		}
	}
	num_head = remove_redundant(the_repository, array, num_head);
	if (num_head < 0) {
		free(array);
		return NULL;
	}
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
	int ret;

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

	if (repo_parse_commit(the_repository, new_commit) < 0)
		return 0;

	commit_list_insert(old_commit, &old_commit_list);
	ret = repo_is_descendant_of(the_repository,
				    new_commit, old_commit_list);
	if (ret < 0)
		exit(128);
	free_commit_list(old_commit_list);
	return ret;
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
					  timestamp_t cutoff)
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

	if (commit_graph_generation(candidate) < cutoff)
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
	timestamp_t cutoff = GENERATION_NUMBER_INFINITY;
	const struct commit_list *p;

	for (p = want; p; p = p->next) {
		timestamp_t generation;
		struct commit *c = p->item;
		load_commit_graph_info(the_repository, c);
		generation = commit_graph_generation(c);
		if (generation < cutoff)
			cutoff = generation;
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
	return repo_is_descendant_of(the_repository, commit, list);
}

int can_all_from_reach_with_flag(struct object_array *from,
				 unsigned int with_flag,
				 unsigned int assign_flag,
				 time_t min_commit_date,
				 timestamp_t min_generation)
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
		if (repo_parse_commit(the_repository, list[nr_commits]) ||
		    commit_graph_generation(list[nr_commits]) < min_generation) {
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

					if (repo_parse_commit(the_repository, parent->item) ||
					    parent->item->date < min_commit_date ||
					    commit_graph_generation(parent->item) < min_generation)
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

	for (i = 0; i < from->nr; i++) {
		struct object *from_one = from->objects[i].item;

		if (from_one)
			from_one->flags &= ~assign_flag;
	}

	return result;
}

int can_all_from_reach(struct commit_list *from, struct commit_list *to,
		       int cutoff_by_min_date)
{
	struct object_array from_objs = OBJECT_ARRAY_INIT;
	time_t min_commit_date = cutoff_by_min_date ? from->item->date : 0;
	struct commit_list *from_iter = from, *to_iter = to;
	int result;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;

	while (from_iter) {
		add_object_array(&from_iter->item->object, NULL, &from_objs);

		if (!repo_parse_commit(the_repository, from_iter->item)) {
			timestamp_t generation;
			if (from_iter->item->date < min_commit_date)
				min_commit_date = from_iter->item->date;

			generation = commit_graph_generation(from_iter->item);
			if (generation < min_generation)
				min_generation = generation;
		}

		from_iter = from_iter->next;
	}

	while (to_iter) {
		if (!repo_parse_commit(the_repository, to_iter->item)) {
			timestamp_t generation;
			if (to_iter->item->date < min_commit_date)
				min_commit_date = to_iter->item->date;

			generation = commit_graph_generation(to_iter->item);
			if (generation < min_generation)
				min_generation = generation;
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
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;
	int num_to_find = 0;

	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };

	for (item = to; item < to_last; item++) {
		timestamp_t generation;
		struct commit *c = *item;

		repo_parse_commit(the_repository, c);
		generation = commit_graph_generation(c);
		if (generation < min_generation)
			min_generation = generation;

		if (!(c->object.flags & PARENT1)) {
			c->object.flags |= PARENT1;
			num_to_find++;
		}
	}

	for (item = from; item < from_last; item++) {
		struct commit *c = *item;
		if (!(c->object.flags & PARENT2)) {
			c->object.flags |= PARENT2;
			repo_parse_commit(the_repository, c);

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

			repo_parse_commit(the_repository, p);

			if (commit_graph_generation(p) < min_generation)
				continue;

			if (p->object.flags & PARENT2)
				continue;

			p->object.flags |= PARENT2;
			prio_queue_put(&queue, p);
		}
	}

	clear_prio_queue(&queue);

	clear_commit_marks_many(nr_to, to, PARENT1);
	clear_commit_marks_many(nr_from, from, PARENT2);

	return found_commits;
}

define_commit_slab(bit_arrays, struct bitmap *);
static struct bit_arrays bit_arrays;

static void insert_no_dup(struct prio_queue *queue, struct commit *c)
{
	if (c->object.flags & PARENT2)
		return;
	prio_queue_put(queue, c);
	c->object.flags |= PARENT2;
}

static struct bitmap *get_bit_array(struct commit *c, int width)
{
	struct bitmap **bitmap = bit_arrays_at(&bit_arrays, c);
	if (!*bitmap)
		*bitmap = bitmap_word_alloc(width);
	return *bitmap;
}

static void free_bit_array(struct commit *c)
{
	struct bitmap **bitmap = bit_arrays_at(&bit_arrays, c);
	if (!*bitmap)
		return;
	bitmap_free(*bitmap);
	*bitmap = NULL;
}

void ahead_behind(struct repository *r,
		  struct commit **commits, size_t commits_nr,
		  struct ahead_behind_count *counts, size_t counts_nr)
{
	struct prio_queue queue = { .compare = compare_commits_by_gen_then_commit_date };
	size_t width = DIV_ROUND_UP(commits_nr, BITS_IN_EWORD);

	if (!commits_nr || !counts_nr)
		return;

	for (size_t i = 0; i < counts_nr; i++) {
		counts[i].ahead = 0;
		counts[i].behind = 0;
	}

	ensure_generations_valid(r, commits, commits_nr);

	init_bit_arrays(&bit_arrays);

	for (size_t i = 0; i < commits_nr; i++) {
		struct commit *c = commits[i];
		struct bitmap *bitmap = get_bit_array(c, width);

		bitmap_set(bitmap, i);
		insert_no_dup(&queue, c);
	}

	while (queue_has_nonstale(&queue)) {
		struct commit *c = prio_queue_get(&queue);
		struct commit_list *p;
		struct bitmap *bitmap_c = get_bit_array(c, width);

		for (size_t i = 0; i < counts_nr; i++) {
			int reach_from_tip = !!bitmap_get(bitmap_c, counts[i].tip_index);
			int reach_from_base = !!bitmap_get(bitmap_c, counts[i].base_index);

			if (reach_from_tip ^ reach_from_base) {
				if (reach_from_base)
					counts[i].behind++;
				else
					counts[i].ahead++;
			}
		}

		for (p = c->parents; p; p = p->next) {
			struct bitmap *bitmap_p;

			repo_parse_commit(r, p->item);

			bitmap_p = get_bit_array(p->item, width);
			bitmap_or(bitmap_p, bitmap_c);

			/*
			 * If this parent is reachable from every starting
			 * commit, then none of its ancestors can contribute
			 * to the ahead/behind count. Mark it as STALE, so
			 * we can stop the walk when every commit in the
			 * queue is STALE.
			 */
			if (bitmap_popcount(bitmap_p) == commits_nr)
				p->item->object.flags |= STALE;

			insert_no_dup(&queue, p->item);
		}

		free_bit_array(c);
	}

	/* STALE is used here, PARENT2 is used by insert_no_dup(). */
	repo_clear_commit_marks(r, PARENT2 | STALE);
	while (prio_queue_peek(&queue)) {
		struct commit *c = prio_queue_get(&queue);
		free_bit_array(c);
	}
	clear_bit_arrays(&bit_arrays);
	clear_prio_queue(&queue);
}

struct commit_and_index {
	struct commit *commit;
	unsigned int index;
	timestamp_t generation;
};

static int compare_commit_and_index_by_generation(const void *va, const void *vb)
{
	const struct commit_and_index *a = (const struct commit_and_index *)va;
	const struct commit_and_index *b = (const struct commit_and_index *)vb;

	if (a->generation > b->generation)
		return 1;
	if (a->generation < b->generation)
		return -1;
	return 0;
}

void tips_reachable_from_bases(struct repository *r,
			       struct commit_list *bases,
			       struct commit **tips, size_t tips_nr,
			       int mark)
{
	struct commit_and_index *commits;
	size_t min_generation_index = 0;
	timestamp_t min_generation;
	struct commit_list *stack = NULL;

	if (!bases || !tips || !tips_nr)
		return;

	/*
	 * Do a depth-first search starting at 'bases' to search for the
	 * tips. Stop at the lowest (un-found) generation number. When
	 * finding the lowest commit, increase the minimum generation
	 * number to the next lowest (un-found) generation number.
	 */

	CALLOC_ARRAY(commits, tips_nr);

	for (size_t i = 0; i < tips_nr; i++) {
		commits[i].commit = tips[i];
		commits[i].index = i;
		commits[i].generation = commit_graph_generation(tips[i]);
	}

	/* Sort with generation number ascending. */
	QSORT(commits, tips_nr, compare_commit_and_index_by_generation);
	min_generation = commits[0].generation;

	while (bases) {
		repo_parse_commit(r, bases->item);
		commit_list_insert(bases->item, &stack);
		bases = bases->next;
	}

	while (stack) {
		int explored_all_parents = 1;
		struct commit_list *p;
		struct commit *c = stack->item;
		timestamp_t c_gen = commit_graph_generation(c);

		/* Does it match any of our tips? */
		for (size_t j = min_generation_index; j < tips_nr; j++) {
			if (c_gen < commits[j].generation)
				break;

			if (commits[j].commit == c) {
				tips[commits[j].index]->object.flags |= mark;

				if (j == min_generation_index) {
					unsigned int k = j + 1;
					while (k < tips_nr &&
					       (tips[commits[k].index]->object.flags & mark))
						k++;

					/* Terminate early if all found. */
					if (k >= tips_nr)
						goto done;

					min_generation_index = k;
					min_generation = commits[k].generation;
				}
			}
		}

		for (p = c->parents; p; p = p->next) {
			repo_parse_commit(r, p->item);

			/* Have we already explored this parent? */
			if (p->item->object.flags & SEEN)
				continue;

			/* Is it below the current minimum generation? */
			if (commit_graph_generation(p->item) < min_generation)
				continue;

			/* Ok, we will explore from here on. */
			p->item->object.flags |= SEEN;
			explored_all_parents = 0;
			commit_list_insert(p->item, &stack);
			break;
		}

		if (explored_all_parents)
			pop_commit(&stack);
	}

done:
	free(commits);
	repo_clear_commit_marks(r, SEEN);
	free_commit_list(stack);
}

/*
 * This slab initializes integers to zero, so use "-1" for "tip is best" and
 * "i + 1" for "bases[i] is best".
 */
define_commit_slab(best_branch_base, int);
static struct best_branch_base best_branch_base;
#define get_best(c) (*best_branch_base_at(&best_branch_base, (c)))
#define set_best(c,v) (*best_branch_base_at(&best_branch_base, (c)) = (v))

int get_branch_base_for_tip(struct repository *r,
			    struct commit *tip,
			    struct commit **bases,
			    size_t bases_nr)
{
	int best_index = -1;
	struct commit *branch_point = NULL;
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
	int found_missing_gen = 0;

	if (!bases_nr)
		return -1;

	repo_parse_commit(r, tip);
	if (commit_graph_generation(tip) == GENERATION_NUMBER_INFINITY)
		found_missing_gen = 1;

	/* Check for missing generation numbers. */
	for (size_t i = 0; i < bases_nr; i++) {
		struct commit *c = bases[i];
		repo_parse_commit(r, c);
		if (commit_graph_generation(c) == GENERATION_NUMBER_INFINITY)
			found_missing_gen = 1;
	}

	if (found_missing_gen) {
		struct commit **commits;
		size_t commits_nr = bases_nr + 1;

		CALLOC_ARRAY(commits, commits_nr);
		COPY_ARRAY(commits, bases, bases_nr);
		commits[bases_nr] = tip;
		ensure_generations_valid(r, commits, commits_nr);
		free(commits);
	}

	/* Initialize queue and slab now that generations are guaranteed. */
	init_best_branch_base(&best_branch_base);
	set_best(tip, -1);
	prio_queue_put(&queue, tip);

	for (size_t i = 0; i < bases_nr; i++) {
		struct commit *c = bases[i];
		int best = get_best(c);

		/* Has this already been marked as best by another commit? */
		if (best) {
			if (best == -1) {
				/* We agree at this position. Stop now. */
				best_index = i + 1;
				goto cleanup;
			}
			continue;
		}

		set_best(c, i + 1);
		prio_queue_put(&queue, c);
	}

	while (queue.nr) {
		struct commit *c = prio_queue_get(&queue);
		int best_for_c = get_best(c);
		int best_for_p, positive;
		struct commit *parent;

		/* Have we reached a known branch point? It's optimal. */
		if (c == branch_point)
			break;

		repo_parse_commit(r, c);
		if (!c->parents)
			continue;

		parent = c->parents->item;
		repo_parse_commit(r, parent);
		best_for_p = get_best(parent);

		if (!best_for_p) {
			/* 'parent' is new, so pass along best_for_c. */
			set_best(parent, best_for_c);
			prio_queue_put(&queue, parent);
			continue;
		}

		if (best_for_p > 0 && best_for_c > 0) {
			/* Collision among bases. Minimize. */
			if (best_for_c < best_for_p)
				set_best(parent, best_for_c);
			continue;
		}

		/*
		 * At this point, we have reached a commit that is reachable
		 * from the tip, either from 'c' or from an earlier commit to
		 * have 'parent' as its first parent.
		 *
		 * Update 'best_index' to match the minimum of all base indices
		 * to reach 'parent'.
		 */

		/* Exactly one is positive due to initial conditions. */
		positive = (best_for_c < 0) ? best_for_p : best_for_c;

		if (best_index < 0 || positive < best_index)
			best_index = positive;

		/* No matter what, track that the parent is reachable from tip. */
		set_best(parent, -1);
		branch_point = parent;
	}

cleanup:
	clear_best_branch_base(&best_branch_base);
	clear_prio_queue(&queue);
	return best_index > 0 ? best_index - 1 : -1;
}
