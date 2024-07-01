#include "test-lib.h"
#include "strbuf.h"
#include "strvec.h"

#define check_strvec(vec, ...) \
	check_strvec_loc(TEST_LOCATION(), vec, __VA_ARGS__)
LAST_ARG_MUST_BE_NULL
static void check_strvec_loc(const char *loc, struct strvec *vec, ...)
{
	va_list ap;
	size_t nr = 0;

	va_start(ap, vec);
	while (1) {
		const char *str = va_arg(ap, const char *);
		if (!str)
			break;

		if (!check_uint(vec->nr, >, nr) ||
		    !check_uint(vec->alloc, >, nr) ||
		    !check_str(vec->v[nr], str)) {
			struct strbuf msg = STRBUF_INIT;
			strbuf_addf(&msg, "strvec index %"PRIuMAX, (uintmax_t) nr);
			test_assert(loc, msg.buf, 0);
			strbuf_release(&msg);
			va_end(ap);
			return;
		}

		nr++;
	}
	va_end(ap);

	check_uint(vec->nr, ==, nr);
	check_uint(vec->alloc, >=, nr);
	check_pointer_eq(vec->v[nr], NULL);
}

int cmd_main(int argc, const char **argv)
{
	if (TEST_RUN("static initialization")) {
		struct strvec vec = STRVEC_INIT;
		check_pointer_eq(vec.v, empty_strvec);
		check_uint(vec.nr, ==, 0);
		check_uint(vec.alloc, ==, 0);
	}

	if (TEST_RUN("dynamic initialization")) {
		struct strvec vec;
		strvec_init(&vec);
		check_pointer_eq(vec.v, empty_strvec);
		check_uint(vec.nr, ==, 0);
		check_uint(vec.alloc, ==, 0);
	}

	if (TEST_RUN("clear")) {
		struct strvec vec = STRVEC_INIT;
		strvec_push(&vec, "foo");
		strvec_clear(&vec);
		check_pointer_eq(vec.v, empty_strvec);
		check_uint(vec.nr, ==, 0);
		check_uint(vec.alloc, ==, 0);
	}

	if (TEST_RUN("push")) {
		struct strvec vec = STRVEC_INIT;

		strvec_push(&vec, "foo");
		check_strvec(&vec, "foo", NULL);

		strvec_push(&vec, "bar");
		check_strvec(&vec, "foo", "bar", NULL);

		strvec_clear(&vec);
	}

	if (TEST_RUN("pushf")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushf(&vec, "foo: %d", 1);
		check_strvec(&vec, "foo: 1", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("pushl")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		check_strvec(&vec, "foo", "bar", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("pushv")) {
		const char *strings[] = {
			"foo", "bar", "baz", NULL,
		};
		struct strvec vec = STRVEC_INIT;

		strvec_pushv(&vec, strings);
		check_strvec(&vec, "foo", "bar", "baz", NULL);

		strvec_clear(&vec);
	}

	if (TEST_RUN("replace at head")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_replace(&vec, 0, "replaced");
		check_strvec(&vec, "replaced", "bar", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("replace at tail")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_replace(&vec, 2, "replaced");
		check_strvec(&vec, "foo", "bar", "replaced", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("replace in between")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_replace(&vec, 1, "replaced");
		check_strvec(&vec, "foo", "replaced", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("replace with substring")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", NULL);
		strvec_replace(&vec, 0, vec.v[0] + 1);
		check_strvec(&vec, "oo", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("remove at head")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_remove(&vec, 0);
		check_strvec(&vec, "bar", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("remove at tail")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_remove(&vec, 2);
		check_strvec(&vec, "foo", "bar", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("remove in between")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_remove(&vec, 1);
		check_strvec(&vec, "foo", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("pop with empty array")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pop(&vec);
		check_strvec(&vec, NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("pop with non-empty array")) {
		struct strvec vec = STRVEC_INIT;
		strvec_pushl(&vec, "foo", "bar", "baz", NULL);
		strvec_pop(&vec);
		check_strvec(&vec, "foo", "bar", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("split empty string")) {
		struct strvec vec = STRVEC_INIT;
		strvec_split(&vec, "");
		check_strvec(&vec, NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("split single item")) {
		struct strvec vec = STRVEC_INIT;
		strvec_split(&vec, "foo");
		check_strvec(&vec, "foo", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("split multiple items")) {
		struct strvec vec = STRVEC_INIT;
		strvec_split(&vec, "foo bar baz");
		check_strvec(&vec, "foo", "bar", "baz", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("split whitespace only")) {
		struct strvec vec = STRVEC_INIT;
		strvec_split(&vec, " \t\n");
		check_strvec(&vec, NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("split multiple consecutive whitespaces")) {
		struct strvec vec = STRVEC_INIT;
		strvec_split(&vec, "foo\n\t bar");
		check_strvec(&vec, "foo", "bar", NULL);
		strvec_clear(&vec);
	}

	if (TEST_RUN("detach")) {
		struct strvec vec = STRVEC_INIT;
		const char **detached;

		strvec_push(&vec, "foo");

		detached = strvec_detach(&vec);
		check_str(detached[0], "foo");
		check_pointer_eq(detached[1], NULL);

		check_pointer_eq(vec.v, empty_strvec);
		check_uint(vec.nr, ==, 0);
		check_uint(vec.alloc, ==, 0);

		free((char *) detached[0]);
		free(detached);
	}

	return test_done();
}
