#!/bin/sh
#
# Copyright (c) 2012 Valentin Duperray, Lucien Kong, Franck Jonas,
#		     Thomas Nguy, Khoi Nguyen
#		     Grenoble INP Ensimag
#

test_description='Compatibility with $XDG_CONFIG_HOME/git/ files'

. ./test-lib.sh

test_expect_success 'read config: xdg file exists and ~/.gitconfig doesn'\''t' '
	mkdir -p .config/git &&
	echo "[alias]" >.config/git/config &&
	echo "	myalias = !echo in_config" >>.config/git/config &&
	echo in_config >expected &&
	git myalias >actual &&
	test_cmp expected actual
'


test_expect_success 'read config: xdg file exists and ~/.gitconfig exists' '
	>.gitconfig &&
	echo "[alias]" >.gitconfig &&
	echo "	myalias = !echo in_gitconfig" >>.gitconfig &&
	echo in_gitconfig >expected &&
	git myalias >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --get: xdg file exists and ~/.gitconfig doesn'\''t' '
	rm .gitconfig &&
	echo "[user]" >.config/git/config &&
	echo "	name = read_config" >>.config/git/config &&
	echo read_config >expected &&
	git config --get user.name >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --get: xdg file exists and ~/.gitconfig exists' '
	>.gitconfig &&
	echo "[user]" >.gitconfig &&
	echo "	name = read_gitconfig" >>.gitconfig &&
	echo read_gitconfig >expected &&
	git config --get user.name >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --list: xdg file exists and ~/.gitconfig doesn'\''t' '
	rm .gitconfig &&
	echo user.name=read_config >expected &&
	git config --global --list >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --list: xdg file exists and ~/.gitconfig exists' '
	>.gitconfig &&
	echo "[user]" >.gitconfig &&
	echo "	name = read_gitconfig" >>.gitconfig &&
	echo user.name=read_gitconfig >expected &&
	git config --global --list >actual &&
	test_cmp expected actual
'


test_done
