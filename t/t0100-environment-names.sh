#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='general environment name warning test.

This test makes sure that use of deprecated environment variables
trigger the warnings from gitenv().'

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

date >path0
git-update-cache --add path0
tree=$(git-write-tree)

AUTHOR_DATE='Wed May 11 23:55:18 2005'
AUTHOR_EMAIL='author@example.xz'
AUTHOR_NAME='A U Thor'
COMMIT_AUTHOR_EMAIL='author@example.xz'
COMMIT_AUTHOR_NAME='A U Thor'
SHA1_FILE_DIRECTORY=.git/objects

export_them

echo 'foo' | git-commit-tree $tree >/dev/null 2>errmsg
cat >expected-err <<\EOF
warning: Attempting to use SHA1_FILE_DIRECTORY
warning: GIT environment variables have been renamed.
warning: Please adjust your scripts and environment.
warning: old AUTHOR_DATE => new GIT_AUTHOR_DATE
warning: old AUTHOR_EMAIL => new GIT_AUTHOR_EMAIL
warning: old AUTHOR_NAME => new GIT_AUTHOR_NAME
warning: old COMMIT_AUTHOR_EMAIL => new GIT_COMMITTER_EMAIL
warning: old COMMIT_AUTHOR_NAME => new GIT_COMMITTER_NAME
warning: old SHA1_FILE_DIRECTORY => new GIT_OBJECT_DIRECTORY
EOF
sed -ne '/^warning: /p' <errmsg >generated-err

test_expect_success \
    'using old names should issue warnings.' \
    'cmp generated-err expected-err'

for ev in $env_vars
do
	new=$(expr "$ev" : '\(.*\):')
	old=$(expr "$ev" : '.*:\(.*\)')
	# Build and eval the following:
	# NEWENV=$OLDENV
	evstr="$new=\$$old"
	eval "$evstr"
done
export_them
echo 'foo' | git-commit-tree $tree >/dev/null 2>errmsg
sed -ne '/^warning: /p' <errmsg >generated-err

test_expect_success \
    'using old names but having new names should not issue warnings.' \
    'cmp generated-err /dev/null'

test_done
