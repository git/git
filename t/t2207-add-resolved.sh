#!/bin/sh

test_description='git add --resolved

Test that "git add --resolved" stages conflict-resolved paths and
refuses to stage when conflict markers remain.'

. ./test-lib.sh

test_expect_success 'setup repo' '
	echo base >file1.txt &&
	echo base >file2.txt &&
	echo base >file3.txt &&
	echo base >file4.txt &&
	git add file1.txt file2.txt file3.txt file4.txt &&
	git commit -m initial &&

	git branch topic &&
	echo "ours 1" >file1.txt &&
	echo "ours 2" >file2.txt &&
	echo "ours 3" >file3.txt &&
	git commit -a -m ours &&

	git checkout topic &&
	echo "theirs 1" >file1.txt &&
	echo "theirs 2" >file2.txt &&
	echo "theirs 3" >file3.txt &&
	git commit -a -m theirs &&

	git checkout @{-1}
'

test_expect_success 'git add --resolved refuses files with conflict markers' '
	test_when_finished "git reset --hard HEAD" &&
	test_must_fail git merge topic &&
	echo "resolved 1" >file1.txt &&
	test_must_fail git add --resolved 2>err &&
	test_grep "the following paths still have conflict markers:" err &&
	test_grep "file2.txt" err &&
	test_grep "file3.txt" err &&
	# Index should remain unmerged for all files
	git ls-files -u file1.txt >unmerged &&
	test_line_count = 3 unmerged
'

test_expect_success 'git add --resolved succeeds when all conflict markers are removed' '
	test_when_finished "git reset --hard HEAD" &&
	test_must_fail git merge topic &&
	echo "resolved 1" >file1.txt &&
	echo "resolved 2" >file2.txt &&
	echo "resolved 3" >file3.txt &&
	git add --resolved &&
	git ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	git ls-files -s file1.txt file2.txt file3.txt >staged &&
	test_line_count = 3 staged
'

test_expect_success 'git add --resolved ignores unconflicted modified files' '
	test_when_finished "git reset --hard HEAD" &&
	echo "unconflicted local change" >>file4.txt &&
	test_must_fail git merge topic &&
	echo "resolved 1" >file1.txt &&
	echo "resolved 2" >file2.txt &&
	echo "resolved 3" >file3.txt &&
	git add --resolved &&
	# file1, file2, file3 should be staged as resolved
	git ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	# file4 should remain unstaged in working tree
	git diff file4.txt >diff_out &&
	test_grep "unconflicted local change" diff_out &&
	git diff --cached file4.txt >cached_out &&
	test_must_be_empty cached_out
'

test_expect_success 'git add --resolved handles file removals' '
	test_when_finished "git reset --hard HEAD" &&
	test_must_fail git merge topic &&
	echo "resolved 1" >file1.txt &&
	rm file2.txt &&
	echo "resolved 3" >file3.txt &&
	git add --resolved &&
	git ls-files -s file2.txt >out &&
	test_must_be_empty out
'

test_expect_success 'git add --resolved honors pathspec' '
	test_when_finished "git reset --hard HEAD" &&
	test_must_fail git merge topic &&
	echo "resolved 1" >file1.txt &&
	# file2.txt and file3.txt still have conflict markers,
	# but pathspec targets only file1.txt
	git add --resolved file1.txt &&
	git ls-files -u file1.txt >unmerged1 &&
	test_must_be_empty unmerged1 &&
	git ls-files -u file2.txt >unmerged2 &&
	test_line_count = 3 unmerged2
'

test_expect_success 'git add --resolved incompatibility with -u and -A' '
	test_must_fail git add --resolved -u 2>err1 &&
	test_grep "cannot be used together" err1 &&
	test_must_fail git add --resolved -A 2>err2 &&
	test_grep "cannot be used together" err2
'

test_done
