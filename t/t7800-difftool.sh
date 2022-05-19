#!/bin/sh
#
# Copyright (c) 2009, 2010, 2012, 2013 David Aguilar
#

test_description='but-difftool

Testing basic diff tool invocation
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

difftool_test_setup ()
{
	test_config diff.tool test-tool &&
	test_config difftool.test-tool.cmd 'cat "$LOCAL"' &&
	test_config difftool.bogus-tool.cmd false
}

prompt_given ()
{
	prompt="$1"
	test "$prompt" = "Launch 'test-tool' [Y/n]? branch"
}

test_expect_success 'basic usage requires no repo' '
	test_expect_code 129 but difftool -h >output &&
	test_i18ngrep ^usage: output &&
	# create a ceiling directory to prevent Git from finding a repo
	mkdir -p not/repo &&
	test_when_finished rm -r not &&
	test_expect_code 129 \
	env BUT_CEILING_DIRECTORIES="$(pwd)/not" \
	but -C not/repo difftool -h >output &&
	test_i18ngrep ^usage: output
'

# Create a file on main and change it on branch
test_expect_success 'setup' '
	echo main >file &&
	but add file &&
	but cummit -m "added file" &&

	but checkout -b branch main &&
	echo branch >file &&
	but cummit -a -m "branch changed file" &&
	but checkout main
'

# Configure a custom difftool.<tool>.cmd and use it
test_expect_success 'custom commands' '
	difftool_test_setup &&
	test_config difftool.test-tool.cmd "cat \"\$REMOTE\"" &&
	echo main >expect &&
	but difftool --no-prompt branch >actual &&
	test_cmp expect actual &&

	test_config difftool.test-tool.cmd "cat \"\$LOCAL\"" &&
	echo branch >expect &&
	but difftool --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'custom tool commands override built-ins' '
	test_config difftool.vimdiff.cmd "cat \"\$REMOTE\"" &&
	echo main >expect &&
	but difftool --tool vimdiff --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool ignores bad --tool values' '
	: >expect &&
	test_must_fail \
		but difftool --no-prompt --tool=bad-tool branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool forwards arguments to diff' '
	difftool_test_setup &&
	>for-diff &&
	but add for-diff &&
	echo changes>for-diff &&
	but add for-diff &&
	: >expect &&
	but difftool --cached --no-prompt -- for-diff >actual &&
	test_cmp expect actual &&
	but reset -- for-diff &&
	rm for-diff
'

test_expect_success 'difftool ignores exit code' '
	test_config difftool.error.cmd false &&
	but difftool -y -t error branch
'

test_expect_success 'difftool forwards exit code with --trust-exit-code' '
	test_config difftool.error.cmd false &&
	test_must_fail but difftool -y --trust-exit-code -t error branch
'

test_expect_success 'difftool forwards exit code with --trust-exit-code for built-ins' '
	test_config difftool.vimdiff.path false &&
	test_must_fail but difftool -y --trust-exit-code -t vimdiff branch
'

test_expect_success 'difftool honors difftool.trustExitCode = true' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode true &&
	test_must_fail but difftool -y -t error branch
'

test_expect_success 'difftool honors difftool.trustExitCode = false' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode false &&
	but difftool -y -t error branch
'

test_expect_success 'difftool ignores exit code with --no-trust-exit-code' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode true &&
	but difftool -y --no-trust-exit-code -t error branch
'

test_expect_success 'difftool stops on error with --trust-exit-code' '
	test_when_finished "rm -f for-diff .but/fail-right-file" &&
	test_when_finished "but reset -- for-diff" &&
	write_script .but/fail-right-file <<-\EOF &&
	echo failed
	exit 1
	EOF
	>for-diff &&
	but add for-diff &&
	test_must_fail but difftool -y --trust-exit-code \
		--extcmd .but/fail-right-file branch >actual &&
	test_line_count = 1 actual
'

test_expect_success 'difftool honors exit status if command not found' '
	test_config difftool.nonexistent.cmd i-dont-exist &&
	test_config difftool.trustExitCode false &&
	test_must_fail but difftool -y -t nonexistent branch
'

