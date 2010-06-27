#!/bin/sh

test_description='test automatic tag following'

. ./test-lib.sh

case $(uname -s) in
*MINGW*)
	skip_all="GIT_DEBUG_SEND_PACK not supported - skipping tests"
	test_done
esac

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

cat - <<EOF >expect
#S
want $A
#E
EOF
test_expect_success 'fetch A (new commit : 1 connection)' '
	rm -f $U
	(
		cd cloned &&
		GIT_DEBUG_SEND_PACK=3 git fetch 3>../$U &&
		test $A = $(git rev-parse --verify origin/master)
	) &&
	test -s $U &&
	cut -d" " -f1,2 $U >actual &&
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

cat - <<EOF >expect
#S
want $C
want $T
#E
EOF
test_expect_success 'fetch C, T (new branch, tag : 1 connection)' '
	rm -f $U
	(
		cd cloned &&
		GIT_DEBUG_SEND_PACK=3 git fetch 3>../$U &&
		test $C = $(git rev-parse --verify origin/cat) &&
		test $T = $(git rev-parse --verify tag1) &&
		test $A = $(git rev-parse --verify tag1^0)
	) &&
	test -s $U &&
	cut -d" " -f1,2 $U >actual &&
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

cat - <<EOF >expect
#S
want $B
want $S
#E
EOF
test_expect_success 'fetch B, S (commit and tag : 1 connection)' '
	rm -f $U
	(
		cd cloned &&
		GIT_DEBUG_SEND_PACK=3 git fetch 3>../$U &&
		test $B = $(git rev-parse --verify origin/master) &&
		test $B = $(git rev-parse --verify tag2^0) &&
		test $S = $(git rev-parse --verify tag2)
	) &&
	test -s $U &&
	cut -d" " -f1,2 $U >actual &&
	test_cmp expect actual
'

cat - <<EOF >expect
#S
want $B
want $S
#E
EOF
test_expect_success 'new clone fetch master and tags' '
	git branch -D cat
	rm -f $U
	(
		mkdir clone2 &&
		cd clone2 &&
		git init &&
		git remote add origin .. &&
		GIT_DEBUG_SEND_PACK=3 git fetch 3>../$U &&
		test $B = $(git rev-parse --verify origin/master) &&
		test $S = $(git rev-parse --verify tag2) &&
		test $B = $(git rev-parse --verify tag2^0) &&
		test $T = $(git rev-parse --verify tag1) &&
		test $A = $(git rev-parse --verify tag1^0)
	) &&
	test -s $U &&
	cut -d" " -f1,2 $U >actual &&
	test_cmp expect actual
'

test_done
