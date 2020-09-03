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
# Rebase has lots of useful options like --whitepsace=fix, which are
# actually all built in terms of flags to git-am.  Since neither
# --merge nor --interactive (nor any options that imply those two) use
# git-am, using them together will result in flags like --whitespace=fix
# being ignored.  Make sure rebase warns the user and aborts instead.
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

}

test_rebase_am_only --whitespace=fix
test_rebase_am_only -C4

test_expect_success REBASE_P '--preserve-merges incompatible with --signoff' '
	git checkout B^0 &&
	test_must_fail git rebase --preserve-merges --signoff A
'

test_expect_success REBASE_P \
	'--preserve-merges incompatible with --rebase-merges' '
	git checkout B^0 &&
	test_must_fail git rebase --preserve-merges --rebase-merges A
'

test_done
