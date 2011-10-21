#!/bin/sh
#
# Copyright (c) 2009, 2010 David Aguilar
#

test_description='git-difftool

Testing basic diff tool invocation
'

. ./test-lib.sh

remove_config_vars()
{
	# Unset all config variables used by git-difftool
	git config --unset diff.tool
	git config --unset diff.guitool
	git config --unset difftool.test-tool.cmd
	git config --unset difftool.prompt
	git config --unset merge.tool
	git config --unset mergetool.test-tool.cmd
	git config --unset mergetool.prompt
	return 0
}

restore_test_defaults()
{
	# Restores the test defaults used by several tests
	remove_config_vars
	unset GIT_DIFF_TOOL
	unset GIT_DIFFTOOL_PROMPT
	unset GIT_DIFFTOOL_NO_PROMPT
	git config diff.tool test-tool &&
	git config difftool.test-tool.cmd 'cat $LOCAL'
	git config difftool.bogus-tool.cmd false
}

prompt_given()
{
	prompt="$1"
	test "$prompt" = "Launch 'test-tool' [Y/n]: branch"
}

stdin_contains()
{
	grep >/dev/null "$1"
}

stdin_doesnot_contain()
{
	! stdin_contains "$1"
}

# Create a file on master and change it on branch
test_expect_success PERL 'setup' '
	echo master >file &&
	git add file &&
	git commit -m "added file" &&

	git checkout -b branch master &&
	echo branch >file &&
	git commit -a -m "branch changed file" &&
	git checkout master
'

# Configure a custom difftool.<tool>.cmd and use it
test_expect_success PERL 'custom commands' '
	restore_test_defaults &&
	git config difftool.test-tool.cmd "cat \$REMOTE" &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "master" &&

	restore_test_defaults &&
	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch"
'

# Ensures that git-difftool ignores bogus --tool values
test_expect_success PERL 'difftool ignores bad --tool values' '
	diff=$(git difftool --no-prompt --tool=bad-tool branch)
	test "$?" = 1 &&
	test "$diff" = ""
'

