#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='but branch assorted tests'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'prepare a trivial repository' '
	echo Hello >A &&
	but update-index --add A &&
	but cummit -m "Initial cummit." &&
	but branch -M main &&
	echo World >>A &&
	but update-index --add A &&
	but cummit -m "Second cummit." &&
	HEAD=$(but rev-parse --verify HEAD)
'

test_expect_success 'but branch --help should not have created a bogus branch' '
	test_might_fail but branch --man --help </dev/null >/dev/null 2>&1 &&
	test_path_is_missing .but/refs/heads/--help
'

test_expect_success 'branch -h in broken repository' '
	mkdir broken &&
	(
		cd broken &&
		but init -b main &&
		>.but/refs/heads/main &&
		test_expect_code 129 but branch -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'but branch abc should create a branch' '
	but branch abc && test_path_is_file .but/refs/heads/abc
'

test_expect_success 'but branch abc should fail when abc exists' '
	test_must_fail but branch abc
'

test_expect_success 'but branch --force abc should fail when abc is checked out' '
	test_when_finished but switch main &&
	but switch abc &&
	test_must_fail but branch --force abc HEAD~1
'

test_expect_success 'but branch --force abc should succeed when abc exists' '
	but rev-parse HEAD~1 >expect &&
	but branch --force abc HEAD~1 &&
	but rev-parse abc >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch a/b/c should create a branch' '
	but branch a/b/c && test_path_is_file .but/refs/heads/a/b/c
'

test_expect_success 'but branch mb main... should create a branch' '
	but branch mb main... && test_path_is_file .but/refs/heads/mb
'

test_expect_success 'but branch HEAD should fail' '
	test_must_fail but branch HEAD
'

cat >expect <<EOF
$ZERO_OID $HEAD $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> 1117150200 +0000	branch: Created from main
EOF
test_expect_success 'but branch --create-reflog d/e/f should create a branch and a log' '
	BUT_CUMMITTER_DATE="2005-05-26 23:30" \
	but -c core.logallrefupdates=false branch --create-reflog d/e/f &&
	test_path_is_file .but/refs/heads/d/e/f &&
	test_path_is_file .but/logs/refs/heads/d/e/f &&
	test_cmp expect .but/logs/refs/heads/d/e/f
'

test_expect_success 'but branch -d d/e/f should delete a branch and a log' '
	but branch -d d/e/f &&
	test_path_is_missing .but/refs/heads/d/e/f &&
	test_must_fail but reflog exists refs/heads/d/e/f
'

test_expect_success 'but branch j/k should work after branch j has been deleted' '
	but branch j &&
	but branch -d j &&
	but branch j/k
'

test_expect_success 'but branch l should work after branch l/m has been deleted' '
	but branch l/m &&
	but branch -d l/m &&
	but branch l
'

test_expect_success 'but branch -m dumps usage' '
	test_expect_code 128 but branch -m 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'but branch -m m broken_symref should work' '
	test_when_finished "but branch -D broken_symref" &&
	but branch --create-reflog m &&
	but symbolic-ref refs/heads/broken_symref refs/heads/i_am_broken &&
	but branch -m m broken_symref &&
	but reflog exists refs/heads/broken_symref &&
	test_must_fail but reflog exists refs/heads/i_am_broken
'

test_expect_success 'but branch -m m m/m should work' '
	but branch --create-reflog m &&
	but branch -m m m/m &&
	but reflog exists refs/heads/m/m
'

test_expect_success 'but branch -m n/n n should work' '
	but branch --create-reflog n/n &&
	but branch -m n/n n &&
	but reflog exists refs/heads/n
'

# The topmost entry in reflog for branch bbb is about branch creation.
# Hence, we compare bbb@{1} (instead of bbb@{0}) with aaa@{0}.

test_expect_success 'but branch -m bbb should rename checked out branch' '
	test_when_finished but branch -D bbb &&
	test_when_finished but checkout main &&
	but checkout -b aaa &&
	but cummit --allow-empty -m "a new cummit" &&
	but rev-parse aaa@{0} >expect &&
	but branch -m bbb &&
	but rev-parse bbb@{1} >actual &&
	test_cmp expect actual &&
	but symbolic-ref HEAD >actual &&
	echo refs/heads/bbb >expect &&
	test_cmp expect actual
'

test_expect_success 'renaming checked out branch works with d/f conflict' '
	test_when_finished "but branch -D foo/bar || but branch -D foo" &&
	test_when_finished but checkout main &&
	but checkout -b foo &&
	but branch -m foo/bar &&
	but symbolic-ref HEAD >actual &&
	echo refs/heads/foo/bar >expect &&
	test_cmp expect actual
'

test_expect_success 'but branch -m o/o o should fail when o/p exists' '
	but branch o/o &&
	but branch o/p &&
	test_must_fail but branch -m o/o o
'

test_expect_success 'but branch -m o/q o/p should fail when o/p exists' '
	but branch o/q &&
	test_must_fail but branch -m o/q o/p
'

test_expect_success 'but branch -M o/q o/p should work when o/p exists' '
	but branch -M o/q o/p
'

test_expect_success 'but branch -m -f o/q o/p should work when o/p exists' '
	but branch o/q &&
	but branch -m -f o/q o/p
'

test_expect_success 'but branch -m q r/q should fail when r exists' '
	but branch q &&
	but branch r &&
	test_must_fail but branch -m q r/q
'

test_expect_success 'but branch -M foo bar should fail when bar is checked out' '
	but branch bar &&
	but checkout -b foo &&
	test_must_fail but branch -M bar foo
'

test_expect_success 'but branch -M foo bar should fail when bar is checked out in worktree' '
	but branch -f bar &&
	test_when_finished "but worktree remove wt && but branch -D wt" &&
	but worktree add wt &&
	test_must_fail but branch -M bar wt
'

test_expect_success 'but branch -M baz bam should succeed when baz is checked out' '
	but checkout -b baz &&
	but branch bam &&
	but branch -M baz bam &&
	test $(but rev-parse --abbrev-ref HEAD) = bam
'

test_expect_success 'but branch -M baz bam should add entries to .but/logs/HEAD' '
	msg="Branch: renamed refs/heads/baz to refs/heads/bam" &&
	grep " 0\{40\}.*$msg$" .but/logs/HEAD &&
	grep "^0\{40\}.*$msg$" .but/logs/HEAD
'

test_expect_success 'but branch -M should leave orphaned HEAD alone' '
	but init -b main orphan &&
	(
		cd orphan &&
		test_cummit initial &&
		but checkout --orphan lonely &&
		grep lonely .but/HEAD &&
		test_path_is_missing .but/refs/head/lonely &&
		but branch -M main mistress &&
		grep lonely .but/HEAD
	)
'

test_expect_success 'resulting reflog can be shown by log -g' '
	oid=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
	HEAD@{0} $oid $msg
	HEAD@{2} $oid checkout: moving from foo to baz
	EOF
	but log -g --format="%gd %H %gs" -2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch -M baz bam should succeed when baz is checked out as linked working tree' '
	but checkout main &&
	but worktree add -b baz bazdir &&
	but worktree add -f bazdir2 baz &&
	but branch -M baz bam &&
	test $(but -C bazdir rev-parse --abbrev-ref HEAD) = bam &&
	test $(but -C bazdir2 rev-parse --abbrev-ref HEAD) = bam &&
	rm -r bazdir bazdir2 &&
	but worktree prune
'

test_expect_success 'but branch -M baz bam should succeed within a worktree in which baz is checked out' '
	but checkout -b baz &&
	but worktree add -f bazdir baz &&
	(
		cd bazdir &&
		but branch -M baz bam &&
		test $(but rev-parse --abbrev-ref HEAD) = bam
	) &&
	test $(but rev-parse --abbrev-ref HEAD) = bam &&
	rm -r bazdir &&
	but worktree prune
'

test_expect_success 'but branch -M main should work when main is checked out' '
	but checkout main &&
	but branch -M main
'

test_expect_success 'but branch -M main main should work when main is checked out' '
	but checkout main &&
	but branch -M main main
'

test_expect_success 'but branch -M topic topic should work when main is checked out' '
	but checkout main &&
	but branch topic &&
	but branch -M topic topic
'

test_expect_success 'but branch -v -d t should work' '
	but branch t &&
	but rev-parse --verify refs/heads/t &&
	but branch -v -d t &&
	test_must_fail but rev-parse --verify refs/heads/t
'

test_expect_success 'but branch -v -m t s should work' '
	but branch t &&
	but rev-parse --verify refs/heads/t &&
	but branch -v -m t s &&
	test_must_fail but rev-parse --verify refs/heads/t &&
	but rev-parse --verify refs/heads/s &&
	but branch -d s
'

test_expect_success 'but branch -m -d t s should fail' '
	but branch t &&
	but rev-parse refs/heads/t &&
	test_must_fail but branch -m -d t s &&
	but branch -d t &&
	test_must_fail but rev-parse refs/heads/t
'

test_expect_success 'but branch --list -d t should fail' '
	but branch t &&
	but rev-parse refs/heads/t &&
	test_must_fail but branch --list -d t &&
	but branch -d t &&
	test_must_fail but rev-parse refs/heads/t
'

test_expect_success 'deleting checked-out branch from repo that is a submodule' '
	test_when_finished "rm -rf repo1 repo2" &&

	but init repo1 &&
	but init repo1/sub &&
	test_cummit -C repo1/sub x &&
	but -C repo1 submodule add ./sub &&
	but -C repo1 cummit -m "adding sub" &&

	but clone --recurse-submodules repo1 repo2 &&
	but -C repo2/sub checkout -b work &&
	test_must_fail but -C repo2/sub branch -D work
'

test_expect_success 'bare main worktree has HEAD at branch deleted by secondary worktree' '
	test_when_finished "rm -rf nonbare base secondary" &&

	but init -b main nonbare &&
	test_cummit -C nonbare x &&
	but clone --bare nonbare bare &&
	but -C bare worktree add --detach ../secondary main &&
	but -C secondary branch -D main
'

test_expect_success 'but branch --list -v with --abbrev' '
	test_when_finished "but branch -D t" &&
	but branch t &&
	but branch -v --list t >actual.default &&
	but branch -v --list --abbrev t >actual.abbrev &&
	test_cmp actual.default actual.abbrev &&

	but branch -v --list --no-abbrev t >actual.noabbrev &&
	but branch -v --list --abbrev=0 t >actual.0abbrev &&
	but -c core.abbrev=no branch -v --list t >actual.noabbrev-conf &&
	test_cmp actual.noabbrev actual.0abbrev &&
	test_cmp actual.noabbrev actual.noabbrev-conf &&

	but branch -v --list --abbrev=36 t >actual.36abbrev &&
	# how many hexdibuts are used?
	read name objdefault rest <actual.abbrev &&
	read name obj36 rest <actual.36abbrev &&
	objfull=$(but rev-parse --verify t) &&

	# are we really getting abbreviations?
	test "$obj36" != "$objdefault" &&
	expr "$obj36" : "$objdefault" >/dev/null &&
	test "$objfull" != "$obj36" &&
	expr "$objfull" : "$obj36" >/dev/null

'

test_expect_success 'but branch --column' '
	COLUMNS=81 but branch --column=column >actual &&
	cat >expect <<\EOF &&
  a/b/c   bam     foo     l     * main    n       o/p     r
  abc     bar     j/k     m/m     mb      o/o     q       topic
EOF
	test_cmp expect actual
'

test_expect_success 'but branch --column with an extremely long branch name' '
	long=this/is/a/part/of/long/branch/name &&
	long=z$long/$long/$long/$long &&
	test_when_finished "but branch -d $long" &&
	but branch $long &&
	COLUMNS=80 but branch --column=column >actual &&
	cat >expect <<EOF &&
  a/b/c
  abc
  bam
  bar
  foo
  j/k
  l
  m/m
* main
  mb
  n
  o/o
  o/p
  q
  r
  topic
  $long
EOF
	test_cmp expect actual
'

test_expect_success 'but branch with column.*' '
	but config column.ui column &&
	but config column.branch "dense" &&
	COLUMNS=80 but branch >actual &&
	but config --unset column.branch &&
	but config --unset column.ui &&
	cat >expect <<\EOF &&
  a/b/c   bam   foo   l   * main   n     o/p   r
  abc     bar   j/k   m/m   mb     o/o   q     topic
EOF
	test_cmp expect actual
'

test_expect_success 'but branch --column -v should fail' '
	test_must_fail but branch --column -v
'

test_expect_success 'but branch -v with column.ui ignored' '
	but config column.ui column &&
	COLUMNS=80 but branch -v | cut -c -8 | sed "s/ *$//" >actual &&
	but config --unset column.ui &&
	cat >expect <<\EOF &&
  a/b/c
  abc
  bam
  bar
  foo
  j/k
  l
  m/m
* main
  mb
  n
  o/o
  o/p
  q
  r
  topic
EOF
	test_cmp expect actual
'

mv .but/config .but/config-saved

test_expect_success SHA1 'but branch -m q q2 without config should succeed' '
	but branch -m q q2 &&
	but branch -m q2 q
'

mv .but/config-saved .but/config

but config branch.s/s.dummy Hello

test_expect_success 'but branch -m s/s s should work when s/t is deleted' '
	but branch --create-reflog s/s &&
	but reflog exists refs/heads/s/s &&
	but branch --create-reflog s/t &&
	but reflog exists refs/heads/s/t &&
	but branch -d s/t &&
	but branch -m s/s s &&
	but reflog exists refs/heads/s
'

test_expect_success 'config information was renamed, too' '
	test $(but config branch.s.dummy) = Hello &&
	test_must_fail but config branch.s/s.dummy
'

test_expect_success 'but branch -m correctly renames multiple config sections' '
	test_when_finished "but checkout main" &&
	but checkout -b source main &&

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
	cat config.branch >>.but/config &&
	but branch -m source dest &&
	but config -f .but/config -l | grep -F -e source -e dest -e some.gar >actual &&
	test_cmp expect actual &&

	# ...and that the comments for those sections are also
	# preserved.
	cat config.branch | sed "s/\"source\"/\"dest\"/" >expect &&
	sed -n -e "/Note the lack/,\$p" .but/config >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch -c dumps usage' '
	test_expect_code 128 but branch -c 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'but branch --copy dumps usage' '
	test_expect_code 128 but branch --copy 2>err &&
	test_i18ngrep "branch name required" err
'

test_expect_success 'but branch -c d e should work' '
	but branch --create-reflog d &&
	but reflog exists refs/heads/d &&
	but config branch.d.dummy Hello &&
	but branch -c d e &&
	but reflog exists refs/heads/d &&
	but reflog exists refs/heads/e &&
	echo Hello >expect &&
	but config branch.e.dummy >actual &&
	test_cmp expect actual &&
	echo Hello >expect &&
	but config branch.d.dummy >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch --copy is a synonym for -c' '
	but branch --create-reflog copy &&
	but reflog exists refs/heads/copy &&
	but config branch.copy.dummy Hello &&
	but branch --copy copy copy-to &&
	but reflog exists refs/heads/copy &&
	but reflog exists refs/heads/copy-to &&
	echo Hello >expect &&
	but config branch.copy.dummy >actual &&
	test_cmp expect actual &&
	echo Hello >expect &&
	but config branch.copy-to.dummy >actual &&
	test_cmp expect actual
'

test_expect_success 'but branch -c ee ef should copy ee to create branch ef' '
	but checkout -b ee &&
	but reflog exists refs/heads/ee &&
	but config branch.ee.dummy Hello &&
	but branch -c ee ef &&
	but reflog exists refs/heads/ee &&
	but reflog exists refs/heads/ef &&
	test $(but config branch.ee.dummy) = Hello &&
	test $(but config branch.ef.dummy) = Hello &&
	test $(but rev-parse --abbrev-ref HEAD) = ee
'

test_expect_success 'but branch -c f/f g/g should work' '
	but branch --create-reflog f/f &&
	but reflog exists refs/heads/f/f &&
	but config branch.f/f.dummy Hello &&
	but branch -c f/f g/g &&
	but reflog exists refs/heads/f/f &&
	but reflog exists refs/heads/g/g &&
	test $(but config branch.f/f.dummy) = Hello &&
	test $(but config branch.g/g.dummy) = Hello
'

test_expect_success 'but branch -c m2 m2 should work' '
	but branch --create-reflog m2 &&
	but reflog exists refs/heads/m2 &&
	but config branch.m2.dummy Hello &&
	but branch -c m2 m2 &&
	but reflog exists refs/heads/m2 &&
	test $(but config branch.m2.dummy) = Hello
'

test_expect_success 'but branch -c zz zz/zz should fail' '
	but branch --create-reflog zz &&
	but reflog exists refs/heads/zz &&
	test_must_fail but branch -c zz zz/zz
'

test_expect_success 'but branch -c b/b b should fail' '
	but branch --create-reflog b/b &&
	test_must_fail but branch -c b/b b
'

test_expect_success 'but branch -C o/q o/p should work when o/p exists' '
	but branch --create-reflog o/q &&
	but reflog exists refs/heads/o/q &&
	but reflog exists refs/heads/o/p &&
	but branch -C o/q o/p
'

test_expect_success 'but branch -c -f o/q o/p should work when o/p exists' '
	but reflog exists refs/heads/o/q &&
	but reflog exists refs/heads/o/p &&
	but branch -c -f o/q o/p
'

test_expect_success 'but branch -c qq rr/qq should fail when rr exists' '
	but branch qq &&
	but branch rr &&
	test_must_fail but branch -c qq rr/qq
'

test_expect_success 'but branch -C b1 b2 should fail when b2 is checked out' '
	but branch b1 &&
	but checkout -b b2 &&
	test_must_fail but branch -C b1 b2
'

test_expect_success 'but branch -C c1 c2 should succeed when c1 is checked out' '
	but checkout -b c1 &&
	but branch c2 &&
	but branch -C c1 c2 &&
	test $(but rev-parse --abbrev-ref HEAD) = c1
'

test_expect_success 'but branch -C c1 c2 should never touch HEAD' '
	msg="Branch: copied refs/heads/c1 to refs/heads/c2" &&
	! grep "$msg$" .but/logs/HEAD
'

test_expect_success 'but branch -C main should work when main is checked out' '
	but checkout main &&
	but branch -C main
'

test_expect_success 'but branch -C main main should work when main is checked out' '
	but checkout main &&
	but branch -C main main
'

test_expect_success 'but branch -C main5 main5 should work when main is checked out' '
	but checkout main &&
	but branch main5 &&
	but branch -C main5 main5
'

test_expect_success 'but branch -C ab cd should overwrite existing config for cd' '
	but branch --create-reflog cd &&
	but reflog exists refs/heads/cd &&
	but config branch.cd.dummy CD &&
	but branch --create-reflog ab &&
	but reflog exists refs/heads/ab &&
	but config branch.ab.dummy AB &&
	but branch -C ab cd &&
	but reflog exists refs/heads/ab &&
	but reflog exists refs/heads/cd &&
	test $(but config branch.ab.dummy) = AB &&
	test $(but config branch.cd.dummy) = AB
'

test_expect_success 'but branch -c correctly copies multiple config sections' '
	FOO=1 &&
	export FOO &&
	test_when_finished "but checkout main" &&
	but checkout -b source2 main &&

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
	cat config.branch >>.but/config &&
	but branch -c source2 dest2 &&
	but config -f .but/config -l | grep -F -e source2 -e dest2 -e more.gar >actual &&
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
	sed -n -e "/Comment for source2/,\$p" .but/config >actual &&
	test_cmp expect actual
'

test_expect_success 'deleting a symref' '
	but branch target &&
	but symbolic-ref refs/heads/symref refs/heads/target &&
	echo "Deleted branch symref (was refs/heads/target)." >expect &&
	but branch -d symref >actual &&
	test_path_is_file .but/refs/heads/target &&
	test_path_is_missing .but/refs/heads/symref &&
	test_cmp expect actual
'

test_expect_success 'deleting a dangling symref' '
	but symbolic-ref refs/heads/dangling-symref nowhere &&
	test_path_is_file .but/refs/heads/dangling-symref &&
	echo "Deleted branch dangling-symref (was nowhere)." >expect &&
	but branch -d dangling-symref >actual &&
	test_path_is_missing .but/refs/heads/dangling-symref &&
	test_cmp expect actual
'

test_expect_success 'deleting a self-referential symref' '
	but symbolic-ref refs/heads/self-reference refs/heads/self-reference &&
	test_path_is_file .but/refs/heads/self-reference &&
	echo "Deleted branch self-reference (was refs/heads/self-reference)." >expect &&
	but branch -d self-reference >actual &&
	test_path_is_missing .but/refs/heads/self-reference &&
	test_cmp expect actual
'

test_expect_success 'renaming a symref is not allowed' '
	but symbolic-ref refs/heads/topic refs/heads/main &&
	test_must_fail but branch -m topic new-topic &&
	but symbolic-ref refs/heads/topic &&
	test_path_is_file .but/refs/heads/main &&
	test_path_is_missing .but/refs/heads/new-topic
'

test_expect_success SYMLINKS 'but branch -m u v should fail when the reflog for u is a symlink' '
	but branch --create-reflog u &&
	mv .but/logs/refs/heads/u real-u &&
	ln -s real-u .but/logs/refs/heads/u &&
	test_must_fail but branch -m u v
'

test_expect_success SYMLINKS 'but branch -m with symlinked .but/refs' '
	test_when_finished "rm -rf subdir" &&
	but init --bare subdir &&

	rm -rfv subdir/refs subdir/objects subdir/packed-refs &&
	ln -s ../.but/refs subdir/refs &&
	ln -s ../.but/objects subdir/objects &&
	ln -s ../.but/packed-refs subdir/packed-refs &&

	but -C subdir rev-parse --absolute-but-dir >subdir.dir &&
	but rev-parse --absolute-but-dir >our.dir &&
	! test_cmp subdir.dir our.dir &&

	but -C subdir log &&
	but -C subdir branch rename-src &&
	but rev-parse rename-src >expect &&
	but -C subdir branch -m rename-src rename-dest &&
	but rev-parse rename-dest >actual &&
	test_cmp expect actual &&
	but branch -D rename-dest
'

test_expect_success 'test tracking setup via --track' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track my1 local/main &&
	test $(but config branch.my1.remote) = local &&
	test $(but config branch.my1.merge) = refs/heads/main
'

test_expect_success 'test tracking setup (non-wildcard, matching)' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/main:refs/remotes/local/main &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track my4 local/main &&
	test $(but config branch.my4.remote) = local &&
	test $(but config branch.my4.merge) = refs/heads/main
'

test_expect_success 'tracking setup fails on non-matching refspec' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but config remote.local.fetch refs/heads/s:refs/remotes/local/s &&
	test_must_fail but branch --track my5 local/main &&
	test_must_fail but config branch.my5.remote &&
	test_must_fail but config branch.my5.merge
'

test_expect_success 'test tracking setup via config' '
	but config branch.autosetupmerge true &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch my3 local/main &&
	test $(but config branch.my3.remote) = local &&
	test $(but config branch.my3.merge) = refs/heads/main
'

test_expect_success 'test overriding tracking setup via --no-track' '
	but config branch.autosetupmerge true &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track my2 local/main &&
	but config branch.autosetupmerge false &&
	! test "$(but config branch.my2.remote)" = local &&
	! test "$(but config branch.my2.merge)" = refs/heads/main
'

test_expect_success 'no tracking without .fetch entries' '
	but config branch.autosetupmerge true &&
	but branch my6 s &&
	but config branch.autosetupmerge false &&
	test -z "$(but config branch.my6.remote)" &&
	test -z "$(but config branch.my6.merge)"
'

test_expect_success 'test tracking setup via --track but deeper' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/o/o || but fetch local) &&
	but branch --track my7 local/o/o &&
	test "$(but config branch.my7.remote)" = local &&
	test "$(but config branch.my7.merge)" = refs/heads/o/o