test_expect_success 'difftool honors --gui' '
	difftool_test_setup &&
	test_config merge.tool bogus-tool &&
	test_config diff.tool bogus-tool &&
	test_config diff.guitool test-tool &&

	echo branch >expect &&
	but difftool --no-prompt --gui branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui last setting wins' '
	difftool_test_setup &&
	: >expect &&
	but difftool --no-prompt --gui --no-gui >actual &&
	test_cmp expect actual &&

	test_config merge.tool bogus-tool &&
	test_config diff.tool bogus-tool &&
	test_config diff.guitool test-tool &&
	echo branch >expect &&
	but difftool --no-prompt --no-gui --gui branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui works without configured diff.guitool' '
	difftool_test_setup &&
	echo branch >expect &&
	but difftool --no-prompt --gui branch >actual &&
	test_cmp expect actual
'

# Specify the diff tool using $BUT_DIFF_TOOL
test_expect_success 'BUT_DIFF_TOOL variable' '
	difftool_test_setup &&
	but config --unset diff.tool &&
	echo branch >expect &&
	BUT_DIFF_TOOL=test-tool but difftool --no-prompt branch >actual &&
	test_cmp expect actual
'

# Test the $BUT_*_TOOL variables and ensure
# that $BUT_DIFF_TOOL always wins unless --tool is specified
test_expect_success 'BUT_DIFF_TOOL overrides' '
	difftool_test_setup &&
	test_config diff.tool bogus-tool &&
	test_config merge.tool bogus-tool &&

	echo branch >expect &&
	BUT_DIFF_TOOL=test-tool but difftool --no-prompt branch >actual &&
	test_cmp expect actual &&

	test_config diff.tool bogus-tool &&
	test_config merge.tool bogus-tool &&
	BUT_DIFF_TOOL=bogus-tool \
		but difftool --no-prompt --tool=test-tool branch >actual &&
	test_cmp expect actual
'

# Test that we don't have to pass --no-prompt to difftool
# when $BUT_DIFFTOOL_NO_PROMPT is true
test_expect_success 'BUT_DIFFTOOL_NO_PROMPT variable' '
	difftool_test_setup &&
	echo branch >expect &&
	BUT_DIFFTOOL_NO_PROMPT=true but difftool branch >actual &&
	test_cmp expect actual
'

# but-difftool supports the difftool.prompt variable.
# Test that BUT_DIFFTOOL_PROMPT can override difftool.prompt = false
test_expect_success 'BUT_DIFFTOOL_PROMPT variable' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo >input &&
	BUT_DIFFTOOL_PROMPT=true but difftool branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# Test that we don't have to pass --no-prompt when difftool.prompt is false
test_expect_success 'difftool.prompt config variable is false' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo branch >expect &&
	but difftool branch >actual &&
	test_cmp expect actual
'

# Test that we don't have to pass --no-prompt when mergetool.prompt is false
test_expect_success 'difftool merge.prompt = false' '
	difftool_test_setup &&
	test_might_fail but config --unset difftool.prompt &&
	test_config mergetool.prompt false &&
	echo branch >expect &&
	but difftool branch >actual &&
	test_cmp expect actual
'

# Test that the -y flag can override difftool.prompt = true
test_expect_success 'difftool.prompt can overridden with -y' '
	difftool_test_setup &&
	test_config difftool.prompt true &&
	echo branch >expect &&
	but difftool -y branch >actual &&
	test_cmp expect actual
'

# Test that the --prompt flag can override difftool.prompt = false
test_expect_success 'difftool.prompt can overridden with --prompt' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo >input &&
	but difftool --prompt branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# Test that the last flag passed on the command-line wins
test_expect_success 'difftool last flag wins' '
	difftool_test_setup &&
	echo branch >expect &&
	but difftool --prompt --no-prompt branch >actual &&
	test_cmp expect actual &&
	echo >input &&
	but difftool --no-prompt --prompt branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# but-difftool falls back to but-mergetool config variables
