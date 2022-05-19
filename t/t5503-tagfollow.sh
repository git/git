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
	but add file &&
	but cummit -m L &&
	L=$(but rev-parse --verify HEAD) &&

	(
		mkdir cloned &&
		cd cloned &&
		but init-db &&
		but remote add -f origin ..
	) &&

	test_tick &&
	echo A >file &&
	but add file &&
	but cummit -m A &&
	A=$(but rev-parse --verify HEAD)
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

test_expect_success 'fetch A (new cummit : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH but fetch &&
		test $A = $(but rev-parse --verify origin/main)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success "create tag T on A, create C on branch cat" '
	but tag -a -m tag1 tag1 $A &&
	T=$(but rev-parse --verify tag1) &&

	but checkout -b cat &&
	echo C >file &&
	but add file &&
	but cummit -m C &&
	C=$(but rev-parse --verify HEAD) &&
	but checkout main
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
		GIT_TRACE_PACKET=$UPATH but fetch &&
		test $C = $(but rev-parse --verify origin/cat) &&
		test $T = $(but rev-parse --verify tag1) &&
		test $A = $(but rev-parse --verify tag1^0)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_expect_success "create cummits O, B, tag S on B" '
	test_tick &&
	echo O >file &&
	but add file &&
	but cummit -m O &&

	test_tick &&
	echo B >file &&
	but add file &&
	but cummit -m B &&
	B=$(but rev-parse --verify HEAD) &&

	but tag -a -m tag2 tag2 $B &&
	S=$(but rev-parse --verify tag2)
'

test_expect_success 'setup expect' '
cat - <<EOF >expect
want $B
want $S
EOF
'

test_expect_success 'fetch B, S (cummit and tag : 1 connection)' '
	rm -f $U &&
	(
		cd cloned &&
		GIT_TRACE_PACKET=$UPATH but fetch &&
		test $B = $(but rev-parse --verify origin/main) &&
		test $B = $(but rev-parse --verify tag2^0) &&
		test $S = $(but rev-parse --verify tag2)
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
	test_might_fail but branch -D cat &&
	rm -f $U &&
	(
		mkdir clone2 &&
		cd clone2 &&
		but init &&
		but remote add origin .. &&
		GIT_TRACE_PACKET=$UPATH but fetch &&
		test $B = $(but rev-parse --verify origin/main) &&
		test $S = $(but rev-parse --verify tag2) &&
		test $B = $(but rev-parse --verify tag2^0) &&
		test $T = $(but rev-parse --verify tag1) &&
		test $A = $(but rev-parse --verify tag1^0)
	) &&
	get_needs $U >actual &&
	test_cmp expect actual
'

test_done
