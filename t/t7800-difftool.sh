#!/bin/sh
#
# Copyright (c) 2009, 2010, 2012, 2013 David Aguilar
#

test_description='git-difftool

Testing basic diff tool invocation
'

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
	test_expect_code 129 git difftool -h >output &&
	test_i18ngrep ^usage: output &&
	# create a ceiling directory to prevent Git from finding a repo
	mkdir -p not/repo &&
	test_when_finished rm -r not &&
	test_expect_code 129 \
	env GIT_CEILING_DIRECTORIES="$(pwd)/not" \
	git -C not/repo difftool -h >output &&
	test_i18ngrep ^usage: output
'

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
	difftool_test_setup &&
	test_config difftool.test-tool.cmd "cat \"\$REMOTE\"" &&
	echo master >expect &&
	git difftool --no-prompt branch >actual &&
	test_cmp expect actual &&

	test_config difftool.test-tool.cmd "cat \"\$LOCAL\"" &&
	echo branch >expect &&
	git difftool --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'custom tool commands override built-ins' '
	test_config difftool.vimdiff.cmd "cat \"\$REMOTE\"" &&
	echo master >expect &&
	git difftool --tool vimdiff --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool ignores bad --tool values' '
	: >expect &&
	test_must_fail \
		git difftool --no-prompt --tool=bad-tool branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool forwards arguments to diff' '
	difftool_test_setup &&
	>for-diff &&
	git add for-diff &&
	echo changes>for-diff &&
	git add for-diff &&
	: >expect &&
	git difftool --cached --no-prompt -- for-diff >actual &&
	test_cmp expect actual &&
	git reset -- for-diff &&
	rm for-diff
'

test_expect_success 'difftool ignores exit code' '
	test_config difftool.error.cmd false &&
	git difftool -y -t error branch
'

test_expect_success 'difftool forwards exit code with --trust-exit-code' '
	test_config difftool.error.cmd false &&
	test_must_fail git difftool -y --trust-exit-code -t error branch
'

test_expect_success 'difftool forwards exit code with --trust-exit-code for built-ins' '
	test_config difftool.vimdiff.path false &&
	test_must_fail git difftool -y --trust-exit-code -t vimdiff branch
'

test_expect_success 'difftool honors difftool.trustExitCode = true' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode true &&
	test_must_fail git difftool -y -t error branch
'

test_expect_success 'difftool honors difftool.trustExitCode = false' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode false &&
	git difftool -y -t error branch
'

test_expect_success 'difftool ignores exit code with --no-trust-exit-code' '
	test_config difftool.error.cmd false &&
	test_config difftool.trustExitCode true &&
	git difftool -y --no-trust-exit-code -t error branch
'

test_expect_success 'difftool stops on error with --trust-exit-code' '
	test_when_finished "rm -f for-diff .git/fail-right-file" &&
	test_when_finished "git reset -- for-diff" &&
	write_script .git/fail-right-file <<-\EOF &&
	echo failed
	exit 1
	EOF
	>for-diff &&
	git add for-diff &&
	test_must_fail git difftool -y --trust-exit-code \
		--extcmd .git/fail-right-file branch >actual &&
	test_line_count = 1 actual
'

test_expect_success 'difftool honors exit status if command not found' '
	test_config difftool.nonexistent.cmd i-dont-exist &&
	test_config difftool.trustExitCode false &&
	test_must_fail git difftool -y -t nonexistent branch
'

test_expect_success 'difftool honors --gui' '
	difftool_test_setup &&
	test_config merge.tool bogus-tool &&
	test_config diff.tool bogus-tool &&
	test_config diff.guitool test-tool &&

	echo branch >expect &&
	git difftool --no-prompt --gui branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui last setting wins' '
	difftool_test_setup &&
	: >expect &&
	git difftool --no-prompt --gui --no-gui >actual &&
	test_cmp expect actual &&

	test_config merge.tool bogus-tool &&
	test_config diff.tool bogus-tool &&
	test_config diff.guitool test-tool &&
	echo branch >expect &&
	git difftool --no-prompt --no-gui --gui branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui works without configured diff.guitool' '
	difftool_test_setup &&
	echo branch >expect &&
	git difftool --no-prompt --gui branch >actual &&
	test_cmp expect actual
