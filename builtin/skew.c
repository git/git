#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"

unsigned long worst_skew = 0;

static void check_skew_recurse(struct commit *c, unsigned long when)
{
	struct commit_list *p;

	if (c->object.flags & SEEN)
		return;
	c->object.flags |= SEEN;

	if (parse_commit(c) < 0)
		return;

	if (c->date > when) {
		unsigned long skew = c->date - when;
		if (skew > worst_skew)
			worst_skew = skew;
	}

	for (p = c->parents; p; p = p->next)
		check_skew_recurse(p->item, c->date < when ? c->date : when);
}

static void check_skew(struct commit *c)
{
	check_skew_recurse(c, time(NULL));
}

int cmd_skew(int argc, const char **argv, const char *prefix) {
	struct rev_info revs;
	int i;

	git_config(git_default_config, NULL);
	init_revisions(&revs, prefix);
	argc = setup_revisions(argc, argv, &revs, NULL);

	for (i = 0; i < revs.pending.nr; i++) {
		struct object *o = revs.pending.objects[i].item;
		if (o->type == OBJ_COMMIT)
			check_skew((struct commit *)o);
	}

	printf("%lu\n", worst_skew);
	return 0;
}
