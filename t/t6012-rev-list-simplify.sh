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
	echo "initial" >lost &&
	git add file lost &&
	test_tick && git commit -m "Initial file and lost" &&
	note A &&

	git branch other-branch &&

	echo "Hello" >file &&
	echo "second" >lost &&
	git add file lost &&
	test_tick && git commit -m "Modified file and lost" &&
	note B &&

	git checkout other-branch &&

	echo "Hello" >file &&
	>lost &&
	git add file lost &&
	test_tick && git commit -m "Modified the file identically" &&
	note C &&

	echo "This is a stupid example" >another-file &&
	git add another-file &&
	test_tick && git commit -m "Add another file" &&
	note D &&

	test_tick &&
	test_must_fail git merge -m "merge" master &&
	>lost && git commit -a -m "merge" &&
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
	note I &&

	git symbolic-ref HEAD refs/heads/unrelated &&
	git rm -f "*" &&
	echo "Unrelated branch" >side &&
	git add side &&
	test_tick && git commit -m "Side root" &&
	note J &&

	git checkout master &&
	test_tick && git merge --allow-unrelated-histories -m "Coolest" unrelated &&
	note K &&

	echo "Immaterial" >elif &&
	git add elif &&
	test_tick && git commit -m "Last" &&
	note L
'

FMT='tformat:%P 	%H | %s'

check_outcome () {
	outcome=$1
	shift
	for c in $1
	do
		echo "$c"
	done >expect &&
	shift &&
	param="$*" &&
	test_expect_$outcome "log $param" '
		git log --pretty="$FMT" --parents $param |
		unnote >actual &&
		sed -e "s/^.*	\([^ ]*\) .*/\1/" >check <actual &&
		test_cmp expect check
	'
}

check_result () {
	check_outcome success "$@"
}

check_result 'L K J I H G F E D C B A' --full-history
check_result 'K I H E C B A' --full-history -- file
check_result 'K I H E C B A' --full-history --topo-order -- file
check_result 'K I H E C B A' --full-history --date-order -- file
check_result 'I E C B A' --simplify-merges -- file
check_result 'I B A' -- file
check_result 'I B A' --topo-order -- file
check_result 'H' --first-parent -- another-file

check_result 'E C B A' --full-history E -- lost
test_expect_success 'full history simplification without parent' '
	printf "%s\n" E C B A >expect &&
	git log --pretty="$FMT" --full-history E -- lost |
	unnote >actual &&
	sed -e "s/^.*	\([^ ]*\) .*/\1/" >check <actual &&
	test_cmp expect check
'

test_expect_success '--full-diff is not affected by --parents' '
	git log -p --pretty="%H" --full-diff -- file >expected &&
	git log -p --pretty="%H" --full-diff --parents -- file >actual &&
	test_cmp expected actual
'

test_done