'

# Specify the diff tool using $GIT_DIFF_TOOL
test_expect_success 'GIT_DIFF_TOOL variable' '
	difftool_test_setup &&
	git config --unset diff.tool &&
	echo branch >expect &&
	GIT_DIFF_TOOL=test-tool git difftool --no-prompt branch >actual &&
	test_cmp expect actual
'

# Test the $GIT_*_TOOL variables and ensure
# that $GIT_DIFF_TOOL always wins unless --tool is specified
test_expect_success 'GIT_DIFF_TOOL overrides' '
	difftool_test_setup &&
	test_config diff.tool bogus-tool &&
	test_config merge.tool bogus-tool &&

	echo branch >expect &&
	GIT_DIFF_TOOL=test-tool git difftool --no-prompt branch >actual &&
	test_cmp expect actual &&

	test_config diff.tool bogus-tool &&
	test_config merge.tool bogus-tool &&
	GIT_DIFF_TOOL=bogus-tool \
		git difftool --no-prompt --tool=test-tool branch >actual &&
	test_cmp expect actual
'

# Test that we don't have to pass --no-prompt to difftool
# when $GIT_DIFFTOOL_NO_PROMPT is true
test_expect_success 'GIT_DIFFTOOL_NO_PROMPT variable' '
	difftool_test_setup &&
	echo branch >expect &&
	GIT_DIFFTOOL_NO_PROMPT=true git difftool branch >actual &&
	test_cmp expect actual
'

# git-difftool supports the difftool.prompt variable.
# Test that GIT_DIFFTOOL_PROMPT can override difftool.prompt = false
test_expect_success 'GIT_DIFFTOOL_PROMPT variable' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo >input &&
	GIT_DIFFTOOL_PROMPT=true git difftool branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# Test that we don't have to pass --no-prompt when difftool.prompt is false
test_expect_success 'difftool.prompt config variable is false' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo branch >expect &&
	git difftool branch >actual &&
	test_cmp expect actual
'

# Test that we don't have to pass --no-prompt when mergetool.prompt is false
test_expect_success 'difftool merge.prompt = false' '
	difftool_test_setup &&
	test_might_fail git config --unset difftool.prompt &&
	test_config mergetool.prompt false &&
	echo branch >expect &&
	git difftool branch >actual &&
	test_cmp expect actual
'

# Test that the -y flag can override difftool.prompt = true
test_expect_success 'difftool.prompt can overridden with -y' '
	difftool_test_setup &&
	test_config difftool.prompt true &&
	echo branch >expect &&
	git difftool -y branch >actual &&
	test_cmp expect actual
'

# Test that the --prompt flag can override difftool.prompt = false
test_expect_success 'difftool.prompt can overridden with --prompt' '
	difftool_test_setup &&
	test_config difftool.prompt false &&
	echo >input &&
	git difftool --prompt branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# Test that the last flag passed on the command-line wins
test_expect_success 'difftool last flag wins' '
	difftool_test_setup &&
	echo branch >expect &&
	git difftool --prompt --no-prompt branch >actual &&
	test_cmp expect actual &&
	echo >input &&
	git difftool --no-prompt --prompt branch <input >output &&
	prompt=$(tail -1 <output) &&
	prompt_given "$prompt"
'

