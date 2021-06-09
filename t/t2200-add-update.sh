#!/bin/sh

test_description='git add -u

This test creates a working tree state with three files:

  top (previously committed, modified)
  dir/sub (previously committed, modified)
  dir/other (untracked)

and issues a git add -u with path limiting on "dir" to add
only the updates to dir/sub.

Also tested are "git add -u" without limiting, and "git add -u"
without contents changes, and other conditions'

. ./test-lib.sh

test_expect_success setup '
	echo initial >check &&
	echo initial >top &&
	echo initial >foo &&
	mkdir dir1 dir2 &&
	echo initial >dir1/sub1 &&
	echo initial >dir1/sub2 &&
	echo initial >dir2/sub3 &&
	git add check dir1 dir2 top foo &&
	test_tick &&
	git commit -m initial &&

	echo changed >check &&
	echo changed >top &&
	echo changed >dir2/sub3 &&
	rm -f dir1/sub1 &&
	echo other >dir2/other
'

test_expect_success update '
	git add -u dir1 dir2
'

test_expect_success 'update noticed a removal' '
	test "$(git ls-files dir1/sub1)" = ""
'

test_expect_success 'update touched correct path' '
	test "$(git diff-files --name-status dir2/sub3)" = ""
'

test_expect_success 'update did not touch other tracked files' '
	test "$(git diff-files --name-status check)" = "M	check" &&
	test "$(git diff-files --name-status top)" = "M	top"
'

test_expect_success 'update did not touch untracked files' '
	test "$(git ls-files dir2/other)" = ""
'

test_expect_success 'cache tree has not been corrupted' '

	git ls-files -s |
	sed -e "s/ 0	/	/" >expect &&
	git ls-tree -r $(git write-tree) |
	sed -e "s/ blob / /" >current &&
	test_cmp expect current

'

test_expect_success 'update from a subdirectory' '
	(
		cd dir1 &&
		echo more >sub2 &&
		git add -u sub2
	)
'

test_expect_success 'change gets noticed' '

	test "$(git diff-files --name-status dir1)" = ""

'

test_expect_success 'non-qualified update in subdir updates from the root' '
	(
		cd dir1 &&
		echo even more >>sub2 &&
		git --literal-pathspecs add -u &&
		echo even more >>sub2 &&
		git add -u
	) &&
	git diff-files --name-only >actual &&
	test_must_be_empty actual
'

test_expect_success 'replace a file with a symlink' '

	rm foo &&
	test_ln_s_add top foo

'

test_expect_success 'add everything changed' '

	git add -u &&
	test -z "$(git diff-files)"

'

test_expect_success 'touch and then add -u' '

	touch check &&
	git add -u &&
	test -z "$(git diff-files)"

'

test_expect_success 'touch and then add explicitly' '

	touch check &&
	git add check &&
	test -z "$(git diff-files)"

'

test_expect_success 'add -n -u should not add but just report' '

	(
		echo "add '\''check'\''" &&
		echo "remove '\''top'\''"
	) >expect &&
	before=$(git ls-files -s check top) &&
	echo changed >>check &&
	rm -f top &&
	git add -n -u >actual &&
	after=$(git ls-files -s check top) &&

	test "$before" = "$after" &&
	test_cmp expect actual

'

test_expect_success 'add -u resolves unmerged paths' '
	git reset --hard &&
	one=$(echo 1 | git hash-object -w --stdin) &&
	two=$(echo 2 | git hash-object -w --stdin) &&
	three=$(echo 3 | git hash-object -w --stdin) &&
	{
		for path in path1 path2
		do
			echo "100644 $one 1	$path"
			echo "100644 $two 2	$path"
			echo "100644 $three 3	$path"
		done
		echo "100644 $one 1	path3"
		echo "100644 $one 1	path4"
		echo "100644 $one 3	path5"
		echo "100644 $one 3	path6"
	} |
	git update-index --index-info &&
	echo 3 >path1 &&
	echo 2 >path3 &&
	echo 2 >path5 &&

	# Fail to explicitly resolve removed paths with "git add"
	test_must_fail git add --no-all path4 &&
	test_must_fail git add --no-all path6 &&

	# "add -u" should notice removals no matter what stages
	# the index entries are in.
	git add -u &&
	git ls-files -s path1 path2 path3 path4 path5 path6 >actual &&
	{
		echo "100644 $three 0	path1"
		echo "100644 $two 0	path3"
		echo "100644 $two 0	path5"
	} >expect &&
	test_cmp expect actual
'

test_expect_success '"add -u non-existent" should fail' '
	test_must_fail git add -u non-existent &&
	git ls-files >actual &&
	! grep "non-existent" actual
'

test_done
