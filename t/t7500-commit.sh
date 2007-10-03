#!/bin/sh
#
# Copyright (c) 2007 Steven Grimm
#

test_description='git-commit

Tests for selected commit options.'

. ./test-lib.sh

commit_msg_is () {
	test "`git log --pretty=format:%s%b -1`" = "$1"
}

# A sanity check to see if commit is working at all.
test_expect_success 'a basic commit in an empty tree should succeed' '
	echo content > foo &&
	git add foo &&
	git commit -m "initial commit"
'

test_expect_success 'nonexistent template file should return error' '
	echo changes >> foo &&
	git add foo &&
	! git commit --template "$PWD"/notexist
'

test_expect_success 'nonexistent template file in config should return error' '
	git config commit.template "$PWD"/notexist &&
	! git commit &&
	git config --unset commit.template
'

# From now on we'll use a template file that exists.
TEMPLATE="$PWD"/template

test_expect_success 'unedited template should not commit' '
	echo "template line" > "$TEMPLATE" &&
	! git commit --template "$TEMPLATE"
'

test_expect_success 'unedited template with comments should not commit' '
	echo "# comment in template" >> "$TEMPLATE" &&
	! git commit --template "$TEMPLATE"
'

test_expect_success 'a Signed-off-by line by itself should not commit' '
	! GIT_EDITOR=../t7500/add-signed-off git commit --template "$TEMPLATE"
'

test_expect_success 'adding comments to a template should not commit' '
	! GIT_EDITOR=../t7500/add-comments git commit --template "$TEMPLATE"
'

test_expect_success 'adding real content to a template should commit' '
	GIT_EDITOR=../t7500/add-content git commit --template "$TEMPLATE" &&
	commit_msg_is "template linecommit message"
'

test_expect_success '-t option should be short for --template' '
	echo "short template" > "$TEMPLATE" &&
	echo "new content" >> foo &&
	git add foo &&
	GIT_EDITOR=../t7500/add-content git commit -t "$TEMPLATE" &&
	commit_msg_is "short templatecommit message"
'

test_expect_success 'config-specified template should commit' '
	echo "new template" > "$TEMPLATE" &&
	git config commit.template "$TEMPLATE" &&
	echo "more content" >> foo &&
	git add foo &&
	GIT_EDITOR=../t7500/add-content git commit &&
	git config --unset commit.template &&
	commit_msg_is "new templatecommit message"
'

test_expect_success 'explicit commit message should override template' '
	echo "still more content" >> foo &&
	git add foo &&
	GIT_EDITOR=../t7500/add-content git commit --template "$TEMPLATE" \
		-m "command line msg" &&
	commit_msg_is "command line msg"
'

test_expect_success 'commit message from file should override template' '
	echo "content galore" >> foo &&
	git add foo &&
	echo "standard input msg" |
		GIT_EDITOR=../t7500/add-content git commit \
			--template "$TEMPLATE" --file - &&
	commit_msg_is "standard input msg"
'

test_done
