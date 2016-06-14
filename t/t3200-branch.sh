#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git branch assorted tests'

. ./test-lib.sh

test_expect_success 'prepare a trivial repository' '
	echo Hello >A &&
	git update-index --add A &&
	git commit -m "Initial commit." &&
	echo World >>A &&
	git update-index --add A &&
	git commit -m "Second commit." &&
	HEAD=$(git rev-parse --verify HEAD)
'

test_expect_success 'git branch --help should not have created a bogus branch' '
	test_might_fail git branch --man --help </dev/null >/dev/null 2>&1 &&
	test_path_is_missing .git/refs/heads/--help
'

test_expect_success 'branch -h in broken repository' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/refs/heads/master &&
		test_expect_code 129 git branch -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'git branch abc should create a branch' '
	git branch abc && test_path_is_file .git/refs/heads/abc
'

test_expect_success 'git branch a/b/c should create a branch' '
	git branch a/b/c && test_path_is_file .git/refs/heads/a/b/c
'

test_expect_success 'git branch HEAD should fail' '
	test_must_fail git branch HEAD
'

cat >expect <<EOF
$_z40 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success 'git branch -l d/e/f should create a branch and a log' '
	GIT_COMMITTER_DATE="2005-05-26 23:30" \
	git branch -l d/e/f &&
	test_path_is_file .git/refs/heads/d/e/f &&
	test_path_is_file .git/logs/refs/heads/d/e/f &&
	test_cmp expect .git/logs/refs/heads/d/e/f
'

test_expect_success 'git branch -d d/e/f should delete a branch and a log' '
	git branch -d d/e/f &&
	test_path_is_missing .git/refs/heads/d/e/f &&
	test_must_fail git reflog exists refs/heads/d/e/f
'

test_expect_success 'git branch j/k should work after branch j has been deleted' '
	git branch j &&
	git branch -d j &&
	git branch j/k
'

test_expect_success 'git branch l should work after branch l/m has been deleted' '
	git branch l/m &&
	git branch -d l/m &&
	git branch l
'

test_expect_success 'git branch -m dumps usage' '
	test_expect_code 128 git branch -m 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'git branch -m m broken_symref should work' '
	test_when_finished "git branch -D broken_symref" &&
	git branch -l m &&
	git symbolic-ref refs/heads/broken_symref refs/heads/i_am_broken &&
	git branch -m m broken_symref &&
	git reflog exists refs/heads/broken_symref &&
	test_must_fail git reflog exists refs/heads/i_am_broken
'

test_expect_success 'git branch -m m m/m should work' '
	git branch -l m &&
	git branch -m m m/m &&
	git reflog exists refs/heads/m/m
'

test_expect_success 'git branch -m n/n n should work' '
	git branch -l n/n &&
	git branch -m n/n n &&
	git reflog exists refs/heads/n
'

test_expect_success 'git branch -m o/o o should fail when o/p exists' '
	git branch o/o &&
	git branch o/p &&
	test_must_fail git branch -m o/o o
'

test_expect_success 'git branch -m o/q o/p should fail when o/p exists' '
	git branch o/q &&
	test_must_fail git branch -m o/q o/p
'

test_expect_success 'git branch -M o/q o/p should work when o/p exists' '
	git branch -M o/q o/p
'

test_expect_success 'git branch -m -f o/q o/p should work when o/p exists' '
	git branch o/q &&
	git branch -m -f o/q o/p
'

test_expect_success 'git branch -m q r/q should fail when r exists' '
	git branch q &&
	git branch r &&
	test_must_fail git branch -m q r/q
'

test_expect_success 'git branch -M foo bar should fail when bar is checked out' '
	git branch bar &&
	git checkout -b foo &&
	test_must_fail git branch -M bar foo
'

test_expect_success 'git branch -M baz bam should succeed when baz is checked out' '
	git checkout -b baz &&
	git branch bam &&
	git branch -M baz bam &&
	test $(git rev-parse --abbrev-ref HEAD) = bam
'

test_expect_success 'git branch -M baz bam should succeed when baz is checked out as linked working tree' '
	git checkout master &&
	git worktree add -b baz bazdir &&
	git worktree add -f bazdir2 baz &&
	git branch -M baz bam &&
	test $(git -C bazdir rev-parse --abbrev-ref HEAD) = bam &&
	test $(git -C bazdir2 rev-parse --abbrev-ref HEAD) = bam
