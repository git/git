#!/bin/sh
# shellcheck disable=SC2016

# shellcheck disable=SC2034
test_description='git add respects submodule ignore=all and explicit pathspec'

# This test covers the behavior of "git add", "git status" and "git log" when
# dealing with submodules that have the ignore=all setting in
# .gitmodules. It ensures that changes in such submodules are
# ignored by default, but can be staged with "git add --force".

# shellcheck disable=SC1091
. ./test-lib.sh

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

base_path=$(pwd -P)

#1
test_expect_success 'setup: create origin repos'  '
	cd "${base_path}" &&
	git config --global protocol.file.allow always &&
	git init sub &&
		pwd &&
		cd sub &&
		test_commit sub_file1 &&
		git tag v1.0 &&
		test_commit sub_file2 &&
		git tag v2.0 &&
		test_commit sub_file3 &&
		git tag v3.0 &&
	cd "${base_path}" &&
	git init main &&
		cd main &&
		test_commit first &&
	cd "${base_path}"
'
#2
# add submodule with default config (ignore=none) and
# check log that is contains a path entry for the submodule 'sub'
# change the commit in the submodule and check that 'git status' shows it as modified
test_expect_success 'main: add submodule with default config'  '
	cd "${base_path}" &&
	cd main &&
	git submodule add ../sub &&
	git commit -m "add submodule" &&
	git log --oneline --name-only | grep "^sub$" &&
	git -C sub reset --hard v2.0 &&
	git status --porcelain | grep "^ M sub$" &&
	echo
'
#3
# change the submodule config to ignore=all and check that status and log do not show changes
test_expect_success 'main: submodule config ignore=all'  '
	cd "${base_path}" &&
	cd main &&
	git config -f .gitmodules submodule.sub.ignore all &&
	GIT_TRACE=1 git add . &&
	git commit -m "update submodule config sub.ignore all" &&
	! git status --porcelain | grep "^.*$" &&
	! git log --oneline --name-only | grep "^sub$" &&
	echo
'
#4
# change the commit in the submodule and check that 'git status' does not show it as modified
# but 'git status --ignore-submodules=none' does show it as modified
test_expect_success 'sub: change to different sha1 and check status in main'  '
	cd "${base_path}" &&
	cd main &&
	git -C sub reset --hard v1.0 &&
	! git status --porcelain | grep "^ M sub$" &&
	git status --ignore-submodules=none --porcelain | grep "^ M sub$" &&
	echo
'

#5
# check that normal 'git add' does not stage the change in the submodule
test_expect_success 'main: check normal add and status'  '
	cd "${base_path}" &&
	cd main &&
	GIT_TRACE=1 git add . &&
	! git status --porcelain | grep "^ M sub$" &&
	echo
'

#6
# check that 'git add --force .' does not stage the change in the submodule
# and that 'git status' does not show it as modified
test_expect_success 'main: check --force add . and status'  '
	cd "${base_path}" &&
	cd main &&
	GIT_TRACE=1 git add --force . &&
	! git status --porcelain | grep "^M  sub$" &&
	echo
'

#7
# check that 'git add .' does not stage the change in the submodule
# and that 'git status' does not show it as modified
test_expect_success 'main: check _add sub_ and status'  '
	cd "${base_path}" &&
	cd main &&
	GIT_TRACE=1 git add sub 2>&1 | grep "Skipping submodule due to ignore=all: sub" &&
	! git status --porcelain | grep "^M  sub$" &&
	echo
'

#8
# check that 'git add --force sub' does stage the change in the submodule
# check that 'git add --force ./sub/' does stage the change in the submodule
# and that 'git status --porcelain' does show it as modified
# commit it..
# check that 'git log --ignore-submodules=none' shows the submodule change
# in the log
test_expect_success 'main: check force add sub and ./sub/ and status'  '
	cd "${base_path}" &&
	cd main &&
	echo "Adding with --force should work: git add --force sub" &&
	GIT_TRACE=1 git add --force sub &&
	git status --porcelain | grep "^M  sub$" &&
	git restore --staged sub &&
	! git status --porcelain | grep "^M  sub$" &&
	echo "Adding with --force should work: git add --force ./sub/" &&
	GIT_TRACE=1 git add --force ./sub/ &&
	git status --porcelain | grep "^M  sub$" &&
	git commit -m "update submodule pointer" &&
	! git status --porcelain | grep "^ M sub$" &&
	git log --ignore-submodules=none --name-only --oneline | grep "^sub$" &&
	echo
'

test_done
exit 0
