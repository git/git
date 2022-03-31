#!/bin/sh

test_description='test automatic tag following'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# End state of the repository:
#
#         T - tag1          S - tag2
#        /                 /
#   L - A ------ O ------ B
#    \   \                 \
#     \   C - origin/cat    \
#      origin/main           main

test_expect_success setup '
	test_tick &&
	echo ichi >file &&
	git add file &&
	git commit -m L &&
	L=$(git rev-parse --verify HEAD) &&

	(
		mkdir cloned &&
		cd cloned &&
		git init-db &&
		git remote add -f origin ..
	) &&

	test_tick &&
	echo A >file &&
	git add file &&
	git commit -m A &&
	A=$(git rev-parse --verify HEAD)
'

U=UPLOAD_LOG
UPATH="$(pwd)/$U"

test_expect_success 'setup expect' '
cat - <<EOF >expect
want $A
EOF
'

get_needs () {
	test -s "$1" &&
	perl -alne '
		next unless $F[1] eq "upload-pack<";
		next unless $F[2] eq "want";
		print $F[2], " ", $F[3];
	' "$1"
}

test_expect_success 'fetch A (new commit : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $A = $(git rev-parse --verify origin/main)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success "create tag T on A, create C on branch cat" '
	git tag -a -m tag1 tag1 $A &&
	T=$(git rev-parse --verify tag1) &&

	git checkout -b cat &&
	echo C >file &&
	git add file &&
	git commit -m C &&
	C=$(git rev-parse --verify HEAD) &&
	git checkout main
'

test_expect_success 'setup expect' '
cat - <<EOF >expect
want $C
want $T
EOF
'

test_expect_success 'fetch C, T (new branch, tag : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $C = $(git rev-parse --verify origin/cat) &&
		test $T = $(git rev-parse --verify tag1) &&
		test $A = $(git rev-parse --verify tag1^0)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success "create commits O, B, tag S on B" '
	test_tick &&
	echo O >file &&
	git add file &&
	git commit -m O &&

	test_tick &&
	echo B >file &&
	git add file &&
	git commit -m B &&
	B=$(git rev-parse --verify HEAD) &&

	git tag -a -m tag2 tag2 $B &&
	S=$(git rev-parse --verify tag2)
'

test_expect_success 'setup expect' '
cat - <<EOF >expect
want $B
want $S
EOF
'

test_expect_success 'fetch B, S (commit and tag : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $B = $(git rev-parse --verify origin/main) &&
		test $B = $(git rev-parse --verify tag2^0) &&
		test $S = $(git rev-parse --verify tag2)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success 'setup expect' '
cat - <<EOF >expect
want $B
want $S
EOF
'

test_expect_success 'new clone fetch main and tags' '
	test_might_fail git branch -D cat &&
	rm -f $U &&
	(
		mkdir clone2 &&
		cd clone2 &&
		git init &&
		git remote add origin .. &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $B = $(git rev-parse --verify origin/main) &&
		test $S = $(git rev-parse --verify tag2) &&
		test $B = $(git rev-parse --verify tag2^0) &&
		test $T = $(git rev-parse --verify tag1) &&
		test $A = $(git rev-parse --verify tag1^0)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success 'atomic fetch with failing backfill' '
	git init clone3 &&

	# We want to test whether a failure when backfilling tags correctly
	# aborts the complete transaction when `--atomic` is passed: we should
	# neither create the branch nor should we create the tag when either
	# one of both fails to update correctly.
	#
	# To trigger failure we simply abort when backfilling a tag.
	test_hook -C clone3 reference-transaction <<-\EOF &&
		while read oldrev newrev reference
		do
			if test "$reference" = refs/tags/tag1
			then
				exit 1
			fi
		done
	EOF

	test_must_fail git -C clone3 fetch --atomic .. $B:refs/heads/something &&
	test_must_fail git -C clone3 rev-parse --verify refs/heads/something &&
	test_must_fail git -C clone3 rev-parse --verify refs/tags/tag2
'

test_expect_success 'atomic fetch with backfill should use single transaction' '
	git init clone4 &&

	# Fetching with the `--atomic` flag should update all references in a
	# single transaction, including backfilled tags. We thus expect to see
	# a single reference transaction for the created branch and tags.
	cat >expected <<-EOF &&
		prepared
		$ZERO_OID $B refs/heads/something
		$ZERO_OID $S refs/tags/tag2
		$ZERO_OID $T refs/tags/tag1
		committed
		$ZERO_OID $B refs/heads/something
		$ZERO_OID $S refs/tags/tag2
		$ZERO_OID $T refs/tags/tag1
	EOF

	test_hook -C clone4 reference-transaction <<-\EOF &&
		( echo "$*" && cat ) >>actual
	EOF

	git -C clone4 fetch --atomic .. $B:refs/heads/something &&
	test_cmp expected clone4/actual
'

test_expect_success 'backfill failure causes command to fail' '
	git init clone5 &&

	# Create a tag that is nested below the tag we are about to fetch via
	# the backfill mechanism. This causes a D/F conflict when backfilling
	# and should thus cause the command to fail.
	empty_blob=$(git -C clone5 hash-object -w --stdin </dev/null) &&
	git -C clone5 update-ref refs/tags/tag1/nested $empty_blob &&

	test_must_fail git -C clone5 fetch .. $B:refs/heads/something &&
	test $B = $(git -C clone5 rev-parse --verify refs/heads/something) &&
	test $S = $(git -C clone5 rev-parse --verify tag2) &&
	test_must_fail git -C clone5 rev-parse --verify tag1
'

test_done
