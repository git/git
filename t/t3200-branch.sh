#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git branch assorted tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

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
$ZERO_OID $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
EOF
test_expect_success 'git branch --create-reflog d/e/f should create a branch and a log' '
	GIT_COMMITTER_DATE="2005-05-26 23:30" \
	git -c core.logallrefupdates=false branch --create-reflog d/e/f &&
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
	git branch --create-reflog m &&
	git symbolic-ref refs/heads/broken_symref refs/heads/i_am_broken &&
	git branch -m m broken_symref &&
	git reflog exists refs/heads/broken_symref &&
	test_must_fail git reflog exists refs/heads/i_am_broken
'

test_expect_success 'git branch -m m m/m should work' '
	git branch --create-reflog m &&
	git branch -m m m/m &&
	git reflog exists refs/heads/m/m
'

test_expect_success 'git branch -m n/n n should work' '
	git branch --create-reflog n/n &&
	git branch -m n/n n &&
	git reflog exists refs/heads/n
'

# The topmost entry in reflog for branch bbb is about branch creation.
# Hence, we compare bbb@{1} (instead of bbb@{0}) with aaa@{0}.

test_expect_success 'git branch -m bbb should rename checked out branch' '
	test_when_finished git branch -D bbb &&
	test_when_finished git checkout master &&
	git checkout -b aaa &&
	git commit --allow-empty -m "a new commit" &&
	git rev-parse aaa@{0} >expect &&
	git branch -m bbb &&
	git rev-parse bbb@{1} >actual &&
	test_cmp expect actual &&
	git symbolic-ref HEAD >actual &&
	echo refs/heads/bbb >expect &&
	test_cmp expect actual
'

test_expect_success 'renaming checked out branch works with d/f conflict' '
	test_when_finished "git branch -D foo/bar || git branch -D foo" &&
	test_when_finished git checkout master &&
	git checkout -b foo &&
	git branch -m foo/bar &&
	git symbolic-ref HEAD >actual &&
	echo refs/heads/foo/bar >expect &&
	test_cmp expect actual
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

test_expect_success 'git branch -M baz bam should add entries to .git/logs/HEAD' '
	msg="Branch: renamed refs/heads/baz to refs/heads/bam" &&
	grep " 0\{40\}.*$msg$" .git/logs/HEAD &&
	grep "^0\{40\}.*$msg$" .git/logs/HEAD
'

test_expect_success 'git branch -M should leave orphaned HEAD alone' '
	git init orphan &&
	(
		cd orphan &&
		test_commit initial &&
		git checkout --orphan lonely &&
		grep lonely .git/HEAD &&
		test_path_is_missing .git/refs/head/lonely &&
		git branch -M master mistress &&
		grep lonely .git/HEAD
	)
'

test_expect_success 'resulting reflog can be shown by log -g' '
	oid=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	HEAD@{0} $oid $msg
	HEAD@{2} $oid checkout: moving from foo to baz
	EOF
	git log -g --format="%gd %H %gs" -2 HEAD >actual &&
	test_cmp expect actual
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
	git rev-parse --verify refs/heads/t &&
	git branch -v -d t &&
	test_must_fail git rev-parse --verify refs/heads/t
'

test_expect_success 'git branch -v -m t s should work' '
	git branch t &&
	git rev-parse --verify refs/heads/t &&
	git branch -v -m t s &&
	test_must_fail git rev-parse --verify refs/heads/t &&
	git rev-parse --verify refs/heads/s &&
	git branch -d s
'

test_expect_success 'git branch -m -d t s should fail' '
	git branch t &&
	git rev-parse refs/heads/t &&
	test_must_fail git branch -m -d t s &&
	git branch -d t &&
	test_must_fail git rev-parse refs/heads/t
'

test_expect_success 'git branch --list -d t should fail' '
	git branch t &&
	git rev-parse refs/heads/t &&
	test_must_fail git branch --list -d t &&
	git branch -d t &&
	test_must_fail git rev-parse refs/heads/t
