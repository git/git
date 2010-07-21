#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git branch --foo should not create bogus branch

This test runs git branch --help and checks that the argument is properly
handled.  Specifically, that a bogus branch is not created.
'
. ./test-lib.sh

test_expect_success \
    'prepare a trivial repository' \
    'echo Hello > A &&
     git update-index --add A &&
     git commit -m "Initial commit." &&
     echo World >> A &&
     git update-index --add A &&
     git commit -m "Second commit." &&
     HEAD=$(git rev-parse --verify HEAD)'

test_expect_success \
    'git branch --help should not have created a bogus branch' '
     git branch --help </dev/null >/dev/null 2>/dev/null;
     ! test -f .git/refs/heads/--help
'

test_expect_success \
    'git branch abc should create a branch' \
    'git branch abc && test -f .git/refs/heads/abc'

test_expect_success \
    'git branch a/b/c should create a branch' \
    'git branch a/b/c && test -f .git/refs/heads/a/b/c'

cat >expect <<EOF
0000000000000000000000000000000000000000 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success \
    'git branch -l d/e/f should create a branch and a log' \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
     git branch -l d/e/f &&
	 test -f .git/refs/heads/d/e/f &&
	 test -f .git/logs/refs/heads/d/e/f &&
	 test_cmp expect .git/logs/refs/heads/d/e/f'

test_expect_success \
    'git branch -d d/e/f should delete a branch and a log' \
	'git branch -d d/e/f &&
	 test ! -f .git/refs/heads/d/e/f &&
	 test ! -f .git/logs/refs/heads/d/e/f'

test_expect_success \
    'git branch j/k should work after branch j has been deleted' \
       'git branch j &&
        git branch -d j &&
        git branch j/k'

test_expect_success \
    'git branch l should work after branch l/m has been deleted' \
       'git branch l/m &&
        git branch -d l/m &&
        git branch l'

test_expect_success \
    'git branch -m m m/m should work' \
       'git branch -l m &&
        git branch -m m m/m &&
        test -f .git/logs/refs/heads/m/m'

test_expect_success \
    'git branch -m n/n n should work' \
       'git branch -l n/n &&
        git branch -m n/n n
        test -f .git/logs/refs/heads/n'

test_expect_success 'git branch -m o/o o should fail when o/p exists' '
	git branch o/o &&
        git branch o/p &&
	test_must_fail git branch -m o/o o
'

test_expect_success 'git branch -m q r/q should fail when r exists' '
	git branch q &&
	git branch r &&
	test_must_fail git branch -m q r/q
'

mv .git/config .git/config-saved

test_expect_success 'git branch -m q q2 without config should succeed' '
	git branch -m q q2 &&
	git branch -m q2 q
'

mv .git/config-saved .git/config

git config branch.s/s.dummy Hello

test_expect_success \
    'git branch -m s/s s should work when s/t is deleted' \
       'git branch -l s/s &&
        test -f .git/logs/refs/heads/s/s &&
        git branch -l s/t &&
        test -f .git/logs/refs/heads/s/t &&
        git branch -d s/t &&
        git branch -m s/s s &&
        test -f .git/logs/refs/heads/s'

test_expect_success 'config information was renamed, too' \
	"test $(git config branch.s.dummy) = Hello &&
	 test_must_fail git config branch.s/s/dummy"

test_expect_success 'renaming a symref is not allowed' \
'
	git symbolic-ref refs/heads/master2 refs/heads/master &&
	test_must_fail git branch -m master2 master3 &&
	git symbolic-ref refs/heads/master2 &&
	test -f .git/refs/heads/master &&
	! test -f .git/refs/heads/master3
'

test_expect_success SYMLINKS \
    'git branch -m u v should fail when the reflog for u is a symlink' '
     git branch -l u &&
     mv .git/logs/refs/heads/u real-u &&
     ln -s real-u .git/logs/refs/heads/u &&
     test_must_fail git branch -m u v