'

test_expect_success 'test deleting branch deletes branch config' '
	but branch -d my7 &&
	test -z "$(but config branch.my7.remote)" &&
	test -z "$(but config branch.my7.merge)"
'

test_expect_success 'test deleting branch without config' '
	but branch my7 s &&
	sha1=$(but rev-parse my7 | cut -c 1-7) &&
	echo "Deleted branch my7 (was $sha1)." >expect &&
	but branch -d my7 >actual 2>&1 &&
	test_cmp expect actual
'

test_expect_success 'deleting currently checked out branch fails' '
	but worktree add -b my7 my7 &&
	test_must_fail but -C my7 branch -d my7 &&
	test_must_fail but branch -d my7 &&
	rm -r my7 &&
	but worktree prune
'

test_expect_success 'test --track without .fetch entries' '
	but branch --track my8 &&
	test "$(but config branch.my8.remote)" &&
	test "$(but config branch.my8.merge)"
'

test_expect_success 'branch from non-branch HEAD w/autosetupmerge=always' '
	but config branch.autosetupmerge always &&
	but branch my9 HEAD^ &&
	but config branch.autosetupmerge false
'

test_expect_success 'branch from non-branch HEAD w/--track causes failure' '
	test_must_fail but branch --track my10 HEAD^
'

