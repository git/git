/*
 * test-treap.c: code to exercise the svn importer's treap structure
 */

#include "cache.h"
#include "vcs-svn/obj_pool.h"
#include "vcs-svn/trp.h"

struct int_node {
	uintmax_t n;
	struct trp_node children;
};

obj_pool_gen(node, struct int_node, 3)

static int node_cmp(struct int_node *a, struct int_node *b)
{
	return (a->n > b->n) - (a->n < b->n);
}

trp_gen(static, treap_, struct int_node, children, node, node_cmp)

static void strtonode(struct int_node *item, const char *s)
{
	char *end;
	item->n = strtoumax(s, &end, 10);
	if (*s == '\0' || (*end != '\n' && *end != '\0'))
		die("invalid integer: %s", s);
}

int main(int argc, char *argv[])
{
	struct strbuf sb = STRBUF_INIT;
	struct trp_root root = { ~0U };
	uint32_t item;

	if (argc != 1)
		usage("test-treap < ints");

	while (strbuf_getline(&sb, stdin, '\n') != EOF) {
		struct int_node *node = node_pointer(node_alloc(1));

		item = node_offset(node);
		strtonode(node, sb.buf);
		node = treap_insert(&root, node_pointer(item));
		if (node_offset(node) != item)
			die("inserted %"PRIu32" in place of %"PRIu32"",
				node_offset(node), item);
	}

	item = node_offset(treap_first(&root));
	while (~item) {
		uint32_t next;
		struct int_node *tmp = node_pointer(node_alloc(1));

		tmp->n = node_pointer(item)->n;
		next = node_offset(treap_next(&root, node_pointer(item)));

		treap_remove(&root, node_pointer(item));
		item = node_offset(treap_nsearch(&root, tmp));

		if (item != next && (!~item || node_pointer(item)->n != tmp->n))
			die("found %"PRIuMAX" in place of %"PRIuMAX"",
				~item ? node_pointer(item)->n : ~(uintmax_t) 0,
				~next ? node_pointer(next)->n : ~(uintmax_t) 0);
		printf("%"PRIuMAX"\n", tmp->n);
	}
	node_reset();
	return 0;
}
