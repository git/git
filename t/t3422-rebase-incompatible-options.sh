#!/bin/sh

test_description='test if rebase detects and aborts on incompatible options'

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

	test_expect_success "$opt incompatible with --autosquash" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --autosquash A
	"

	test_expect_success "$opt incompatible with --interactive" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --interactive A
	"

	test_expect_success "$opt incompatible with --exec" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --exec 'true' A
	"

	test_expect_success "$opt incompatible with --keep-empty" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --keep-empty A
	"

	test_expect_success "$opt incompatible with --empty=..." "
		git checkout B^0 &&
		test_must_fail git rebase $opt --empty=ask A
	"

	test_expect_success "$opt incompatible with --no-reapply-cherry-picks" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --no-reapply-cherry-picks A
	"

	test_expect_success "$opt incompatible with --reapply-cherry-picks" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --reapply-cherry-picks A
	"

	test_expect_success "$opt incompatible with --rebase-merges" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --rebase-merges A
	"

	test_expect_success "$opt incompatible with --update-refs" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --update-refs A
	"

	test_expect_success "$opt incompatible with --root without --onto" "
		git checkout B^0 &&
		test_must_fail git rebase $opt --root A
	"

	test_expect_success "$opt incompatible with rebase.rebaseMerges" "
		git checkout B^0 &&
		test_must_fail git -c rebase.rebaseMerges=true rebase $opt A 2>err &&
		grep -e --no-rebase-merges err
	"

	test_expect_success "$opt incompatible with rebase.updateRefs" "
		git checkout B^0 &&
		test_must_fail git -c rebase.updateRefs=true rebase $opt A 2>err &&
		grep -e --no-update-refs err
	"

	test_expect_success "$opt okay with overridden rebase.rebaseMerges" "
		test_when_finished \"git reset --hard B^0\" &&
		git checkout B^0 &&
		git -c rebase.rebaseMerges=true rebase --no-rebase-merges $opt A
	"

	test_expect_success "$opt okay with overridden rebase.updateRefs" "
		test_when_finished \"git reset --hard B^0\" &&
		git checkout B^0 &&
		git -c rebase.updateRefs=true rebase --no-update-refs $opt A
	"
}

# Check options which imply --apply
test_rebase_am_only --whitespace=fix
test_rebase_am_only -C4
# Also check an explicit --apply
test_rebase_am_only --apply

test_done
