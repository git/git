#!/bin/sh

test_description='git-hook command'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'git hook usage' '
	test_expect_code 129 git hook &&
	test_expect_code 129 git hook run &&
	test_expect_code 129 git hook run -h &&
	test_expect_code 129 git hook run --unknown 2>err &&
	grep "unknown option" err
'

test_expect_success 'git hook run: nonexistent hook' '
	cat >stderr.expect <<-\EOF &&
	error: cannot find a hook named test-hook
	EOF
	test_expect_code 1 git hook run test-hook 2>stderr.actual &&
	test_cmp stderr.expect stderr.actual
'

test_expect_success 'git hook run: nonexistent hook with --ignore-missing' '
	git hook run --ignore-missing does-not-exist 2>stderr.actual &&
	test_must_be_empty stderr.actual
'

test_expect_success 'git hook run: basic' '
	test_hook test-hook <<-EOF &&
	echo Test hook
	EOF

	cat >expect <<-\EOF &&
	Test hook
	EOF
	git hook run test-hook 2>actual &&
	test_cmp expect actual
'

test_expect_success 'git hook run: stdout and stderr both write to our stderr' '
	test_hook test-hook <<-EOF &&
	echo >&1 Will end up on stderr
	echo >&2 Will end up on stderr
	EOF

	cat >stderr.expect <<-\EOF &&
	Will end up on stderr
	Will end up on stderr
	EOF
	git hook run test-hook >stdout.actual 2>stderr.actual &&
	test_cmp stderr.expect stderr.actual &&
	test_must_be_empty stdout.actual
'

for code in 1 2 128 129
do
	test_expect_success "git hook run: exit code $code is passed along" '
		test_hook test-hook <<-EOF &&
		exit $code
		EOF

		test_expect_code $code git hook run test-hook
	'
done

test_expect_success 'git hook run arg u ments without -- is not allowed' '
	test_expect_code 129 git hook run test-hook arg u ments
'

test_expect_success 'git hook run -- pass arguments' '
	test_hook test-hook <<-\EOF &&
	echo $1
	echo $2
	EOF

	cat >expect <<-EOF &&
	arg
	u ments
	EOF

	git hook run test-hook -- arg "u ments" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'git hook run -- out-of-repo runs excluded' '
	test_hook test-hook <<-EOF &&
	echo Test hook
	EOF

	nongit test_must_fail git hook run test-hook
'

test_expect_success 'git -c core.hooksPath=<PATH> hook run' '
	mkdir my-hooks &&
	write_script my-hooks/test-hook <<-\EOF &&
	echo Hook ran $1
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
	git hook run test-hook -- ignored 2>>actual &&
	git -c core.hooksPath=my-hooks hook run test-hook -- one 2>>actual &&
	git -c core.hooksPath=my-hooks/ hook run test-hook -- two 2>>actual &&
	git -c core.hooksPath="$PWD/my-hooks" hook run test-hook -- three 2>>actual &&
	git -c core.hooksPath="$PWD/my-hooks/" hook run test-hook -- four 2>>actual &&
	test_cmp expect actual
'

test_hook_tty () {
	cat >expect <<-\EOF
	STDOUT TTY
	STDERR TTY
	EOF

	test_when_finished "rm -rf repo" &&
	git init repo &&

	test_commit -C repo A &&
	test_commit -C repo B &&
	git -C repo reset --soft HEAD^ &&

	test_hook -C repo pre-commit <<-EOF &&
	test -t 1 && echo STDOUT TTY >>actual || echo STDOUT NO TTY >>actual &&
	test -t 2 && echo STDERR TTY >>actual || echo STDERR NO TTY >>actual
	EOF

	test_terminal git -C repo "$@" &&
	test_cmp expect repo/actual
}

test_expect_success TTY 'git hook run: stdout and stderr are connected to a TTY' '
	test_hook_tty hook run pre-commit
'

test_expect_success TTY 'git commit: stdout and stderr are connected to a TTY' '
	test_hook_tty commit -m"B.new"
'

test_expect_success 'git hook run a hook with a bad shebang' '
	test_when_finished "rm -rf bad-hooks" &&
	mkdir bad-hooks &&
	write_script bad-hooks/test-hook "/bad/path/no/spaces" </dev/null &&

	test_expect_code 1 git \
		-c core.hooksPath=bad-hooks \
		hook run test-hook >out 2>err &&
	test_must_be_empty out &&

	# TODO: We should emit the same (or at least a more similar)
	# error on MINGW (essentially Git for Windows) and all other
	# platforms.. See the OS-specific code in start_command()
	grep -E "^(error|fatal): cannot (exec|spawn) .*bad-hooks/test-hook" err
'

test_expect_success 'stdin to hooks' '
	write_script .git/hooks/test-hook <<-\EOF &&
	echo BEGIN stdin
	cat
	echo END stdin
	EOF

	cat >expect <<-EOF &&
	BEGIN stdin
	hello
	END stdin
	EOF

	echo hello >input &&
	git hook run --to-stdin=input test-hook 2>actual &&
	test_cmp expect actual
'

test_done
