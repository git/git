#include "cache.h"
#include "noop.h"
#include "../commit.h"
#include "../fetch-negotiator.h"

static void known_common(struct fetch_negotiator *n, struct commit *c)
{
	/* do nothing */
}

static void add_tip(struct fetch_negotiator *n, struct commit *c)
{
	/* do nothing */
}

static const struct object_id *next(struct fetch_negotiator *n)
{
	return NULL;
}

static int ack(struct fetch_negotiator *n, struct commit *c)
{
	/*
	 * This negotiator does not emit any commits, so there is no commit to
	 * be acknowledged. If there is any ack, there is a bug.
	 */
	BUG("ack with noop negotiator, which does not emit any commits");
	return 0;
}

static void release(struct fetch_negotiator *n)
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
