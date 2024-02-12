#include "git-compat-util.h"
#include "noop.h"
#include "../fetch-negotiator.h"

static void known_common(struct fetch_negotiator *n UNUSED,
			 struct commit *c UNUSED)
{
	/* do nothing */
}

static void add_tip(struct fetch_negotiator *n UNUSED,
		    struct commit *c UNUSED)
{
	/* do nothing */
}

static const struct object_id *next(struct fetch_negotiator *n UNUSED)
{
	return NULL;
}

static int ack(struct fetch_negotiator *n UNUSED, struct commit *c UNUSED)
{
	/*
	 * This negotiator does not emit any commits, so there is no commit to
	 * be acknowledged. If there is any ack, there is a bug.
	 */
	BUG("ack with noop negotiator, which does not emit any commits");
	return 0;
}

static void release(struct fetch_negotiator *n UNUSED)
{
	/* nothing to release */
}

void noop_negotiator_init(struct fetch_negotiator *negotiator)
{
	negotiator->known_common = known_common;
	negotiator->add_tip = add_tip;
	negotiator->next = next;
	negotiator->ack = ack;
	negotiator->release = release;
	negotiator->data = NULL;
}
