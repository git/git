#!/bin/sh

test_description='ls-tree with(out) globs'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a aa "a[a]" &&
	touch a/one aa/two "a[a]/three" &&
	git add a/one aa/two "a[a]/three" &&
	git commit -m test
'

test_expect_success 'ls-tree a[a] matches literally' '
	cat >expected <<EOF &&
100644 blob e69de29bb2d1d6434b8b29ae775ad8c2e48c5391	a[a]/three
EOF
	git ls-tree -r HEAD "a[a]" >actual &&
	test_cmp expected actual
'

test_done