'

test_expect_success 'git branch -M baz bam should succeed within a worktree in which baz is checked out' '
	git checkout -b baz &&
	git worktree add -f bazdir3 baz &&
	(
		cd bazdir3 &&
		git branch -M baz bam &&
		test $(git rev-parse --abbrev-ref HEAD) = bam
	) &&
	test $(git rev-parse --abbrev-ref HEAD) = bam
'

test_expect_success 'git branch -M master should work when master is checked out' '
	git checkout master &&
	git branch -M master
'

test_expect_success 'git branch -M master master should work when master is checked out' '
	git checkout master &&
	git branch -M master master
'

test_expect_success 'git branch -M master2 master2 should work when master is checked out' '
	git checkout master &&
	git branch master2 &&
	git branch -M master2 master2
'

test_expect_success 'git branch -v -d t should work' '
	git branch t &&
	test_path_is_file .git/refs/heads/t &&
	git branch -v -d t &&
	test_path_is_missing .git/refs/heads/t
'

test_expect_success 'git branch -v -m t s should work' '
	git branch t &&
	test_path_is_file .git/refs/heads/t &&
	git branch -v -m t s &&
	test_path_is_missing .git/refs/heads/t &&
	test_path_is_file .git/refs/heads/s &&
	git branch -d s
'

test_expect_success 'git branch -m -d t s should fail' '
	git branch t &&
	test_path_is_file .git/refs/heads/t &&
	test_must_fail git branch -m -d t s &&
	git branch -d t &&
	test_path_is_missing .git/refs/heads/t
'

test_expect_success 'git branch --list -d t should fail' '
	git branch t &&
	test_path_is_file .git/refs/heads/t &&
	test_must_fail git branch --list -d t &&
	git branch -d t &&
	test_path_is_missing .git/refs/heads/t
'

test_expect_success 'git branch --column' '
	COLUMNS=81 git branch --column=column >actual &&
	cat >expected <<\EOF &&
  a/b/c     bam       foo       l       * master    n         o/p       r
  abc       bar       j/k       m/m       master2   o/o       q
EOF
	test_cmp expected actual
'

test_expect_success 'git branch --column with an extremely long branch name' '
	long=this/is/a/part/of/long/branch/name &&
	long=z$long/$long/$long/$long &&
	test_when_finished "git branch -d $long" &&
	git branch $long &&
	COLUMNS=80 git branch --column=column >actual &&
	cat >expected <<EOF &&
  a/b/c
  abc
  bam
  bar
  foo
  j/k
  l
  m/m
* master
  master2
  n
  o/o
  o/p
  q
  r
  $long
EOF
	test_cmp expected actual
'

test_expect_success 'git branch with column.*' '
	git config column.ui column &&
	git config column.branch "dense" &&
	COLUMNS=80 git branch >actual &&
	git config --unset column.branch &&
	git config --unset column.ui &&
	cat >expected <<\EOF &&
  a/b/c   bam   foo   l   * master    n     o/p   r
  abc     bar   j/k   m/m   master2   o/o   q
EOF
	test_cmp expected actual
'

test_expect_success 'git branch --column -v should fail' '
	test_must_fail git branch --column -v
'

test_expect_success 'git branch -v with column.ui ignored' '
	git config column.ui column &&
	COLUMNS=80 git branch -v | cut -c -10 | sed "s/ *$//" >actual &&
	git config --unset column.ui &&
	cat >expected <<\EOF &&
  a/b/c
  abc
  bam
  bar
  foo
  j/k
  l
  m/m
* master
  master2
  n
  o/o
  o/p
  q
  r
EOF
	test_cmp expected actual
'

mv .git/config .git/config-saved

test_expect_success 'git branch -m q q2 without config should succeed' '
	git branch -m q q2 &&
	git branch -m q2 q
'

mv .git/config-saved .git/config

git config branch.s/s.dummy Hello

test_expect_success 'git branch -m s/s s should work when s/t is deleted' '
	git branch -l s/s &&
	git reflog exists refs/heads/s/s &&
	git branch -l s/t &&
	git reflog exists refs/heads/s/t &&
	git branch -d s/t &&
	git branch -m s/s s &&
	git reflog exists refs/heads/s
'

test_expect_success 'config information was renamed, too' '
	test $(git config branch.s.dummy) = Hello &&
	test_must_fail git config branch.s/s/dummy
'

