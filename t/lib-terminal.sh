#!/bin/sh

test_expect_success 'set up terminal for tests' '
	if
		test_have_prereq PERL &&
		"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl \
			sh -c "test -t 1 && test -t 2"
	then
		test_set_prereq TTY &&
		test_terminal () {
			if ! test_declared_prereq TTY
			then
				echo >&4 "test_terminal: need to declare TTY prerequisite"
				return 127
			fi
			"$PERL_PATH" "$TEST_DIRECTORY"/test-terminal.perl "$@"
		}
	fi
'
