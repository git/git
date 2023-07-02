#include "git-compat-util.h"
#include "skipping.h"
#include "../commit.h"
#include "../fetch-negotiator.h"
#include "../hex.h"
#include "../prio-queue.h"
#include "../refs.h"
#include "../repository.h"
#include "../tag.h"

/* Remember to update object flag allocation in object.h */
/*
 * Both us and the server know that both parties have this object.
 */
#define COMMON		(1U << 2)
/*
 * The server has told us that it has this object. We still need to tell the
 * server that we have this object (or one of its descendants), but since we are
 * going to do that, we do not need to tell the server about its ancestors.
 */
#define ADVERTISED	(1U << 3)
/*
 * This commit has entered the priority queue.
 */
#define SEEN		(1U << 4)
/*
 * This commit has left the priority queue.
 */
#define POPPED		(1U << 5)

static int marked;

/*
 * An entry in the priority queue.
 */
struct entry {
	struct commit *commit;

	/*
	 * Used only if commit is not COMMON.
	 */
	uint16_t original_ttl;
	uint16_t ttl;
};

struct data {
	struct prio_queue rev_list;

	/*
	 * The number of non-COMMON commits in rev_list.
	 */
	int non_common_revs;
};

static int compare(const void *a_, const void *b_, void *data UNUSED)
{
	const struct entry *a = a_;
	const struct entry *b = b_;
	return compare_commits_by_commit_date(a->commit, b->commit, NULL);
}

static struct entry *rev_list_push(struct data *data, struct commit *commit, int mark)
{
	struct entry *entry;
	commit->object.flags |= mark | SEEN;

	CALLOC_ARRAY(entry, 1);
	entry->commit = commit;
	prio_queue_put(&data->rev_list, entry);

	if (!(mark & COMMON))
		data->non_common_revs++;
	return entry;
}

static int clear_marks(const char *refname, const struct object_id *oid,
		       int flag UNUSED,
		       void *cb_data UNUSED)
{
	struct object *o = deref_tag(the_repository, parse_object(the_repository, oid), refname, 0);

	if (o && o->type == OBJ_COMMIT)
		clear_commit_marks((struct commit *)o,
				   COMMON | ADVERTISED | SEEN | POPPED);
	return 0;
}

/*
 * Mark this SEEN commit and all its parsed SEEN ancestors as COMMON.
 */
static void mark_common(struct data *data, struct commit *seen_commit)
{
	struct prio_queue queue = { NULL };
	struct commit *c;

	if (seen_commit->object.flags & COMMON)
		return;

	prio_queue_put(&queue, seen_commit);
	seen_commit->object.flags |= COMMON;
	while ((c = prio_queue_get(&queue))) {
		struct commit_list *p;

		if (!(c->object.flags & POPPED))
			data->non_common_revs--;

		if (!c->object.parsed)
			continue;
		for (p = c->parents; p; p = p->next) {
			if (!(p->item->object.flags & SEEN) ||
			    (p->item->object.flags & COMMON))
				continue;

			p->item->object.flags |= COMMON;
			prio_queue_put(&queue, p->item);
		}
	}

	clear_prio_queue(&queue);
}

/*
 * Ensure that the priority queue has an entry for to_push, and ensure that the
 * entry has the correct flags and ttl.
 *
 * This function returns 1 if an entry was found or created, and 0 otherwise
 * (because the entry for this commit had already been popped).
 */
static int push_parent(struct data *data, struct entry *entry,
		       struct commit *to_push)
{
	struct entry *parent_entry;

	if (to_push->object.flags & SEEN) {
		int i;
		if (to_push->object.flags & POPPED)
			/*
			 * The entry for this commit has already been popped,
			 * due to clock skew. Pretend that this parent does not
			 * exist.
			 */
			return 0;
		/*
		 * Find the existing entry and use it.
		 */
		for (i = 0; i < data->rev_list.nr; i++) {
			parent_entry = data->rev_list.array[i].data;
			if (parent_entry->commit == to_push)
				goto parent_found;
		}
		BUG("missing parent in priority queue");
parent_found:
		;
	} else {
		parent_entry = rev_list_push(data, to_push, 0);
	}

	if (entry->commit->object.flags & (COMMON | ADVERTISED)) {
		mark_common(data, to_push);
	} else {
		uint16_t new_original_ttl = entry->ttl
			? entry->original_ttl : entry->original_ttl * 3 / 2 + 1;
		uint16_t new_ttl = entry->ttl
			? entry->ttl - 1 : new_original_ttl;
		if (parent_entry->original_ttl < new_original_ttl) {
			parent_entry->original_ttl = new_original_ttl;
			parent_entry->ttl = new_ttl;
		}
	}

	return 1;
}

static const struct object_id *get_rev(struct data *data)
{
	struct commit *to_send = NULL;

	while (to_send == NULL) {
		struct entry *entry;
		struct commit *commit;
		struct commit_list *p;
		int parent_pushed = 0;

		if (data->rev_list.nr == 0 || data->non_common_revs == 0)
			return NULL;

		entry = prio_queue_get(&data->rev_list);
		commit = entry->commit;
		commit->object.flags |= POPPED;
		if (!(commit->object.flags & COMMON))
			data->non_common_revs--;

		if (!(commit->object.flags & COMMON) && !entry->ttl)
			to_send = commit;

		repo_parse_commit(the_repository, commit);
		for (p = commit->parents; p; p = p->next)
			parent_pushed |= push_parent(data, entry, p->item);

		if (!(commit->object.flags & COMMON) && !parent_pushed)
			/*
			 * This commit has no parents, or all of its parents
			 * have already been popped (due to clock skew), so send
			 * it anyway.
			 */
			to_send = commit;

		free(entry);
	}

	return &to_send->object.oid;
}

static void known_common(struct fetch_negotiator *n, struct commit *c)
{
	if (c->object.flags & SEEN)
		return;
	rev_list_push(n->data, c, ADVERTISED);
}

static void add_tip(struct fetch_negotiator *n, struct commit *c)
{
	n->known_common = NULL;
	if (c->object.flags & SEEN)
		return;
	rev_list_push(n->data, c, 0);
}

static const struct object_id *next(struct fetch_negotiator *n)
{
	n->known_common = NULL;
	n->add_tip = NULL;
	return get_rev(n->data);
}

static int ack(struct fetch_negotiator *n, struct commit *c)
{
	int known_to_be_common = !!(c->object.flags & COMMON);
	if (!(c->object.flags & SEEN))
		die("received ack for commit %s not sent as 'have'\n",
		    oid_to_hex(&c->object.oid));
	mark_common(n->data, c);
	return known_to_be_common;
}

static void release(struct fetch_negotiator *n)
{
	clear_prio_queue(&((struct data *)n->data)->rev_list);
	FREE_AND_NULL(n->data);
}

void skipping_negotiator_init(struct fetch_negotiator *negotiator)
{
	struct data *data;
	negotiator->known_common = known_common;
	negotiator->add_tip = add_tip;
	negotiator->next = next;
	negotiator->ack = ack;
	negotiator->release = release;
	negotiator->data = CALLOC_ARRAY(data, 1);
	data->rev_list.compare = compare;

	if (marked)
		for_each_ref(clear_marks, NULL);
	marked = 1;
}