'

test_expect_success 'git branch --list -v with --abbrev' '
	test_when_finished "git branch -D t" &&
	git branch t &&
	git branch -v --list t >actual.default &&
	git branch -v --list --abbrev t >actual.abbrev &&
	test_cmp actual.default actual.abbrev &&

	git branch -v --list --no-abbrev t >actual.noabbrev &&
	git branch -v --list --abbrev=0 t >actual.0abbrev &&
	test_cmp actual.noabbrev actual.0abbrev &&

	git branch -v --list --abbrev=36 t >actual.36abbrev &&
	# how many hexdigits are used?
	read name objdefault rest <actual.abbrev &&
	read name obj36 rest <actual.36abbrev &&
	objfull=$(git rev-parse --verify t) &&

	# are we really getting abbreviations?
	test "$obj36" != "$objdefault" &&
	expr "$obj36" : "$objdefault" >/dev/null &&
	test "$objfull" != "$obj36" &&
	expr "$objfull" : "$obj36" >/dev/null

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
	git branch --create-reflog s/s &&
	git reflog exists refs/heads/s/s &&
	git branch --create-reflog s/t &&
	git reflog exists refs/heads/s/t &&
	git branch -d s/t &&
	git branch -m s/s s &&
	git reflog exists refs/heads/s
'

test_expect_success 'config information was renamed, too' '
	test $(git config branch.s.dummy) = Hello &&
	test_must_fail git config branch.s/s.dummy
'

test_expect_success 'git branch -m correctly renames multiple config sections' '
	test_when_finished "git checkout master" &&
	git checkout -b source master &&

	# Assert that a config file with multiple config sections has
	# those sections preserved...
	cat >expect <<-\EOF &&
	branch.dest.key1=value1
	some.gar.b=age
	branch.dest.key2=value2
	EOF
	cat >config.branch <<\EOF &&
;; Note the lack of -\EOF above & mixed indenting here. This is
;; intentional, we are also testing that the formatting of copied
;; sections is preserved.

;; Comment for source. Tabs
[branch "source"]
	;; Comment for the source value
	key1 = value1
;; Comment for some.gar. Spaces
[some "gar"]
    ;; Comment for the some.gar value
    b = age
;; Comment for source, again. Mixed tabs/spaces.
[branch "source"]
    ;; Comment for the source value, again
	key2 = value2
EOF
	cat config.branch >>.git/config &&
	git branch -m source dest &&
	git config -f .git/config -l | grep -F -e source -e dest -e some.gar >actual &&
	test_cmp expect actual &&

	# ...and that the comments for those sections are also
	# preserved.
	cat config.branch | sed "s/\"source\"/\"dest\"/" >expect &&
	sed -n -e "/Note the lack/,\$p" .git/config >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch -c dumps usage' '
	test_expect_code 128 git branch -c 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'git branch --copy dumps usage' '
	test_expect_code 128 git branch --copy 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'git branch -c d e should work' '
	git branch --create-reflog d &&
	git reflog exists refs/heads/d &&
	git config branch.d.dummy Hello &&
	git branch -c d e &&
	git reflog exists refs/heads/d &&
	git reflog exists refs/heads/e &&
	echo Hello >expect &&
	git config branch.e.dummy >actual &&
	test_cmp expect actual &&
	echo Hello >expect &&
	git config branch.d.dummy >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch --copy is a synonym for -c' '
	git branch --create-reflog copy &&
	git reflog exists refs/heads/copy &&
	git config branch.copy.dummy Hello &&
	git branch --copy copy copy-to &&
	git reflog exists refs/heads/copy &&
	git reflog exists refs/heads/copy-to &&
	echo Hello >expect &&
	git config branch.copy.dummy >actual &&
	test_cmp expect actual &&
	echo Hello >expect &&
	git config branch.copy-to.dummy >actual &&
	test_cmp expect actual
'