# so test that behavior here
test_expect_success 'difftool + mergetool config variables' '
	test_config merge.tool test-tool &&
	test_config mergetool.test-tool.cmd "cat \$LOCAL" &&
	echo branch >expect &&
	but difftool --no-prompt branch >actual &&
	test_cmp expect actual &&
	but difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool to something bogus, diff.tool to test-tool
	test_config merge.tool bogus-tool &&
	test_config diff.tool test-tool &&
	but difftool --no-prompt branch >actual &&
	test_cmp expect actual &&
	but difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool, diff.tool to something bogus, merge.guitool to test-tool
	test_config diff.tool bogus-tool &&
	test_config merge.guitool test-tool &&
	but difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool, diff.tool, merge.guitool to something bogus, diff.guitool to test-tool
	test_config merge.guitool bogus-tool &&
	test_config diff.guitool test-tool &&
	but difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool.<tool>.path' '
	test_config difftool.tkdiff.path echo &&
	but difftool --tool=tkdiff --no-prompt branch >output &&
	grep file output >grep-output &&
	test_line_count = 1 grep-output
'

test_expect_success 'difftool --extcmd=cat' '
	echo branch >expect &&
	echo main >>expect &&
	but difftool --no-prompt --extcmd=cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat' '
	echo branch >expect &&
	echo main >>expect &&
	but difftool --no-prompt --extcmd=cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool -x cat' '
	echo branch >expect &&
	echo main >>expect &&
	but difftool --no-prompt -x cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd echo arg1' '
	echo file >expect &&
	but difftool --no-prompt \
		--extcmd sh\ -c\ \"echo\ \$1\" branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat arg1' '
	echo main >expect &&
	but difftool --no-prompt \
		--extcmd sh\ -c\ \"cat\ \$1\" branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat arg2' '
	echo branch >expect &&
	but difftool --no-prompt \
		--extcmd sh\ -c\ \"cat\ \\\"\$2\\\"\" branch >actual &&
	test_cmp expect actual
'

# Create a second file on main and a different version on branch
test_expect_success 'setup with 2 files different' '
	echo m2 >file2 &&
	but add file2 &&
	but cummit -m "added file2" &&

	but checkout branch &&
	echo br2 >file2 &&
	but add file2 &&
	but cummit -a -m "branch changed file2" &&
	but checkout main
'

test_expect_success 'say no to the first file' '
	(echo n && echo) >input &&
	but difftool -x cat branch <input >output &&
	grep m2 output &&
	grep br2 output &&
	! grep main output &&
	! grep branch output
'

test_expect_success 'say no to the second file' '
	(echo && echo n) >input &&
	but difftool -x cat branch <input >output &&
	grep main output &&
	grep branch output &&
	! grep m2 output &&
	! grep br2 output
'

test_expect_success 'ending prompt input with EOF' '
	but difftool -x cat branch </dev/null >output &&
	! grep main output &&
	! grep branch output &&
	! grep m2 output &&
	! grep br2 output
'

test_expect_success 'difftool --tool-help' '
	but difftool --tool-help >output &&
	grep tool output
'

test_expect_success 'setup change in subdirectory' '
	but checkout main &&
	mkdir sub &&
	echo main >sub/sub &&
	but add sub/sub &&
	but cummit -m "added sub/sub" &&
	but tag v1 &&
	echo test >>file &&
	echo test >>sub/sub &&
	but add file sub/sub &&
	but cummit -m "modified both"
'

test_expect_success 'difftool -d with growing paths' '
	a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	but init growing &&
	(
		cd growing &&
		echo "test -f \"\$2/b\"" | write_script .but/test-for-b.sh &&
		one=$(printf 1 | but hash-object -w --stdin) &&
		two=$(printf 2 | but hash-object -w --stdin) &&
		but update-index --add \
			--cacheinfo 100644,$one,$a --cacheinfo 100644,$two,b &&
		tree1=$(but write-tree) &&
		but update-index --add \
			--cacheinfo 100644,$two,$a --cacheinfo 100644,$one,b &&
		tree2=$(but write-tree) &&
		but checkout -- $a &&
		but difftool -d --extcmd .but/test-for-b.sh $tree1 $tree2
	)
'

run_dir_diff_test () {
	test_expect_success "$1 --no-symlinks" "
		symlinks=--no-symlinks &&
		$2
	"
	test_expect_success SYMLINKS "$1 --symlinks" "
		symlinks=--symlinks &&
		$2
	"
}

run_dir_diff_test 'difftool -d' '
	but difftool -d $symlinks --extcmd ls branch >output &&
	grep "^sub$" output &&
	grep "^file$" output
'

run_dir_diff_test 'difftool --dir-diff' '
	but difftool --dir-diff $symlinks --extcmd ls branch >output &&
	grep "^sub$" output &&
	grep "^file$" output
'