test_expect_success 'branch from tag w/--track causes failure' '
	but tag foobar &&
	test_must_fail but branch --track my11 foobar
'

test_expect_success '--set-upstream-to fails on multiple branches' '
	echo "fatal: too many arguments to set new upstream" >expect &&
	test_must_fail but branch --set-upstream-to main a b c 2>err &&
	test_cmp expect err
'

test_expect_success '--set-upstream-to fails on detached HEAD' '
	but checkout HEAD^{} &&
	test_when_finished but checkout - &&
	echo "fatal: could not set upstream of HEAD to main when it does not point to any branch." >expect &&
	test_must_fail but branch --set-upstream-to main 2>err &&
	test_cmp expect err
'

test_expect_success '--set-upstream-to fails on a missing dst branch' '
	echo "fatal: branch '"'"'does-not-exist'"'"' does not exist" >expect &&
	test_must_fail but branch --set-upstream-to main does-not-exist 2>err &&
	test_cmp expect err
'

test_expect_success '--set-upstream-to fails on a missing src branch' '
	test_must_fail but branch --set-upstream-to does-not-exist main 2>err &&
	test_i18ngrep "the requested upstream branch '"'"'does-not-exist'"'"' does not exist" err
'

test_expect_success '--set-upstream-to fails on a non-ref' '
	echo "fatal: cannot set up tracking information; starting point '"'"'HEAD^{}'"'"' is not a branch" >expect &&
	test_must_fail but branch --set-upstream-to HEAD^{} 2>err &&
	test_cmp expect err
