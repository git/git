#define USE_THE_REPOSITORY_VARIABLE

#include "unit-test.h"
#include "object.h"
#include "decorate.h"
#include "repository.h"

struct test_vars {
	struct object *one, *two, *three;
	struct decoration n;
	int decoration_a, decoration_b;
};

static struct test_vars vars;

void test_example_decorate__initialize(void)
{
	struct object_id one_oid = { { 1 } }, two_oid = { { 2 } }, three_oid = { { 3 } };

	vars.one = lookup_unknown_object(the_repository, &one_oid);
	vars.two = lookup_unknown_object(the_repository, &two_oid);
	vars.three = lookup_unknown_object(the_repository, &three_oid);
}

void test_example_decorate__cleanup(void)
{
	clear_decoration(&vars.n, NULL);
}

void test_example_decorate__add(void)
{
	cl_assert_equal_p(add_decoration(&vars.n, vars.one, &vars.decoration_a), NULL);
	cl_assert_equal_p(add_decoration(&vars.n, vars.two, NULL), NULL);
}

void test_example_decorate__readd(void)
{
	cl_assert_equal_p(add_decoration(&vars.n, vars.one, &vars.decoration_a), NULL);
	cl_assert_equal_p(add_decoration(&vars.n, vars.two, NULL), NULL);
	cl_assert_equal_p(add_decoration(&vars.n, vars.one, NULL), &vars.decoration_a);
	cl_assert_equal_p(add_decoration(&vars.n, vars.two, &vars.decoration_b), NULL);
}

void test_example_decorate__lookup(void)
{
	cl_assert_equal_p(add_decoration(&vars.n, vars.two, &vars.decoration_b), NULL);
	cl_assert_equal_p(add_decoration(&vars.n, vars.one, NULL), NULL);
	cl_assert_equal_p(lookup_decoration(&vars.n, vars.two), &vars.decoration_b);
	cl_assert_equal_p(lookup_decoration(&vars.n, vars.one), NULL);
}

void test_example_decorate__loop(void)
{
	int objects_noticed = 0;

	cl_assert_equal_p(add_decoration(&vars.n, vars.one, &vars.decoration_a), NULL);
	cl_assert_equal_p(add_decoration(&vars.n, vars.two, &vars.decoration_b), NULL);

	for (size_t i = 0; i < vars.n.size; i++)
		if (vars.n.entries[i].base)
			objects_noticed++;

	cl_assert_equal_i(objects_noticed, 2);
}
