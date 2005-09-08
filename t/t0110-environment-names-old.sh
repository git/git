#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Using new and old environment names.

This test makes sure that use of deprecated environment variables
still works, using both new and old names makes new one take precedence,
and GIT_DIR and GIT_ALTERNATE_OBJECT_DIRECTORIES mechanism works.'

env_vars='GIT_AUTHOR_DATE:AUTHOR_DATE
GIT_AUTHOR_EMAIL:AUTHOR_EMAIL
GIT_AUTHOR_NAME:AUTHOR_NAME
GIT_COMMITTER_EMAIL:COMMIT_AUTHOR_EMAIL
GIT_COMMITTER_NAME:COMMIT_AUTHOR_NAME
GIT_ALTERNATE_OBJECT_DIRECTORIES:SHA1_FILE_DIRECTORIES
GIT_OBJECT_DIRECTORY:SHA1_FILE_DIRECTORY
'

. ./test-lib.sh

export_them () {
	for ev in $env_vars
	do
		new=$(expr "$ev" : '\(.*\):')
		old=$(expr "$ev" : '.*:\(.*\)')
		# Build and eval the following:
		# case "${VAR+set}" in set) export VAR;; esac
		evstr='case "${'$new'+set}" in set) export '$new';; esac'
		eval "$evstr"
		evstr='case "${'$old'+set}" in set) export '$old';; esac'
		eval "$evstr"
	done
}

SHA1_FILE_DIRECTORY=.svn/objects ;# whoa
export SHA1_FILE_DIRECTORY

rm -fr .git
mkdir .svn
test_expect_success \
    'using SHA1_FILE_DIRECTORY in git-init-db' \
    'git-init-db && test -d .svn/objects/cb'

unset SHA1_FILE_DIRECTORY
GIT_DIR=.svn
export GIT_DIR
rm -fr .git .svn
mkdir .svn
test_expect_success \
    'using GIT_DIR in git-init-db' \
    'git-init-db && test -d .svn/objects/cb'

date >path0
test_expect_success \
    'using GIT_DIR in git-update-index' \
    'git-update-index --add path0 && test -f .svn/index'

sedScript='s|\(..\)|.svn/objects/\1/|'

test_expect_success \
    'using GIT_DIR in git-write-tree' \
    'tree=$(git-write-tree) &&
     test -f $(echo "$tree" | sed -e "$sedScript")'

AUTHOR_DATE='Sat May 14 00:00:00 2005 -0000'
AUTHOR_EMAIL='author@example.xz'
AUTHOR_NAME='A U Thor'
COMMIT_AUTHOR_EMAIL='author@example.xz'
COMMIT_AUTHOR_NAME='A U Thor'
export_them

test_expect_success \
    'using GIT_DIR and old variable names in git-commit-tree' \
    'commit=$(echo foo | git-commit-tree $tree) &&
     test -f $(echo "$commit" | sed -e "$sedScript")'

test_expect_success \
    'using GIT_DIR in git-cat-file' \
    'git-cat-file commit $commit >current'

cat >expected <<\EOF
author A U Thor <author@example.xz>
committer A U Thor <author@example.xz>
EOF
test_expect_success \
    'verify old AUTHOR variables were used correctly in commit' \
    'sed -ne '\''/^\(author\)/s|>.*|>|p'\'' -e'\''/^\(committer\)/s|>.*|>|p'\''\    current > out && cmp out expected'

unset GIT_DIR
test_expect_success \
    'git-init-db without GIT_DIR' \
    'git-init-db && test -d .git && test -d .git/objects/ef'

SHA1_FILE_DIRECTORIES=.svn/objects
export SHA1_FILE_DIRECTORIES

test_expect_success \
    'using SHA1_FILE_DIRECTORIES with git-ls-tree' \
    'git-ls-tree $commit && git-ls-tree $tree'

GIT_AUTHOR_DATE='Sat May 14 12:00:00 2005 -0000'
GIT_AUTHOR_EMAIL='rohtua@example.xz'
GIT_AUTHOR_NAME='R O Htua'
GIT_COMMITTER_EMAIL='rohtua@example.xz'
GIT_COMMITTER_NAME='R O Htua'
export_them

sedScript='s|\(..\)|.git/objects/\1/|'
test_expect_success \
    'using new author variables with git-commit-tree' \
    'commit2=$(echo foo | git-commit-tree $tree) &&
     test -f $(echo "$commit2" | sed -e "$sedScript")'

GIT_ALTERNATE_OBJECT_DIRECTORIES=.git/objects
GIT_DIR=nowhere
export GIT_DIR GIT_ALTERNATE_OBJECT_DIRECTORIES

test_expect_success \
    'git-cat-file with GIT_DIR and GIT_ALTERNATE_OBJECT_DIRECTORIES' \
    'git-cat-file commit $commit2 >current'

cat >expected <<\EOF
author R O Htua <rohtua@example.xz>
committer R O Htua <rohtua@example.xz>
EOF
test_expect_success \
    'verify new AUTHOR variables were used correctly in commit.' \
    'sed -ne '\''/^\(author\)/s|>.*|>|p'\'' -e'\''/^\(committer\)/s|>.*|>|p'\''\    current > out && cmp out expected'

test_done