run_dir_diff_test 'difftool --dir-diff avoids repeated slashes in TMPDIR' '
	TMPDIR="${TMPDIR:-/tmp}////" \
		but difftool --dir-diff $symlinks --extcmd echo branch >output &&
	grep -v // output >actual &&
	test_line_count = 1 actual
'

run_dir_diff_test 'difftool --dir-diff ignores --prompt' '
	but difftool --dir-diff $symlinks --prompt --extcmd ls branch >output &&
	grep "^sub$" output &&
	grep "^file$" output
'

run_dir_diff_test 'difftool --dir-diff branch from subdirectory' '
	(
		cd sub &&
		but difftool --dir-diff $symlinks --extcmd ls branch >output &&
		# "sub" must only exist in "right"
		# "file" and "file2" must be listed in both "left" and "right"
		grep "^sub$" output >sub-output &&
		test_line_count = 1 sub-output &&
		grep "^file$" output >file-output &&
		test_line_count = 2 file-output &&
		grep "^file2$" output >file2-output &&
		test_line_count = 2 file2-output
	)
'

run_dir_diff_test 'difftool --dir-diff v1 from subdirectory' '
	(
		cd sub &&
		but difftool --dir-diff $symlinks --extcmd ls v1 >output &&
		# "sub" and "file" exist in both v1 and HEAD.
		# "file2" is unchanged.
		grep "^sub$" output >sub-output &&
		test_line_count = 2 sub-output &&
		grep "^file$" output >file-output &&
		test_line_count = 2 file-output &&
		! grep "^file2$" output
	)
'

run_dir_diff_test 'difftool --dir-diff branch from subdirectory w/ pathspec' '
	(
		cd sub &&
		but difftool --dir-diff $symlinks --extcmd ls branch -- .>output &&
		# "sub" only exists in "right"
		# "file" and "file2" must not be listed
		grep "^sub$" output >sub-output &&
		test_line_count = 1 sub-output &&
		! grep "^file$" output
	)
'

run_dir_diff_test 'difftool --dir-diff v1 from subdirectory w/ pathspec' '
	(
		cd sub &&
		but difftool --dir-diff $symlinks --extcmd ls v1 -- .>output &&
		# "sub" exists in v1 and HEAD
		# "file" is filtered out by the pathspec
		grep "^sub$" output >sub-output &&
		test_line_count = 2 sub-output &&
		! grep "^file$" output
	)
'

run_dir_diff_test 'difftool --dir-diff from subdirectory with BUT_DIR set' '
	(
		BUT_DIR=$(pwd)/.but &&
		export BUT_DIR &&
		BUT_WORK_TREE=$(pwd) &&
		export BUT_WORK_TREE &&
		cd sub &&
		but difftool --dir-diff $symlinks --extcmd ls \
			branch -- sub >output &&
		grep "^sub$" output &&
		! grep "^file$" output
	)
'

run_dir_diff_test 'difftool --dir-diff when worktree file is missing' '
	test_when_finished but reset --hard &&
	rm file2 &&
	but difftool --dir-diff $symlinks --extcmd ls branch main >output &&
	grep "^file2$" output
'

run_dir_diff_test 'difftool --dir-diff with unmerged files' '
	test_when_finished but reset --hard &&
	test_config difftool.echo.cmd "echo ok" &&
	but checkout -B conflict-a &&
	but checkout -B conflict-b &&
	but checkout conflict-a &&
	echo a >>file &&
	but add file &&
	but cummit -m conflict-a &&
	but checkout conflict-b &&
	echo b >>file &&
	but add file &&
	but cummit -m conflict-b &&
	but checkout main &&
	but merge conflict-a &&
	test_must_fail but merge conflict-b &&
	cat >expect <<-EOF &&
		ok
	EOF
	but difftool --dir-diff $symlinks -t echo >actual &&
	test_cmp expect actual
'

write_script .but/CHECK_SYMLINKS <<\EOF
for f in file file2 sub/sub
do
	echo "$f"
	ls -ld "$2/$f" | sed -e 's/.* -> //'
done >actual
EOF

test_expect_success SYMLINKS 'difftool --dir-diff --symlinks without unstaged changes' '
	cat >expect <<-EOF &&
	file
	$PWD/file
	file2
	$PWD/file2
	sub/sub
	$PWD/sub/sub
	EOF
	but difftool --dir-diff --symlinks \
		--extcmd "./.but/CHECK_SYMLINKS" branch HEAD &&
	test_cmp expect actual
