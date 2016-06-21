/*
 * Copyright (c) 2005, Jon Seymour
 *
 * For more information about epoch theory on which this module is based,
 * refer to http://blackcubes.dyndns.org/epoch/. That web page defines
 * terms such as "epoch" and "minimal, non-linear epoch" and provides rationales
 * for some of the algorithms used here.
 *
 */
#include <stdlib.h>

/* Provides arbitrary precision integers required to accurately represent
 * fractional mass: */
#include <openssl/bn.h>

#include "cache.h"
#include "commit.h"
#include "epoch.h"

struct fraction {
	BIGNUM numerator;
	BIGNUM denominator;
};

#define HAS_EXACTLY_ONE_PARENT(n) ((n)->parents && !(n)->parents->next)

static BN_CTX *context = NULL;
static struct fraction *one = NULL;
static struct fraction *zero = NULL;

static BN_CTX *get_BN_CTX(void)
{
	if (!context) {
		context = BN_CTX_new();
	}
	return context;
}

static struct fraction *new_zero(void)
{
	struct fraction *result = xmalloc(sizeof(*result));
	BN_init(&result->numerator);
	BN_init(&result->denominator);
	BN_zero(&result->numerator);
	BN_one(&result->denominator);
	return result;
}

static void clear_fraction(struct fraction *fraction)
{
	BN_clear(&fraction->numerator);
	BN_clear(&fraction->denominator);
}

static struct fraction *divide(struct fraction *result, struct fraction *fraction, int divisor)
{
	BIGNUM bn_divisor;

	BN_init(&bn_divisor);
	BN_set_word(&bn_divisor, divisor);

	BN_copy(&result->numerator, &fraction->numerator);
	BN_mul(&result->denominator, &fraction->denominator, &bn_divisor, get_BN_CTX());

	BN_clear(&bn_divisor);
	return result;
}

static struct fraction *init_fraction(struct fraction *fraction)
{
	BN_init(&fraction->numerator);
	BN_init(&fraction->denominator);
	BN_zero(&fraction->numerator);
	BN_one(&fraction->denominator);
	return fraction;
}

static struct fraction *get_one(void)
{
	if (!one) {
		one = new_zero();
		BN_one(&one->numerator);
	}
	return one;
}

static struct fraction *get_zero(void)
{
	if (!zero) {
		zero = new_zero();
	}
	return zero;
}

static struct fraction *copy(struct fraction *to, struct fraction *from)
{
	BN_copy(&to->numerator, &from->numerator);
	BN_copy(&to->denominator, &from->denominator);
	return to;
}

static struct fraction *add(struct fraction *result, struct fraction *left, struct fraction *right)
{
	BIGNUM a, b, gcd;

	BN_init(&a);
	BN_init(&b);
	BN_init(&gcd);

	BN_mul(&a, &left->numerator, &right->denominator, get_BN_CTX());
	BN_mul(&b, &left->denominator, &right->numerator, get_BN_CTX());
	BN_mul(&result->denominator, &left->denominator, &right->denominator, get_BN_CTX());
	BN_add(&result->numerator, &a, &b);

	BN_gcd(&gcd, &result->denominator, &result->numerator, get_BN_CTX());
	BN_div(&result->denominator, NULL, &result->denominator, &gcd, get_BN_CTX());
	BN_div(&result->numerator, NULL, &result->numerator, &gcd, get_BN_CTX());

	BN_clear(&a);
	BN_clear(&b);
	BN_clear(&gcd);

	return result;
}

static int compare(struct fraction *left, struct fraction *right)
{
	BIGNUM a, b;
	int result;

	BN_init(&a);
	BN_init(&b);

	BN_mul(&a, &left->numerator, &right->denominator, get_BN_CTX());
	BN_mul(&b, &left->denominator, &right->numerator, get_BN_CTX());

	result = BN_cmp(&a, &b);

	BN_clear(&a);
	BN_clear(&b);

	return result;
}

struct mass_counter {
	struct fraction seen;
	struct fraction pending;
};

static struct mass_counter *new_mass_counter(struct commit *commit, struct fraction *pending)
{
	struct mass_counter *mass_counter = xmalloc(sizeof(*mass_counter));
	memset(mass_counter, 0, sizeof(*mass_counter));

	init_fraction(&mass_counter->seen);
	init_fraction(&mass_counter->pending);

	copy(&mass_counter->pending, pending);
	copy(&mass_counter->seen, get_zero());

	if (commit->object.util) {
		die("multiple attempts to initialize mass counter for %s",
		    sha1_to_hex(commit->object.sha1));
	}