'

test_expect_success '--set-upstream-to fails on locked config' '
	test_when_finished "rm -f .but/config.lock" &&
	>.but/config.lock &&
	but branch locked &&
	test_must_fail but branch --set-upstream-to locked 2>err &&
	test_i18ngrep "could not lock config file .but/config" err
'

test_expect_success 'use --set-upstream-to modify HEAD' '
	test_config branch.main.remote foo &&
	test_config branch.main.merge foo &&
	but branch my12 &&
	but branch --set-upstream-to my12 &&
	test "$(but config branch.main.remote)" = "." &&
	test "$(but config branch.main.merge)" = "refs/heads/my12"
'

test_expect_success 'use --set-upstream-to modify a particular branch' '
	but branch my13 &&
	but branch --set-upstream-to main my13 &&
	test_when_finished "but branch --unset-upstream my13" &&
	test "$(but config branch.my13.remote)" = "." &&
	test "$(but config branch.my13.merge)" = "refs/heads/main"
'

test_expect_success '--unset-upstream should fail if given a non-existent branch' '
	echo "fatal: Branch '"'"'i-dont-exist'"'"' has no upstream information" >expect &&
	test_must_fail but branch --unset-upstream i-dont-exist 2>err &&
	test_cmp expect err
'

test_expect_success '--unset-upstream should fail if config is locked' '
	test_when_finished "rm -f .but/config.lock" &&
	but branch --set-upstream-to locked &&
	>.but/config.lock &&
	test_must_fail but branch --unset-upstream 2>err &&
	test_i18ngrep "could not lock config file .but/config" err
