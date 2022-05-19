#!/bin/sh

test_description='support for reading config from a blob'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create config blob' '
	cat >config <<-\EOF &&
	[some]
		value = 1
	EOF
	but add config &&
	but cummit -m foo
'

test_expect_success 'list config blob contents' '
	echo some.value=1 >expect &&
	but config --blob=HEAD:config --list >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch value from blob' '
	echo true >expect &&
	but config --blob=HEAD:config --bool some.value >actual &&
	test_cmp expect actual
'

test_expect_success 'reading non-existing value from blob is an error' '
	test_must_fail but config --blob=HEAD:config non.existing
'

test_expect_success 'reading from blob and file is an error' '
	test_must_fail but config --blob=HEAD:config --system --list
'

test_expect_success 'reading from missing ref is an error' '
	test_must_fail but config --blob=HEAD:doesnotexist --list
'

test_expect_success 'reading from non-blob is an error' '
	test_must_fail but config --blob=HEAD --list
'

test_expect_success 'setting a value in a blob is an error' '
	test_must_fail but config --blob=HEAD:config some.value foo
'

test_expect_success 'deleting a value in a blob is an error' '
	test_must_fail but config --blob=HEAD:config --unset some.value
'

test_expect_success 'editing a blob is an error' '
	test_must_fail but config --blob=HEAD:config --edit
'

test_expect_success 'parse errors in blobs are properly attributed' '
	cat >config <<-\EOF &&
	[some]
		value = "
	EOF
	but add config &&
	but cummit -m broken &&

	test_must_fail but config --blob=HEAD:config some.value 2>err &&
	test_i18ngrep "HEAD:config" err
'

test_expect_success 'can parse blob ending with CR' '
	test_cummit --printf CR config "[some]key = value\\r" &&
	echo value >expect &&
	but config --blob=HEAD:config some.key >actual &&
	test_cmp expect actual
'

test_expect_success 'config --blob outside of a repository is an error' '
	nonbut test_must_fail but config --blob=foo --list
'

test_done
