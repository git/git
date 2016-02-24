#!/bin/sh

test_description='test re-include patterns'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir -p fooo foo/bar tmp &&
	touch abc foo/def foo/bar/ghi foo/bar/bar
'

test_expect_success 'no match, do not enter subdir and waste cycles' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/foo
	!fooo/bar/bar
	EOF
	GIT_TRACE_EXCLUDE="$(pwd)/tmp/trace" git ls-files -o --exclude-standard >tmp/actual &&
	! grep "enter .foo/.\$" tmp/trace &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by literal pathname pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	/foo
	!foo/bar/bar
	EOF
	cat >fooo/.gitignore <<-\EOF &&
	!/*
	EOF	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by wildcard pathname pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	/fo?
	!foo/bar/bar
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by literal basename pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	foo
	!foo/bar/bar
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by wildcard basename pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	fo?
	!foo/bar/bar
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by literal mustbedir, basename pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	foo/
	!foo/bar/bar
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'match, excluded by literal mustbedir, pathname pattern' '
	cat >.gitignore <<-\EOF &&
	/tmp
	/fooo
	/foo/
	!foo/bar/bar
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	.gitignore
	abc
	foo/bar/bar
	EOF
	test_cmp tmp/expected tmp/actual
'

test_expect_success 'prepare for nested negatives' '
	cat >.git/info/exclude <<-\EOF &&
	/.gitignore
	/tmp
	/foo
	/abc
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	test_must_be_empty tmp/actual &&
	mkdir -p 1/2/3/4 &&
	touch 1/f 1/2/f 1/2/3/f 1/2/3/4/f
'

test_expect_success 'match, literal pathname, nested negatives' '
	cat >.gitignore <<-\EOF &&
	/1
	!1/2
	1/2/3
	!1/2/3/4
	EOF
	git ls-files -o --exclude-standard >tmp/actual &&
	cat >tmp/expected <<-\EOF &&
	1/2/3/4/f
	1/2/f
	EOF
	test_cmp tmp/expected tmp/actual
'

test_done
