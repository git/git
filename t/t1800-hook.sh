#!/bin/sh

test_description='but-hook command'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'but hook usage' '
	test_expect_code 129 but hook &&
	test_expect_code 129 but hook run &&
	test_expect_code 129 but hook run -h &&
	test_expect_code 129 but hook run --unknown 2>err &&
	grep "unknown option" err
'

test_expect_success 'but hook run: nonexistent hook' '
	cat >stderr.expect <<-\EOF &&
	error: cannot find a hook named test-hook
	EOF
	test_expect_code 1 but hook run test-hook 2>stderr.actual &&
	test_cmp stderr.expect stderr.actual
'

test_expect_success 'but hook run: nonexistent hook with --ignore-missing' '
	but hook run --ignore-missing does-not-exist 2>stderr.actual &&
	test_must_be_empty stderr.actual
'

test_expect_success 'but hook run: basic' '
	test_hook test-hook <<-EOF &&
	echo Test hook
	EOF

	cat >expect <<-\EOF &&
	Test hook
	EOF
	but hook run test-hook 2>actual &&
	test_cmp expect actual
'

test_expect_success 'but hook run: stdout and stderr both write to our stderr' '
	test_hook test-hook <<-EOF &&
	echo >&1 Will end up on stderr
	echo >&2 Will end up on stderr
	EOF

	cat >stderr.expect <<-\EOF &&
	Will end up on stderr
	Will end up on stderr
	EOF
	but hook run test-hook >stdout.actual 2>stderr.actual &&
	test_cmp stderr.expect stderr.actual &&
	test_must_be_empty stdout.actual
'

for code in 1 2 128 129
do
	test_expect_success "but hook run: exit code $code is passed along" '
		test_hook test-hook <<-EOF &&
		exit $code
		EOF

		test_expect_code $code but hook run test-hook
	'
done

test_expect_success 'but hook run arg u ments without -- is not allowed' '
	test_expect_code 129 but hook run test-hook arg u ments
'

test_expect_success 'but hook run -- pass arguments' '
	test_hook test-hook <<-\EOF &&
	echo $1
	echo $2
	EOF

	cat >expect <<-EOF &&
	arg
	u ments
	EOF

	but hook run test-hook -- arg "u ments" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'but hook run -- out-of-repo runs excluded' '
	test_hook test-hook <<-EOF &&
	echo Test hook
	EOF

	nonbut test_must_fail but hook run test-hook
'

test_expect_success 'but -c core.hooksPath=<PATH> hook run' '
	mkdir my-hooks &&
	write_script my-hooks/test-hook <<-\EOF &&
	echo Hook ran $1 >>actual
	EOF

	cat >expect <<-\EOF &&
	Test hook
	Hook ran one
	Hook ran two
	Hook ran three
	Hook ran four
	EOF

	test_hook test-hook <<-EOF &&
	echo Test hook
	EOF

	# Test various ways of specifying the path. See also
	# t1350-config-hooks-path.sh
	>actual &&
	but hook run test-hook -- ignored 2>>actual &&
	but -c core.hooksPath=my-hooks hook run test-hook -- one 2>>actual &&
	but -c core.hooksPath=my-hooks/ hook run test-hook -- two 2>>actual &&
	but -c core.hooksPath="$PWD/my-hooks" hook run test-hook -- three 2>>actual &&
	but -c core.hooksPath="$PWD/my-hooks/" hook run test-hook -- four 2>>actual &&
	test_cmp expect actual
'

test_done
