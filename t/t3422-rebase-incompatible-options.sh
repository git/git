#!/bin/sh

test_description='test if rebase detects and aborts on incompatible options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_seq 2 9 >foo &&
	but add foo &&
	but cummit -m orig &&

	but branch A &&
	but branch B &&

	but checkout A &&
	test_seq 1 9 >foo &&
	but add foo &&
	but cummit -m A &&

	but checkout B &&
	echo "q qfoo();" | q_to_tab >>foo &&
	but add foo &&
	but cummit -m B
'

#
# Rebase has lots of useful options like --whitepsace=fix, which are
# actually all built in terms of flags to but-am.  Since neither
# --merge nor --interactive (nor any options that imply those two) use
# but-am, using them together will result in flags like --whitespace=fix
# being ignored.  Make sure rebase warns the user and aborts instead.
#

test_rebase_am_only () {
	opt=$1
	shift
	test_expect_success "$opt incompatible with --merge" "
		but checkout B^0 &&
		test_must_fail but rebase $opt --merge A
	"

	test_expect_success "$opt incompatible with --strategy=ours" "
		but checkout B^0 &&
		test_must_fail but rebase $opt --strategy=ours A
	"

	test_expect_success "$opt incompatible with --strategy-option=ours" "
		but checkout B^0 &&
		test_must_fail but rebase $opt --strategy-option=ours A
	"

	test_expect_success "$opt incompatible with --interactive" "
		but checkout B^0 &&
		test_must_fail but rebase $opt --interactive A
	"

	test_expect_success "$opt incompatible with --exec" "
		but checkout B^0 &&
		test_must_fail but rebase $opt --exec 'true' A
	"

}

test_rebase_am_only --whitespace=fix
test_rebase_am_only -C4

test_done