test_expect_success 'git branch -c ee ef should copy ee to create branch ef' '
	git checkout -b ee &&
	git reflog exists refs/heads/ee &&
	git config branch.ee.dummy Hello &&
	git branch -c ee ef &&
	git reflog exists refs/heads/ee &&
	git reflog exists refs/heads/ef &&
	test $(git config branch.ee.dummy) = Hello &&
	test $(git config branch.ef.dummy) = Hello &&
	test $(git rev-parse --abbrev-ref HEAD) = ee
'

test_expect_success 'git branch -c f/f g/g should work' '
	git branch --create-reflog f/f &&
	git reflog exists refs/heads/f/f &&
	git config branch.f/f.dummy Hello &&
	git branch -c f/f g/g &&
	git reflog exists refs/heads/f/f &&
	git reflog exists refs/heads/g/g &&
	test $(git config branch.f/f.dummy) = Hello &&
	test $(git config branch.g/g.dummy) = Hello
'

test_expect_success 'git branch -c m2 m2 should work' '
	git branch --create-reflog m2 &&
	git reflog exists refs/heads/m2 &&
	git config branch.m2.dummy Hello &&
	git branch -c m2 m2 &&
	git reflog exists refs/heads/m2 &&
	test $(git config branch.m2.dummy) = Hello
'

test_expect_success 'git branch -c zz zz/zz should fail' '
	git branch --create-reflog zz &&
	git reflog exists refs/heads/zz &&
	test_must_fail git branch -c zz zz/zz
'

test_expect_success 'git branch -c b/b b should fail' '
	git branch --create-reflog b/b &&
	test_must_fail git branch -c b/b b
'

test_expect_success 'git branch -C o/q o/p should work when o/p exists' '
	git branch --create-reflog o/q &&
	git reflog exists refs/heads/o/q &&
	git reflog exists refs/heads/o/p &&
	git branch -C o/q o/p
'

test_expect_success 'git branch -c -f o/q o/p should work when o/p exists' '
	git reflog exists refs/heads/o/q &&
	git reflog exists refs/heads/o/p &&
	git branch -c -f o/q o/p
'

test_expect_success 'git branch -c qq rr/qq should fail when rr exists' '
	git branch qq &&
	git branch rr &&
	test_must_fail git branch -c qq rr/qq
'

test_expect_success 'git branch -C b1 b2 should fail when b2 is checked out' '
	git branch b1 &&
	git checkout -b b2 &&
	test_must_fail git branch -C b1 b2
'

test_expect_success 'git branch -C c1 c2 should succeed when c1 is checked out' '
	git checkout -b c1 &&
	git branch c2 &&
	git branch -C c1 c2 &&
	test $(git rev-parse --abbrev-ref HEAD) = c1
'

test_expect_success 'git branch -C c1 c2 should never touch HEAD' '
	msg="Branch: copied refs/heads/c1 to refs/heads/c2" &&
	! grep "$msg$" .git/logs/HEAD
'

test_expect_success 'git branch -C master should work when master is checked out' '
	git checkout master &&
	git branch -C master
'

test_expect_success 'git branch -C master master should work when master is checked out' '
	git checkout master &&
	git branch -C master master
'

test_expect_success 'git branch -C master5 master5 should work when master is checked out' '
	git checkout master &&
	git branch master5 &&
	git branch -C master5 master5
'

test_expect_success 'git branch -C ab cd should overwrite existing config for cd' '
	git branch --create-reflog cd &&
	git reflog exists refs/heads/cd &&
	git config branch.cd.dummy CD &&
	git branch --create-reflog ab &&
	git reflog exists refs/heads/ab &&
	git config branch.ab.dummy AB &&
	git branch -C ab cd &&
	git reflog exists refs/heads/ab &&
	git reflog exists refs/heads/cd &&
	test $(git config branch.ab.dummy) = AB &&
	test $(git config branch.cd.dummy) = AB
'

