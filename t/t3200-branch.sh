#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git branch --foo should not create bogus branch

This test runs git branch --help and checks that the argument is properly
handled.  Specifically, that a bogus branch is not created.
'
. ./test-lib.sh

test_expect_success \
    'prepare an trivial repository' \
    'echo Hello > A &&
     git-update-index --add A &&
     git-commit -m "Initial commit." &&
     HEAD=$(git-rev-parse --verify HEAD)'

test_expect_success \
    'git branch --help should return success now.' \
    'git-branch --help'

test_expect_failure \
    'git branch --help should not have created a bogus branch' \
    'test -f .git/refs/heads/--help'

test_expect_success \
    'git branch abc should create a branch' \
    'git-branch abc && test -f .git/refs/heads/abc'

test_expect_success \
    'git branch a/b/c should create a branch' \
    'git-branch a/b/c && test -f .git/refs/heads/a/b/c'

cat >expect <<EOF
0000000000000000000000000000000000000000 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from HEAD
EOF
test_expect_success \
    'git branch -l d/e/f should create a branch and a log' \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
     git-branch -l d/e/f &&
	 test -f .git/refs/heads/d/e/f &&
	 test -f .git/logs/refs/heads/d/e/f &&
	 diff expect .git/logs/refs/heads/d/e/f'

test_expect_success \
    'git branch -d d/e/f should delete a branch and a log' \
	'git-branch -d d/e/f &&
	 test ! -f .git/refs/heads/d/e/f &&
	 test ! -f .git/logs/refs/heads/d/e/f'

cat >expect <<EOF
0000000000000000000000000000000000000000 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	checkout: Created from master^0
EOF
test_expect_success \
    'git checkout -b g/h/i -l should create a branch and a log' \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
     git-checkout -b g/h/i -l master &&
	 test -f .git/refs/heads/g/h/i &&
	 test -f .git/logs/refs/heads/g/h/i &&
	 diff expect .git/logs/refs/heads/g/h/i'

test_expect_success \
    'git branch j/k should work after branch j has been deleted' \
       'git-branch j &&
        git-branch -d j &&
        git-branch j/k'

test_expect_success \
    'git branch l should work after branch l/m has been deleted' \
       'git-branch l/m &&
        git-branch -d l/m &&
        git-branch l'

test_done