# git-difftool falls back to git-mergetool config variables
# so test that behavior here
test_expect_success 'difftool + mergetool config variables' '
	test_config merge.tool test-tool &&
	test_config mergetool.test-tool.cmd "cat \$LOCAL" &&
	echo branch >expect &&
	git difftool --no-prompt branch >actual &&
	test_cmp expect actual &&
	git difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool to something bogus, diff.tool to test-tool
	test_config merge.tool bogus-tool &&
	test_config diff.tool test-tool &&
	git difftool --no-prompt branch >actual &&
	test_cmp expect actual &&
	git difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool, diff.tool to something bogus, merge.guitool to test-tool
	test_config diff.tool bogus-tool &&
	test_config merge.guitool test-tool &&
	git difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual &&

	# set merge.tool, diff.tool, merge.guitool to something bogus, diff.guitool to test-tool
	test_config merge.guitool bogus-tool &&
	test_config diff.guitool test-tool &&
	git difftool --gui --no-prompt branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool.<tool>.path' '
	test_config difftool.tkdiff.path echo &&
	git difftool --tool=tkdiff --no-prompt branch >output &&
	grep file output >grep-output &&
	test_line_count = 1 grep-output
'

test_expect_success 'difftool --extcmd=cat' '
	echo branch >expect &&
	echo master >>expect &&
	git difftool --no-prompt --extcmd=cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat' '
	echo branch >expect &&
	echo master >>expect &&
	git difftool --no-prompt --extcmd=cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool -x cat' '
	echo branch >expect &&
	echo master >>expect &&
	git difftool --no-prompt -x cat branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd echo arg1' '
	echo file >expect &&
	git difftool --no-prompt \
		--extcmd sh\ -c\ \"echo\ \$1\" branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat arg1' '
	echo master >expect &&
	git difftool --no-prompt \
		--extcmd sh\ -c\ \"cat\ \$1\" branch >actual &&
	test_cmp expect actual
'

test_expect_success 'difftool --extcmd cat arg2' '
	echo branch >expect &&
	git difftool --no-prompt \
		--extcmd sh\ -c\ \"cat\ \\\"\$2\\\"\" branch >actual &&
	test_cmp expect actual
'

# Create a second file on master and a different version on branch
test_expect_success 'setup with 2 files different' '
	echo m2 >file2 &&
	git add file2 &&
	git commit -m "added file2" &&

	git checkout branch &&
	echo br2 >file2 &&
	git add file2 &&
	git commit -a -m "branch changed file2" &&
	git checkout master
'

test_expect_success 'say no to the first file' '
	(echo n && echo) >input &&
	git difftool -x cat branch <input >output &&
	grep m2 output &&
	grep br2 output &&
	! grep master output &&
	! grep branch output
'

test_expect_success 'say no to the second file' '
	(echo && echo n) >input &&
	git difftool -x cat branch <input >output &&
	grep master output &&
	grep branch output &&
	! grep m2 output &&
	! grep br2 output
'

test_expect_success 'ending prompt input with EOF' '
	git difftool -x cat branch </dev/null >output &&
	! grep master output &&
	! grep branch output &&
	! grep m2 output &&
	! grep br2 output
'

test_expect_success 'difftool --tool-help' '
	git difftool --tool-help >output &&
	grep tool output
'

test_expect_success 'setup change in subdirectory' '
	git checkout master &&
	mkdir sub &&
	echo master >sub/sub &&
	git add sub/sub &&
	git commit -m "added sub/sub" &&
	git tag v1 &&
	echo test >>file &&
	echo test >>sub/sub &&
	git add file sub/sub &&
	git commit -m "modified both"
'

