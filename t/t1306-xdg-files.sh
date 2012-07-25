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

test_expect_success '"$XDG_CONFIG_HOME overrides $HOME/.config/git' '
	mkdir -p "$HOME"/xdg/git &&
	echo "[user]name = in_xdg" >"$HOME"/xdg/git/config &&
	echo in_xdg >expected &&
	XDG_CONFIG_HOME="$HOME"/xdg git config --get-all user.name >actual &&
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


test_expect_success 'Setup' '
	git init git &&
	cd git &&
	echo foo >to_be_excluded
'


test_expect_success 'Exclusion of a file in the XDG ignore file' '
	mkdir -p "$HOME"/.config/git/ &&
	echo to_be_excluded >"$HOME"/.config/git/ignore &&
	test_must_fail git add to_be_excluded
'

test_expect_success '$XDG_CONFIG_HOME overrides $HOME/.config/git/ignore' '
	mkdir -p "$HOME"/xdg/git &&
	echo content >excluded_by_xdg_only &&
	echo excluded_by_xdg_only >"$HOME"/xdg/git/ignore &&
	test_when_finished "git read-tree --empty" &&
	(XDG_CONFIG_HOME="$HOME/xdg" &&
	 export XDG_CONFIG_HOME &&
	 git add to_be_excluded &&
	 test_must_fail git add excluded_by_xdg_only
	)
'

test_expect_success 'Exclusion in both XDG and local ignore files' '
	echo to_be_excluded >.gitignore &&
	test_must_fail git add to_be_excluded
'


test_expect_success 'Exclusion in a non-XDG global ignore file' '
	rm .gitignore &&
	echo >"$HOME"/.config/git/ignore &&
	echo to_be_excluded >"$HOME"/my_gitignore &&
	git config core.excludesfile "$HOME"/my_gitignore &&
	test_must_fail git add to_be_excluded
'

test_expect_success 'Checking XDG ignore file when HOME is unset' '
	>expected &&
	(sane_unset HOME &&
	 git config --unset core.excludesfile &&
	 git ls-files --exclude-standard --ignored >actual) &&
	test_cmp expected actual
'

test_expect_success 'Checking attributes in the XDG attributes file' '
	echo foo >f &&
	git check-attr -a f >actual &&
	test_line_count -eq 0 actual &&
	echo "f attr_f" >"$HOME"/.config/git/attributes &&
	echo "f: attr_f: set" >expected &&
	git check-attr -a f >actual &&
	test_cmp expected actual
'

test_expect_success 'Checking XDG attributes when HOME is unset' '
	>expected &&
	(sane_unset HOME &&
	 git check-attr -a f >actual) &&
	test_cmp expected actual
'

test_expect_success '$XDG_CONFIG_HOME overrides $HOME/.config/git/attributes' '
	mkdir -p "$HOME"/xdg/git &&
	echo "f attr_f=xdg" >"$HOME"/xdg/git/attributes &&
	echo "f: attr_f: xdg" >expected &&
	XDG_CONFIG_HOME="$HOME/xdg" git check-attr -a f >actual &&
	test_cmp expected actual
'

test_expect_success 'Checking attributes in both XDG and local attributes files' '
	echo "f -attr_f" >.gitattributes &&
	echo "f: attr_f: unset" >expected &&
	git check-attr -a f >actual &&
	test_cmp expected actual
'


test_expect_success 'Checking attributes in a non-XDG global attributes file' '
	test_might_fail rm .gitattributes &&
	echo "f attr_f=test" >"$HOME"/my_gitattributes &&
	git config core.attributesfile "$HOME"/my_gitattributes &&
	echo "f: attr_f: test" >expected &&
	git check-attr -a f >actual &&
	test_cmp expected actual
'


test_expect_success 'write: xdg file exists and ~/.gitconfig doesn'\''t' '
	mkdir -p "$HOME"/.config/git &&
	>"$HOME"/.config/git/config &&
	test_might_fail rm "$HOME"/.gitconfig &&
	git config --global user.name "write_config" &&
	echo "[user]" >expected &&
	echo "	name = write_config" >>expected &&
	test_cmp expected "$HOME"/.config/git/config
'


test_expect_success 'write: xdg file exists and ~/.gitconfig exists' '
	>"$HOME"/.gitconfig &&
	git config --global user.name "write_gitconfig" &&
	echo "[user]" >expected &&
	echo "	name = write_gitconfig" >>expected &&
	test_cmp expected "$HOME"/.gitconfig
'


test_expect_success 'write: ~/.config/git/ exists and config file doesn'\''t' '
	test_might_fail rm "$HOME"/.gitconfig &&
	test_might_fail rm "$HOME"/.config/git/config &&
	git config --global user.name "write_gitconfig" &&
	echo "[user]" >expected &&
	echo "	name = write_gitconfig" >>expected &&
	test_cmp expected "$HOME"/.gitconfig
'


test_done
