#!/bin/sh

test_description='magic pathspec tests using git-add'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir sub anothersub &&
	: >sub/foo &&
	: >anothersub/foo
'

test_expect_success 'add :/' "
	cat >expected <<-EOF &&
	add 'anothersub/foo'
	add 'expected'
	add 'sub/actual'
	add 'sub/foo'
	EOF
	(cd sub && git add -n :/ >actual) &&
	test_cmp expected sub/actual
"

cat >expected <<EOF
add 'anothersub/foo'
EOF

test_expect_success 'add :/anothersub' '
	(cd sub && git add -n :/anothersub >actual) &&
	test_cmp expected sub/actual
'

test_expect_success 'add :/non-existent' '
	(cd sub && test_must_fail git add -n :/non-existent)
'

cat >expected <<EOF
add 'sub/foo'
EOF

if mkdir ":" 2>/dev/null
then
	test_set_prereq COLON_DIR
fi

test_expect_success COLON_DIR 'a file with the same (long) magic name exists' '
	: >":(icase)ha" &&
	test_must_fail git add -n ":(icase)ha" &&
	git add -n "./:(icase)ha"
'

test_expect_success COLON_DIR 'a file with the same (short) magic name exists' '
	: >":/bar" &&
	test_must_fail git add -n :/bar &&
	git add -n "./:/bar"
'

test_done