'

write_script modify-right-file <<\EOF
echo "new content" >"$2/file"
EOF

run_dir_diff_test 'difftool --dir-diff syncs worktree with unstaged change' '
	test_when_finished but reset --hard &&
	echo "orig content" >file &&
	but difftool -d $symlinks --extcmd "$PWD/modify-right-file" branch &&
	echo "new content" >expect &&
	test_cmp expect file
'

run_dir_diff_test 'difftool --dir-diff syncs worktree without unstaged change' '
	test_when_finished but reset --hard &&
	but difftool -d $symlinks --extcmd "$PWD/modify-right-file" branch &&
	echo "new content" >expect &&
	test_cmp expect file
'

write_script modify-file <<\EOF
echo "new content" >file
EOF

test_expect_success 'difftool --no-symlinks does not overwrite working tree file ' '
	echo "orig content" >file &&
	but difftool --dir-diff --no-symlinks --extcmd "$PWD/modify-file" branch &&
	echo "new content" >expect &&
	test_cmp expect file
'

write_script modify-both-files <<\EOF
echo "wt content" >file &&
echo "tmp content" >"$2/file" &&
echo "$2" >tmpdir
EOF

test_expect_success 'difftool --no-symlinks detects conflict ' '
	(
		TMPDIR=$TRASH_DIRECTORY &&
		export TMPDIR &&
		echo "orig content" >file &&
		test_must_fail but difftool --dir-diff --no-symlinks --extcmd "$PWD/modify-both-files" branch &&
		echo "wt content" >expect &&
		test_cmp expect file &&
		echo "tmp content" >expect &&
		test_cmp expect "$(cat tmpdir)/file"
	)
'

test_expect_success 'difftool properly honors butlink and core.worktree' '
	test_when_finished rm -rf submod/ule &&
	but submodule add ./. submod/ule &&
	test_config -C submod/ule diff.tool checktrees &&
	test_config -C submod/ule difftool.checktrees.cmd '\''
		test -d "$LOCAL" && test -d "$REMOTE" && echo good
		'\'' &&
	(
		cd submod/ule &&
		echo good >expect &&
		but difftool --tool=checktrees --dir-diff HEAD~ >actual &&
		test_cmp expect actual &&
		rm -f expect actual
	)
'

