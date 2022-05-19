#!/bin/sh
#
# Copyright (c) 2012 Daniel Graña
#

test_description='Test submodules on detached working tree

This test verifies that "but submodule" initialization, update and addition works
on detached working trees
'

TEST_NO_CREATE_REPO=1
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'submodule on detached working tree' '
	but init --bare remote &&
	test_create_repo bundle1 &&
	(
		cd bundle1 &&
		test_cummit "shoot" &&
		but rev-parse --verify HEAD >../expect
	) &&
	mkdir home &&
	(
		cd home &&
		GIT_WORK_TREE="$(pwd)" &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_WORK_TREE GIT_DIR &&
		but clone --bare ../remote .dotfiles &&
		but submodule add ../bundle1 .vim/bundle/sogood &&
		test_cummit "sogood" &&
		(
			unset GIT_WORK_TREE GIT_DIR &&
			cd .vim/bundle/sogood &&
			but rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		) &&
		but push origin main
	) &&
	mkdir home2 &&
	(
		cd home2 &&
		but clone --bare ../remote .dotfiles &&
		GIT_WORK_TREE="$(pwd)" &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_WORK_TREE GIT_DIR &&
		but checkout main &&
		but submodule update --init &&
		(
			unset GIT_WORK_TREE GIT_DIR &&
			cd .vim/bundle/sogood &&
			but rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		)
	)
'

test_expect_success 'submodule on detached working pointed by core.worktree' '
	mkdir home3 &&
	(
		cd home3 &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_DIR &&
		but clone --bare ../remote "$GIT_DIR" &&
		but config core.bare false &&
		but config core.worktree .. &&
		but checkout main &&
		but submodule add ../bundle1 .vim/bundle/dupe &&
		test_cummit "dupe" &&
		but push origin main
	) &&
	(
		cd home &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_DIR &&
		but config core.bare false &&
		but config core.worktree .. &&
		but pull &&
		but submodule update --init &&
		test -f .vim/bundle/dupe/shoot.t
	)
'

test_done