'

test_expect_success 'test --unset-upstream on HEAD' '
	but branch my14 &&
	test_config branch.main.remote foo &&
	test_config branch.main.merge foo &&
	but branch --set-upstream-to my14 &&
	but branch --unset-upstream &&
	test_must_fail but config branch.main.remote &&
	test_must_fail but config branch.main.merge &&
	# fail for a branch without upstream set
	echo "fatal: Branch '"'"'main'"'"' has no upstream information" >expect &&
	test_must_fail but branch --unset-upstream 2>err &&
	test_cmp expect err
'

test_expect_success '--unset-upstream should fail on multiple branches' '
	echo "fatal: too many arguments to unset upstream" >expect &&
	test_must_fail but branch --unset-upstream a b c 2>err &&
	test_cmp expect err
'

test_expect_success '--unset-upstream should fail on detached HEAD' '
	but checkout HEAD^{} &&
	test_when_finished but checkout - &&
	echo "fatal: could not unset upstream of HEAD when it does not point to any branch." >expect &&
	test_must_fail but branch --unset-upstream 2>err &&
	test_cmp expect err
'

test_expect_success 'test --unset-upstream on a particular branch' '
	but branch my15 &&
	but branch --set-upstream-to main my14 &&
	but branch --unset-upstream my14 &&
	test_must_fail but config branch.my14.remote &&
	test_must_fail but config branch.my14.merge