test_expect_success 'difftool -d with growing paths' '
	a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	git init growing &&
	(
		cd growing &&
		echo "test -f \"\$2/b\"" | write_script .git/test-for-b.sh &&
		one=$(printf 1 | git hash-object -w --stdin) &&
		two=$(printf 2 | git hash-object -w --stdin) &&
		git update-index --add \
			--cacheinfo 100644,$one,$a --cacheinfo 100644,$two,b &&
		tree1=$(git write-tree) &&
		git update-index --add \
			--cacheinfo 100644,$two,$a --cacheinfo 100644,$one,b &&
		tree2=$(git write-tree) &&
		git checkout -- $a &&
		git difftool -d --extcmd .git/test-for-b.sh $tree1 $tree2
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
	git difftool -d $symlinks --extcmd ls branch >output &&
	grep sub output &&
	grep file output
'

run_dir_diff_test 'difftool --dir-diff' '
	git difftool --dir-diff $symlinks --extcmd ls branch >output &&
	grep sub output &&
	grep file output
'

run_dir_diff_test 'difftool --dir-diff ignores --prompt' '
	git difftool --dir-diff $symlinks --prompt --extcmd ls branch >output &&
	grep sub output &&
	grep file output
'

run_dir_diff_test 'difftool --dir-diff branch from subdirectory' '
	(
		cd sub &&
		git difftool --dir-diff $symlinks --extcmd ls branch >output &&
		# "sub" must only exist in "right"
		# "file" and "file2" must be listed in both "left" and "right"
		grep sub output >sub-output &&
		test_line_count = 1 sub-output &&
		grep file"$" output >file-output &&
		test_line_count = 2 file-output &&
		grep file2 output >file2-output &&
		test_line_count = 2 file2-output
	)
'

run_dir_diff_test 'difftool --dir-diff v1 from subdirectory' '
	(
		cd sub &&
		git difftool --dir-diff $symlinks --extcmd ls v1 >output &&
		# "sub" and "file" exist in both v1 and HEAD.
		# "file2" is unchanged.
		grep sub output >sub-output &&
		test_line_count = 2 sub-output &&
		grep file output >file-output &&
		test_line_count = 2 file-output &&
		! grep file2 output
	)
'

run_dir_diff_test 'difftool --dir-diff branch from subdirectory w/ pathspec' '
	(
		cd sub &&
		git difftool --dir-diff $symlinks --extcmd ls branch -- .>output &&
		# "sub" only exists in "right"
		# "file" and "file2" must not be listed
		grep sub output >sub-output &&
		test_line_count = 1 sub-output &&
		! grep file output
	)
'

run_dir_diff_test 'difftool --dir-diff v1 from subdirectory w/ pathspec' '
	(
		cd sub &&
		git difftool --dir-diff $symlinks --extcmd ls v1 -- .>output &&
		# "sub" exists in v1 and HEAD
		# "file" is filtered out by the pathspec
		grep sub output >sub-output &&
		test_line_count = 2 sub-output &&
		! grep file output
	)
'

run_dir_diff_test 'difftool --dir-diff from subdirectory with GIT_DIR set' '
	(
		GIT_DIR=$(pwd)/.git &&
		export GIT_DIR &&
		GIT_WORK_TREE=$(pwd) &&
		export GIT_WORK_TREE &&
		cd sub &&
		git difftool --dir-diff $symlinks --extcmd ls \
			branch -- sub >output &&
		grep sub output &&
		! grep file output
	)
'

run_dir_diff_test 'difftool --dir-diff when worktree file is missing' '
	test_when_finished git reset --hard &&
	rm file2 &&
	git difftool --dir-diff $symlinks --extcmd ls branch master >output &&
	grep file2 output
'

run_dir_diff_test 'difftool --dir-diff with unmerged files' '
	test_when_finished git reset --hard &&
	test_config difftool.echo.cmd "echo ok" &&
	git checkout -B conflict-a &&
	git checkout -B conflict-b &&
	git checkout conflict-a &&
	echo a >>file &&
	git add file &&
	git commit -m conflict-a &&
	git checkout conflict-b &&
	echo b >>file &&
	git add file &&
	git commit -m conflict-b &&
	git checkout master &&
	git merge conflict-a &&
	test_must_fail git merge conflict-b &&
	cat >expect <<-EOF &&
		ok
	EOF
	git difftool --dir-diff $symlinks -t echo >actual &&
	test_cmp expect actual
'

write_script .git/CHECK_SYMLINKS <<\EOF
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
	git difftool --dir-diff --symlinks \
		--extcmd "./.git/CHECK_SYMLINKS" branch HEAD &&
	test_cmp expect actual
'

write_script modify-right-file <<\EOF
echo "new content" >"$2/file"
EOF

run_dir_diff_test 'difftool --dir-diff syncs worktree with unstaged change' '
	test_when_finished git reset --hard &&
	echo "orig content" >file &&
	git difftool -d $symlinks --extcmd "$PWD/modify-right-file" branch &&
	echo "new content" >expect &&
	test_cmp expect file
'

run_dir_diff_test 'difftool --dir-diff syncs worktree without unstaged change' '
	test_when_finished git reset --hard &&
	git difftool -d $symlinks --extcmd "$PWD/modify-right-file" branch &&
	echo "new content" >expect &&
	test_cmp expect file
'

write_script modify-file <<\EOF
echo "new content" >file
EOF

test_expect_success 'difftool --no-symlinks does not overwrite working tree file ' '
	echo "orig content" >file &&
	git difftool --dir-diff --no-symlinks --extcmd "$PWD/modify-file" branch &&
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
		test_must_fail git difftool --dir-diff --no-symlinks --extcmd "$PWD/modify-both-files" branch &&
		echo "wt content" >expect &&
		test_cmp expect file &&
		echo "tmp content" >expect &&
		test_cmp expect "$(cat tmpdir)/file"
	)