test_expect_success 'deleting a symref' '
	git branch target &&
	git symbolic-ref refs/heads/symref refs/heads/target &&
	echo "Deleted branch symref (was refs/heads/target)." >expect &&
	git branch -d symref >actual &&
	test_path_is_file .git/refs/heads/target &&
	test_path_is_missing .git/refs/heads/symref &&
	test_i18ncmp expect actual
'

test_expect_success 'deleting a dangling symref' '
	git symbolic-ref refs/heads/dangling-symref nowhere &&
	test_path_is_file .git/refs/heads/dangling-symref &&
	echo "Deleted branch dangling-symref (was nowhere)." >expect &&
	git branch -d dangling-symref >actual &&
	test_path_is_missing .git/refs/heads/dangling-symref &&
	test_i18ncmp expect actual
'

test_expect_success 'deleting a self-referential symref' '
	git symbolic-ref refs/heads/self-reference refs/heads/self-reference &&
	test_path_is_file .git/refs/heads/self-reference &&
	echo "Deleted branch self-reference (was refs/heads/self-reference)." >expect &&
	git branch -d self-reference >actual &&
	test_path_is_missing .git/refs/heads/self-reference &&
	test_i18ncmp expect actual
'

test_expect_success 'renaming a symref is not allowed' '
	git symbolic-ref refs/heads/master2 refs/heads/master &&
	test_must_fail git branch -m master2 master3 &&
	git symbolic-ref refs/heads/master2 &&
	test_path_is_file .git/refs/heads/master &&
	test_path_is_missing .git/refs/heads/master3
'

test_expect_success SYMLINKS 'git branch -m u v should fail when the reflog for u is a symlink' '
	git branch -l u &&
	mv .git/logs/refs/heads/u real-u &&
	ln -s real-u .git/logs/refs/heads/u &&
	test_must_fail git branch -m u v
'

test_expect_success 'test tracking setup via --track' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track my1 local/master &&
	test $(git config branch.my1.remote) = local &&
	test $(git config branch.my1.merge) = refs/heads/master
'

test_expect_success 'test tracking setup (non-wildcard, matching)' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/master:refs/remotes/local/master &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track my4 local/master &&
	test $(git config branch.my4.remote) = local &&
	test $(git config branch.my4.merge) = refs/heads/master
'

test_expect_success 'tracking setup fails on non-matching refspec' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git config remote.local.fetch refs/heads/s:refs/remotes/local/s &&
	test_must_fail git branch --track my5 local/master &&
	test_must_fail git config branch.my5.remote &&
	test_must_fail git config branch.my5.merge
'

test_expect_success 'test tracking setup via config' '
	git config branch.autosetupmerge true &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch my3 local/master &&
	test $(git config branch.my3.remote) = local &&
	test $(git config branch.my3.merge) = refs/heads/master
'

test_expect_success 'test overriding tracking setup via --no-track' '
	git config branch.autosetupmerge true &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track my2 local/master &&
	git config branch.autosetupmerge false &&
	! test "$(git config branch.my2.remote)" = local &&
	! test "$(git config branch.my2.merge)" = refs/heads/master
'

test_expect_success 'no tracking without .fetch entries' '
	git config branch.autosetupmerge true &&
	git branch my6 s &&
	git config branch.autosetupmerge false &&
	test -z "$(git config branch.my6.remote)" &&
	test -z "$(git config branch.my6.merge)"
'

test_expect_success 'test tracking setup via --track but deeper' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/o/o || git fetch local) &&
	git branch --track my7 local/o/o &&
	test "$(git config branch.my7.remote)" = local &&
	test "$(git config branch.my7.merge)" = refs/heads/o/o
'

test_expect_success 'test deleting branch deletes branch config' '
	git branch -d my7 &&
	test -z "$(git config branch.my7.remote)" &&
	test -z "$(git config branch.my7.merge)"
'

test_expect_success 'test deleting branch without config' '
	git branch my7 s &&
	sha1=$(git rev-parse my7 | cut -c 1-7) &&
	echo "Deleted branch my7 (was $sha1)." >expect &&
	git branch -d my7 >actual 2>&1 &&
	test_i18ncmp expect actual
'

test_expect_success 'deleting currently checked out branch fails' '
	git worktree add -b my7 my7 &&
	test_must_fail git -C my7 branch -d my7 &&
	test_must_fail git branch -d my7
'

