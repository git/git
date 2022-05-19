#!/bin/sh

test_description='but rebase - test patch id computation'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

scramble () {
	i=0
	while read x
	do
		if test $i -ne 0
		then
			echo "$x"
		fi
		i=$((($i+1) % 10))
	done <"$1" >"$1.new"
	mv -f "$1.new" "$1"
}

test_expect_success 'setup' '
	but cummit --allow-empty -m initial &&
	but tag root
'

test_expect_success 'setup: 500 lines' '
	rm -f .butattributes &&
	but checkout -q -f main &&
	but reset --hard root &&
	test_seq 500 >file &&
	but add file &&
	but cummit -q -m initial &&
	but branch -f other &&

	scramble file &&
	but add file &&
	but cummit -q -m "change big file" &&

	but checkout -q other &&
	: >newfile &&
	but add newfile &&
	but cummit -q -m "add small file" &&

	but cherry-pick main >/dev/null 2>&1
'

test_expect_success 'setup attributes' '
	echo "file binary" >.butattributes
'

test_expect_success 'detect upstream patch' '
	but checkout -q main &&
	scramble file &&
	but add file &&
	but cummit -q -m "change big file again" &&
	but checkout -q other^{} &&
	but rebase main &&
	but rev-list main...HEAD~ >revs &&
	test_must_be_empty revs
'

test_expect_success 'do not drop patch' '
	but branch -f squashed main &&
	but checkout -q -f squashed &&
	but reset -q --soft HEAD~2 &&
	but cummit -q -m squashed &&
	but checkout -q other^{} &&
	test_must_fail but rebase squashed &&
	but rebase --quit
'

test_done