	commit->object.util = mass_counter;

	return mass_counter;
}

static void free_mass_counter(struct mass_counter *counter)
{
	clear_fraction(&counter->seen);
	clear_fraction(&counter->pending);
	free(counter);
}

/*
 * Finds the base commit of a list of commits.
 *
 * One property of the commit being searched for is that every commit reachable
 * from the base commit is reachable from the commits in the starting list only
 * via paths that include the base commit.
 *
 * This algorithm uses a conservation of mass approach to find the base commit.
 *
 * We start by injecting one unit of mass into the graph at each
 * of the commits in the starting list. Injecting mass into a commit
 * is achieved by adding to its pending mass counter and, if it is not already
 * enqueued, enqueuing the commit in a list of pending commits, in latest
 * commit date first order.
 *
 * The algorithm then preceeds to visit each commit in the pending queue.
 * Upon each visit, the pending mass is added to the mass already seen for that
 * commit and then divided into N equal portions, where N is the number of
 * parents of the commit being visited. The divided portions are then injected
 * into each of the parents.
 *
 * The algorithm continues until we discover a commit which has seen all the
 * mass originally injected or until we run out of things to do.
 *
 * If we find a commit that has seen all the original mass, we have found
 * the common base of all the commits in the starting list.
 *
 * The algorithm does _not_ depend on accurate timestamps for correct operation.
 * However, reasonably sane (e.g. non-random) timestamps are required in order
 * to prevent an exponential performance characteristic. The occasional
 * timestamp inaccuracy will not dramatically affect performance but may
 * result in more nodes being processed than strictly necessary.
 *
 * This procedure sets *boundary to the address of the base commit. It returns
 * non-zero if, and only if, there was a problem parsing one of the
 * commits discovered during the traversal.
 */
static int find_base_for_list(struct commit_list *list, struct commit **boundary)
{
	int ret = 0;
	struct commit_list *cleaner = NULL;
	struct commit_list *pending = NULL;
	struct fraction injected;
	init_fraction(&injected);
	*boundary = NULL;

	for (; list; list = list->next) {
		struct commit *item = list->item;

		if (!item->object.util) {
			new_mass_counter(list->item, get_one());
			add(&injected, &injected, get_one());

			commit_list_insert(list->item, &cleaner);
			commit_list_insert(list->item, &pending);
		}
	}

	while (!*boundary && pending && !ret) {
		struct commit *latest = pop_commit(&pending);
		struct mass_counter *latest_node = (struct mass_counter *) latest->object.util;
		int num_parents;

		if ((ret = parse_commit(latest)))
			continue;
		add(&latest_node->seen, &latest_node->seen, &latest_node->pending);

		num_parents = count_parents(latest);
		if (num_parents) {
			struct fraction distribution;
			struct commit_list *parents;

			divide(init_fraction(&distribution), &latest_node->pending, num_parents);

			for (parents = latest->parents; parents; parents = parents->next) {
				struct commit *parent = parents->item;
				struct mass_counter *parent_node = (struct mass_counter *) parent->object.util;

				if (!parent_node) {
					parent_node = new_mass_counter(parent, &distribution);
					insert_by_date(parent, &pending);
					commit_list_insert(parent, &cleaner);
				} else {
					if (!compare(&parent_node->pending, get_zero()))
						insert_by_date(parent, &pending);
					add(&parent_node->pending, &parent_node->pending, &distribution);
				}
			}

			clear_fraction(&distribution);
		}

		if (!compare(&latest_node->seen, &injected))
			*boundary = latest;
		copy(&latest_node->pending, get_zero());
	}

	while (cleaner) {
		struct commit *next = pop_commit(&cleaner);
		free_mass_counter((struct mass_counter *) next->object.util);
		next->object.util = NULL;
	}

	if (pending)
		free_commit_list(pending);

	clear_fraction(&injected);
	return ret;
}


/*
 * Finds the base of an minimal, non-linear epoch, headed at head, by
 * applying the find_base_for_list to a list consisting of the parents
 */
static int find_base(struct commit *head, struct commit **boundary)
{
	int ret = 0;
	struct commit_list *pending = NULL;
	struct commit_list *next;

	for (next = head->parents; next; next = next->next) {
		commit_list_insert(next->item, &pending);
	}
	ret = find_base_for_list(pending, boundary);
	free_commit_list(pending);

	return ret;
}

/*
 * This procedure traverses to the boundary of the first epoch in the epoch
 * sequence of the epoch headed at head_of_epoch. This is either the end of
 * the maximal linear epoch or the base of a minimal non-linear epoch.
 *
 * The queue of pending nodes is sorted in reverse date order and each node
 * is currently in the queue at most once.
 */
