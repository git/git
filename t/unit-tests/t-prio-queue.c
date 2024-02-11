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

static void test_prio_queue(int *input, int *result, size_t input_size)
{
	struct prio_queue pq = { intcmp };

	for (int i = 0, j = 0; i < input_size; i++) {
		void *peek, *get;
		switch(input[i]) {
		case GET:
			peek = prio_queue_peek(&pq);
			get = prio_queue_get(&pq);
			if (!check(peek == get))
				return;
			if(!check_int(result[j++], ==, show(get)))
				test_msg("failed at result[] index %d", j-1);
			break;
		case DUMP:
			while ((peek = prio_queue_peek(&pq))) {
				get = prio_queue_get(&pq);
				if (!check(peek == get))
					return;
				if(!check_int(result[j++], ==, show(get)))
					test_msg("failed at result[] index %d", j-1);
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
	clear_prio_queue(&pq);
}

#define BASIC_INPUT 2, 6, 3, 10, 9, 5, 7, 4, 5, 8, 1, DUMP
#define BASIC_RESULT 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10

#define MIXED_PUT_GET_INPUT 6, 2, 4, GET, 5, 3, GET, GET, 1, DUMP
#define MIXED_PUT_GET_RESULT 2, 3, 4, 1, 5, 6

#define EMPTY_QUEUE_INPUT 1, 2, GET, GET, GET, 1, 2, GET, GET, GET
#define EMPTY_QUEUE_RESULT 1, 2, MISSING, 1, 2, MISSING

#define STACK_INPUT STACK, 8, 1, 5, 4, 6, 2, 3, DUMP
#define STACK_RESULT 3, 2, 6, 4, 5, 1, 8

#define REVERSE_STACK_INPUT STACK, 1, 2, 3, 4, 5, 6, REVERSE, DUMP
#define REVERSE_STACK_RESULT 1, 2, 3, 4, 5, 6

#define TEST_INPUT(INPUT, RESULT, name)			\
  static void test_##name(void)				\
{								\
	int input[] = {INPUT};					\
	int result[] = {RESULT};				\
	test_prio_queue(input, result, ARRAY_SIZE(input));	\
}

TEST_INPUT(BASIC_INPUT, BASIC_RESULT, basic)
TEST_INPUT(MIXED_PUT_GET_INPUT, MIXED_PUT_GET_RESULT, mixed)
TEST_INPUT(EMPTY_QUEUE_INPUT, EMPTY_QUEUE_RESULT, empty)
TEST_INPUT(STACK_INPUT, STACK_RESULT, stack)
TEST_INPUT(REVERSE_STACK_INPUT, REVERSE_STACK_RESULT, reverse)

int cmd_main(int argc, const char **argv)
{
	TEST(test_basic(), "prio-queue works for basic input");
	TEST(test_mixed(), "prio-queue works for mixed put & get commands");
	TEST(test_empty(), "prio-queue works when queue is empty");
	TEST(test_stack(), "prio-queue works when used as a LIFO stack");
	TEST(test_reverse(), "prio-queue works when LIFO stack is reversed");

	return test_done();
}
