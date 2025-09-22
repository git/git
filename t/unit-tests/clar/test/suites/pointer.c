#include "clar.h"

void test_pointer__equal(void)
{
	void *p1 = (void *)0x1;
	cl_assert_equal_p(p1, p1);
}

void test_pointer__unequal(void)
{
	void *p1 = (void *)0x1, *p2 = (void *)0x2;
	cl_assert_equal_p(p1, p2);
}
