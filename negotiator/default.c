#include "cache.h"
#include "default.h"
#include "../commit.h"
#include "../fetch-negotiator.h"
#include "../prio-queue.h"
#include "../refs.h"
#include "../tag.h"

/* Remember to update object flag allocation in object.h */
#define COMMON		(1U << 2)
#define COMMON_REF	(1U << 3)
#define SEEN		(1U << 4)
#define POPPED		(1U << 5)

static int marked;

struct negotiation_state {
	struct prio_queue rev_list;
	int non_common_revs;
};

static void rev_list_push(struct negotiation_state *ns,
			  struct commit *commit, int mark)
{
	if (!(commit->object.flags & mark)) {
		commit->object.flags |= mark;

		if (repo_parse_commit(the_repository, commit))
			return;

		prio_queue_put(&ns->rev_list, commit);

		if (!(commit->object.flags & COMMON))
			ns->non_common_revs++;
	}
}

static int clear_marks(const char *refname, const struct object_id *oid,
		       int flag UNUSED,
		       void *cb_data UNUSED)
{
	struct object *o = deref_tag(the_repository, parse_object(the_repository, oid), refname, 0);

	if (o && o->type == OBJ_COMMIT)
		clear_commit_marks((struct commit *)o,
				   COMMON | COMMON_REF | SEEN | POPPED);
	return 0;
}

/*
 * This function marks a rev and its ancestors as common.
 * In some cases, it is desirable to mark only the ancestors (for example
 * when only the server does not yet know that they are common).
 */
static void mark_common(struct negotiation_state *ns, struct commit *commit,
		int ancestors_only, int dont_parse)
{
	if (commit != NULL && !(commit->object.flags & COMMON)) {
		struct object *o = (struct object *)commit;

		if (!ancestors_only)
			o->flags |= COMMON;

		if (!(o->flags & SEEN))
			rev_list_push(ns, commit, SEEN);
		else {
			struct commit_list *parents;

			if (!ancestors_only && !(o->flags & POPPED))
				ns->non_common_revs--;
			if (!o->parsed && !dont_parse)
				if (repo_parse_commit(the_repository, commit))
					return;

			for (parents = commit->parents;
					parents;
					parents = parents->next)
				mark_common(ns, parents->item, 0,
					    dont_parse);
		}
	}
}

/*
 * Get the next rev to send, ignoring the common.
 */
static const struct object_id *get_rev(struct negotiation_state *ns)
{
	struct commit *commit = NULL;

	while (commit == NULL) {
		unsigned int mark;
		struct commit_list *parents;

		if (ns->rev_list.nr == 0 || ns->non_common_revs == 0)
			return NULL;

		commit = prio_queue_get(&ns->rev_list);
		repo_parse_commit(the_repository, commit);
		parents = commit->parents;

		commit->object.flags |= POPPED;
		if (!(commit->object.flags & COMMON))
			ns->non_common_revs--;

		if (commit->object.flags & COMMON) {
			/* do not send "have", and ignore ancestors */
			commit = NULL;
			mark = COMMON | SEEN;
		} else if (commit->object.flags & COMMON_REF)
			/* send "have", and ignore ancestors */
			mark = COMMON | SEEN;
		else
			/* send "have", also for its ancestors */
			mark = SEEN;

		while (parents) {
			if (!(parents->item->object.flags & SEEN))
				rev_list_push(ns, parents->item, mark);
			if (mark & COMMON)
				mark_common(ns, parents->item, 1, 0);
			parents = parents->next;
		}
	}

	return &commit->object.oid;
}

static void known_common(struct fetch_negotiator *n, struct commit *c)
{
	if (!(c->object.flags & SEEN)) {
		rev_list_push(n->data, c, COMMON_REF | SEEN);
		mark_common(n->data, c, 1, 1);
	}
}

static void add_tip(struct fetch_negotiator *n, struct commit *c)
{
	n->known_common = NULL;
	rev_list_push(n->data, c, SEEN);
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
	mark_common(n->data, c, 0, 1);
	return known_to_be_common;
}

static void release(struct fetch_negotiator *n)
{
	clear_prio_queue(&((struct negotiation_state *)n->data)->rev_list);
	FREE_AND_NULL(n->data);
}

void default_negotiator_init(struct fetch_negotiator *negotiator)
{
	struct negotiation_state *ns;
	negotiator->known_common = known_common;
	negotiator->add_tip = add_tip;
	negotiator->next = next;
	negotiator->ack = ack;
	negotiator->release = release;
	negotiator->data = CALLOC_ARRAY(ns, 1);
	ns->rev_list.compare = compare_commits_by_commit_date;

	if (marked)
		for_each_ref(clear_marks, NULL);
	marked = 1;
}