static int find_next_epoch_boundary(struct commit *head_of_epoch, struct commit **boundary)
{
	int ret;
	struct commit *item = head_of_epoch;

	ret = parse_commit(item);
	if (ret)
		return ret;

	if (HAS_EXACTLY_ONE_PARENT(item)) {
		/*
		 * We are at the start of a maximimal linear epoch.
		 * Traverse to the end.
		 */
		while (HAS_EXACTLY_ONE_PARENT(item) && !ret) {
			item = item->parents->item;
			ret = parse_commit(item);
		}
		*boundary = item;

	} else {
		/*
		 * Otherwise, we are at the start of a minimal, non-linear
		 * epoch - find the common base of all parents.
		 */
		ret = find_base(item, boundary);
	}

	return ret;
}

/*
 * Returns non-zero if parent is known to be a parent of child.
 */
static int is_parent_of(struct commit *parent, struct commit *child)
{
	struct commit_list *parents;
	for (parents = child->parents; parents; parents = parents->next) {
		if (!memcmp(parent->object.sha1, parents->item->object.sha1,
		            sizeof(parents->item->object.sha1)))
			return 1;
	}
	return 0;
}

/*
 * Pushes an item onto the merge order stack. If the top of the stack is
 * marked as being a possible "break", we check to see whether it actually
 * is a break.
 */
static void push_onto_merge_order_stack(struct commit_list **stack, struct commit *item)
{
	struct commit_list *top = *stack;
	if (top && (top->item->object.flags & DISCONTINUITY)) {
		if (is_parent_of(top->item, item)) {
			top->item->object.flags &= ~DISCONTINUITY;
		}
	}
	commit_list_insert(item, stack);
}

/*
 * Marks all interesting, visited commits reachable from this commit
 * as uninteresting. We stop recursing when we reach the epoch boundary,
 * an unvisited node or a node that has already been marking uninteresting.
 *
 * This doesn't actually mark all ancestors between the start node and the
 * epoch boundary uninteresting, but does ensure that they will eventually
 * be marked uninteresting when the main sort_first_epoch() traversal
 * eventually reaches them.
 */
static void mark_ancestors_uninteresting(struct commit *commit)
{
	unsigned int flags = commit->object.flags;
	int visited = flags & VISITED;
	int boundary = flags & BOUNDARY;
	int uninteresting = flags & UNINTERESTING;
	struct commit_list *next;

	commit->object.flags |= UNINTERESTING;

	/*
	 * We only need to recurse if
	 *      we are not on the boundary and
	 *      we have not already been marked uninteresting and
	 *      we have already been visited.
	 *
	 * The main sort_first_epoch traverse will mark unreachable
	 * all uninteresting, unvisited parents as they are visited
	 * so there is no need to duplicate that traversal here.
	 *
	 * Similarly, if we are already marked uninteresting
	 * then either all ancestors have already been marked
	 * uninteresting or will be once the sort_first_epoch
	 * traverse reaches them.
	 */

	if (uninteresting || boundary || !visited)
		return;

	for (next = commit->parents; next; next = next->next)
		mark_ancestors_uninteresting(next->item);
}

/*
 * Sorts the nodes of the first epoch of the epoch sequence of the epoch headed at head
 * into merge order.
 */
static void sort_first_epoch(struct commit *head, struct commit_list **stack)
{
	struct commit_list *parents;

	head->object.flags |= VISITED;

	/*
	 * TODO: By sorting the parents in a different order, we can alter the
	 * merge order to show contemporaneous changes in parallel branches
	 * occurring after "local" changes. This is useful for a developer
	 * when a developer wants to see all changes that were incorporated
	 * into the same merge as her own changes occur after her own
	 * changes.
	 */

	for (parents = head->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;

		if (head->object.flags & UNINTERESTING) {
			/*
			 * Propagates the uninteresting bit to all parents.
			 * if we have already visited this parent, then
			 * the uninteresting bit will be propagated to each
			 * reachable commit that is still not marked
			 * uninteresting and won't otherwise be reached.
			 */
			mark_ancestors_uninteresting(parent);
		}

		if (!(parent->object.flags & VISITED)) {
			if (parent->object.flags & BOUNDARY) {
				if (*stack) {
					die("something else is on the stack - %s",
					    sha1_to_hex((*stack)->item->object.sha1));
				}
				push_onto_merge_order_stack(stack, parent);
				parent->object.flags |= VISITED;

			} else {
				sort_first_epoch(parent, stack);
				if (parents) {
					/*
					 * This indicates a possible
					 * discontinuity it may not be be
					 * actual discontinuity if the head
					 * of parent N happens to be the tail
					 * of parent N+1.
					 *
					 * The next push onto the stack will
					 * resolve the question.
					 */
					(*stack)->item->object.flags |= DISCONTINUITY;
				}
			}
		}
	}

	push_onto_merge_order_stack(stack, head);
}

