#!/bin/sh
#
# Copyright (c) 2012 SZEDER Gábor
#

test_description='test git-specific bash prompt functions'

. ./lib-bash.sh

. "$GIT_BUILD_DIR/contrib/completion/git-prompt.sh"

actual="$TRASH_DIRECTORY/actual"

test_expect_success 'setup for prompt tests' '
	mkdir -p subdir/subsubdir &&
	git init otherrepo &&
	echo 1 > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag -a -m msg1 t1 &&
	git checkout -b b1 &&
	echo 2 > file &&
	git commit -m "second b1" file &&
	echo 3 > file &&
	git commit -m "third b1" file &&
	git tag -a -m msg2 t2 &&
	git checkout -b b2 master &&
	echo 0 > file &&
	git commit -m "second b2" file &&
	git checkout master
'

test_expect_success 'gitdir - from command line (through $__git_dir)' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	(
		__git_dir="$TRASH_DIRECTORY/otherrepo/.git" &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - repo as argument' '
	echo "otherrepo/.git" > expected &&
	__gitdir "otherrepo" > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - remote as argument' '
	echo "remote" > expected &&
	__gitdir "remote" > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - .git directory in cwd' '
	echo ".git" > expected &&
	__gitdir > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - .git directory in parent' '
	echo "$TRASH_DIRECTORY/.git" > expected &&
	(
		cd subdir/subsubdir &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - cwd is a .git directory' '
	echo "." > expected &&
	(
		cd .git &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - parent is a .git directory' '
	echo "$TRASH_DIRECTORY/.git" > expected &&
	(
		cd .git/refs/heads &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - $GIT_DIR set while .git directory in cwd' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	(
		GIT_DIR="$TRASH_DIRECTORY/otherrepo/.git" &&
		export GIT_DIR &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - $GIT_DIR set while .git directory in parent' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	(
		GIT_DIR="$TRASH_DIRECTORY/otherrepo/.git" &&
		export GIT_DIR &&
		cd subdir &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - non-existing $GIT_DIR' '
	(
		GIT_DIR="$TRASH_DIRECTORY/non-existing" &&
		export GIT_DIR &&
		test_must_fail __gitdir
	)
'

test_expect_success 'gitdir - gitfile in cwd' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	echo "gitdir: $TRASH_DIRECTORY/otherrepo/.git" > subdir/.git &&
	test_when_finished "rm -f subdir/.git" &&
	(
		cd subdir &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - gitfile in parent' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	echo "gitdir: $TRASH_DIRECTORY/otherrepo/.git" > subdir/.git &&
	test_when_finished "rm -f subdir/.git" &&
	(
		cd subdir/subsubdir &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success SYMLINKS 'gitdir - resulting path avoids symlinks' '
	echo "$TRASH_DIRECTORY/otherrepo/.git" > expected &&
	mkdir otherrepo/dir &&
	test_when_finished "rm -rf otherrepo/dir" &&
	ln -s otherrepo/dir link &&
	test_when_finished "rm -f link" &&
	(
		cd link &&
		__gitdir > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'gitdir - not a git repository' '
	(
		cd subdir/subsubdir &&
		GIT_CEILING_DIRECTORIES="$TRASH_DIRECTORY" &&
		export GIT_CEILING_DIRECTORIES &&
		test_must_fail __gitdir
	)
'

test_expect_success 'prompt - branch name' '
	printf " (master)" > expected &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - detached head' '
	printf " ((%s...))" $(git log -1 --format="%h" b1^) > expected &&
	git checkout b1^ &&
	test_when_finished "git checkout master" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - contains' '
	printf " ((t2~1))" > expected &&
	git checkout b1^ &&
	test_when_finished "git checkout master" &&
	(
		GIT_PS1_DESCRIBE_STYLE=contains &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - branch' '
	printf " ((b1~1))" > expected &&
	git checkout b1^ &&
	test_when_finished "git checkout master" &&
	(
		GIT_PS1_DESCRIBE_STYLE=branch &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - describe' '
	printf " ((t1-1-g%s))" $(git log -1 --format="%h" b1^) > expected &&
	git checkout b1^ &&
	test_when_finished "git checkout master" &&
	(
		GIT_PS1_DESCRIBE_STYLE=describe &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - describe detached head - default' '
	printf " ((t2))" > expected &&
	git checkout --detach b1 &&
	test_when_finished "git checkout master" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside .git directory' '
	printf " (GIT_DIR!)" > expected &&
	(
		cd .git &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - deep inside .git directory' '
	printf " (GIT_DIR!)" > expected &&
	(
		cd .git/refs/heads &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - inside bare repository' '
	printf " (BARE:master)" > expected &&
	git init --bare bare.git &&
	test_when_finished "rm -rf bare.git" &&
	(
		cd bare.git &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - interactive rebase' '
	printf " (b1|REBASE-i)" > expected
	echo "#!$SHELL_PATH" >fake_editor.sh &&
	cat >>fake_editor.sh <<\EOF &&
echo "edit $(git log -1 --format="%h")" > "$1"
EOF
	test_when_finished "rm -f fake_editor.sh" &&
	chmod a+x fake_editor.sh &&
	test_set_editor "$TRASH_DIRECTORY/fake_editor.sh" &&
	git checkout b1 &&
	test_when_finished "git checkout master" &&
	git rebase -i HEAD^ &&
	test_when_finished "git rebase --abort"
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase merge' '
	printf " (b2|REBASE-m)" > expected &&
	git checkout b2 &&
	test_when_finished "git checkout master" &&
	test_must_fail git rebase --merge b1 b2 &&
	test_when_finished "git rebase --abort" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - rebase' '
	printf " ((t2)|REBASE)" > expected &&
	git checkout b2 &&
	test_when_finished "git checkout master" &&
	test_must_fail git rebase b1 b2 &&
	test_when_finished "git rebase --abort" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - merge' '
	printf " (b1|MERGING)" > expected &&
	git checkout b1 &&
	test_when_finished "git checkout master" &&
	test_must_fail git merge b2 &&
	test_when_finished "git reset --hard" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - cherry-pick' '
	printf " (master|CHERRY-PICKING)" > expected &&
	test_must_fail git cherry-pick b1 &&
	test_when_finished "git reset --hard" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - bisect' '
	printf " (master|BISECTING)" > expected &&
	git bisect start &&
	test_when_finished "git bisect reset" &&
	__git_ps1 > "$actual" &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - clean' '
	printf " (master)" > expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty worktree' '
	printf " (master *)" > expected &&
	echo "dirty" > file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index' '
	printf " (master +)" > expected &&
	echo "dirty" > file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - dirty index and worktree' '
	printf " (master *+)" > expected &&
	echo "dirty index" > file &&
	test_when_finished "git reset --hard" &&
	git add -u &&
	echo "dirty worktree" > file &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - before root commit' '
	printf " (master #)" > expected &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		cd otherrepo &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - disabled by config' '
	printf " (master)" > expected &&
	echo "dirty" > file &&
	test_when_finished "git reset --hard" &&
	test_config bash.showDirtyState false &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - dirty status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" > expected &&
	echo "dirty" > file &&
	test_when_finished "git reset --hard" &&
	(
		GIT_PS1_SHOWDIRTYSTATE=y &&
		cd .git &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - no stash' '
	printf " (master)" > expected &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - stash' '
	printf " (master $)" > expected &&
	echo 2 >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - stash status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" > expected &&
	echo 2 >file &&
	git stash &&
	test_when_finished "git stash drop" &&
	(
		GIT_PS1_SHOWSTASHSTATE=y &&
		cd .git &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - no untracked files' '
	printf " (master)" > expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd otherrepo &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - untracked files' '
	printf " (master %%)" > expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - untracked files status indicator - not shown inside .git directory' '
	printf " (GIT_DIR!)" > expected &&
	(
		GIT_PS1_SHOWUNTRACKEDFILES=y &&
		cd .git &&
		__git_ps1 > "$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'prompt - format string starting with dash' '
	printf -- "-master" > expected &&
	__git_ps1 "-%s" > "$actual" &&
	test_cmp expected "$actual"
'

test_done