test_expect_success 'git branch -c correctly copies multiple config sections' '
	FOO=1 &&
	export FOO &&
	test_when_finished "git checkout master" &&
	git checkout -b source2 master &&

	# Assert that a config file with multiple config sections has
	# those sections preserved...
	cat >expect <<-\EOF &&
	branch.source2.key1=value1
	branch.dest2.key1=value1
	more.gar.b=age
	branch.source2.key2=value2
	branch.dest2.key2=value2
	EOF
	cat >config.branch <<\EOF &&
;; Note the lack of -\EOF above & mixed indenting here. This is
;; intentional, we are also testing that the formatting of copied
;; sections is preserved.

;; Comment for source2. Tabs
[branch "source2"]
	;; Comment for the source2 value
	key1 = value1
;; Comment for more.gar. Spaces
[more "gar"]
    ;; Comment for the more.gar value
    b = age
;; Comment for source2, again. Mixed tabs/spaces.
[branch "source2"]
    ;; Comment for the source2 value, again
	key2 = value2
EOF
	cat config.branch >>.git/config &&
	git branch -c source2 dest2 &&
	git config -f .git/config -l | grep -F -e source2 -e dest2 -e more.gar >actual &&
	test_cmp expect actual &&

	# ...and that the comments and formatting for those sections
	# is also preserved.
	cat >expect <<\EOF &&
;; Comment for source2. Tabs
[branch "source2"]
	;; Comment for the source2 value
	key1 = value1
;; Comment for more.gar. Spaces
[branch "dest2"]
	;; Comment for the source2 value
	key1 = value1
;; Comment for more.gar. Spaces
[more "gar"]
    ;; Comment for the more.gar value
    b = age
;; Comment for source2, again. Mixed tabs/spaces.
[branch "source2"]
    ;; Comment for the source2 value, again
	key2 = value2
[branch "dest2"]
    ;; Comment for the source2 value, again
	key2 = value2
EOF
	sed -n -e "/Comment for source2/,\$p" .git/config >actual &&
	test_cmp expect actual
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
	git branch --create-reflog u &&
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
	test_when_finished "git branch --unset-upstream my13" &&
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

test_expect_success 'disabled option --set-upstream fails' '
    test_must_fail git branch --set-upstream origin/master
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
$ZERO_OID $HEAD $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	branch: Created from master
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
	test_cmp expect EDITOR_OUTPUT
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

test_expect_success '--merged is incompatible with --no-merged' '
	test_must_fail git branch --merged HEAD --no-merged HEAD
'

test_expect_success '--list during rebase' '
	test_when_finished "reset_rebase" &&
	git checkout master &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	set_fake_editor &&
	git rebase -i HEAD~2 &&
	git branch --list >actual &&
	test_i18ngrep "rebasing master" actual
'

test_expect_success '--list during rebase from detached HEAD' '
	test_when_finished "reset_rebase && git checkout master" &&
	git checkout master^0 &&
	oid=$(git rev-parse --short HEAD) &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	set_fake_editor &&
	git rebase -i HEAD~2 &&
	git branch --list >actual &&
	test_i18ngrep "rebasing detached HEAD $oid" actual
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

test_expect_success 'configured committerdate sort' '
	git init sort &&
	(
		cd sort &&
		git config branch.sort committerdate &&
		test_commit initial &&
		git checkout -b a &&
		test_commit a &&
		git checkout -b c &&
		test_commit c &&
		git checkout -b b &&
		test_commit b &&
		git branch >actual &&
		cat >expect <<-\EOF &&
		  master
		  a
		  c
		* b
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'option override configured sort' '
	(
		cd sort &&
		git config branch.sort committerdate &&
		git branch --sort=refname >actual &&
		cat >expect <<-\EOF &&
		  a
		* b
		  c
		  master
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'invalid sort parameter in configuration' '
	(
		cd sort &&
		git config branch.sort "v:notvalid" &&
		test_must_fail git branch
	)
'

test_done
