#include "test-tool.h"
#include "cache.h"
#include "object.h"
#include "decorate.h"

int cmd__example_decorate(int argc, const char **argv)
{
	struct decoration n;
	struct object_id one_oid = { {1} };
	struct object_id two_oid = { {2} };
	struct object_id three_oid = { {3} };
	struct object *one, *two, *three;

	int decoration_a, decoration_b;

	void *ret;

	int i, objects_noticed = 0;

	/*
	 * The struct must be zero-initialized.
	 */
	memset(&n, 0, sizeof(n));

	/*
	 * Add 2 objects, one with a non-NULL decoration and one with a NULL
	 * decoration.
	 */
	one = lookup_unknown_object(the_repository, &one_oid);
	two = lookup_unknown_object(the_repository, &two_oid);
	ret = add_decoration(&n, one, &decoration_a);
	if (ret)
		BUG("when adding a brand-new object, NULL should be returned");
	ret = add_decoration(&n, two, NULL);
	if (ret)
		BUG("when adding a brand-new object, NULL should be returned");

	/*
	 * When re-adding an already existing object, the old decoration is
	 * returned.
	 */
	ret = add_decoration(&n, one, NULL);
	if (ret != &decoration_a)
		BUG("when readding an already existing object, existing decoration should be returned");
	ret = add_decoration(&n, two, &decoration_b);
	if (ret)
		BUG("when readding an already existing object, existing decoration should be returned");

	/*
	 * Lookup returns the added declarations, or NULL if the object was
	 * never added.
	 */
	ret = lookup_decoration(&n, one);
	if (ret)
		BUG("lookup should return added declaration");
	ret = lookup_decoration(&n, two);
	if (ret != &decoration_b)
		BUG("lookup should return added declaration");
	three = lookup_unknown_object(the_repository, &three_oid);
	ret = lookup_decoration(&n, three);
	if (ret)
		BUG("lookup for unknown object should return NULL");

	/*
	 * The user can also loop through all entries.
	 */
	for (i = 0; i < n.size; i++) {
		if (n.entries[i].base)
			objects_noticed++;
	}
	if (objects_noticed != 2)
		BUG("should have 2 objects");

	return 0;
}