test_expect_success 'test --track without .fetch entries' '
	git branch --track my8 &&
	test "$(git config branch.my8.remote)" &&
	test "$(git config branch.my8.merge)"
'

test_expect_success 'branch from non-branch HEAD w/autosetupmerge=always' '
	git config branch.autosetupmerge always &&
	git branch my9 HEAD^ &&
	git config branch.autosetupmerge false
'

test_expect_success 'branch from non-branch HEAD w/--track causes failure' '
	test_must_fail git branch --track my10 HEAD^
'

test_expect_success 'branch from tag w/--track causes failure' '
	git tag foobar &&
	test_must_fail git branch --track my11 foobar
'

test_expect_success '--set-upstream-to fails on multiple branches' '
	test_must_fail git branch --set-upstream-to master a b c
'

test_expect_success '--set-upstream-to fails on detached HEAD' '
	git checkout HEAD^{} &&
	test_must_fail git branch --set-upstream-to master &&
	git checkout -
'

test_expect_success '--set-upstream-to fails on a missing dst branch' '
	test_must_fail git branch --set-upstream-to master does-not-exist
'

test_expect_success '--set-upstream-to fails on a missing src branch' '
	test_must_fail git branch --set-upstream-to does-not-exist master
'

test_expect_success '--set-upstream-to fails on a non-ref' '
	test_must_fail git branch --set-upstream-to HEAD^{}
'

test_expect_success '--set-upstream-to fails on locked config' '
	test_when_finished "rm -f .git/config.lock" &&
	>.git/config.lock &&
	git branch locked &&
	test_must_fail git branch --set-upstream-to locked
'

test_expect_success 'use --set-upstream-to modify HEAD' '
	test_config branch.master.remote foo &&
	test_config branch.master.merge foo &&
	git branch my12 &&
	git branch --set-upstream-to my12 &&
	test "$(git config branch.master.remote)" = "." &&
	test "$(git config branch.master.merge)" = "refs/heads/my12"
'

test_expect_success 'use --set-upstream-to modify a particular branch' '
	git branch my13 &&
	git branch --set-upstream-to master my13 &&
	test "$(git config branch.my13.remote)" = "." &&
	test "$(git config branch.my13.merge)" = "refs/heads/master"
'

test_expect_success '--unset-upstream should fail if given a non-existent branch' '
	test_must_fail git branch --unset-upstream i-dont-exist
'

test_expect_success '--unset-upstream should fail if config is locked' '
	test_when_finished "rm -f .git/config.lock" &&
	git branch --set-upstream-to locked &&
	>.git/config.lock &&
	test_must_fail git branch --unset-upstream
'

test_expect_success 'test --unset-upstream on HEAD' '
	git branch my14 &&
	test_config branch.master.remote foo &&
	test_config branch.master.merge foo &&
	git branch --set-upstream-to my14 &&
	git branch --unset-upstream &&
	test_must_fail git config branch.master.remote &&
	test_must_fail git config branch.master.merge &&
	# fail for a branch without upstream set
	test_must_fail git branch --unset-upstream
'

test_expect_success '--unset-upstream should fail on multiple branches' '
	test_must_fail git branch --unset-upstream a b c
'

test_expect_success '--unset-upstream should fail on detached HEAD' '
	git checkout HEAD^{} &&
	test_must_fail git branch --unset-upstream &&
	git checkout -
'

test_expect_success 'test --unset-upstream on a particular branch' '
	git branch my15 &&
	git branch --set-upstream-to master my14 &&
	git branch --unset-upstream my14 &&
	test_must_fail git config branch.my14.remote &&
	test_must_fail git config branch.my14.merge
'

test_expect_success '--set-upstream shows message when creating a new branch that exists as remote-tracking' '
	git update-ref refs/remotes/origin/master HEAD &&
	git branch --set-upstream origin/master 2>actual &&
	test_when_finished git update-ref -d refs/remotes/origin/master &&
	test_when_finished git branch -d origin/master &&
	cat >expected <<EOF &&
The --set-upstream flag is deprecated and will be removed. Consider using --track or --set-upstream-to

If you wanted to make '"'master'"' track '"'origin/master'"', do this:

    git branch -d origin/master
    git branch --set-upstream-to origin/master
EOF
	test_i18ncmp expected actual
'

test_expect_success '--set-upstream with two args only shows the deprecation message' '
	git branch --set-upstream master my13 2>actual &&
	test_when_finished git branch --unset-upstream master &&
	cat >expected <<EOF &&
