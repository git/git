# Helpers for tests of but's choice of pager.

test_expect_success 'determine default pager' '
	test_might_fail but config --unset core.pager &&
	less=$(
		sane_unset PAGER BUT_PAGER &&
		but var BUT_PAGER
	) &&
	test -n "$less"
'

if expr "$less" : '[a-z][a-z]*$' >/dev/null
then
	test_set_prereq SIMPLEPAGER
fi
