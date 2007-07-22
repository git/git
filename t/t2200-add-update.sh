#!/bin/sh

test_description='git add -u with path limiting

This test creates a working tree state with three files:

  top (previously committed, modified)
  dir/sub (previously committed, modified)
  dir/other (untracked)

and issues a git add -u with path limiting on "dir" to add
only the updates to dir/sub.'

. ./test-lib.sh

test_expect_success 'setup' '
echo initial >top &&
mkdir dir &&
echo initial >dir/sub &&
git add dir/sub top &&
git-commit -m initial &&
echo changed >top &&
echo changed >dir/sub &&
echo other >dir/other
'

test_expect_success 'update' 'git add -u dir'

test_expect_success 'update touched correct path' \
  'test "`git diff-files --name-status dir/sub`" = ""'

test_expect_success 'update did not touch other tracked files' \
  'test "`git diff-files --name-status top`" = "M	top"'

test_expect_success 'update did not touch untracked files' \
  'test "`git diff-files --name-status dir/other`" = ""'

test_done
