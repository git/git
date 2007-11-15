#!/bin/sh

test_description='git add -u

This test creates a working tree state with three files:

  top (previously committed, modified)
  dir/sub (previously committed, modified)
  dir/other (untracked)

and issues a git add -u with path limiting on "dir" to add
only the updates to dir/sub.

Also tested are "git add -u" without limiting, and "git add -u"
without contents changes.'

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
	test_tick
	git-commit -m initial &&

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
	test "$(git-ls-files dir1/sub1)" = ""
'

test_expect_success 'update touched correct path' '
	test "$(git-diff-files --name-status dir2/sub3)" = ""
'

test_expect_success 'update did not touch other tracked files' '
	test "$(git-diff-files --name-status check)" = "M	check" &&
	test "$(git-diff-files --name-status top)" = "M	top"
'

test_expect_success 'update did not touch untracked files' '
	test "$(git-ls-files dir2/other)" = ""
'

test_expect_success 'cache tree has not been corrupted' '

	git ls-files -s |
	sed -e "s/ 0	/	/" >expect &&
	git ls-tree -r $(git write-tree) |
	sed -e "s/ blob / /" >current &&
	diff -u expect current

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

test_expect_success 'replace a file with a symlink' '

	rm foo &&
	ln -s top foo &&
	git add -u -- foo

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

test_done
