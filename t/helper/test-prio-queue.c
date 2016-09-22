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

int cmd_main(int argc, const char **argv)
{
	struct prio_queue pq = { intcmp };

	while (*++argv) {
		if (!strcmp(*argv, "get"))
			show(prio_queue_get(&pq));
		else if (!strcmp(*argv, "dump")) {
			int *v;
			while ((v = prio_queue_get(&pq)))
			       show(v);
		}
		else {
			int *v = malloc(sizeof(*v));
			*v = atoi(*argv);
			prio_queue_put(&pq, v);
		}
	}

	return 0;
}