/*
 * Emit the contents of the stack.
 *
 * The stack is freed and replaced by NULL.
 *
 * Sets the return value to STOP if no further output should be generated.
 */
static int emit_stack(struct commit_list **stack, emitter_func emitter, int include_last)
{
	unsigned int seen = 0;
	int action = CONTINUE;

	while (*stack && (action != STOP)) {
		struct commit *next = pop_commit(stack);
		seen |= next->object.flags;
		if (*stack || include_last) {
			if (!*stack) 
				next->object.flags |= BOUNDARY;
			action = emitter(next);
		}
	}

	if (*stack) {
		free_commit_list(*stack);
		*stack = NULL;
	}

	return (action == STOP || (seen & UNINTERESTING)) ? STOP : CONTINUE;
}

/*
 * Sorts an arbitrary epoch into merge order by sorting each epoch
 * of its epoch sequence into order.
 *
 * Note: this algorithm currently leaves traces of its execution in the
 * object flags of nodes it discovers. This should probably be fixed.
 */
static int sort_in_merge_order(struct commit *head_of_epoch, emitter_func emitter)
{
	struct commit *next = head_of_epoch;
	int ret = 0;
	int action = CONTINUE;

	ret = parse_commit(head_of_epoch);

	next->object.flags |= BOUNDARY;

	while (next && next->parents && !ret && (action != STOP)) {
		struct commit *base = NULL;

		ret = find_next_epoch_boundary(next, &base);
		if (ret)
			return ret;
		next->object.flags |= BOUNDARY;
		if (base)
			base->object.flags |= BOUNDARY;

		if (HAS_EXACTLY_ONE_PARENT(next)) {
			while (HAS_EXACTLY_ONE_PARENT(next)
			       && (action != STOP)
			       && !ret) {
				if (next->object.flags & UNINTERESTING) {
					action = STOP;
				} else {
					action = emitter(next);
				}
				if (action != STOP) {
					next = next->parents->item;
					ret = parse_commit(next);
				}
			}

		} else {
			struct commit_list *stack = NULL;
			sort_first_epoch(next, &stack);
			action = emit_stack(&stack, emitter, (base == NULL));
			next = base;
		}
	}

	if (next && (action != STOP) && !ret) {
		emitter(next);
	}

	return ret;
}

/*
 * Sorts the nodes reachable from a starting list in merge order, we
 * first find the base for the starting list and then sort all nodes
 * in this subgraph using the sort_first_epoch algorithm. Once we have
 * reached the base we can continue sorting using sort_in_merge_order.
 */
int sort_list_in_merge_order(struct commit_list *list, emitter_func emitter)
{
	struct commit_list *stack = NULL;
	struct commit *base;
	int ret = 0;
	int action = CONTINUE;
	struct commit_list *reversed = NULL;

	for (; list; list = list->next)
		commit_list_insert(list->item, &reversed);

	if (!reversed)
		return ret;
	else if (!reversed->next) {
		/*
		 * If there is only one element in the list, we can sort it
		 * using sort_in_merge_order.
		 */
		base = reversed->item;
	} else {
		/*
		 * Otherwise, we search for the base of the list.
		 */
		ret = find_base_for_list(reversed, &base);
		if (ret)
			return ret;
		if (base)
			base->object.flags |= BOUNDARY;

		while (reversed) {
			struct commit * next = pop_commit(&reversed);

			if (!(next->object.flags & VISITED) && next!=base) {
				sort_first_epoch(next, &stack);
				if (reversed) {
					/*
					 * If we have more commits 
					 * to push, then the first
					 * push for the next parent may 
					 * (or may * not) represent a 
					 * discontinuity with respect
					 * to the parent currently on 
					 * the top of the stack.
					 *
					 * Mark it for checking here, 
					 * and check it with the next 
					 * push. See sort_first_epoch()
					 * for more details.
					 */
					stack->item->object.flags |= DISCONTINUITY;
				}
			}
		}

		action = emit_stack(&stack, emitter, (base==NULL));
	}

	if (base && (action != STOP)) {
		ret = sort_in_merge_order(base, emitter);
	}

	return ret;
}
