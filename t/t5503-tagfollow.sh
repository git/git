#!/bin/sh

test_description='test automatic tag following'

. ./test-lib.sh

# End state of the repository:
#
#         T - tag1          S - tag2
#        /                 /
#   L - A ------ O ------ B
#    \   \                 \
#     \   C - origin/cat    \
#      origin/master         master

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
		last if $F[2] eq "0000";
		print $F[2], " ", $F[3];
	' "$1"
}

test_expect_success 'fetch A (new commit : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $A = $(git rev-parse --verify origin/master)
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
	git checkout master
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
		test $B = $(git rev-parse --verify origin/master) &&
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

test_expect_success 'new clone fetch master and tags' '
	git branch -D cat
	rm -f $U
	(
		mkdir clone2 &&
		cd clone2 &&
		git init &&
		git remote add origin .. &&
		GIT_TRACE_PACKET=$UPATH git fetch &&
		test $B = $(git rev-parse --verify origin/master) &&
		test $S = $(git rev-parse --verify tag2) &&
		test $B = $(git rev-parse --verify tag2^0) &&
		test $T = $(git rev-parse --verify tag1) &&
		test $A = $(git rev-parse --verify tag1^0)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_done
