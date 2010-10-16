#!/bin/sh

test_expect_success 'set up terminal for tests' '
	if test -t 1
	then
		>stdout_is_tty
	elif
		test_have_prereq PERL &&
		"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl \
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
		"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl "$@"
	}
	test_set_prereq TTY
else
	say "# no usable terminal, so skipping some tests"
fi