test_expect_success SYMLINKS 'difftool --dir-diff symlinked directories' '
	test_when_finished but reset --hard &&
	but init dirlinks &&
	(
		cd dirlinks &&
		but config diff.tool checktrees &&
		but config difftool.checktrees.cmd "echo good" &&
		mkdir foo &&
		: >foo/bar &&
		but add foo/bar &&
		test_cummit symlink-one &&
		ln -s foo link &&
		but add link &&
		test_cummit symlink-two &&
		echo good >expect &&
		but difftool --tool=checktrees --dir-diff HEAD~ >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'difftool --dir-diff handles modified symlinks' '
	test_when_finished but reset --hard &&
	touch b &&
	ln -s b c &&
	but add b c &&
	test_tick &&
	but cummit -m initial &&
	touch d &&
	rm c &&
	ln -s d c &&
	cat >expect <<-EOF &&
		c

		c
	EOF
	but difftool --symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	but difftool --no-symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	# The left side contains symlink "c" that points to "b"
	test_config difftool.cat.cmd "cat \$LOCAL/c" &&
	printf "%s\n" b >expect &&

	but difftool --symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	but difftool --symlinks --no-symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	# The right side contains symlink "c" that points to "d"
	test_config difftool.cat.cmd "cat \$REMOTE/c" &&
	printf "%s\n" d >expect &&

	but difftool --symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	but difftool --no-symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	# Deleted symlinks
	rm -f c &&
	cat >expect <<-EOF &&
		c

	EOF
	but difftool --symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	but difftool --no-symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'difftool --dir-diff writes symlinks as raw text' '
	# Start out on a branch called "branch-init".
	but init -b branch-init symlink-files &&
	(
		cd symlink-files &&
		# This test ensures that symlinks are written as raw text.
		# The "cat" tools output link and file contents.
		but config difftool.cat-left-link.cmd "cat \"\$LOCAL/link\"" &&
		but config difftool.cat-left-a.cmd "cat \"\$LOCAL/file-a\"" &&
		but config difftool.cat-right-link.cmd "cat \"\$REMOTE/link\"" &&
		but config difftool.cat-right-b.cmd "cat \"\$REMOTE/file-b\"" &&

		# Record the empty initial state so that we can come back here
		# later and not have to consider the any cases where difftool
		# will create symlinks back into the worktree.
		test_tick &&
		but cummit --allow-empty -m init &&

		# Create a file called "file-a" with a symlink pointing to it.
		but switch -c branch-a &&
		echo a >file-a &&
		ln -s file-a link &&
		but add file-a link &&
		test_tick &&
		but cummit -m link-to-file-a &&

		# Create a file called "file-b" and point the symlink to it.
		but switch -c branch-b &&
		echo b >file-b &&
		rm link &&
		ln -s file-b link &&
		but add file-b link &&
		but rm file-a &&
		test_tick &&
		but cummit -m link-to-file-b &&

		# Checkout the initial branch so that the --symlinks behavior is
		# not activated. The two directories should be completely
		# independent with no symlinks pointing back here.
		but switch branch-init &&

		# The left link must be "file-a" and "file-a" must contain "a".
		echo file-a >expect &&
		but difftool --symlinks --dir-diff --tool cat-left-link \
			branch-a branch-b >actual &&
		test_cmp expect actual &&

		echo a >expect &&
		but difftool --symlinks --dir-diff --tool cat-left-a \
			branch-a branch-b >actual &&
		test_cmp expect actual &&

		# The right link must be "file-b" and "file-b" must contain "b".
		echo file-b >expect &&
		but difftool --symlinks --dir-diff --tool cat-right-link \
			branch-a branch-b >actual &&
		test_cmp expect actual &&

		echo b >expect &&
		but difftool --symlinks --dir-diff --tool cat-right-b \
			branch-a branch-b >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add -N and difftool -d' '
	test_when_finished but reset --hard &&

	test_write_lines A B C >intent-to-add &&
	but add -N intent-to-add &&
	but difftool --dir-diff --extcmd ls
'

test_expect_success 'difftool --cached with unmerged files' '
	test_when_finished but reset --hard &&

	test_cummit conflicting &&
	test_cummit conflict-a conflict.t a &&
	but reset --hard conflicting &&
	test_cummit conflict-b conflict.t b &&
	test_must_fail but merge conflict-a &&

	but difftool --cached --no-prompt >output &&
	test_must_be_empty output
'

test_expect_success 'outside worktree' '
	echo 1 >1 &&
	echo 2 >2 &&
	test_expect_code 1 nonbut but \
		-c diff.tool=echo -c difftool.echo.cmd="echo \$LOCAL \$REMOTE" \
		difftool --no-prompt --no-index ../1 ../2 >actual &&
	echo "../1 ../2" >expect &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui, --tool and --extcmd are mutually exclusive' '
	difftool_test_setup &&
	test_must_fail but difftool --gui --tool=test-tool &&
	test_must_fail but difftool --gui --extcmd=cat &&
	test_must_fail but difftool --tool=test-tool --extcmd=cat &&
	test_must_fail but difftool --gui --tool=test-tool --extcmd=cat
'

test_expect_success 'difftool --rotate-to' '
	difftool_test_setup &&
	test_when_finished but reset --hard &&
	echo 1 >1 &&
	echo 2 >2 &&
	echo 4 >4 &&
	but add 1 2 4 &&
	but cummit -a -m "124" &&
	but difftool --no-prompt --extcmd=cat --rotate-to="2" HEAD^ >output &&
	cat >expect <<-\EOF &&
	2
	4
	1
	EOF
	test_cmp output expect
'

test_expect_success 'difftool --skip-to' '
	difftool_test_setup &&
	test_when_finished but reset --hard &&
	but difftool --no-prompt --extcmd=cat --skip-to="2" HEAD^ >output &&
	cat >expect <<-\EOF &&
	2
	4
	EOF
	test_cmp output expect
'

test_expect_success 'difftool --rotate/skip-to error condition' '
	test_must_fail but difftool --no-prompt --extcmd=cat --rotate-to="3" HEAD^ &&
	test_must_fail but difftool --no-prompt --extcmd=cat --skip-to="3" HEAD^
'
test_done
