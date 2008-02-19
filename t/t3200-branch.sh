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
     git-commit -m "Initial commit." &&
     echo World >> A &&
     git update-index --add A &&
     git-commit -m "Second commit." &&
     HEAD=$(git rev-parse --verify HEAD)'

test_expect_failure \
    'git branch --help should not have created a bogus branch' \
    'git branch --help </dev/null >/dev/null 2>/dev/null || :
     test -f .git/refs/heads/--help'

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
	 diff expect .git/logs/refs/heads/d/e/f'

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

test_expect_failure \
    'git branch -m o/o o should fail when o/p exists' \
       'git branch o/o &&
        git branch o/p &&
        git branch -m o/o o'

test_expect_failure \
    'git branch -m q r/q should fail when r exists' \
       'git branch q &&
         git branch r &&
         git branch -m q r/q'

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
	 ! git config branch.s/s/dummy"

test_expect_failure \
    'git branch -m u v should fail when the reflog for u is a symlink' \
    'git branch -l u &&
     mv .git/logs/refs/heads/u real-u &&
     ln -s real-u .git/logs/refs/heads/u &&
     git branch -m u v'

test_expect_success 'test tracking setup via --track' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git-fetch local) &&
     git branch --track my1 local/master &&
     test $(git config branch.my1.remote) = local &&
     test $(git config branch.my1.merge) = refs/heads/master'

test_expect_success 'test tracking setup (non-wildcard, matching)' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/master:refs/remotes/local/master &&
     (git show-ref -q refs/remotes/local/master || git-fetch local) &&
     git branch --track my4 local/master &&
     test $(git config branch.my4.remote) = local &&
     test $(git config branch.my4.merge) = refs/heads/master'

test_expect_success 'test tracking setup (non-wildcard, not matching)' \
    'git config remote.local.url . &&
     git config remote.local.fetch refs/heads/s:refs/remotes/local/s &&
     (git show-ref -q refs/remotes/local/master || git-fetch local) &&
     git branch --track my5 local/master &&
     ! test "$(git config branch.my5.remote)" = local &&
     ! test "$(git config branch.my5.merge)" = refs/heads/master'

test_expect_success 'test tracking setup via config' \
    'git config branch.autosetupmerge true &&
     git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git-fetch local) &&
     git branch my3 local/master &&
     test $(git config branch.my3.remote) = local &&
     test $(git config branch.my3.merge) = refs/heads/master'

test_expect_success 'avoid ambiguous track' '
	git config branch.autosetupmerge true &&
	git config remote.ambi1.url = lalala &&
	git config remote.ambi1.fetch = refs/heads/lalala:refs/heads/master &&
	git config remote.ambi2.url = lilili &&
	git config remote.ambi2.fetch = refs/heads/lilili:refs/heads/master &&
	git branch all1 master &&
	test -z "$(git config branch.all1.merge)"
'

test_expect_success 'test overriding tracking setup via --no-track' \
    'git config branch.autosetupmerge true &&
     git config remote.local.url . &&
     git config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
     (git show-ref -q refs/remotes/local/master || git-fetch local) &&
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
     (git show-ref -q refs/remotes/local/o/o || git-fetch local) &&
     git branch --track my7 local/o/o &&
     test "$(git config branch.my7.remote)" = local &&
     test "$(git config branch.my7.merge)" = refs/heads/o/o'

test_expect_success 'test deleting branch deletes branch config' \
    'git branch -d my7 &&
     test -z "$(git config branch.my7.remote)" &&
     test -z "$(git config branch.my7.merge)"'

test_expect_success 'test deleting branch without config' \
    'git branch my7 s &&
     test "$(git branch -d my7 2>&1)" = "Deleted branch my7."'

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
    '!(git branch --track my10 HEAD^)'

# Keep this test last, as it changes the current branch
cat >expect <<EOF
0000000000000000000000000000000000000000 $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success \
    'git checkout -b g/h/i -l should create a branch and a log' \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
     git-checkout -b g/h/i -l master &&
	 test -f .git/refs/heads/g/h/i &&
	 test -f .git/logs/refs/heads/g/h/i &&
	 diff expect .git/logs/refs/heads/g/h/i'

test_done
