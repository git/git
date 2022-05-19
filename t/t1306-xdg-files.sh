#!/bin/sh
#
# Copyright (c) 2012 Valentin Duperray, Lucien Kong, Franck Jonas,
#		     Thomas Nguy, Khoi Nguyen
#		     Grenoble INP Ensimag
#

test_description='Compatibility with $XDG_CONFIG_HOME/but/ files'

. ./test-lib.sh

test_expect_success 'read config: xdg file exists and ~/.butconfig doesn'\''t' '
	mkdir -p .config/but &&
	echo "[alias]" >.config/but/config &&
	echo "	myalias = !echo in_config" >>.config/but/config &&
	echo in_config >expected &&
	but myalias >actual &&
	test_cmp expected actual
'


test_expect_success 'read config: xdg file exists and ~/.butconfig exists' '
	>.butconfig &&
	echo "[alias]" >.butconfig &&
	echo "	myalias = !echo in_butconfig" >>.butconfig &&
	echo in_butconfig >expected &&
	but myalias >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --get: xdg file exists and ~/.butconfig doesn'\''t' '
	rm .butconfig &&
	echo "[user]" >.config/but/config &&
	echo "	name = read_config" >>.config/but/config &&
	echo read_config >expected &&
	but config --get user.name >actual &&
	test_cmp expected actual
'

test_expect_success '"$XDG_CONFIG_HOME overrides $HOME/.config/but' '
	mkdir -p "$HOME"/xdg/but &&
	echo "[user]name = in_xdg" >"$HOME"/xdg/but/config &&
	echo in_xdg >expected &&
	XDG_CONFIG_HOME="$HOME"/xdg but config --get-all user.name >actual &&
	test_cmp expected actual
'

test_expect_success 'read with --get: xdg file exists and ~/.butconfig exists' '
	>.butconfig &&
	echo "[user]" >.butconfig &&
	echo "	name = read_butconfig" >>.butconfig &&
	echo read_butconfig >expected &&
	but config --get user.name >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --list: xdg file exists and ~/.butconfig doesn'\''t' '
	rm .butconfig &&
	echo user.name=read_config >expected &&
	but config --global --list >actual &&
	test_cmp expected actual
'


test_expect_success 'read with --list: xdg file exists and ~/.butconfig exists' '
	>.butconfig &&
	echo "[user]" >.butconfig &&
	echo "	name = read_butconfig" >>.butconfig &&
	echo user.name=read_butconfig >expected &&
	but config --global --list >actual &&
	test_cmp expected actual
'


test_expect_success 'Setup' '
	but init but &&
	cd but &&
	echo foo >to_be_excluded
'


test_expect_success 'Exclusion of a file in the XDG ignore file' '
	mkdir -p "$HOME"/.config/but/ &&
	echo to_be_excluded >"$HOME"/.config/but/ignore &&
	test_must_fail but add to_be_excluded
'

test_expect_success '$XDG_CONFIG_HOME overrides $HOME/.config/but/ignore' '
	mkdir -p "$HOME"/xdg/but &&
	echo content >excluded_by_xdg_only &&
	echo excluded_by_xdg_only >"$HOME"/xdg/but/ignore &&
	test_when_finished "but read-tree --empty" &&
	(XDG_CONFIG_HOME="$HOME/xdg" &&
	 export XDG_CONFIG_HOME &&
	 but add to_be_excluded &&
	 test_must_fail but add excluded_by_xdg_only
	)
'

test_expect_success 'Exclusion in both XDG and local ignore files' '
	echo to_be_excluded >.butignore &&
	test_must_fail but add to_be_excluded
'


test_expect_success 'Exclusion in a non-XDG global ignore file' '
	rm .butignore &&
	echo >"$HOME"/.config/but/ignore &&
	echo to_be_excluded >"$HOME"/my_butignore &&
	but config core.excludesfile "$HOME"/my_butignore &&
	test_must_fail but add to_be_excluded
'

test_expect_success 'Checking XDG ignore file when HOME is unset' '
	(sane_unset HOME &&
	 but config --unset core.excludesfile &&
	 but ls-files --exclude-standard --ignored --others >actual) &&
	test_must_be_empty actual
'

test_expect_success 'Checking attributes in the XDG attributes file' '
	echo foo >f &&
	but check-attr -a f >actual &&
	test_line_count -eq 0 actual &&
	echo "f attr_f" >"$HOME"/.config/but/attributes &&
	echo "f: attr_f: set" >expected &&
	but check-attr -a f >actual &&
	test_cmp expected actual
'

test_expect_success 'Checking XDG attributes when HOME is unset' '
	(sane_unset HOME &&
	 but check-attr -a f >actual) &&
	test_must_be_empty actual
'

test_expect_success '$XDG_CONFIG_HOME overrides $HOME/.config/but/attributes' '
	mkdir -p "$HOME"/xdg/but &&
	echo "f attr_f=xdg" >"$HOME"/xdg/but/attributes &&
	echo "f: attr_f: xdg" >expected &&
	XDG_CONFIG_HOME="$HOME/xdg" but check-attr -a f >actual &&
	test_cmp expected actual
'

test_expect_success 'Checking attributes in both XDG and local attributes files' '
	echo "f -attr_f" >.butattributes &&
	echo "f: attr_f: unset" >expected &&
	but check-attr -a f >actual &&
	test_cmp expected actual
'


test_expect_success 'Checking attributes in a non-XDG global attributes file' '
	rm -f .butattributes &&
	echo "f attr_f=test" >"$HOME"/my_butattributes &&
	but config core.attributesfile "$HOME"/my_butattributes &&
	echo "f: attr_f: test" >expected &&
	but check-attr -a f >actual &&
	test_cmp expected actual
'


test_expect_success 'write: xdg file exists and ~/.butconfig doesn'\''t' '
	mkdir -p "$HOME"/.config/but &&
	>"$HOME"/.config/but/config &&
	rm -f "$HOME"/.butconfig &&
	but config --global user.name "write_config" &&
	echo "[user]" >expected &&
	echo "	name = write_config" >>expected &&
	test_cmp expected "$HOME"/.config/but/config
'


test_expect_success 'write: xdg file exists and ~/.butconfig exists' '
	>"$HOME"/.butconfig &&
	but config --global user.name "write_butconfig" &&
	echo "[user]" >expected &&
	echo "	name = write_butconfig" >>expected &&
	test_cmp expected "$HOME"/.butconfig
'


test_expect_success 'write: ~/.config/but/ exists and config file doesn'\''t' '
	rm -f "$HOME"/.butconfig &&
	rm -f "$HOME"/.config/but/config &&
	but config --global user.name "write_butconfig" &&
	echo "[user]" >expected &&
	echo "	name = write_butconfig" >>expected &&
	test_cmp expected "$HOME"/.butconfig
'


test_done