'

test_expect_success 'disabled option --set-upstream fails' '
	test_must_fail but branch --set-upstream origin/main
'

test_expect_success '--set-upstream-to notices an error to set branch as own upstream' "
	but branch --set-upstream-to refs/heads/my13 my13 2>actual &&
	cat >expect <<-\EOF &&
	warning: not setting branch 'my13' as its own upstream
	EOF
	test_expect_code 1 but config branch.my13.remote &&
	test_expect_code 1 but config branch.my13.merge &&
	test_cmp expect actual
"

# Keep this test last, as it changes the current branch
cat >expect <<EOF
$ZERO_OID $HEAD $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> 1117150200 +0000	branch: Created from main
EOF
test_expect_success 'but checkout -b g/h/i -l should create a branch and a log' '
	BUT_CUMMITTER_DATE="2005-05-26 23:30" \
	but checkout -b g/h/i -l main &&
	test_path_is_file .but/refs/heads/g/h/i &&
	test_path_is_file .but/logs/refs/heads/g/h/i &&
	test_cmp expect .but/logs/refs/heads/g/h/i
'

test_expect_success 'checkout -b makes reflog by default' '
	but checkout main &&
	but config --unset core.logAllRefUpdates &&
	but checkout -b alpha &&
	but rev-parse --verify alpha@{0}
'

test_expect_success 'checkout -b does not make reflog when core.logAllRefUpdates = false' '
	but checkout main &&
	but config core.logAllRefUpdates false &&
	but checkout -b beta &&
	test_must_fail but rev-parse --verify beta@{0}
'

test_expect_success 'checkout -b with -l makes reflog when core.logAllRefUpdates = false' '
	but checkout main &&
	but checkout -lb gamma &&
	but config --unset core.logAllRefUpdates &&
	but rev-parse --verify gamma@{0}
'

test_expect_success 'avoid ambiguous track and advise' '
	but config branch.autosetupmerge true &&
	but config remote.ambi1.url lalala &&
	but config remote.ambi1.fetch refs/heads/lalala:refs/heads/main &&
	but config remote.ambi2.url lilili &&
	but config remote.ambi2.fetch refs/heads/lilili:refs/heads/main &&
	cat <<-EOF >expected &&
	fatal: not tracking: ambiguous information for ref '\''refs/heads/main'\''
	hint: There are multiple remotes whose fetch refspecs map to the remote
	hint: tracking ref '\''refs/heads/main'\'':
	hint:   ambi1
	hint:   ambi2
	hint: ''
	hint: This is typically a configuration error.
	hint: ''
	hint: To support setting up tracking branches, ensure that
	hint: different remotes'\'' fetch refspecs map into different
	hint: tracking namespaces.
	EOF
	test_must_fail but branch all1 main 2>actual &&
	test_cmp expected actual &&
	test -z "$(but config branch.all1.merge)"
'

test_expect_success 'autosetuprebase local on a tracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase local &&
	(but show-ref -q refs/remotes/local/o || but fetch local) &&
	but branch mybase &&
	but branch --track myr1 mybase &&
	test "$(but config branch.myr1.remote)" = . &&
	test "$(but config branch.myr1.merge)" = refs/heads/mybase &&
	test "$(but config branch.myr1.rebase)" = true
'

test_expect_success 'autosetuprebase always on a tracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase always &&
	(but show-ref -q refs/remotes/local/o || but fetch local) &&
	but branch mybase2 &&
	but branch --track myr2 mybase &&
	test "$(but config branch.myr2.remote)" = . &&
	test "$(but config branch.myr2.merge)" = refs/heads/mybase &&
	test "$(but config branch.myr2.rebase)" = true
'

test_expect_success 'autosetuprebase remote on a tracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase remote &&
	(but show-ref -q refs/remotes/local/o || but fetch local) &&
	but branch mybase3 &&
	but branch --track myr3 mybase2 &&
	test "$(but config branch.myr3.remote)" = . &&
	test "$(but config branch.myr3.merge)" = refs/heads/mybase2 &&
	! test "$(but config branch.myr3.rebase)" = true
'

test_expect_success 'autosetuprebase never on a tracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase never &&
	(but show-ref -q refs/remotes/local/o || but fetch local) &&
	but branch mybase4 &&
	but branch --track myr4 mybase2 &&
	test "$(but config branch.myr4.remote)" = . &&
	test "$(but config branch.myr4.merge)" = refs/heads/mybase2 &&
	! test "$(but config branch.myr4.rebase)" = true
'

test_expect_success 'autosetuprebase local on a tracked remote branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase local &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track myr5 local/main &&
	test "$(but config branch.myr5.remote)" = local &&
	test "$(but config branch.myr5.merge)" = refs/heads/main &&
	! test "$(but config branch.myr5.rebase)" = true
'

test_expect_success 'autosetuprebase never on a tracked remote branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase never &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track myr6 local/main &&
	test "$(but config branch.myr6.remote)" = local &&
	test "$(but config branch.myr6.merge)" = refs/heads/main &&
	! test "$(but config branch.myr6.rebase)" = true