'

test_expect_success 'difftool properly honors gitlink and core.worktree' '
	test_when_finished rm -rf submod/ule &&
	git submodule add ./. submod/ule &&
	test_config -C submod/ule diff.tool checktrees &&
	test_config -C submod/ule difftool.checktrees.cmd '\''
		test -d "$LOCAL" && test -d "$REMOTE" && echo good
		'\'' &&
	(
		cd submod/ule &&
		echo good >expect &&
		git difftool --tool=checktrees --dir-diff HEAD~ >actual &&
		test_cmp expect actual &&
		rm -f expect actual
	)
'

test_expect_success SYMLINKS 'difftool --dir-diff symlinked directories' '
	test_when_finished git reset --hard &&
	git init dirlinks &&
	(
		cd dirlinks &&
		git config diff.tool checktrees &&
		git config difftool.checktrees.cmd "echo good" &&
		mkdir foo &&
		: >foo/bar &&
		git add foo/bar &&
		test_commit symlink-one &&
		ln -s foo link &&
		git add link &&
		test_commit symlink-two &&
		echo good >expect &&
		git difftool --tool=checktrees --dir-diff HEAD~ >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'difftool --dir-diff handles modified symlinks' '
	test_when_finished git reset --hard &&
	touch b &&
	ln -s b c &&
	git add b c &&
	test_tick &&
	git commit -m initial &&
	touch d &&
	rm c &&
	ln -s d c &&
	cat >expect <<-EOF &&
		b
		c

		c
	EOF
	git difftool --symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	git difftool --no-symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	# The left side contains symlink "c" that points to "b"
	test_config difftool.cat.cmd "cat \$LOCAL/c" &&
	printf "%s\n" b >expect &&

	git difftool --symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	git difftool --symlinks --no-symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	# The right side contains symlink "c" that points to "d"
	test_config difftool.cat.cmd "cat \$REMOTE/c" &&
	printf "%s\n" d >expect &&

	git difftool --symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	git difftool --no-symlinks --dir-diff --tool cat >actual &&
	test_cmp expect actual &&

	# Deleted symlinks
	rm -f c &&
	cat >expect <<-EOF &&
		b
		c

	EOF
	git difftool --symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual &&

	git difftool --no-symlinks --dir-diff --extcmd ls >output &&
	grep -v ^/ output >actual &&
	test_cmp expect actual
'

test_expect_success 'outside worktree' '
	echo 1 >1 &&
	echo 2 >2 &&
	test_expect_code 1 nongit git \
		-c diff.tool=echo -c difftool.echo.cmd="echo \$LOCAL \$REMOTE" \
		difftool --no-prompt --no-index ../1 ../2 >actual &&
	echo "../1 ../2" >expect &&
	test_cmp expect actual
'

test_expect_success 'difftool --gui, --tool and --extcmd are mutually exclusive' '
	difftool_test_setup &&
	test_must_fail git difftool --gui --tool=test-tool &&
	test_must_fail git difftool --gui --extcmd=cat &&
	test_must_fail git difftool --tool=test-tool --extcmd=cat &&
	test_must_fail git difftool --gui --tool=test-tool --extcmd=cat
'

test_done