The --set-upstream flag is deprecated and will be removed. Consider using --track or --set-upstream-to
EOF
	test_i18ncmp expected actual
'

test_expect_success '--set-upstream with one arg only shows the deprecation message if the branch existed' '
	git branch --set-upstream my13 2>actual &&
	test_when_finished git branch --unset-upstream my13 &&
	cat >expected <<EOF &&
The --set-upstream flag is deprecated and will be removed. Consider using --track or --set-upstream-to
EOF
	test_i18ncmp expected actual
'

test_expect_success '--set-upstream-to notices an error to set branch as own upstream' '
	git branch --set-upstream-to refs/heads/my13 my13 2>actual &&
	cat >expected <<-\EOF &&
	warning: Not setting branch my13 as its own upstream.
	EOF
	test_expect_code 1 git config branch.my13.remote &&
	test_expect_code 1 git config branch.my13.merge &&
	test_i18ncmp expected actual
'

# Keep this test last, as it changes the current branch
cat >expect <<EOF
$_z40 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success 'git checkout -b g/h/i -l should create a branch and a log' '
	GIT_COMMITTER_DATE="2005-05-26 23:30" \
	git checkout -b g/h/i -l master &&
	test_path_is_file .git/refs/heads/g/h/i &&
	test_path_is_file .git/logs/refs/heads/g/h/i &&
	test_cmp expect .git/logs/refs/heads/g/h/i
'

test_expect_success 'checkout -b makes reflog by default' '
	git checkout master &&
	git config --unset core.logAllRefUpdates &&
	git checkout -b alpha &&
	git rev-parse --verify alpha@{0}
'

test_expect_success 'checkout -b does not make reflog when core.logAllRefUpdates = false' '
	git checkout master &&
	git config core.logAllRefUpdates false &&
	git checkout -b beta &&
	test_must_fail git rev-parse --verify beta@{0}
'

test_expect_success 'checkout -b with -l makes reflog when core.logAllRefUpdates = false' '
	git checkout master &&
	git checkout -lb gamma &&
	git config --unset core.logAllRefUpdates &&
	git rev-parse --verify gamma@{0}
'

test_expect_success 'avoid ambiguous track' '
	git config branch.autosetupmerge true &&
	git config remote.ambi1.url lalala &&
	git config remote.ambi1.fetch refs/heads/lalala:refs/heads/master &&
	git config remote.ambi2.url lilili &&
	git config remote.ambi2.fetch refs/heads/lilili:refs/heads/master &&
	test_must_fail git branch all1 master &&
	test -z "$(git config branch.all1.merge)"
'

test_expect_success 'autosetuprebase local on a tracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase local &&
	(git show-ref -q refs/remotes/local/o || git fetch local) &&
	git branch mybase &&
	git branch --track myr1 mybase &&
	test "$(git config branch.myr1.remote)" = . &&
	test "$(git config branch.myr1.merge)" = refs/heads/mybase &&
	test "$(git config branch.myr1.rebase)" = true
'

test_expect_success 'autosetuprebase always on a tracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase always &&
	(git show-ref -q refs/remotes/local/o || git fetch local) &&
	git branch mybase2 &&
	git branch --track myr2 mybase &&
	test "$(git config branch.myr2.remote)" = . &&
	test "$(git config branch.myr2.merge)" = refs/heads/mybase &&
	test "$(git config branch.myr2.rebase)" = true
'

test_expect_success 'autosetuprebase remote on a tracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase remote &&
	(git show-ref -q refs/remotes/local/o || git fetch local) &&
	git branch mybase3 &&
	git branch --track myr3 mybase2 &&
	test "$(git config branch.myr3.remote)" = . &&
	test "$(git config branch.myr3.merge)" = refs/heads/mybase2 &&
	! test "$(git config branch.myr3.rebase)" = true
'

test_expect_success 'autosetuprebase never on a tracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase never &&
	(git show-ref -q refs/remotes/local/o || git fetch local) &&
	git branch mybase4 &&
	git branch --track myr4 mybase2 &&
	test "$(git config branch.myr4.remote)" = . &&
	test "$(git config branch.myr4.merge)" = refs/heads/mybase2 &&
	! test "$(git config branch.myr4.rebase)" = true
'