'

test_expect_success 'autosetuprebase remote on a tracked remote branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase remote &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track myr7 local/main &&
	test "$(but config branch.myr7.remote)" = local &&
	test "$(but config branch.myr7.merge)" = refs/heads/main &&
	test "$(but config branch.myr7.rebase)" = true
'

test_expect_success 'autosetuprebase always on a tracked remote branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	but config branch.autosetuprebase remote &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track myr8 local/main &&
	test "$(but config branch.myr8.remote)" = local &&
	test "$(but config branch.myr8.merge)" = refs/heads/main &&
	test "$(but config branch.myr8.rebase)" = true
'

test_expect_success 'autosetuprebase unconfigured on a tracked remote branch' '
	but config --unset branch.autosetuprebase &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --track myr9 local/main &&
	test "$(but config branch.myr9.remote)" = local &&
	test "$(but config branch.myr9.merge)" = refs/heads/main &&
	test "z$(but config branch.myr9.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on a tracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/o || but fetch local) &&
	but branch mybase10 &&
	but branch --track myr10 mybase2 &&
	test "$(but config branch.myr10.remote)" = . &&
	test "$(but config branch.myr10.merge)" = refs/heads/mybase2 &&
	test "z$(but config branch.myr10.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on untracked local branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr11 mybase2 &&
	test "z$(but config branch.myr11.remote)" = z &&
	test "z$(but config branch.myr11.merge)" = z &&
	test "z$(but config branch.myr11.rebase)" = z
'

test_expect_success 'autosetuprebase unconfigured on untracked remote branch' '
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr12 local/main &&
	test "z$(but config branch.myr12.remote)" = z &&
	test "z$(but config branch.myr12.merge)" = z &&
	test "z$(but config branch.myr12.rebase)" = z
'

test_expect_success 'autosetuprebase never on an untracked local branch' '
	but config branch.autosetuprebase never &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr13 mybase2 &&
	test "z$(but config branch.myr13.remote)" = z &&
	test "z$(but config branch.myr13.merge)" = z &&
	test "z$(but config branch.myr13.rebase)" = z
'

test_expect_success 'autosetuprebase local on an untracked local branch' '
	but config branch.autosetuprebase local &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr14 mybase2 &&
	test "z$(but config branch.myr14.remote)" = z &&
	test "z$(but config branch.myr14.merge)" = z &&
	test "z$(but config branch.myr14.rebase)" = z
'

test_expect_success 'autosetuprebase remote on an untracked local branch' '
	but config branch.autosetuprebase remote &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr15 mybase2 &&
	test "z$(but config branch.myr15.remote)" = z &&
	test "z$(but config branch.myr15.merge)" = z &&
	test "z$(but config branch.myr15.rebase)" = z
'

test_expect_success 'autosetuprebase always on an untracked local branch' '
	but config branch.autosetuprebase always &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr16 mybase2 &&
	test "z$(but config branch.myr16.remote)" = z &&
	test "z$(but config branch.myr16.merge)" = z &&
	test "z$(but config branch.myr16.rebase)" = z
'

test_expect_success 'autosetuprebase never on an untracked remote branch' '
	but config branch.autosetuprebase never &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr17 local/main &&
	test "z$(but config branch.myr17.remote)" = z &&
	test "z$(but config branch.myr17.merge)" = z &&
	test "z$(but config branch.myr17.rebase)" = z
'

test_expect_success 'autosetuprebase local on an untracked remote branch' '
	but config branch.autosetuprebase local &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr18 local/main &&
	test "z$(but config branch.myr18.remote)" = z &&
	test "z$(but config branch.myr18.merge)" = z &&
	test "z$(but config branch.myr18.rebase)" = z
'

test_expect_success 'autosetuprebase remote on an untracked remote branch' '
	but config branch.autosetuprebase remote &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr19 local/main &&
	test "z$(but config branch.myr19.remote)" = z &&
	test "z$(but config branch.myr19.merge)" = z &&
	test "z$(but config branch.myr19.rebase)" = z
'

