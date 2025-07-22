#include "unit-test.h"
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
	size_t j = 0;

	for (size_t i = 0; i < input_size; i++) {
		void *peek, *get;
		switch(input[i]) {
		case GET:
			peek = prio_queue_peek(&pq);
			get = prio_queue_get(&pq);
			cl_assert(peek == get);
			cl_assert(j < result_size);
			cl_assert_equal_i(result[j], show(get));
			j++;
			break;
		case DUMP:
			while ((peek = prio_queue_peek(&pq))) {
				get = prio_queue_get(&pq);
				cl_assert(peek == get);
				cl_assert(j < result_size);
				cl_assert_equal_i(result[j], show(get));
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
	cl_assert_equal_i(j, result_size);
	clear_prio_queue(&pq);
}

#define TEST_INPUT(input, result) \
	test_prio_queue(input, ARRAY_SIZE(input), result, ARRAY_SIZE(result))

void test_prio_queue__basic(void)
{
	TEST_INPUT(((int []){ 2, 6, 3, 10, 9, 5, 7, 4, 5, 8, 1, DUMP }),
		   ((int []){ 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10 }));
}

void test_prio_queue__mixed(void)
{
	TEST_INPUT(((int []){ 6, 2, 4, GET, 5, 3, GET, GET, 1, DUMP }),
		   ((int []){ 2, 3, 4, 1, 5, 6 }));
}

void test_prio_queue__empty(void)
{
	TEST_INPUT(((int []){ 1, 2, GET, GET, GET, 1, 2, GET, GET, GET }),
		   ((int []){ 1, 2, MISSING, 1, 2, MISSING }));
}

void test_prio_queue__stack(void)
{
	TEST_INPUT(((int []){ STACK, 8, 1, 5, 4, 6, 2, 3, DUMP }),
		   ((int []){ 3, 2, 6, 4, 5, 1, 8 }));
}

void test_prio_queue__reverse_stack(void)
{
	TEST_INPUT(((int []){ STACK, 1, 2, 3, 4, 5, 6, REVERSE, DUMP }),
		   ((int []){ 1, 2, 3, 4, 5, 6 }));
}
