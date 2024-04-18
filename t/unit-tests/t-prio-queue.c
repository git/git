#include "test-lib.h"
#include "prio-queue.h"

static int intcmp(const void *va, const void *vb, void *data UNUSED)
{
	const int *a = va, *b = vb;
	return *a - *b;
}


#define MISSING  -1
#define DUMP	 -2
#define STACK	 -3
#define GET	 -4
#define REVERSE  -5

static int show(int *v)
{
	return v ? *v : MISSING;
}

static void test_prio_queue(int *input, size_t input_size,
			    int *result, size_t result_size)
{
	struct prio_queue pq = { intcmp };
	int j = 0;

	for (int i = 0; i < input_size; i++) {
		void *peek, *get;
		switch(input[i]) {
		case GET:
			peek = prio_queue_peek(&pq);
			get = prio_queue_get(&pq);
			if (!check(peek == get))
				return;
			if (!check_uint(j, <, result_size))
				break;
			if (!check_int(result[j], ==, show(get)))
				test_msg("      j: %d", j);
			j++;
			break;
		case DUMP:
			while ((peek = prio_queue_peek(&pq))) {
				get = prio_queue_get(&pq);
				if (!check(peek == get))
					return;
				if (!check_uint(j, <, result_size))
					break;
				if (!check_int(result[j], ==, show(get)))
					test_msg("      j: %d", j);
				j++;
			}
			break;
		case STACK:
			pq.compare = NULL;
			break;
		case REVERSE:
			prio_queue_reverse(&pq);
			break;
		default:
			prio_queue_put(&pq, &input[i]);
			break;
		}
	}
	check_uint(j, ==, result_size);
	clear_prio_queue(&pq);
}

#define TEST_INPUT(input, result) \
	test_prio_queue(input, ARRAY_SIZE(input), result, ARRAY_SIZE(result))

int cmd_main(int argc, const char **argv)
{
	TEST(TEST_INPUT(((int []){ 2, 6, 3, 10, 9, 5, 7, 4, 5, 8, 1, DUMP }),
			((int []){ 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10 })),
	     "prio-queue works for basic input");
	TEST(TEST_INPUT(((int []){ 6, 2, 4, GET, 5, 3, GET, GET, 1, DUMP }),
			((int []){ 2, 3, 4, 1, 5, 6 })),
	     "prio-queue works for mixed put & get commands");
	TEST(TEST_INPUT(((int []){ 1, 2, GET, GET, GET, 1, 2, GET, GET, GET }),
			((int []){ 1, 2, MISSING, 1, 2, MISSING })),
	     "prio-queue works when queue is empty");
	TEST(TEST_INPUT(((int []){ STACK, 8, 1, 5, 4, 6, 2, 3, DUMP }),
			((int []){ 3, 2, 6, 4, 5, 1, 8 })),
	     "prio-queue works when used as a LIFO stack");
	TEST(TEST_INPUT(((int []){ STACK, 1, 2, 3, 4, 5, 6, REVERSE, DUMP }),
			((int []){ 1, 2, 3, 4, 5, 6 })),
	     "prio-queue works when LIFO stack is reversed");

	return test_done();
}
