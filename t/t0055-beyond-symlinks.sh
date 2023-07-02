#!/bin/sh

test_description='update-index and add refuse to add beyond symlinks'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success SYMLINKS setup '
	>a &&
	mkdir b &&
	ln -s b c &&
	>c/d &&
	git update-index --add a b/d
'

test_expect_success SYMLINKS 'update-index --add beyond symlinks' '
	test_must_fail git update-index --add c/d &&
	cat >expect <<-\EOF &&
	a
	b/d
	EOF
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'add beyond symlinks' '
	test_must_fail git add c/d &&
	cat >expect <<-\EOF &&
	a
	b/d
	EOF
	git ls-files >actual &&
	test_cmp expect actual
'

test_done
