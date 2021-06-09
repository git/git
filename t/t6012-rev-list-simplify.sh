#!/bin/sh

test_description='merge simplification'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

note () {
	git tag "$1"
}

unnote () {
	git name-rev --tags --stdin | sed -e "s|$OID_REGEX (tags/\([^)]*\)) |\1 |g"
}

#
# Create a test repo with interesting commit graph:
#
# A--B----------G--H--I--K--L
#  \  \           /     /
#   \  \         /     /
#    C------E---F     J
#        \_/
#
# The commits are laid out from left-to-right starting with
# the root commit A and terminating at the tip commit L.
#
# There are a few places where we adjust the commit date or
# author date to make the --topo-order, --date-order, and
# --author-date-order flags produce different output.

test_expect_success setup '
	echo "Hi there" >file &&
	echo "initial" >lost &&
	git add file lost &&
	test_tick && git commit -m "Initial file and lost" &&
	note A &&

	git branch other-branch &&

	git symbolic-ref HEAD refs/heads/unrelated &&
	git rm -f "*" &&
	echo "Unrelated branch" >side &&
	git add side &&
	test_tick && git commit -m "Side root" &&
	note J &&
	git checkout main &&

	echo "Hello" >file &&
	echo "second" >lost &&
	git add file lost &&
	test_tick && GIT_AUTHOR_DATE=$(($test_tick + 120)) git commit -m "Modified file and lost" &&
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
	test_must_fail git merge -m "merge" main &&
	>lost && git commit -a -m "merge" &&
	note E &&

	echo "Yet another" >elif &&
	git add elif &&
	test_tick && git commit -m "Irrelevant change" &&
	note F &&

	git checkout main &&
	echo "Yet another" >elif &&
	git add elif &&
	test_tick && git commit -m "Another irrelevant change" &&
	note G &&

	test_tick && git merge -m "merge" other-branch &&
	note H &&

	echo "Final change" >file &&
	test_tick && git commit -a -m "Final change" &&
	note I &&

	git checkout main &&
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

check_result 'L K J I H F E D C G B A' --full-history --topo-order
check_result 'L K I H G F E D C B J A' --full-history
check_result 'L K I H G F E D C B J A' --full-history --date-order
check_result 'L K I H G F E D B C J A' --full-history --author-date-order
check_result 'K I H E C B A' --full-history -- file
check_result 'K I H E C B A' --full-history --topo-order -- file
check_result 'K I H E C B A' --full-history --date-order -- file
check_result 'K I H E B C A' --full-history --author-date-order -- file
check_result 'I E C B A' --simplify-merges -- file
check_result 'I E C B A' --simplify-merges --topo-order -- file
check_result 'I E C B A' --simplify-merges --date-order -- file
check_result 'I E B C A' --simplify-merges --author-date-order -- file
check_result 'I B A' -- file
check_result 'I B A' --topo-order -- file
check_result 'I B A' --date-order -- file
check_result 'I B A' --author-date-order -- file
check_result 'H' --first-parent -- another-file
check_result 'H' --first-parent --topo-order -- another-file

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

#
# Create a new history to demonstrate the value of --show-pulls
# with respect to the subtleties of simplified history, --full-history,
# and --simplify-merges.
#
#   .-A---M-----C--N---O---P
#  /     / \  \  \/   /   /
# I     B   \  R-'`-Z'   /
#  \   /     \/         /
#   \ /      /\        /
#    `---X--'  `---Y--'
#
# This example is explained in Documentation/rev-list-options.txt

test_expect_success 'setup rebuild repo' '
	rm -rf .git * &&
	git init &&
	git switch -c topic &&

	echo base >file &&
	git add file &&
	test_commit I &&

	echo A >file &&
	git add file &&
	test_commit A &&

	git switch -c branchB I &&
	echo B >file &&
	git add file &&
	test_commit B &&

	git switch topic &&
	test_must_fail git merge -m "M" B &&
	echo A >file &&
	echo B >>file &&
	git add file &&
	git merge --continue &&
	note M &&

	echo C >other &&
	git add other &&
	test_commit C &&

	git switch -c branchX I &&
	echo X >file &&
	git add file &&
	test_commit X &&

	git switch -c branchR M &&
	git merge -m R -Xtheirs X &&
	note R &&

	git switch topic &&
	git merge -m N R &&
	note N &&

	git switch -c branchY M &&
	echo Y >y &&
	git add y &&
	test_commit Y &&

	git switch -c branchZ C &&
	echo Z >z &&
	git add z &&
	test_commit Z &&

	git switch topic &&
	git merge -m O Z &&
	note O &&

	git merge -m P Y &&
	note P
'

check_result 'X I' -- file
check_result 'N R X I' --show-pulls -- file

check_result 'P O N R X M B A I' --full-history --topo-order -- file
check_result 'N R X M B A I' --simplify-merges --topo-order --show-pulls -- file
check_result 'R X M B A I' --simplify-merges --topo-order -- file
check_result 'N M A I' --first-parent -- file
check_result 'N M A I' --first-parent --show-pulls -- file

# --ancestry-path implies --full-history
check_result 'P O N R M' --topo-order \
	--ancestry-path A..HEAD -- file
check_result 'P O N R M' --topo-order \
	--show-pulls \
	--ancestry-path A..HEAD -- file
check_result 'P O N R M' --topo-order \
	--full-history \
	--ancestry-path A..HEAD -- file
check_result 'R M' --topo-order \
	--simplify-merges \
	--ancestry-path A..HEAD -- file
check_result 'N R M' --topo-order \
	--simplify-merges --show-pulls \
	--ancestry-path A..HEAD -- file

test_expect_success 'log --graph --simplify-merges --show-pulls' '
	cat >expect <<-\EOF &&
	* N
	*   R
	|\  
	| * X
	* |   M
	|\ \  
	| * | B
	| |/  
	* / A
	|/  
	* I
	EOF
	git log --graph --pretty="%s" \
		--simplify-merges --show-pulls \
		-- file >actual &&
	test_cmp expect actual
'

test_done
