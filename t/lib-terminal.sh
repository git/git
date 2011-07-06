#!/bin/sh

test_expect_success PERL 'set up terminal for tests' '
	# Reading from the pty master seems to get stuck _sometimes_
	# on Mac OS X 10.5.0, using Perl 5.10.0 or 5.8.9.
	#
	# Reproduction recipe: run
	#
	#	i=0
	#	while ./test-terminal.perl echo hi $i
	#	do
	#		: $((i = $i + 1))
	#	done
	#
	# After 2000 iterations or so it hangs.
	# https://rt.cpan.org/Ticket/Display.html?id=65692
	#
	if test "$(uname -s)" = Darwin
	then
		:
	elif
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
