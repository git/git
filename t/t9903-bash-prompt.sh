#!/bin/sh
#
# Copyright (c) 2012 SZEDER Gábor
#

test_description='test but-specific bash prompt functions'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-bash.sh

. "$GIT_BUILD_DIR/contrib/completion/but-prompt.sh"

actual="$TRASH_DIRECTORY/actual"
c_red='\\[\\e[31m\\]'
c_green='\\[\\e[32m\\]'
c_lblue='\\[\\e[1;34m\\]'
c_clear='\\[\\e[0m\\]'

test_expect_success 'setup for prompt tests' '
	but init otherrepo &&
	echo 1 >file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but tag -a -m msg1 t1 &&
	but checkout -b b1 &&
	echo 2 >file &&
	but cummit -m "second b1" file &&
	echo 3 >file &&
	but cummit -m "third b1" file &&
	but tag -a -m msg2 t2 &&
	but checkout -b b2 main &&
	echo 0 >file &&
	but cummit -m "second b2" file &&
	echo 00 >file &&
	but cummit -m "another b2" file &&
	echo 000 >file &&
	but cummit -m "yet another b2" file &&
	mkdir ignored_dir &&
	echo "ignored_dir/" >>.butignore &&
	but checkout main
'

test_expect_success 'prompt - branch name' '
	printf " (main)" >expected &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success SYMLINKS 'prompt - branch name - symlink symref' '
	printf " (main)" >expected &&
	test_when_finished "but checkout main" &&
	test_config core.preferSymlinkRefs true &&
	but checkout main &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - unborn branch' '
	printf " (unborn)" >expected &&
	but checkout --orphan unborn &&
	test_when_finished "but checkout main" &&
	__but_ps1 >"$actual" &&
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
	but init "$repo_with_newline" &&
	test_when_finished "rm -rf \"$repo_with_newline\"" &&
	mkdir "$repo_with_newline"/subdir &&
	(
		cd "$repo_with_newline/subdir" &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - detached head' '
	printf " ((%s...))" $(but log -1 --format="%h" --abbrev=13 b1^) >expected &&
	test_config core.abbrev 13 &&
	but checkout b1^ &&
	test_when_finished "but checkout main" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - contains' '
	printf " ((t2~1))" >expected &&
	but checkout b1^ &&
	test_when_finished "but checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=contains &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - branch' '
	printf " ((tags/t2~1))" >expected &&
	but checkout b1^ &&
	test_when_finished "but checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=branch &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - describe' '
	printf " ((t1-1-g%s))" $(but log -1 --format="%h" b1^) >expected &&
	but checkout b1^ &&
	test_when_finished "but checkout main" &&
	(
		GIT_PS1_DESCRIBE_STYLE=describe &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - default' '
	printf " ((t2))" >expected &&
	but checkout --detach b1 &&
	test_when_finished "but checkout main" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside .but directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		cd .but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - deep inside .but directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		cd .but/objects &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside bare repository' '
	printf " (BARE:main)" >expected &&
	but init --bare bare.but &&
	test_when_finished "rm -rf bare.but" &&
	(
		cd bare.but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - interactive rebase' '
	printf " (b1|REBASE 2/3)" >expected &&
	write_script fake_editor.sh <<-\EOF &&
		echo "exec echo" >"$1"
		echo "edit $(but log -1 --format="%h")" >>"$1"
		echo "exec echo" >>"$1"
	EOF
	test_when_finished "rm -f fake_editor.sh" &&
	test_set_editor "$TRASH_DIRECTORY/fake_editor.sh" &&
	but checkout b1 &&
	test_when_finished "but checkout main" &&
	but rebase -i HEAD^ &&
	test_when_finished "but rebase --abort" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase merge' '
	printf " (b2|REBASE 1/3)" >expected &&
	but checkout b2 &&
	test_when_finished "but checkout main" &&
	test_must_fail but rebase --merge b1 b2 &&
	test_when_finished "but rebase --abort" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase am' '
	printf " (b2|REBASE 1/3)" >expected &&
	but checkout b2 &&
	test_when_finished "but checkout main" &&
	test_must_fail but rebase --apply b1 b2 &&
	test_when_finished "but rebase --abort" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - merge' '
	printf " (b1|MERGING)" >expected &&
	but checkout b1 &&
	test_when_finished "but checkout main" &&
	test_must_fail but merge b2 &&
	test_when_finished "but reset --hard" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - cherry-pick' '
	printf " (main|CHERRY-PICKING)" >expected &&
	test_must_fail but cherry-pick b1 b1^ &&
	test_when_finished "but cherry-pick --abort" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual" &&
	but reset --merge &&
	test_must_fail but rev-parse CHERRY_PICK_HEAD &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - revert' '
	printf " (main|REVERTING)" >expected &&
	test_must_fail but revert b1^ b1 &&
	test_when_finished "but revert --abort" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual" &&
	but reset --merge &&
	test_must_fail but rev-parse REVERT_HEAD &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bisect' '
	printf " (main|BISECTING)" >expected &&
	but bisect start &&
	test_when_finished "but bisect reset" &&
	__but_ps1 >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - clean' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty worktree' '
	printf " (main *)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index' '
	printf " (main +)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	but add -u &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index and worktree' '
	printf " (main *+)" >expected &&
	echo "dirty index" >file &&
	test_when_finished "but reset --hard" &&
	but add -u &&
	echo "dirty worktree" >file &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - clean' '
	printf " (orphan #)" >expected &&
	test_when_finished "but checkout main" &&
	but checkout --orphan orphan &&
	but reset --hard &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - dirty index' '
	printf " (orphan +)" >expected &&
	test_when_finished "but checkout main" &&
	but checkout --orphan orphan &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - orphan branch - dirty index and worktree' '
	printf " (orphan *+)" >expected &&
	test_when_finished "but checkout main" &&
	but checkout --orphan orphan &&
	>file &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable unset with config disabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	test_config bash.showDirtyState false &&
	(
		sane_unset GIT_PS1_SHOWDIRTYSTATE &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable unset with config enabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	test_config bash.showDirtyState true &&
	(
		sane_unset GIT_PS1_SHOWDIRTYSTATE &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable set with config disabled' '
	printf " (main)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	test_config bash.showDirtyState false &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - shell variable set with config enabled' '
	printf " (main *)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	test_config bash.showDirtyState true &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - not shown inside .but directory' '
	printf " (GIT_DIR!)" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		cd .but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - no stash' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - stash' '
	printf " (main $)" >expected &&
	echo 2 >file &&
	but stash &&
	test_when_finished "but stash drop" &&
	but pack-refs --all &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - not shown inside .but directory' '
	printf " (GIT_DIR!)" >expected &&
	echo 2 >file &&
	but stash &&
	test_when_finished "but stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		cd .but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - no untracked files' '
	printf " (main)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd otherrepo &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - untracked files' '
	printf " (main %%)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__but_ps1 >"$actual"
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
		__but_ps1 >"$actual"
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
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - untracked files outside cwd' '
	printf " (main %%)" >expected &&
	(
		mkdir -p ignored_dir &&
		cd ignored_dir &&
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable unset with config disabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles false &&
	(
		sane_unset GIT_PS1_SHOWUNTRACKEDFILES &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable unset with config enabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles true &&
	(
		sane_unset GIT_PS1_SHOWUNTRACKEDFILES &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable set with config disabled' '
	printf " (main)" >expected &&
	test_config bash.showUntrackedFiles false &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - shell variable set with config enabled' '
	printf " (main %%)" >expected &&
	test_config bash.showUntrackedFiles true &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - not shown inside .but directory' '
	printf " (GIT_DIR!)" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd .but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - format string starting with dash' '
	printf -- "-main" >expected &&
	__but_ps1 "-%s" >"$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - pc mode' '
	printf "BEFORE: (\${__but_ps1_branch_name}):AFTER\\nmain" >expected &&
	(
		__but_ps1 "BEFORE:" ":AFTER" >"$actual" &&
		test_must_be_empty "$actual" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - branch name' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" >"$actual" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - detached head' '
	printf "BEFORE: (${c_red}\${__but_ps1_branch_name}${c_clear}):AFTER\\n(%s...)" $(but log -1 --format="%h" b1^) >expected &&
	but checkout b1^ &&
	test_when_finished "but checkout main" &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty worktree' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_red}*${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty index' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_green}+${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	but add -u &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - dirty index and worktree' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_red}*${c_green}+${c_clear}):AFTER\\nmain" >expected &&
	echo "dirty index" >file &&
	test_when_finished "but reset --hard" &&
	but add -u &&
	echo "dirty worktree" >file &&
	(
		GIT_PS1_SHOWCOLORHINTS=y &&
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - dirty status indicator - before root cummit' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_green}#${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		cd otherrepo &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - inside .but directory' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear}):AFTER\\nGIT_DIR!" >expected &&
	echo "dirty" >file &&
	test_when_finished "but reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		cd .but &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - stash status indicator' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_lblue}\$${c_clear}):AFTER\\nmain" >expected &&
	echo 2 >file &&
	but stash &&
	test_when_finished "but stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bash color pc mode - untracked files status indicator' '
	printf "BEFORE: (${c_green}\${__but_ps1_branch_name}${c_clear} ${c_red}%%${c_clear}):AFTER\\nmain" >expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s\\n%s" "$PS1" "${__but_ps1_branch_name}" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - zsh color pc mode' '
	printf "BEFORE: (%%F{green}main%%f):AFTER" >expected &&
	(
		ZSH_VERSION=5.0.0 &&
		GIT_PS1_SHOWCOLORHINTS=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config disabled' '
	printf " (main)" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config disabled, pc mode' '
	printf "BEFORE: (\${__but_ps1_branch_name}):AFTER" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config unset' '
	printf " (main)" >expected &&
	(
		cd ignored_dir &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var unset, config unset, pc mode' '
	printf "BEFORE: (\${__but_ps1_branch_name}):AFTER" >expected &&
	(
		cd ignored_dir &&
		__but_ps1 "BEFORE:" ":AFTER" &&
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
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config disabled, pc mode' '
	printf "BEFORE: (\${__but_ps1_branch_name}):AFTER" >expected &&
	test_config bash.hideIfPwdIgnored false &&
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config unset' '
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__but_ps1 >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - env var set, config unset, pc mode' '
	printf "BEFORE::AFTER" >expected &&
	(
		cd ignored_dir &&
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		__but_ps1 "BEFORE:" ":AFTER" &&
		printf "%s" "$PS1" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - hide if pwd ignored - inside butdir' '
	printf " (GIT_DIR!)" >expected &&
	(
		GIT_PS1_HIDE_IF_PWD_IGNORED=y &&
		cd .but &&
		__but_ps1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_done
