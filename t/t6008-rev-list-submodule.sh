#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git rev-list involving submodules that this repo has'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	: > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo 1 > file &&
	test_tick &&
	git commit -m second file &&
	echo 2 > file &&
	test_tick &&
	git commit -m third file &&

	rm .git/index &&

	: > super-file &&
	git add super-file &&
	git -c protocol.file.allow=always submodule add "$(pwd)" sub &&
	git symbolic-ref HEAD refs/heads/super &&
	test_tick &&
	git commit -m super-initial &&
	echo 1 > super-file &&
	test_tick &&
	git commit -m super-first super-file &&
	echo 2 > super-file &&
	test_tick &&
	git commit -m super-second super-file
'

test_expect_success "Ilari's test" '
	git rev-list --objects super main ^super^
'

test_done
