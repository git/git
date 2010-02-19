#!/bin/sh

test_description='Test automatic use of a pager.'

. ./test-lib.sh

rm -f stdout_is_tty
test_expect_success 'is stdout a terminal?' '
	if test -t 1
	then
		: > stdout_is_tty
	fi
'

if test -e stdout_is_tty
then
	test_set_prereq TTY
else
	say stdout is not a terminal, so skipping some tests.
fi

unset GIT_PAGER GIT_PAGER_IN_USE
git config --unset core.pager
PAGER='cat > paginated.out'
export PAGER

test_expect_success 'setup' '
	test_commit initial
'

rm -f paginated.out
test_expect_success TTY 'some commands use a pager' '
	git log &&
	test -e paginated.out
'

rm -f paginated.out
test_expect_success TTY 'some commands do not use a pager' '
	git rev-list HEAD &&
	! test -e paginated.out
'

rm -f paginated.out
test_expect_success 'no pager when stdout is a pipe' '
	git log | cat &&
	! test -e paginated.out
'

rm -f paginated.out
test_expect_success 'no pager when stdout is a regular file' '
	git log > file &&
	! test -e paginated.out
'

rm -f paginated.out
test_expect_success TTY 'git --paginate rev-list uses a pager' '
	git --paginate rev-list HEAD  &&
	test -e paginated.out
'

rm -f file paginated.out
test_expect_success 'no pager even with --paginate when stdout is a pipe' '
	git --paginate log | cat &&
	! test -e paginated.out
'

rm -f paginated.out
test_expect_success TTY 'no pager with --no-pager' '
	git --no-pager log &&
	! test -e paginated.out
'

# A colored commit log will begin with an appropriate ANSI escape
# for the first color; the text "commit" comes later.
colorful() {
	read firstline < $1
	! expr "$firstline" : "^[a-zA-Z]" >/dev/null
}

rm -f colorful.log colorless.log
test_expect_success 'tests can detect color' '
	git log --no-color > colorless.log &&
	git log --color > colorful.log &&
	! colorful colorless.log &&
	colorful colorful.log
'

rm -f colorless.log
git config color.ui auto
test_expect_success 'no color when stdout is a regular file' '
	git log > colorless.log &&
	! colorful colorless.log
'

rm -f paginated.out
git config color.ui auto
test_expect_success TTY 'color when writing to a pager' '
	TERM=vt100 git log &&
	colorful paginated.out
'

rm -f colorful.log
git config color.ui auto
test_expect_success 'color when writing to a file intended for a pager' '
	TERM=vt100 GIT_PAGER_IN_USE=true git log > colorful.log &&
	colorful colorful.log
'

unset PAGER GIT_PAGER
git config --unset core.pager
test_expect_success 'determine default pager' '
	less=$(git var GIT_PAGER) &&
	test -n "$less"
'

if expr "$less" : '^[a-z]*$' > /dev/null && test_have_prereq TTY
then
	test_set_prereq SIMPLEPAGER
fi

unset PAGER GIT_PAGER
git config --unset core.pager
rm -f default_pager_used
test_expect_success SIMPLEPAGER 'default pager is used by default' '
	cat > $less <<-EOF &&
	#!$SHELL_PATH
	: > default_pager_used
	EOF
	chmod +x $less &&
	PATH=.:$PATH git log &&
	test -e default_pager_used
'

unset GIT_PAGER
git config --unset core.pager
rm -f PAGER_used
test_expect_success TTY 'PAGER overrides default pager' '
	PAGER=": > PAGER_used" &&
	export PAGER &&
	git log &&
	test -e PAGER_used
'

unset GIT_PAGER
rm -f core.pager_used
test_expect_success TTY 'core.pager overrides PAGER' '
	PAGER=: &&
	export PAGER &&
	git config core.pager ": > core.pager_used" &&
	git log &&
	test -e core.pager_used
'

rm -f GIT_PAGER_used
test_expect_success TTY 'GIT_PAGER overrides core.pager' '
	git config core.pager : &&
	GIT_PAGER=": > GIT_PAGER_used" &&
	export GIT_PAGER &&
	git log &&
	test -e GIT_PAGER_used
'

test_done
