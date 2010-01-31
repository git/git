#!/bin/sh
#
# Copyright (c) 2009 David Aguilar
#

test_description='git-difftool

Testing basic diff tool invocation
'

. ./test-lib.sh

if ! test_have_prereq PERL; then
	say 'skipping difftool tests, perl not available'
	test_done
fi

remove_config_vars()
{
	# Unset all config variables used by git-difftool
	git config --unset diff.tool
	git config --unset difftool.test-tool.cmd
	git config --unset difftool.prompt
	git config --unset merge.tool
	git config --unset mergetool.test-tool.cmd
	return 0
}

restore_test_defaults()
{
	# Restores the test defaults used by several tests
	remove_config_vars
	unset GIT_DIFF_TOOL
	unset GIT_MERGE_TOOL
	unset GIT_DIFFTOOL_PROMPT
	unset GIT_DIFFTOOL_NO_PROMPT
	git config diff.tool test-tool &&
	git config difftool.test-tool.cmd 'cat $LOCAL'
}

prompt_given()
{
	prompt="$1"
	test "$prompt" = "Hit return to launch 'test-tool': branch"
}

# Create a file on master and change it on branch
test_expect_success 'setup' '
	echo master >file &&
	git add file &&
	git commit -m "added file" &&

	git checkout -b branch master &&
	echo branch >file &&
	git commit -a -m "branch changed file" &&
	git checkout master
'

# Configure a custom difftool.<tool>.cmd and use it
test_expect_success 'custom commands' '
	restore_test_defaults &&
	git config difftool.test-tool.cmd "cat \$REMOTE" &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "master" &&

	restore_test_defaults &&
	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch"
'

# Ensures that git-difftool ignores bogus --tool values
test_expect_success 'difftool ignores bad --tool values' '
	diff=$(git difftool --no-prompt --tool=bogus-tool branch)
	test "$?" = 1 &&
	test "$diff" = ""
'

# Specify the diff tool using $GIT_DIFF_TOOL
test_expect_success 'GIT_DIFF_TOOL variable' '
	git config --unset diff.tool
	GIT_DIFF_TOOL=test-tool &&
	export GIT_DIFF_TOOL &&

	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test the $GIT_*_TOOL variables and ensure
# that $GIT_DIFF_TOOL always wins unless --tool is specified
test_expect_success 'GIT_DIFF_TOOL overrides' '
	git config diff.tool bogus-tool &&
	git config merge.tool bogus-tool &&

	GIT_MERGE_TOOL=test-tool &&
	export GIT_MERGE_TOOL &&
	diff=$(git difftool --no-prompt branch) &&
	test "$diff" = "branch" &&
	unset GIT_MERGE_TOOL &&

	GIT_MERGE_TOOL=bogus-tool &&
	GIT_DIFF_TOOL=test-tool &&
	export GIT_MERGE_TOOL &&
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
test_expect_success 'GIT_DIFFTOOL_NO_PROMPT variable' '
	GIT_DIFFTOOL_NO_PROMPT=true &&
	export GIT_DIFFTOOL_NO_PROMPT &&

	diff=$(git difftool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# git-difftool supports the difftool.prompt variable.
# Test that GIT_DIFFTOOL_PROMPT can override difftool.prompt = false
test_expect_success 'GIT_DIFFTOOL_PROMPT variable' '
	git config difftool.prompt false &&
	GIT_DIFFTOOL_PROMPT=true &&
	export GIT_DIFFTOOL_PROMPT &&

	prompt=$(echo | git difftool --prompt branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# Test that we don't have to pass --no-prompt when difftool.prompt is false
test_expect_success 'difftool.prompt config variable is false' '
	git config difftool.prompt false &&

	diff=$(git difftool branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that the -y flag can override difftool.prompt = true
test_expect_success 'difftool.prompt can overridden with -y' '
	git config difftool.prompt true &&

	diff=$(git difftool -y branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults
'

# Test that the --prompt flag can override difftool.prompt = false
test_expect_success 'difftool.prompt can overridden with --prompt' '
	git config difftool.prompt false &&

	prompt=$(echo | git difftool --prompt branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# Test that the last flag passed on the command-line wins
test_expect_success 'difftool last flag wins' '
	diff=$(git difftool --prompt --no-prompt branch) &&
	test "$diff" = "branch" &&

	restore_test_defaults &&

	prompt=$(echo | git difftool --no-prompt --prompt branch | tail -1) &&
	prompt_given "$prompt" &&

	restore_test_defaults
'

# git-difftool falls back to git-mergetool config variables
# so test that behavior here
test_expect_success 'difftool + mergetool config variables' '
	remove_config_vars
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

test_expect_success 'difftool.<tool>.path' '
	git config difftool.tkdiff.path echo &&
	diff=$(git difftool --tool=tkdiff --no-prompt branch) &&
	git config --unset difftool.tkdiff.path &&
	lines=$(echo "$diff" | grep file | wc -l) &&
	test "$lines" -eq 1
'

test_done