test_expect_success 'autosetuprebase local on a tracked remote branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase local &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track myr5 local/master &&
	test "$(git config branch.myr5.remote)" = local &&
	test "$(git config branch.myr5.merge)" = refs/heads/master &&
	! test "$(git config branch.myr5.rebase)" = true
'

test_expect_success 'autosetuprebase never on a tracked remote branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase never &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track myr6 local/master &&
	test "$(git config branch.myr6.remote)" = local &&
	test "$(git config branch.myr6.merge)" = refs/heads/master &&
	! test "$(git config branch.myr6.rebase)" = true
'

test_expect_success 'autosetuprebase remote on a tracked remote branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase remote &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track myr7 local/master &&
	test "$(git config branch.myr7.remote)" = local &&
	test "$(git config branch.myr7.merge)" = refs/heads/master &&
	test "$(git config branch.myr7.rebase)" = true
'

test_expect_success 'autosetuprebase always on a tracked remote branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	git config branch.autosetuprebase remote &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track myr8 local/master &&
	test "$(git config branch.myr8.remote)" = local &&
	test "$(git config branch.myr8.merge)" = refs/heads/master &&
	test "$(git config branch.myr8.rebase)" = true
'

test_expect_success 'autosetuprebase unconfigured on a tracked remote branch' '
	git config --unset branch.autosetuprebase &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --track myr9 local/master &&
	test "$(git config branch.myr9.remote)" = local &&
	test "$(git config branch.myr9.merge)" = refs/heads/master &&
	test "z$(git config branch.myr9.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on a tracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/o || git fetch local) &&
	git branch mybase10 &&
	git branch --track myr10 mybase2 &&
	test "$(git config branch.myr10.remote)" = . &&
	test "$(git config branch.myr10.merge)" = refs/heads/mybase2 &&
	test "z$(git config branch.myr10.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on untracked local branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr11 mybase2 &&
	test "z$(git config branch.myr11.remote)" = z &&
	test "z$(git config branch.myr11.merge)" = z &&
	test "z$(git config branch.myr11.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on untracked remote branch' '
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr12 local/master &&
	test "z$(git config branch.myr12.remote)" = z &&
	test "z$(git config branch.myr12.merge)" = z &&
	test "z$(git config branch.myr12.rebase)" = z
'

test_expect_success 'autosetuprebase never on an untracked local branch' '
	git config branch.autosetuprebase never &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr13 mybase2 &&
	test "z$(git config branch.myr13.remote)" = z &&
	test "z$(git config branch.myr13.merge)" = z &&
	test "z$(git config branch.myr13.rebase)" = z
'

test_expect_success 'autosetuprebase local on an untracked local branch' '
	git config branch.autosetuprebase local &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr14 mybase2 &&
	test "z$(git config branch.myr14.remote)" = z &&
	test "z$(git config branch.myr14.merge)" = z &&
	test "z$(git config branch.myr14.rebase)" = z
'

test_expect_success 'autosetuprebase remote on an untracked local branch' '
	git config branch.autosetuprebase remote &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr15 mybase2 &&
	test "z$(git config branch.myr15.remote)" = z &&
	test "z$(git config branch.myr15.merge)" = z &&
	test "z$(git config branch.myr15.rebase)" = z
'

test_expect_success 'autosetuprebase always on an untracked local branch' '
	git config branch.autosetuprebase always &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr16 mybase2 &&
	test "z$(git config branch.myr16.remote)" = z &&
	test "z$(git config branch.myr16.merge)" = z &&
	test "z$(git config branch.myr16.rebase)" = z
'

test_expect_success 'autosetuprebase never on an untracked remote branch' '
	git config branch.autosetuprebase never &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr17 local/master &&
	test "z$(git config branch.myr17.remote)" = z &&
	test "z$(git config branch.myr17.merge)" = z &&
	test "z$(git config branch.myr17.rebase)" = z
'

test_expect_success 'autosetuprebase local on an untracked remote branch' '
	git config branch.autosetuprebase local &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr18 local/master &&
	test "z$(git config branch.myr18.remote)" = z &&
	test "z$(git config branch.myr18.merge)" = z &&
	test "z$(git config branch.myr18.rebase)" = z
'

test_expect_success 'autosetuprebase remote on an untracked remote branch' '
	git config branch.autosetuprebase remote &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr19 local/master &&
	test "z$(git config branch.myr19.remote)" = z &&
	test "z$(git config branch.myr19.merge)" = z &&
	test "z$(git config branch.myr19.rebase)" = z
