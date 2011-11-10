#!/bin/sh

test_description='merge simplification'

. ./test-lib.sh

note () {
	git tag "$1"
}

unnote () {
	git name-rev --tags --stdin | sed -e "s|$_x40 (tags/\([^)]*\)) |\1 |g"
}

test_expect_success setup '
	echo "Hi there" >file &&
	git add file &&
	test_tick && git commit -m "Initial file" &&
	note A &&

	git branch other-branch &&

	echo "Hello" >file &&
	git add file &&
	test_tick && git commit -m "Modified file" &&
	note B &&

	git checkout other-branch &&

	echo "Hello" >file &&
	git add file &&
	test_tick && git commit -m "Modified the file identically" &&
	note C &&

	echo "This is a stupid example" >another-file &&
	git add another-file &&
	test_tick && git commit -m "Add another file" &&
	note D &&

	test_tick && git merge -m "merge" master &&
	note E &&

	echo "Yet another" >elif &&
	git add elif &&
	test_tick && git commit -m "Irrelevant change" &&
	note F &&

	git checkout master &&
	echo "Yet another" >elif &&
	git add elif &&
	test_tick && git commit -m "Another irrelevant change" &&
	note G &&

	test_tick && git merge -m "merge" other-branch &&
	note H &&

	echo "Final change" >file &&
	test_tick && git commit -a -m "Final change" &&
	note I
'

FMT='tformat:%P 	%H | %s'

check_result () {
	for c in $1
	do
		echo "$c"
	done >expect &&
	shift &&
	param="$*" &&
	test_expect_success "log $param" '
		git log --pretty="$FMT" --parents $param |
		unnote >actual &&
		sed -e "s/^.*	\([^ ]*\) .*/\1/" >check <actual &&
		test_cmp expect check || {
			cat actual
			false
		}
	'
}

check_result 'I H G F E D C B A' --full-history
check_result 'I H E C B A' --full-history -- file
check_result 'I H E C B A' --full-history --topo-order -- file
check_result 'I H E C B A' --full-history --date-order -- file
check_result 'I E C B A' --simplify-merges -- file
check_result 'I B A' -- file
check_result 'I B A' --topo-order -- file

test_done