test_expect_success PERL 'difftool honors --gui' '
	git config merge.tool bogus-tool &&
	git config diff.tool bogus-tool &&
	git config diff.guitool test-tool &&

	diff=$(git difftool --no-prompt --gui branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

test_expect_success PERL 'difftool --gui works without configured diff.guitool' '
	git config diff.tool test-tool &&

	diff=$(git difftool --no-prompt --gui branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Specify the diff tool using $GIT_DIFF_TOOL
test_expect_success PERL 'GIT_DIFF_TOOL variable' '
	test_might_fail git config --unset diff.tool &&
	GIT_DIFF_TOOL=test-tool &&
	export GIT_DIFF_TOOL &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test the $GIT_*_TOOL variables and ensure
# that $GIT_DIFF_TOOL always wins unless --tool is specified
test_expect_success PERL 'GIT_DIFF_TOOL overrides' '
	git config diff.tool bogus-tool &&
	git config merge.tool bogus-tool &&

	GIT_DIFF_TOOL=test-tool &&
	export GIT_DIFF_TOOL &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&

	GIT_DIFF_TOOL=bogus-tool &&
	export GIT_DIFF_TOOL &&

	diff=$(git difftool --no-prompt --tool=test-tool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that we don't have to pass --no-prompt to difftool
# when $GIT_DIFFTOOL_NO_PROMPT is true
test_expect_success PERL 'GIT_DIFFTOOL_NO_PROMPT variable' '
	GIT_DIFFTOOL_NO_PROMPT=true &&
	export GIT_DIFFTOOL_NO_PROMPT &&

	diff=$(git difftool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# git-difftool supports the difftool.prompt variable.
# Test that GIT_DIFFTOOL_PROMPT can override difftool.prompt = false
test_expect_success PERL 'GIT_DIFFTOOL_PROMPT variable' '
	git config difftool.prompt false &&
	GIT_DIFFTOOL_PROMPT=true &&
	export GIT_DIFFTOOL_PROMPT &&

	prompt=$(echo | git difftool branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# Test that we don't have to pass --no-prompt when difftool.prompt is false
test_expect_success PERL 'difftool.prompt config variable is false' '
	git config difftool.prompt false &&

	diff=$(git difftool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that we don't have to pass --no-prompt when mergetool.prompt is false
test_expect_success PERL 'difftool merge.prompt = false' '
	test_might_fail git config --unset difftool.prompt &&
	git config mergetool.prompt false &&

	diff=$(git difftool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that the -y flag can override difftool.prompt = true
test_expect_success PERL 'difftool.prompt can overridden with -y' '
	git config difftool.prompt true &&

	diff=$(git difftool -y branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that the --prompt flag can override difftool.prompt = false
test_expect_success PERL 'difftool.prompt can overridden with --prompt' '
	git config difftool.prompt false &&

	prompt=$(echo | git difftool --prompt branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# Test that the last flag passed on the command-line wins
test_expect_success PERL 'difftool last flag wins' '
	diff=$(git difftool --prompt --no-prompt branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults &&

	prompt=$(echo | git difftool --no-prompt --prompt branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# git-difftool falls back to git-mergetool config variables
# so test that behavior here
test_expect_success PERL 'difftool + mergetool config variables' '
	remove_config_vars &&
	git config merge.tool test-tool &&
	git config mergetool.test-tool.cmd "cat \$LOCAL" &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&

	# set merge.tool to something bogus, diff.tool to test-tool
	git config merge.tool bogus-tool &&
	git config diff.tool test-tool &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

test_expect_success PERL 'difftool.<tool>.path' '
	git config difftool.tkdiff.path echo &&
	diff=$(git difftool --tool=tkdiff --no-prompt branch) &&
	git config --unset difftool.tkdiff.path &&
	lines=$(echo "$diff" | grep file | wc -l) &&
	test "$lines" -eq 1 &&

	restore_test_defaults
'

test_expect_success PERL 'difftool --extcmd=cat' '
	diff=$(git difftool --no-prompt --extcmd=cat branch) &&
	test "$diff" = branch"$LF"master
'

test_expect_success PERL 'difftool --extcmd cat' '
	diff=$(git difftool --no-prompt --extcmd cat branch) &&
	test "$diff" = branch"$LF"master
'

test_expect_success PERL 'difftool -x cat' '
	diff=$(git difftool --no-prompt -x cat branch) &&
	test "$diff" = branch"$LF"master
'

test_expect_success PERL 'difftool --extcmd echo arg1' '
	diff=$(git difftool --no-prompt --extcmd sh\ -c\ \"echo\ \$1\" branch) &&
	test "$diff" = file
'

test_expect_success PERL 'difftool --extcmd cat arg1' '
	diff=$(git difftool --no-prompt --extcmd sh\ -c\ \"cat\ \$1\" branch) &&
	test "$diff" = master
'

test_expect_success PERL 'difftool --extcmd cat arg2' '
	diff=$(git difftool --no-prompt --extcmd sh\ -c\ \"cat\ \$2\" branch) &&
	test "$diff" = branch
'

# Create a second file on master and a different version on branch
test_expect_success PERL 'setup with 2 files different' '
	echo m2 >file2 &&
	git add file2 &&
	git commit -m "added file2" &&

	git checkout branch &&
	echo br2 >file2 &&
	git add file2 &&
	git commit -a -m "branch changed file2" &&
	git checkout master
'

test_expect_success PERL 'say no to the first file' '
	diff=$( (echo n; echo) | git difftool -x cat branch ) &&

	echo "$diff" | stdin_contains m2 &&
	echo "$diff" | stdin_contains br2 &&
	echo "$diff" | stdin_doesnot_contain master &&
	echo "$diff" | stdin_doesnot_contain branch
'

test_expect_success PERL 'say no to the second file' '
	diff=$( (echo; echo n) | git difftool -x cat branch ) &&

	echo "$diff" | stdin_contains master &&
	echo "$diff" | stdin_contains branch &&
	echo "$diff" | stdin_doesnot_contain m2 &&
	echo "$diff" | stdin_doesnot_contain br2
'

test_done
