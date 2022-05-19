#!/bin/sh

test_description='but add -u

This test creates a working tree state with three files:

  top (previously cummitted, modified)
  dir/sub (previously cummitted, modified)
  dir/other (untracked)

and issues a but add -u with path limiting on "dir" to add
only the updates to dir/sub.

Also tested are "but add -u" without limiting, and "but add -u"
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
	but add check dir1 dir2 top foo &&
	test_tick &&
	but cummit -m initial &&

	echo changed >check &&
	echo changed >top &&
	echo changed >dir2/sub3 &&
	rm -f dir1/sub1 &&
	echo other >dir2/other
'

test_expect_success update '
	but add -u dir1 dir2
'

test_expect_success 'update noticed a removal' '
	but ls-files dir1/sub1 >out &&
	test_must_be_empty out
'

test_expect_success 'update touched correct path' '
	but diff-files --name-status dir2/sub3 >out &&
	test_must_be_empty out
'

test_expect_success 'update did not touch other tracked files' '
	echo "M	check" >expect &&
	but diff-files --name-status check >actual &&
	test_cmp expect actual &&

	echo "M	top" >expect &&
	but diff-files --name-status top >actual &&
	test_cmp expect actual
'

test_expect_success 'update did not touch untracked files' '
	but ls-files dir2/other >out &&
	test_must_be_empty out
'

test_expect_success 'cache tree has not been corrupted' '

	but ls-files -s |
	sed -e "s/ 0	/	/" >expect &&
	but ls-tree -r $(but write-tree) |
	sed -e "s/ blob / /" >current &&
	test_cmp expect current

'

test_expect_success 'update from a subdirectory' '
	(
		cd dir1 &&
		echo more >sub2 &&
		but add -u sub2
	)
'

test_expect_success 'change gets noticed' '
	but diff-files --name-status dir1 >out &&
	test_must_be_empty out
'

test_expect_success 'non-qualified update in subdir updates from the root' '
	(
		cd dir1 &&
		echo even more >>sub2 &&
		but --literal-pathspecs add -u &&
		echo even more >>sub2 &&
		but add -u
	) &&
	but diff-files --name-only >actual &&
	test_must_be_empty actual
'

test_expect_success 'replace a file with a symlink' '

	rm foo &&
	test_ln_s_add top foo

'

test_expect_success 'add everything changed' '

	but add -u &&
	but diff-files >out &&
	test_must_be_empty out

'

test_expect_success 'touch and then add -u' '

	touch check &&
	but add -u &&
	but diff-files >out &&
	test_must_be_empty out

'

test_expect_success 'touch and then add explicitly' '

	touch check &&
	but add check &&
	but diff-files >out &&
	test_must_be_empty out

'

test_expect_success 'add -n -u should not add but just report' '

	(
		echo "add '\''check'\''" &&
		echo "remove '\''top'\''"
	) >expect &&
	before=$(but ls-files -s check top) &&
	but count-objects -v >objects_before &&
	echo changed >>check &&
	rm -f top &&
	but add -n -u >actual &&
	after=$(but ls-files -s check top) &&
	but count-objects -v >objects_after &&

	test "$before" = "$after" &&
	test_cmp objects_before objects_after &&
	test_cmp expect actual

'

test_expect_success 'add -u resolves unmerged paths' '
	but reset --hard &&
	one=$(echo 1 | but hash-object -w --stdin) &&
	two=$(echo 2 | but hash-object -w --stdin) &&
	three=$(echo 3 | but hash-object -w --stdin) &&
	{
		for path in path1 path2
		do
			echo "100644 $one 1	$path" &&
			echo "100644 $two 2	$path" &&
			echo "100644 $three 3	$path" || return 1
		done &&
		echo "100644 $one 1	path3" &&
		echo "100644 $one 1	path4" &&
		echo "100644 $one 3	path5" &&
		echo "100644 $one 3	path6"
	} |
	but update-index --index-info &&
	echo 3 >path1 &&
	echo 2 >path3 &&
	echo 2 >path5 &&

	# Fail to explicitly resolve removed paths with "but add"
	test_must_fail but add --no-all path4 &&
	test_must_fail but add --no-all path6 &&

	# "add -u" should notice removals no matter what stages
	# the index entries are in.
	but add -u &&
	but ls-files -s path1 path2 path3 path4 path5 path6 >actual &&
	{
		echo "100644 $three 0	path1" &&
		echo "100644 $two 0	path3" &&
		echo "100644 $two 0	path5"
	} >expect &&
	test_cmp expect actual
'

test_expect_success '"add -u non-existent" should fail' '
	test_must_fail but add -u non-existent &&
	but ls-files >actual &&
	! grep "non-existent" actual
'

test_done
