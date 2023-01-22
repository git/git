#!/bin/sh

test_description='test if rebase detects and aborts on incompatible options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_seq 2 9 >foo &&
	git add foo &&
	git commit -m orig &&

	git branch A &&
	git branch B &&

	git checkout A &&
	test_seq 1 9 >foo &&
	git add foo &&
	git commit -m A &&

	git checkout B &&
	echo "q qfoo();" | q_to_tab >>foo &&
	git add foo &&
	git commit -m B
'

#
# Rebase has a couple options which are specific to the apply backend,
# and several options which are specific to the merge backend.  Flags
# from the different sets cannot work together, and we do not want to
# just ignore one of the sets of flags.  Make sure rebase warns the
# user and aborts instead.
#

test_rebase_am_only () {
	opt=$1
	shift
	test_expect_success "$opt incompatible with --merge" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --merge A
	"

	test_expect_success "$opt incompatible with --strategy=ours" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --strategy=ours A
	"

	test_expect_success "$opt incompatible with --strategy-option=ours" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --strategy-option=ours A
	"

	test_expect_success "$opt incompatible with --interactive" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --interactive A
	"

	test_expect_success "$opt incompatible with --exec" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --exec 'true' A
	"

	test_expect_success "$opt incompatible with --update-refs" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --update-refs A
	"

}

test_rebase_am_only --whitespace=fix
test_rebase_am_only -C4

test_done
