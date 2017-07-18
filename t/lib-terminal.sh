# Helpers for terminal output tests.

# Catch tests which should depend on TTY but forgot to. There's no need
# to additionally check that the TTY prereq is set here.  If the test declared
# it and we are running the test, then it must have been set.
test_terminal () {
	if ! test_declared_prereq TTY
	then
		echo >&4 "test_terminal: need to declare TTY prerequisite"
		return 127
	fi
	perl "$TEST_DIRECTORY"/test-terminal.perl "$@"
}

test_lazy_prereq TTY '
	test_have_prereq PERL &&

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
	test "$(uname -s)" != Darwin &&

	perl "$TEST_DIRECTORY"/test-terminal.perl \
		sh -c "test -t 1 && test -t 2"
'
