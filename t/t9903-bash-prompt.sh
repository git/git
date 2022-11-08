#!/bin/sh
#
# Copyright (c) 2012 SZEDER Gábor
#

test_description='test git-specific bash prompt functions'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-bash.sh

. "$GIT_SOURCE_DIR/contrib/completion/git-prompt.sh"

actual="$TRASH_DIRECTORY/actual"
c_red='\\[\\e[31m\\]'
c_green='\\[\\e[32m\\]'
c_lblue='\\[\\e[1;34m\\]'
c_clear='\\[\\e[0m\\]'

test_expect_success 'setup for prompt tests' '
	git init otherrepo &&
	echo 1 >file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag -a -m msg1 t1 &&
	git checkout -b b1 &&
	echo 2 >file &&
	git commit -m "second b1" file &&
	echo 3 >file &&
	git commit -m "third b1" file &&
	git tag -a -m msg2 t2 &&
	git checkout -b b2 main &&
	echo 0 >file &&
	git commit -m "second b2" file &&
	echo 00 >file &&
	git commit -m "another b2" file &&
	echo 000 >file &&
	git commit -m "yet another b2" file &&
	mkdir ignored_dir &&
	echo "ignored_dir/" >>.gitignore &&
	git checkout main
'

test_expect_success 'prompt - branch name' '
	printf " (main)" >expected &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success SYMLINKS 'prompt - branch name - symlink symref' '
	printf " (main)" >expected &&
	test_when_finished "git checkout main" &&
	test_config core.preferSymlinkRefs true &&
	git checkout main &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - unborn branch' '
	printf " (unborn)" >expected &&
	git checkout --orphan unborn &&
	test_when_finished "git checkout main" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

if test_have_prereq !FUNNYNAMES; then
	say 'Your filesystem does not allow newlines in filenames.'
fi

test_expect_success FUNNYNAMES 'prompt - with newline in path' '
    repo_with_newline="repo
