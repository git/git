#include "test-tool.h"
#include "cache.h"
#include "prio-queue.h"

static int intcmp(const void *va, const void *vb, void *data)
{
	const int *a = va, *b = vb;
	return *a - *b;
}

static void show(int *v)
{
	if (!v)
		printf("NULL\n");
	else
		printf("%d\n", *v);
	free(v);
}

int cmd__prio_queue(int argc, const char **argv)
{
	struct prio_queue pq = { intcmp };

	while (*++argv) {
		if (!strcmp(*argv, "get")) {
			void *peek = prio_queue_peek(&pq);
			void *get = prio_queue_get(&pq);
			if (peek != get)
				BUG("peek and get results do not match");
			show(get);
		} else if (!strcmp(*argv, "dump")) {
			void *peek;
			void *get;
			while ((peek = prio_queue_peek(&pq))) {
				get = prio_queue_get(&pq);
				if (peek != get)
					BUG("peek and get results do not match");
				show(get);
			}
		} else if (!strcmp(*argv, "stack")) {
			pq.compare = NULL;
		} else {
			int *v = xmalloc(sizeof(*v));
			*v = atoi(*argv);
			prio_queue_put(&pq, v);
		}
	}

	return 0;
}
