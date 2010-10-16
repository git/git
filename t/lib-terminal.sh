#!/bin/sh

test_expect_success 'set up terminal for tests' '
	if test -t 1 && test -t 2
	then
		>have_tty
	elif
		test_have_prereq PERL &&
		"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl \
			sh -c "test -t 1 && test -t 2"
	then
		>test_terminal_works
	fi
'

if test -e have_tty
then
	test_terminal_() { "$@"; }
	test_set_prereq TTY
elif test -e test_terminal_works
then
	test_terminal_() {
		"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl "$@"
	}
	test_set_prereq TTY
else
	say "# no usable terminal, so skipping some tests"
fi

test_terminal () {
	if ! test_declared_prereq TTY
	then
		echo >&2 'test_terminal: need to declare TTY prerequisite'
		return 127
	fi
	test_terminal_ "$@"
}
