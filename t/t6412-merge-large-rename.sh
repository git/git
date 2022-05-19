#!/bin/sh

test_description='merging with large rename matrix'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

count() {
	i=1
	while test $i -le $1; do
		echo $i
		i=$(($i + 1))
	done
}

test_expect_success 'setup (initial)' '
	touch file &&
	but add . &&
	but cummit -m initial &&
	but tag initial
'

make_text() {
	echo $1: $2
	for i in $(count 20); do
		echo $1: $i
	done
	echo $1: $3
}

test_rename() {
	test_expect_success "rename ($1, $2)" '
	n='$1' &&
	expect='$2' &&
	but checkout -f main &&
	test_might_fail but branch -D test$n &&
	but reset --hard initial &&
	for i in $(count $n); do
		make_text $i initial initial >$i || return 1
	done &&
	but add . &&
	but cummit -m add=$n &&
	for i in $(count $n); do
		make_text $i changed initial >$i || return 1
	done &&
	but cummit -a -m change=$n &&
	but checkout -b test$n HEAD^ &&
	for i in $(count $n); do
		but rm $i &&
		make_text $i initial changed >$i.moved || return 1
	done &&
	but add . &&
	but cummit -m change+rename=$n &&
	case "$expect" in
		ok) but merge main ;;
		 *) test_must_fail but merge main ;;
	esac
	'
}

test_rename 5 ok

test_expect_success 'set diff.renamelimit to 4' '
	but config diff.renamelimit 4
'
test_rename 4 ok
test_rename 5 fail

test_expect_success 'set merge.renamelimit to 5' '
	but config merge.renamelimit 5
'
test_rename 5 ok
test_rename 6 fail

test_expect_success 'setup large simple rename' '
	but config --unset merge.renamelimit &&
	but config --unset diff.renamelimit &&

	but reset --hard initial &&
	for i in $(count 200); do
		make_text foo bar baz >$i || return 1
	done &&
	but add . &&
	but cummit -m create-files &&

	but branch simple-change &&
	but checkout -b simple-rename &&

	mkdir builtin &&
	but mv [0-9]* builtin/ &&
	but cummit -m renamed &&

	but checkout simple-change &&
	>unrelated-change &&
	but add unrelated-change &&
	but cummit -m unrelated-change
'

test_expect_success 'massive simple rename does not spam added files' '
	sane_unset BUT_MERGE_VERBOSITY &&
	but merge --no-stat simple-rename | grep -v Removing >output &&
	test_line_count -lt 5 output
'

test_done