with
newline" &&
	mkdir "$repo_with_newline" &&
	printf " (main)" >expected &&
	git init "$repo_with_newline" &&
	test_when_finished "rm -rf \"$repo_with_newline\"" &&
	mkdir "$repo_with_newline"/subdir &&
	(
		cd "$repo_with_newline/subdir" &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - detached head' '
	printf " ((%s...))" $(git log -1 --format="%h" --abbrev=13 b1^) >expected &&
	test_config core.abbrev 13 &&
	git checkout b1^ &&
	test_when_finished "git checkout main" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - contains' '
	printf " ((t2~1))" >expected &&
	git checkout b1^ &&
	test_when_finished "git checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=contains &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - branch' '
	printf " ((tags/t2~1))" >expected &&
	git checkout b1^ &&
	test_when_finished "git checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=branch &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - describe' '
	printf " ((t1-1-g%s))" $(git log -1 --format="%h" b1^) >expected &&
	git checkout b1^ &&
	test_when_finished "git checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=describe &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - default' '
	printf " ((t2))" >expected &&
	git checkout --detach b1 &&
	test_when_finished "git checkout main" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside .git directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		cd .git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - deep inside .git directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		cd .git/objects &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside bare repository' '
	printf " (BARE:main)" >expected &&
	git init --bare bare.git &&
	test_when_finished "rm -rf bare.git" &&
	(
		cd bare.git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - interactive rebase' '
	printf " (b1|REBASE 2/3)" >expected &&
	write_script fake_editor.sh <<-\EOF &&
		echo "exec echo" >"$1"
		echo "edit $(git log -1 --format="%h")" >>"$1"
		echo "exec echo" >>"$1"
	EOF
	test_when_finished "rm -f fake_editor.sh" &&
	test_set_editor "$TRASH_DIRECTORY/fake_editor.sh" &&
	git checkout b1 &&
	test_when_finished "git checkout main" &&
	git rebase -i HEAD^ &&
	test_when_finished "git rebase --abort" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase merge' '
	printf " (b2|REBASE 1/3)" >expected &&
	git checkout b2 &&
	test_when_finished "git checkout main" &&
	test_must_fail git rebase --merge b1 b2 &&
	test_when_finished "git rebase --abort" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase am' '
	printf " (b2|REBASE 1/3)" >expected &&
	git checkout b2 &&
	test_when_finished "git checkout main" &&
	test_must_fail git rebase --apply b1 b2 &&
	test_when_finished "git rebase --abort" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - merge' '
	printf " (b1|MERGING)" >expected &&
	git checkout b1 &&
	test_when_finished "git checkout main" &&
	test_must_fail git merge b2 &&
	test_when_finished "git reset --hard" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - cherry-pick' '
	printf " (main|CHERRY-PICKING)" >expected &&
	test_must_fail git cherry-pick b1 b1^ &&
	test_when_finished "git cherry-pick --abort" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual" &&
	git reset --merge &&
	test_must_fail git rev-parse CHERRY_PICK_HEAD &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - revert' '
	printf " (main|REVERTING)" >expected &&
	test_must_fail git revert b1^ b1 &&
	test_when_finished "git revert --abort" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual" &&
	git reset --merge &&
	test_must_fail git rev-parse REVERT_HEAD &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bisect' '
	printf " (main|BISECTING)" >expected &&
	git bisect start &&
	test_when_finished "git bisect reset" &&
	__git_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - clean' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty worktree' '
	printf " (main *)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index' '
	printf " (main +)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index and worktree' '
	printf " (main *+)" >expected &&
	echo "dirty index" >file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	echo "dirty worktree" >file &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - clean' '
	printf " (orphan #)" >expected &&
	test_when_finished "git checkout main" &&
	git checkout --orphan orphan &&
	git reset --hard &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - dirty index' '
	printf " (orphan +)" >expected &&
	test_when_finished "git checkout main" &&
	git checkout --orphan orphan &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - dirty index and worktree' '
	printf " (orphan *+)" >expected &&
	test_when_finished "git checkout main" &&
	git checkout --orphan orphan &&
	>file &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable unset with config disabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	test_config bash.showDirtyState false &&
	(
		sane_unset GIT_PS1_SHOWDIRTYSTATE &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable unset with config enabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	test_config bash.showDirtyState true &&
	(
		sane_unset GIT_PS1_SHOWDIRTYSTATE &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable set with config disabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	test_config bash.showDirtyState false &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable set with config enabled' '
	printf " (main *)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	test_config bash.showDirtyState true &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		cd .git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - no stash' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - stash' '
	printf " (main $)" >expected &&
	echo 2 >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	git pack-refs --all &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" >expected &&
	echo 2 >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		cd .git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - no untracked files' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd otherrepo &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - untracked files' '
	printf " (main %%)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - empty untracked dir' '
	printf " (main)" >expected &&
	mkdir otherrepo/untracked-dir &&
	test_when_finished "rm -rf otherrepo/untracked-dir" &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd otherrepo &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - non-empty untracked dir' '
	printf " (main %%)" >expected &&
	mkdir otherrepo/untracked-dir &&
	test_when_finished "rm -rf otherrepo/untracked-dir" &&
	>otherrepo/untracked-dir/untracked-file &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd otherrepo &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - untracked files outside cwd' '
	printf " (main %%)" >expected &&
	(
		mkdir -p ignored_dir &&
		cd ignored_dir &&
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable unset with config disabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles false &&
	(
		sane_unset GIT_PS1_SHOWUNTRACKEDFILES &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable unset with config enabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles true &&
	(
		sane_unset GIT_PS1_SHOWUNTRACKEDFILES &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable set with config disabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles false &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable set with config enabled' '
	printf " (main %%)" >expected &&
	test_config bash.showUntrackedFiles true &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd .git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - format string starting with dash' '
	printf -- "-main" >expected &&
	__git_ps1 "-%s" >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - pc mode' '
	printf "BEFORE: (\${__git_ps1_branch_name}):AFTER\\nmain" >expected &&
	(
		__git_ps1 "BEFORE:" ":AFTER" >"$actual" &&
		test_must_be_empty "$actual" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - branch name' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" >"$actual" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - detached head' '
	printf "BEFORE: (${c_red}\${__git_ps1_branch_name}${c_clear}):AFTER\\n(%s...)" $(git log -1 --format="%h" b1^) >expected &&
	git checkout b1^ &&
	test_when_finished "git checkout main" &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty worktree' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_red}*${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty index' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_green}+${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty index and worktree' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_red}*${c_clear}${c_green}+${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty index" >file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	echo "dirty worktree" >file &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - before root commit' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_green}#${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		cd otherrepo &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - inside .git directory' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear}):AFTER\\nGIT_DIR!" >expected &&
	echo "dirty" >file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		cd .git &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - stash status indicator' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_lblue}\$${c_clear}):AFTER\\nmain" >expected &&
	echo 2 >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - untracked files status indicator' '
	printf "BEFORE: (${c_green}\${__git_ps1_branch_name}${c_clear} ${c_red}%%${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__git_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - zsh color pc mode' '
	printf "BEFORE: (%%F{green}main%%f):AFTER" >expected &&
	(
		ZSH_VERSION=5.0.0 &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config disabled' '
	printf " (main)" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config disabled, pc mode' '
	printf "BEFORE: (\${__git_ps1_branch_name}):AFTER" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config unset' '
	printf " (main)" >expected &&
	(
		cd ignored_dir &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config unset, pc mode' '
	printf "BEFORE: (\${__git_ps1_branch_name}):AFTER" >expected &&
	(
		cd ignored_dir &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config disabled' '
	printf " (main)" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config disabled, pc mode' '
	printf "BEFORE: (\${__git_ps1_branch_name}):AFTER" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config unset' '
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__git_ps1 >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config unset, pc mode' '
	printf "BEFORE::AFTER" >expected &&
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__git_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - inside gitdir' '
	printf " (GIT_DIR!)" >expected &&
	(
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		cd .git &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - conflict indicator' '
	printf " (main|CONFLICT)" >expected &&
	echo "stash" >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	echo "commit" >file &&
	git commit -m "commit" file &&
	test_when_finished "git reset --hard HEAD~" &&
	test_must_fail git stash apply &&
	(
		GIT_PS1_SHOWCONFLICTSTATE="yes" &&
		__git_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_done
