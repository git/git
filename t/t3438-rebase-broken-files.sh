#!/bin/sh

test_description='rebase behavior when on-disk files are broken'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'set up conflicting branches' '
	test_commit base file &&
	git checkout -b branch1 &&
	test_commit one file &&
	git checkout -b branch2 HEAD^ &&
	test_commit two file
'

create_conflict () {
	test_when_finished "git rebase --abort" &&
	git checkout -B tmp branch2 &&
	test_must_fail git rebase branch1
}

check_resolve_fails () {
	echo resolved >file &&
	git add file &&
	test_must_fail git rebase --continue
}

for item in NAME EMAIL DATE
do
	test_expect_success "detect missing GIT_AUTHOR_$item" '
		create_conflict &&

		grep -v $item .git/rebase-merge/author-script >tmp &&
		mv tmp .git/rebase-merge/author-script &&

		check_resolve_fails
	'
done

for item in NAME EMAIL DATE
do
	test_expect_success "detect duplicate GIT_AUTHOR_$item" '
		create_conflict &&

		grep -i $item .git/rebase-merge/author-script >tmp &&
		cat tmp >>.git/rebase-merge/author-script &&

		check_resolve_fails
	'
done

test_expect_success 'unknown key in author-script' '
	create_conflict &&

	echo "GIT_AUTHOR_BOGUS=${SQ}whatever${SQ}" \
		>>.git/rebase-merge/author-script &&

	check_resolve_fails
'

test_expect_success POSIXPERM,SANITY 'unwritable rebased-patches does not leak' '
	>.git/rebased-patches &&
	chmod a-w .git/rebased-patches &&

	git checkout -b side HEAD^ &&
	test_commit unrelated &&
	test_must_fail git rebase --apply --onto tmp HEAD^
'

test_done
