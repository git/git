#!/bin/sh

test_description='Test automatic use of a pager.'

. ./test-lib.sh

cleanup_fail() {
	echo >&2 cleanup failed
	(exit 1)
}

test_expect_success 'set up terminal for tests' '
	rm -f stdout_is_tty ||
	cleanup_fail &&

	if test -t 1
	then
		>stdout_is_tty
	elif
		test_have_prereq PERL &&
		"$PERL_PATH" "$TEST_DIRECTORY"/t7006/test-terminal.perl \
			sh -c "test -t 1"
	then
		>test_terminal_works
	fi
'

if test -e stdout_is_tty
then
	test_terminal() { "$@"; }
	test_set_prereq TTY
elif test -e test_terminal_works
then
	test_terminal() {
		"$PERL_PATH" "$TEST_DIRECTORY"/t7006/test-terminal.perl "$@"
	}
	test_set_prereq TTY
else
	say no usable terminal, so skipping some tests
fi

test_expect_success 'setup' '
	unset GIT_PAGER GIT_PAGER_IN_USE &&
	test_might_fail git config --unset core.pager &&

	PAGER="cat >paginated.out" &&
	export PAGER &&

	test_commit initial
'

test_expect_success TTY 'some commands use a pager' '
	rm -f paginated.out ||
	cleanup_fail &&

	test_terminal git log &&
	test -e paginated.out
'

test_expect_success TTY 'some commands do not use a pager' '
	rm -f paginated.out ||
	cleanup_fail &&

	test_terminal git rev-list HEAD &&
	! test -e paginated.out
'

test_expect_success 'no pager when stdout is a pipe' '
	rm -f paginated.out ||
	cleanup_fail &&

	git log | cat &&
	! test -e paginated.out
'

test_expect_success 'no pager when stdout is a regular file' '
	rm -f paginated.out ||
	cleanup_fail &&

	git log >file &&
	! test -e paginated.out
'

test_expect_success TTY 'git --paginate rev-list uses a pager' '
	rm -f paginated.out ||
	cleanup_fail &&

	test_terminal git --paginate rev-list HEAD &&
	test -e paginated.out
'

test_expect_success 'no pager even with --paginate when stdout is a pipe' '
	rm -f file paginated.out ||
	cleanup_fail &&

	git --paginate log | cat &&
	! test -e paginated.out
'

test_expect_success TTY 'no pager with --no-pager' '
	rm -f paginated.out ||
	cleanup_fail &&

	test_terminal git --no-pager log &&
	! test -e paginated.out
'

# A colored commit log will begin with an appropriate ANSI escape
# for the first color; the text "commit" comes later.
colorful() {
	read firstline <$1
	! expr "$firstline" : "^[a-zA-Z]" >/dev/null
}

test_expect_success 'tests can detect color' '
	rm -f colorful.log colorless.log ||
	cleanup_fail &&

	git log --no-color >colorless.log &&
	git log --color >colorful.log &&
	! colorful colorless.log &&
	colorful colorful.log
'

test_expect_success 'no color when stdout is a regular file' '
	rm -f colorless.log &&
	git config color.ui auto ||
	cleanup_fail &&

	git log >colorless.log &&
	! colorful colorless.log
'

test_expect_success TTY 'color when writing to a pager' '
	rm -f paginated.out &&
	git config color.ui auto ||
	cleanup_fail &&

	(
		TERM=vt100 &&
		export TERM &&
		test_terminal git log
	) &&
	colorful paginated.out
'

test_expect_success 'color when writing to a file intended for a pager' '
	rm -f colorful.log &&
	git config color.ui auto ||
	cleanup_fail &&

	(
		TERM=vt100 &&
		GIT_PAGER_IN_USE=true &&
		export TERM GIT_PAGER_IN_USE &&
		git log >colorful.log
	) &&
	colorful colorful.log
'

test_expect_success 'determine default pager' '
	unset PAGER GIT_PAGER &&
	test_might_fail git config --unset core.pager ||
	cleanup_fail &&

	less=$(git var GIT_PAGER) &&
	test -n "$less"
'

if expr "$less" : '^[a-z][a-z]*$' >/dev/null && test_have_prereq TTY
then
	test_set_prereq SIMPLEPAGER
fi

test_expect_success SIMPLEPAGER 'default pager is used by default' '
	unset PAGER GIT_PAGER &&
	test_might_fail git config --unset core.pager &&
	rm -f default_pager_used ||
	cleanup_fail &&

	cat >$less <<-\EOF &&
	#!/bin/sh
	wc >default_pager_used
	EOF
	chmod +x $less &&
	(
		PATH=.:$PATH &&
		export PATH &&
		test_terminal git log
	) &&
	test -e default_pager_used
'

test_expect_success TTY 'PAGER overrides default pager' '
	unset GIT_PAGER &&
	test_might_fail git config --unset core.pager &&
	rm -f PAGER_used ||
	cleanup_fail &&

	PAGER="wc >PAGER_used" &&
	export PAGER &&
	test_terminal git log &&
	test -e PAGER_used
'

test_expect_success TTY 'core.pager overrides PAGER' '
	unset GIT_PAGER &&
	rm -f core.pager_used ||
	cleanup_fail &&

	PAGER=wc &&
	export PAGER &&
	git config core.pager "wc >core.pager_used" &&
	test_terminal git log &&
	test -e core.pager_used
'

test_expect_success TTY 'GIT_PAGER overrides core.pager' '
	rm -f GIT_PAGER_used ||
	cleanup_fail &&

	git config core.pager wc &&
	GIT_PAGER="wc >GIT_PAGER_used" &&
	export GIT_PAGER &&
	test_terminal git log &&
	test -e GIT_PAGER_used
'

test_done