'

test_expect_success 'autosetuprebase always on an untracked remote branch' '
	git config branch.autosetuprebase always &&
	git config remote.local.url . &&
	git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(git show-ref -q refs/remotes/local/master || git fetch local) &&
	git branch --no-track myr20 local/master &&
	test "z$(git config branch.myr20.remote)" = z &&
	test "z$(git config branch.myr20.merge)" = z &&
	test "z$(git config branch.myr20.rebase)" = z
'

test_expect_success 'autosetuprebase always on detached HEAD' '
	git config branch.autosetupmerge always &&
	test_when_finished git checkout master &&
	git checkout HEAD^0 &&
	git branch my11 &&
	test -z "$(git config branch.my11.remote)" &&
	test -z "$(git config branch.my11.merge)"
'

test_expect_success 'detect misconfigured autosetuprebase (bad value)' '
	git config branch.autosetuprebase garbage &&
	test_must_fail git branch
'

test_expect_success 'detect misconfigured autosetuprebase (no value)' '
	git config --unset branch.autosetuprebase &&
	echo "[branch] autosetuprebase" >>.git/config &&
	test_must_fail git branch &&
	git config --unset branch.autosetuprebase
'

test_expect_success 'attempt to delete a branch without base and unmerged to HEAD' '
	git checkout my9 &&
	git config --unset branch.my8.merge &&
	test_must_fail git branch -d my8
'

test_expect_success 'attempt to delete a branch merged to its base' '
	# we are on my9 which is the initial commit; traditionally
	# we would not have allowed deleting my8 that is not merged
	# to my9, but it is set to track master that already has my8
	git config branch.my8.merge refs/heads/master &&
	git branch -d my8
'

test_expect_success 'attempt to delete a branch merged to its base' '
	git checkout master &&
	echo Third >>A &&
	git commit -m "Third commit" A &&
	git branch -t my10 my9 &&
	git branch -f my10 HEAD^ &&
	# we are on master which is at the third commit, and my10
	# is behind us, so traditionally we would have allowed deleting
	# it; but my10 is set to track my9 that is further behind.
	test_must_fail git branch -d my10
'

test_expect_success 'use set-upstream on the current branch' '
	git checkout master &&
	git --bare init myupstream.git &&
	git push myupstream.git master:refs/heads/frotz &&
	git remote add origin myupstream.git &&
	git fetch &&
	git branch --set-upstream master origin/frotz &&

	test "z$(git config branch.master.remote)" = "zorigin" &&
	test "z$(git config branch.master.merge)" = "zrefs/heads/frotz"

'

test_expect_success 'use --edit-description' '
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	EDITOR=./editor git branch --edit-description &&
		write_script editor <<-\EOF &&
		git stripspace -s <"$1" >"EDITOR_OUTPUT"
	EOF
	EDITOR=./editor git branch --edit-description &&
	echo "New contents" >expect &&
	test_cmp EDITOR_OUTPUT expect
'

test_expect_success 'detect typo in branch name when using --edit-description' '
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	test_must_fail env EDITOR=./editor git branch --edit-description no-such-branch
'

test_expect_success 'refuse --edit-description on unborn branch for now' '
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	git checkout --orphan unborn &&
	test_must_fail env EDITOR=./editor git branch --edit-description
'

test_expect_success '--merged catches invalid object names' '
	test_must_fail git branch --merged 0000000000000000000000000000000000000000
'

test_expect_success 'tracking with unexpected .fetch refspec' '
	rm -rf a b c d &&
	git init a &&
	(
		cd a &&
		test_commit a
	) &&
	git init b &&
	(
		cd b &&
		test_commit b
	) &&
	git init c &&
	(
		cd c &&
		test_commit c &&
		git remote add a ../a &&
		git remote add b ../b &&
		git fetch --all
	) &&
	git init d &&
	(
		cd d &&
		git remote add c ../c &&
		git config remote.c.fetch "+refs/remotes/*:refs/remotes/*" &&
		git fetch c &&
		git branch --track local/a/master remotes/a/master &&
		test "$(git config branch.local/a/master.remote)" = "c" &&
		test "$(git config branch.local/a/master.merge)" = "refs/remotes/a/master" &&
		git rev-parse --verify a >expect &&
		git rev-parse --verify local/a/master >actual &&
		test_cmp expect actual
	)
'

test_done