'

test_expect_success 'test tracking setup via --track' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git fetch local) &&
     git branch --track my1 local/master &&
     test $(git config branch.my1.remote) = local &&
     test $(git config branch.my1.merge) = refs/heads/master'

test_expect_success 'test tracking setup (non-wildcard, matching)' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/master:refs/remotes/local/master &&
     (git show-ref -q refs/remotes/local/master || git fetch local) &&
     git branch --track my4 local/master &&
     test $(git config branch.my4.remote) = local &&
     test $(git config branch.my4.merge) = refs/heads/master'

test_expect_success 'test tracking setup (non-wildcard, not matching)' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/s:refs/remotes/local/s &&
     (git show-ref -q refs/remotes/local/master || git fetch local) &&
     git branch --track my5 local/master &&
     ! test "$(git config branch.my5.remote)" = local &&
     ! test "$(git config branch.my5.merge)" = refs/heads/master'

test_expect_success 'test tracking setup via config' \
    'git config branch.autosetupmerge true &&
     git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git fetch local) &&
     git branch my3 local/master &&
     test $(git config branch.my3.remote) = local &&
     test $(git config branch.my3.merge) = refs/heads/master'

test_expect_success 'test overriding tracking setup via --no-track' \
    'git config branch.autosetupmerge true &&
     git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git fetch local) &&
     git branch --no-track my2 local/master &&
     git config branch.autosetupmerge false &&
     ! test "$(git config branch.my2.remote)" = local &&
     ! test "$(git config branch.my2.merge)" = refs/heads/master'

test_expect_success 'no tracking without .fetch entries' \
    'git config branch.autosetupmerge true &&
     git branch my6 s &&
     git config branch.automsetupmerge false &&
     test -z "$(git config branch.my6.remote)" &&
     test -z "$(git config branch.my6.merge)"'

test_expect_success 'test tracking setup via --track but deeper' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/o/o || git fetch local) &&
     git branch --track my7 local/o/o &&
     test "$(git config branch.my7.remote)" = local &&
     test "$(git config branch.my7.merge)" = refs/heads/o/o'

test_expect_success 'test deleting branch deletes branch config' \
    'git branch -d my7 &&
     test -z "$(git config branch.my7.remote)" &&
     test -z "$(git config branch.my7.merge)"'

test_expect_success 'test deleting branch without config' \
    'git branch my7 s &&
     sha1=$(git rev-parse my7 | cut -c 1-7) &&
     test "$(git branch -d my7 2>&1)" = "Deleted branch my7 (was $sha1)."'

test_expect_success 'test --track without .fetch entries' \
    'git branch --track my8 &&
     test "$(git config branch.my8.remote)" &&
     test "$(git config branch.my8.merge)"'

test_expect_success \
    'branch from non-branch HEAD w/autosetupmerge=always' \
    'git config branch.autosetupmerge always &&
     git branch my9 HEAD^ &&
     git config branch.autosetupmerge false'

test_expect_success \
    'branch from non-branch HEAD w/--track causes failure' \
    'test_must_fail git branch --track my10 HEAD^'

# Keep this test last, as it changes the current branch
cat >expect <<EOF
0000000000000000000000000000000000000000 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success \
    'git checkout -b g/h/i -l should create a branch and a log' \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
     git checkout -b g/h/i -l master &&
	 test -f .git/refs/heads/g/h/i &&
	 test -f .git/logs/refs/heads/g/h/i &&
	 test_cmp expect .git/logs/refs/heads/g/h/i'

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
	git branch all1 master &&
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

test_expect_success 'detect misconfigured autosetuprebase (bad value)' '
	git config branch.autosetuprebase garbage &&
	test_must_fail git branch
'

test_expect_success 'detect misconfigured autosetuprebase (no value)' '
	git config --unset branch.autosetuprebase &&
	echo "[branch] autosetuprebase" >> .git/config &&
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

test_done
