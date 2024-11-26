#!/bin/sh
#
# Copyright (c) 2012 Daniel Graña
#

test_description='Test submodules on detached working tree

This test verifies that "git submodule" initialization, update and addition works
on detached working trees
'

TEST_NO_CREATE_REPO=1
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'submodule on detached working tree' '
	git init --bare remote &&
	test_create_repo bundle1 &&
	(
		cd bundle1 &&
		test_commit "shoot" &&
		git rev-parse --verify HEAD >../expect
	) &&
	mkdir home &&
	(
		cd home &&
		GIT_WORK_TREE="$(pwd)" &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_WORK_TREE GIT_DIR &&
		git clone --bare ../remote .dotfiles &&
		git submodule add ../bundle1 .vim/bundle/sogood &&
		test_commit "sogood" &&
		(
			unset GIT_WORK_TREE GIT_DIR &&
			cd .vim/bundle/sogood &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		) &&
		git push origin main
	) &&
	mkdir home2 &&
	(
		cd home2 &&
		git clone --bare ../remote .dotfiles &&
		GIT_WORK_TREE="$(pwd)" &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_WORK_TREE GIT_DIR &&
		git checkout main &&
		git submodule update --init &&
		(
			unset GIT_WORK_TREE GIT_DIR &&
			cd .vim/bundle/sogood &&
			git rev-parse --verify HEAD >actual &&
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
		git clone --bare ../remote "$GIT_DIR" &&
		git config core.bare false &&
		git config core.worktree .. &&
		git checkout main &&
		git submodule add ../bundle1 .vim/bundle/dupe &&
		test_commit "dupe" &&
		git push origin main
	) &&
	(
		cd home &&
		GIT_DIR="$(pwd)/.dotfiles" &&
		export GIT_DIR &&
		git config core.bare false &&
		git config core.worktree .. &&
		git pull &&
		git submodule update --init &&
		test -f .vim/bundle/dupe/shoot.t
	)
'

test_done