test_expect_success 'autosetuprebase always on an untracked remote branch' '
	but config branch.autosetuprebase always &&
	but config remote.local.url . &&
	but config remote.local.fetch refs/heads/*:refs/remotes/local/* &&
	(but show-ref -q refs/remotes/local/main || but fetch local) &&
	but branch --no-track myr20 local/main &&
	test "z$(but config branch.myr20.remote)" = z &&
	test "z$(but config branch.myr20.merge)" = z &&
	test "z$(but config branch.myr20.rebase)" = z
'

test_expect_success 'autosetuprebase always on detached HEAD' '
	but config branch.autosetupmerge always &&
	test_when_finished but checkout main &&
	but checkout HEAD^0 &&
	but branch my11 &&
	test -z "$(but config branch.my11.remote)" &&
	test -z "$(but config branch.my11.merge)"
'

test_expect_success 'detect misconfigured autosetuprebase (bad value)' '
	but config branch.autosetuprebase garbage &&
	test_must_fail but branch
'

test_expect_success 'detect misconfigured autosetuprebase (no value)' '
	but config --unset branch.autosetuprebase &&
	echo "[branch] autosetuprebase" >>.but/config &&
	test_must_fail but branch &&
	but config --unset branch.autosetuprebase
'

test_expect_success 'attempt to delete a branch without base and unmerged to HEAD' '
	but checkout my9 &&
	but config --unset branch.my8.merge &&
	test_must_fail but branch -d my8
'

test_expect_success 'attempt to delete a branch merged to its base' '
	# we are on my9 which is the initial cummit; traditionally
	# we would not have allowed deleting my8 that is not merged
	# to my9, but it is set to track main that already has my8
	but config branch.my8.merge refs/heads/main &&
	but branch -d my8
'

test_expect_success 'attempt to delete a branch merged to its base' '
	but checkout main &&
	echo Third >>A &&
	but cummit -m "Third cummit" A &&
	but branch -t my10 my9 &&
	but branch -f my10 HEAD^ &&
	# we are on main which is at the third cummit, and my10
	# is behind us, so traditionally we would have allowed deleting
	# it; but my10 is set to track my9 that is further behind.
	test_must_fail but branch -d my10
'

test_expect_success 'branch --delete --force removes dangling branch' '
	but checkout main &&
	test_cummit unstable &&
	hash=$(but rev-parse HEAD) &&
	objpath=$(echo $hash | sed -e "s|^..|.but/objects/&/|") &&
	but branch --no-track dangling &&
	mv $objpath $objpath.x &&
	test_when_finished "mv $objpath.x $objpath" &&
	but branch --delete --force dangling &&
	but for-each-ref refs/heads/dangling >actual &&
	test_must_be_empty actual
'

test_expect_success 'use --edit-description' '
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	EDITOR=./editor but branch --edit-description &&
		write_script editor <<-\EOF &&
		but stripspace -s <"$1" >"EDITOR_OUTPUT"
	EOF
	EDITOR=./editor but branch --edit-description &&
	echo "New contents" >expect &&
	test_cmp expect EDITOR_OUTPUT
'

test_expect_success 'detect typo in branch name when using --edit-description' '
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	test_must_fail env EDITOR=./editor but branch --edit-description no-such-branch
'

test_expect_success 'refuse --edit-description on unborn branch for now' '
	test_when_finished "but checkout main" &&
	write_script editor <<-\EOF &&
		echo "New contents" >"$1"
	EOF
	but checkout --orphan unborn &&
	test_must_fail env EDITOR=./editor but branch --edit-description
'

test_expect_success '--merged catches invalid object names' '
	test_must_fail but branch --merged 0000000000000000000000000000000000000000
'

test_expect_success '--list during rebase' '
	test_when_finished "reset_rebase" &&
	but checkout main &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	set_fake_editor &&
	but rebase -i HEAD~2 &&
	but branch --list >actual &&
	test_i18ngrep "rebasing main" actual
'

test_expect_success '--list during rebase from detached HEAD' '
	test_when_finished "reset_rebase && but checkout main" &&
	but checkout main^0 &&
	oid=$(but rev-parse --short HEAD) &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	set_fake_editor &&
	but rebase -i HEAD~2 &&
	but branch --list >actual &&
	test_i18ngrep "rebasing detached HEAD $oid" actual
'

test_expect_success 'tracking with unexpected .fetch refspec' '
	rm -rf a b c d &&
	but init -b main a &&
	(
		cd a &&
		test_cummit a
	) &&
	but init -b main b &&
	(
		cd b &&
		test_cummit b
	) &&
	but init -b main c &&
	(
		cd c &&
		test_cummit c &&
		but remote add a ../a &&
		but remote add b ../b &&
		but fetch --all
	) &&
	but init -b main d &&
	(
		cd d &&
		but remote add c ../c &&
		but config remote.c.fetch "+refs/remotes/*:refs/remotes/*" &&
		but fetch c &&
		but branch --track local/a/main remotes/a/main &&
		test "$(but config branch.local/a/main.remote)" = "c" &&
		test "$(but config branch.local/a/main.merge)" = "refs/remotes/a/main" &&
		but rev-parse --verify a >expect &&
		but rev-parse --verify local/a/main >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'configured cummitterdate sort' '
	but init -b main sort &&
	(
		cd sort &&
		but config branch.sort cummitterdate &&
		test_cummit initial &&
		but checkout -b a &&
		test_cummit a &&
		but checkout -b c &&
		test_cummit c &&
		but checkout -b b &&
		test_cummit b &&
		but branch >actual &&
		cat >expect <<-\EOF &&
		  main
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
		but config branch.sort cummitterdate &&
		but branch --sort=refname >actual &&
		cat >expect <<-\EOF &&
		  a
		* b
		  c
		  main
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'invalid sort parameter in configuration' '
	(
		cd sort &&
		but config branch.sort "v:notvalid" &&

		# this works in the "listing" mode, so bad sort key
		# is a dying offence.
		test_must_fail but branch &&

		# these do not need to use sorting, and should all
		# succeed
		but branch newone main &&
		but branch -c newone newerone &&
		but branch -m newone newestone &&
		but branch -d newerone newestone
	)
'

test_expect_success 'tracking info copied with --track=inherit' '
	but branch --track=inherit foo2 my1 &&
	test_cmp_config local branch.foo2.remote &&
	test_cmp_config refs/heads/main branch.foo2.merge
'

test_expect_success 'tracking info copied with autoSetupMerge=inherit' '
	test_unconfig branch.autoSetupMerge &&
	# default config does not copy tracking info
	but branch foo-no-inherit my1 &&
	test_cmp_config "" --default "" branch.foo-no-inherit.remote &&
	test_cmp_config "" --default "" branch.foo-no-inherit.merge &&
	# with autoSetupMerge=inherit, we copy tracking info from my1
	test_config branch.autoSetupMerge inherit &&
	but branch foo3 my1 &&
	test_cmp_config local branch.foo3.remote &&
	test_cmp_config refs/heads/main branch.foo3.merge &&
	# no tracking info to inherit from main
	but branch main2 main &&
	test_cmp_config "" --default "" branch.main2.remote &&
	test_cmp_config "" --default "" branch.main2.merge
'

test_expect_success '--track overrides branch.autoSetupMerge' '
	test_config branch.autoSetupMerge inherit &&
	but branch --track=direct foo4 my1 &&
	test_cmp_config . branch.foo4.remote &&
	test_cmp_config refs/heads/my1 branch.foo4.merge &&
	but branch --no-track foo5 my1 &&
	test_cmp_config "" --default "" branch.foo5.remote &&
	test_cmp_config "" --default "" branch.foo5.merge
'

test_done
