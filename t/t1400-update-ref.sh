#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='Test git update-ref and basic ref logging'
. ./test-lib.sh

Z=$_z40

test_expect_success setup '

	for name in A B C D E F
	do
		test_tick &&
		T=$(git write-tree) &&
		sha1=$(echo $name | git commit-tree $T) &&
		eval $name=$sha1
	done

'

m=refs/heads/master
n_dir=refs/heads/gu
n=$n_dir/fixes
outside=refs/foo

test_expect_success \
	"create $m" \
	"git update-ref $m $A &&
	 test $A"' = $(cat .git/'"$m"')'
test_expect_success \
	"create $m" \
	"git update-ref $m $B $A &&
	 test $B"' = $(cat .git/'"$m"')'
test_expect_success "fail to delete $m with stale ref" '
	test_must_fail git update-ref -d $m $A &&
	test $B = "$(cat .git/$m)"
'
test_expect_success "delete $m" '
	git update-ref -d $m $B &&
	! test -f .git/$m
'
rm -f .git/$m

test_expect_success "delete $m without oldvalue verification" "
	git update-ref $m $A &&
	test $A = \$(cat .git/$m) &&
	git update-ref -d $m &&
	! test -f .git/$m
"
rm -f .git/$m

test_expect_success \
	"fail to create $n" \
	"touch .git/$n_dir &&
	 test_must_fail git update-ref $n $A >out 2>err"
rm -f .git/$n_dir out err

test_expect_success \
	"create $m (by HEAD)" \
	"git update-ref HEAD $A &&
	 test $A"' = $(cat .git/'"$m"')'
test_expect_success \
	"create $m (by HEAD)" \
	"git update-ref HEAD $B $A &&
	 test $B"' = $(cat .git/'"$m"')'
test_expect_success "fail to delete $m (by HEAD) with stale ref" '
	test_must_fail git update-ref -d HEAD $A &&
	test $B = $(cat .git/$m)
'
test_expect_success "delete $m (by HEAD)" '
	git update-ref -d HEAD $B &&
	! test -f .git/$m
'
rm -f .git/$m

test_expect_success 'update-ref does not create reflogs by default' '
	test_when_finished "git update-ref -d $outside" &&
	git update-ref $outside $A &&
	git rev-parse $A >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	test_must_fail git reflog exists $outside
'

test_expect_success 'update-ref creates reflogs with --create-reflog' '
	test_when_finished "git update-ref -d $outside" &&
	git update-ref --create-reflog $outside $A &&
	git rev-parse $A >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	git reflog exists $outside
'

test_expect_success \
	"create $m (by HEAD)" \
	"git update-ref HEAD $A &&
	 test $A"' = $(cat .git/'"$m"')'
test_expect_success \
	"pack refs" \
	"git pack-refs --all"
test_expect_success \
	"move $m (by HEAD)" \
	"git update-ref HEAD $B $A &&
	 test $B"' = $(cat .git/'"$m"')'
test_expect_success "delete $m (by HEAD) should remove both packed and loose $m" '
	git update-ref -d HEAD $B &&
	! grep "$m" .git/packed-refs &&
	! test -f .git/$m
'
rm -f .git/$m

cp -f .git/HEAD .git/HEAD.orig
test_expect_success "delete symref without dereference" '
	git update-ref --no-deref -d HEAD &&
	! test -f .git/HEAD
'
cp -f .git/HEAD.orig .git/HEAD

test_expect_success "delete symref without dereference when the referred ref is packed" '
	echo foo >foo.c &&
	git add foo.c &&
	git commit -m foo &&
	git pack-refs --all &&
	git update-ref --no-deref -d HEAD &&
	! test -f .git/HEAD
'
cp -f .git/HEAD.orig .git/HEAD
git update-ref -d $m

test_expect_success 'update-ref -d is not confused by self-reference' '
	git symbolic-ref refs/heads/self refs/heads/self &&
	test_when_finished "rm -f .git/refs/heads/self" &&
	test_path_is_file .git/refs/heads/self &&
	test_must_fail git update-ref -d refs/heads/self &&
	test_path_is_file .git/refs/heads/self
'

test_expect_success 'update-ref --no-deref -d can delete self-reference' '
	git symbolic-ref refs/heads/self refs/heads/self &&
	test_when_finished "rm -f .git/refs/heads/self" &&
	test_path_is_file .git/refs/heads/self &&
	git update-ref --no-deref -d refs/heads/self &&
	test_path_is_missing .git/refs/heads/self
'

test_expect_success 'update-ref --no-deref -d can delete reference to bad ref' '
	>.git/refs/heads/bad &&
	test_when_finished "rm -f .git/refs/heads/bad" &&
	git symbolic-ref refs/heads/ref-to-bad refs/heads/bad &&
	test_when_finished "rm -f .git/refs/heads/ref-to-bad" &&
	test_path_is_file .git/refs/heads/ref-to-bad &&
	git update-ref --no-deref -d refs/heads/ref-to-bad &&
	test_path_is_missing .git/refs/heads/ref-to-bad
'

test_expect_success '(not) create HEAD with old sha1' "
	test_must_fail git update-ref HEAD $A $B
"
test_expect_success "(not) prior created .git/$m" "
	! test -f .git/$m
"
rm -f .git/$m

test_expect_success \
	"create HEAD" \
	"git update-ref HEAD $A"
test_expect_success '(not) change HEAD with wrong SHA1' "
	test_must_fail git update-ref HEAD $B $Z
"
test_expect_success "(not) changed .git/$m" "
	! test $B"' = $(cat .git/'"$m"')
'
rm -f .git/$m

rm -f .git/logs/refs/heads/master
test_expect_success \
	"create $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
	 git update-ref --create-reflog HEAD '"$A"' -m "Initial Creation" &&
	 test '"$A"' = $(cat .git/'"$m"')'
test_expect_success \
	"update $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:31" \
	 git update-ref HEAD'" $B $A "'-m "Switch" &&
	 test '"$B"' = $(cat .git/'"$m"')'
test_expect_success \
	"set $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:41" \
	 git update-ref HEAD'" $A &&
	 test $A"' = $(cat .git/'"$m"')'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150260 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	"test_cmp expect .git/logs/$m"
rm -rf .git/$m .git/logs expect

test_expect_success \
	'enable core.logAllRefUpdates' \
	'git config core.logAllRefUpdates true &&
	 test true = $(git config --bool --get core.logAllRefUpdates)'

test_expect_success \
	"create $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:32" \
	 git update-ref HEAD'" $A "'-m "Initial Creation" &&
	 test '"$A"' = $(cat .git/'"$m"')'
test_expect_success \
	"update $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:33" \
	 git update-ref HEAD'" $B $A "'-m "Switch" &&
	 test '"$B"' = $(cat .git/'"$m"')'
test_expect_success \
	"set $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:43" \
	 git update-ref HEAD '"$A &&
	 test $A"' = $(cat .git/'"$m"')'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	'test_cmp expect .git/logs/$m'
rm -f .git/$m .git/logs/$m expect

git update-ref $m $D
cat >.git/logs/$m <<EOF
0000000000000000000000000000000000000000 $C $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 -0500
$C $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150350 -0500
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 -0500
$F $Z $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150680 -0500
$Z $E $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 -0500
EOF

ed="Thu, 26 May 2005 18:32:00 -0500"
gd="Thu, 26 May 2005 18:33:00 -0500"
ld="Thu, 26 May 2005 18:43:00 -0500"
test_expect_success \
	'Query "master@{May 25 2005}" (before history)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{May 25 2005}" >o 2>e &&
	 test '"$C"' = $(cat o) &&
	 test "warning: Log for '\'master\'' only goes back to $ed." = "$(cat e)"'
test_expect_success \
	"Query master@{2005-05-25} (before history)" \
	'rm -f o e &&
	 git rev-parse --verify master@{2005-05-25} >o 2>e &&
	 test '"$C"' = $(cat o) &&
	 echo test "warning: Log for '\'master\'' only goes back to $ed." = "$(cat e)"'
test_expect_success \
	'Query "master@{May 26 2005 23:31:59}" (1 second before history)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{May 26 2005 23:31:59}" >o 2>e &&
	 test '"$C"' = $(cat o) &&
	 test "warning: Log for '\''master'\'' only goes back to $ed." = "$(cat e)"'
test_expect_success \
	'Query "master@{May 26 2005 23:32:00}" (exactly history start)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{May 26 2005 23:32:00}" >o 2>e &&
	 test '"$C"' = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@{May 26 2005 23:32:30}" (first non-creation change)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{May 26 2005 23:32:30}" >o 2>e &&
	 test '"$A"' = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@{2005-05-26 23:33:01}" (middle of history with gap)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{2005-05-26 23:33:01}" >o 2>e &&
	 test '"$B"' = $(cat o) &&
	 test "warning: Log for ref '"$m has gap after $gd"'." = "$(cat e)"'
test_expect_success \
	'Query "master@{2005-05-26 23:38:00}" (middle of history)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{2005-05-26 23:38:00}" >o 2>e &&
	 test '"$Z"' = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@{2005-05-26 23:43:00}" (exact end of history)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{2005-05-26 23:43:00}" >o 2>e &&
	 test '"$E"' = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@{2005-05-28}" (past end of history)' \
	'rm -f o e &&
	 git rev-parse --verify "master@{2005-05-28}" >o 2>e &&
	 test '"$D"' = $(cat o) &&
	 test "warning: Log for ref '"$m unexpectedly ended on $ld"'." = "$(cat e)"'


rm -f .git/$m .git/logs/$m expect

test_expect_success \
    'creating initial files' \
    'echo TEST >F &&
     git add F &&
	 GIT_AUTHOR_DATE="2005-05-26 23:30" \
	 GIT_COMMITTER_DATE="2005-05-26 23:30" git commit -m add -a &&
	 h_TEST=$(git rev-parse --verify HEAD) &&
	 echo The other day this did not work. >M &&
	 echo And then Bob told me how to fix it. >>M &&
	 echo OTHER >F &&
	 GIT_AUTHOR_DATE="2005-05-26 23:41" \
	 GIT_COMMITTER_DATE="2005-05-26 23:41" git commit -F M -a &&
	 h_OTHER=$(git rev-parse --verify HEAD) &&
	 GIT_AUTHOR_DATE="2005-05-26 23:44" \
	 GIT_COMMITTER_DATE="2005-05-26 23:44" git commit --amend &&
	 h_FIXED=$(git rev-parse --verify HEAD) &&
	 echo Merged initial commit and a later commit. >M &&
	 echo $h_TEST >.git/MERGE_HEAD &&
	 GIT_AUTHOR_DATE="2005-05-26 23:45" \
	 GIT_COMMITTER_DATE="2005-05-26 23:45" git commit -F M &&
	 h_MERGED=$(git rev-parse --verify HEAD) &&
	 rm -f M'

cat >expect <<EOF
$Z $h_TEST $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	commit (initial): add
$h_TEST $h_OTHER $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000	commit: The other day this did not work.
$h_OTHER $h_FIXED $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117151040 +0000	commit (amend): The other day this did not work.
$h_FIXED $h_MERGED $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117151100 +0000	commit (merge): Merged initial commit and a later commit.
EOF
test_expect_success \
	'git commit logged updates' \
	"test_cmp expect .git/logs/$m"
unset h_TEST h_OTHER h_FIXED h_MERGED

test_expect_success \
	'git cat-file blob master:F (expect OTHER)' \
	'test OTHER = $(git cat-file blob master:F)'
test_expect_success \
	'git cat-file blob master@{2005-05-26 23:30}:F (expect TEST)' \
	'test TEST = $(git cat-file blob "master@{2005-05-26 23:30}:F")'
test_expect_success \
	'git cat-file blob master@{2005-05-26 23:42}:F (expect OTHER)' \
	'test OTHER = $(git cat-file blob "master@{2005-05-26 23:42}:F")'

a=refs/heads/a
b=refs/heads/b
c=refs/heads/c
E='""'
F='%s\0'
pws='path with space'

test_expect_success 'stdin test setup' '
	echo "$pws" >"$pws" &&
	git add -- "$pws" &&
	git commit -m "$pws"
'

test_expect_success '-z fails without --stdin' '
	test_must_fail git update-ref -z $m $m $m 2>err &&
	test_i18ngrep "usage: git update-ref" err
'

test_expect_success 'stdin works with no input' '
	>stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse --verify -q $m
'

test_expect_success 'stdin fails on empty line' '
	echo "" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: empty command in input" err
'

test_expect_success 'stdin fails on only whitespace' '
	echo " " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: whitespace before command:  " err
'

test_expect_success 'stdin fails on leading whitespace' '
	echo " create $a $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: whitespace before command:  create $a $m" err
'

test_expect_success 'stdin fails on unknown command' '
	echo "unknown $a" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: unknown command: unknown $a" err
'

test_expect_success 'stdin fails on unbalanced quotes' '
	echo "create $a \"master" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: badly quoted argument: \\\"master" err
'

test_expect_success 'stdin fails on invalid escape' '
	echo "create $a \"ma\zter\"" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: badly quoted argument: \\\"ma\\\\zter\\\"" err
'

test_expect_success 'stdin fails on junk after quoted argument' '
	echo "create \"$a\"master" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: unexpected character after quoted argument: \\\"$a\\\"master" err
'

test_expect_success 'stdin fails create with no ref' '
	echo "create " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create: missing <ref>" err
'

test_expect_success 'stdin fails create with no new value' '
	echo "create $a" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $a: missing <newvalue>" err
'

test_expect_success 'stdin fails create with too many arguments' '
	echo "create $a $m $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $a: extra input:  $m" err
'

test_expect_success 'stdin fails update with no ref' '
	echo "update " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: update: missing <ref>" err
'

test_expect_success 'stdin fails update with no new value' '
	echo "update $a" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: update $a: missing <newvalue>" err
'

test_expect_success 'stdin fails update with too many arguments' '
	echo "update $a $m $m $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: update $a: extra input:  $m" err
'

test_expect_success 'stdin fails delete with no ref' '
	echo "delete " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: delete: missing <ref>" err
'

test_expect_success 'stdin fails delete with too many arguments' '
	echo "delete $a $m $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: delete $a: extra input:  $m" err
'

test_expect_success 'stdin fails verify with too many arguments' '
	echo "verify $a $m $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: verify $a: extra input:  $m" err
'

test_expect_success 'stdin fails option with unknown name' '
	echo "option unknown" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: option unknown: unknown" err
'

test_expect_success 'stdin fails with duplicate refs' '
	cat >stdin <<-EOF &&
	create $a $m
	create $b $m
	create $a $m
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: multiple updates for ref '"'"'$a'"'"' not allowed." err
'

test_expect_success 'stdin create ref works' '
	echo "create $a $m" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin does not create reflogs by default' '
	test_when_finished "git update-ref -d $outside" &&
	echo "create $outside $m" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	test_must_fail git reflog exists $outside
'

test_expect_success 'stdin creates reflogs with --create-reflog' '
	echo "create $outside $m" >stdin &&
	git update-ref --create-reflog --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	git reflog exists $outside
'

test_expect_success 'stdin succeeds with quoted argument' '
	git update-ref -d $a &&
	echo "create $a \"$m\"" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin succeeds with escaped character' '
	git update-ref -d $a &&
	echo "create $a \"ma\\163ter\"" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin update ref creates with zero old value' '
	echo "update $b $m $Z" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	git update-ref -d $b
'

test_expect_success 'stdin update ref creates with empty old value' '
	echo "update $b $m $E" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin create ref works with path with space to blob' '
	echo "create refs/blobs/pws \"$m:$pws\"" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse "$m:$pws" >expect &&
	git rev-parse refs/blobs/pws >actual &&
	test_cmp expect actual &&
	git update-ref -d refs/blobs/pws
'

test_expect_success 'stdin update ref fails with wrong old value' '
	echo "update $c $m $m~1" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$c'"'"'" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin update ref fails with bad old value' '
	echo "update $c $m does-not-exist" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: update $c: invalid <oldvalue>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin create ref fails with bad new value' '
	echo "create $c does-not-exist" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $c: invalid <newvalue>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin create ref fails with zero new value' '
	echo "create $c " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $c: zero <newvalue>" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin update ref works with right old value' '
	echo "update $b $m~1 $m" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete ref fails with wrong old value' '
	echo "delete $a $m~1" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$a'"'"'" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete ref fails with zero old value' '
	echo "delete $a " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: delete $a: zero <oldvalue>" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin update symref works option no-deref' '
	git symbolic-ref TESTSYMREF $b &&
	cat >stdin <<-EOF &&
	option no-deref
	update TESTSYMREF $a $b
	EOF
	git update-ref --stdin <stdin &&
	git rev-parse TESTSYMREF >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete symref works option no-deref' '
	git symbolic-ref TESTSYMREF $b &&
	cat >stdin <<-EOF &&
	option no-deref
	delete TESTSYMREF $b
	EOF
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q TESTSYMREF &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete ref works with right old value' '
	echo "delete $b $m~1" >stdin &&
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q $b
'

test_expect_success 'stdin update/create/verify combination works' '
	cat >stdin <<-EOF &&
	update $a $m
	create $b $m
	verify $c
	EOF
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin verify succeeds for correct value' '
	git rev-parse $m >expect &&
	echo "verify $m $m" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin verify succeeds for missing reference' '
	echo "verify refs/heads/missing $Z" >stdin &&
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q refs/heads/missing
'

test_expect_success 'stdin verify treats no value as missing' '
	echo "verify refs/heads/missing" >stdin &&
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q refs/heads/missing
'

test_expect_success 'stdin verify fails for wrong value' '
	git rev-parse $m >expect &&
	echo "verify $m $m~1" >stdin &&
	test_must_fail git update-ref --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin verify fails for mistaken null value' '
	git rev-parse $m >expect &&
	echo "verify $m $Z" >stdin &&
	test_must_fail git update-ref --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin verify fails for mistaken empty value' '
	M=$(git rev-parse $m) &&
	test_when_finished "git update-ref $m $M" &&
	git rev-parse $m >expect &&
	echo "verify $m" >stdin &&
	test_must_fail git update-ref --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin update refs works with identity updates' '
	cat >stdin <<-EOF &&
	update $a $m $m
	update $b $m $m
	update $c $Z $E
	EOF
	git update-ref --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin update refs fails with wrong old value' '
	git update-ref $c $m &&
	cat >stdin <<-EOF &&
	update $a $m $m
	update $b $m $m
	update $c  ''
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$c'"'"'" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	git rev-parse $c >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete refs works with packed and loose refs' '
	git pack-refs --all &&
	git update-ref $c $m~1 &&
	cat >stdin <<-EOF &&
	delete $a $m
	update $b $Z $m
	update $c $E $m~1
	EOF
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q $a &&
	test_must_fail git rev-parse --verify -q $b &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z works on empty input' '
	>stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse --verify -q $m
'

test_expect_success 'stdin -z fails on empty line' '
	echo "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: whitespace before command: " err
'

test_expect_success 'stdin -z fails on empty command' '
	printf $F "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: empty command in input" err
'

test_expect_success 'stdin -z fails on only whitespace' '
	printf $F " " >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: whitespace before command:  " err
'

test_expect_success 'stdin -z fails on leading whitespace' '
	printf $F " create $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: whitespace before command:  create $a" err
'

test_expect_success 'stdin -z fails on unknown command' '
	printf $F "unknown $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: unknown command: unknown $a" err
'

test_expect_success 'stdin -z fails create with no ref' '
	printf $F "create " >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: create: missing <ref>" err
'

test_expect_success 'stdin -z fails create with no new value' '
	printf $F "create $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: create $a: unexpected end of input when reading <newvalue>" err
'

test_expect_success 'stdin -z fails create with too many arguments' '
	printf $F "create $a" "$m" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: unknown command: $m" err
'

test_expect_success 'stdin -z fails update with no ref' '
	printf $F "update " >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update: missing <ref>" err
'

test_expect_success 'stdin -z fails update with too few args' '
	printf $F "update $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $a: unexpected end of input when reading <oldvalue>" err
'

test_expect_success 'stdin -z emits warning with empty new value' '
	git update-ref $a $m &&
	printf $F "update $a" "" "" >stdin &&
	git update-ref -z --stdin <stdin 2>err &&
	grep "warning: update $a: missing <newvalue>, treating as zero" err &&
	test_must_fail git rev-parse --verify -q $a
'

test_expect_success 'stdin -z fails update with no new value' '
	printf $F "update $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $a: unexpected end of input when reading <newvalue>" err
'

test_expect_success 'stdin -z fails update with no old value' '
	printf $F "update $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $a: unexpected end of input when reading <oldvalue>" err
'

test_expect_success 'stdin -z fails update with too many arguments' '
	printf $F "update $a" "$m" "$m" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: unknown command: $m" err
'

test_expect_success 'stdin -z fails delete with no ref' '
	printf $F "delete " >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: delete: missing <ref>" err
'

test_expect_success 'stdin -z fails delete with no old value' '
	printf $F "delete $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: delete $a: unexpected end of input when reading <oldvalue>" err
'

test_expect_success 'stdin -z fails delete with too many arguments' '
	printf $F "delete $a" "$m" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: unknown command: $m" err
'

test_expect_success 'stdin -z fails verify with too many arguments' '
	printf $F "verify $a" "$m" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: unknown command: $m" err
'

test_expect_success 'stdin -z fails verify with no old value' '
	printf $F "verify $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: verify $a: unexpected end of input when reading <oldvalue>" err
'

test_expect_success 'stdin -z fails option with unknown name' '
	printf $F "option unknown" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: option unknown: unknown" err
'

test_expect_success 'stdin -z fails with duplicate refs' '
	printf $F "create $a" "$m" "create $b" "$m" "create $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: multiple updates for ref '"'"'$a'"'"' not allowed." err
'

test_expect_success 'stdin -z create ref works' '
	printf $F "create $a" "$m" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z update ref creates with zero old value' '
	printf $F "update $b" "$m" "$Z" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	git update-ref -d $b
'

test_expect_success 'stdin -z update ref creates with empty old value' '
	printf $F "update $b" "$m" "" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z create ref works with path with space to blob' '
	printf $F "create refs/blobs/pws" "$m:$pws" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse "$m:$pws" >expect &&
	git rev-parse refs/blobs/pws >actual &&
	test_cmp expect actual &&
	git update-ref -d refs/blobs/pws
'

test_expect_success 'stdin -z update ref fails with wrong old value' '
	printf $F "update $c" "$m" "$m~1" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$c'"'"'" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z update ref fails with bad old value' '
	printf $F "update $c" "$m" "does-not-exist" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $c: invalid <oldvalue>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z create ref fails when ref exists' '
	git update-ref $c $m &&
	git rev-parse "$c" >expect &&
	printf $F "create $c" "$m~1" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$c'"'"'" err &&
	git rev-parse "$c" >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z create ref fails with bad new value' '
	git update-ref -d "$c" &&
	printf $F "create $c" "does-not-exist" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: create $c: invalid <newvalue>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z create ref fails with empty new value' '
	printf $F "create $c" "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: create $c: missing <newvalue>" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z update ref works with right old value' '
	printf $F "update $b" "$m~1" "$m" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z delete ref fails with wrong old value' '
	printf $F "delete $a" "$m~1" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$a'"'"'" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z delete ref fails with zero old value' '
	printf $F "delete $a" "$Z" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: delete $a: zero <oldvalue>" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z update symref works option no-deref' '
	git symbolic-ref TESTSYMREF $b &&
	printf $F "option no-deref" "update TESTSYMREF" "$a" "$b" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse TESTSYMREF >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z delete symref works option no-deref' '
	git symbolic-ref TESTSYMREF $b &&
	printf $F "option no-deref" "delete TESTSYMREF" "$b" >stdin &&
	git update-ref -z --stdin <stdin &&
	test_must_fail git rev-parse --verify -q TESTSYMREF &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z delete ref works with right old value' '
	printf $F "delete $b" "$m~1" >stdin &&
	git update-ref -z --stdin <stdin &&
	test_must_fail git rev-parse --verify -q $b
'

test_expect_success 'stdin -z update/create/verify combination works' '
	printf $F "update $a" "$m" "" "create $b" "$m" "verify $c" "" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z verify succeeds for correct value' '
	git rev-parse $m >expect &&
	printf $F "verify $m" "$m" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z verify succeeds for missing reference' '
	printf $F "verify refs/heads/missing" "$Z" >stdin &&
	git update-ref -z --stdin <stdin &&
	test_must_fail git rev-parse --verify -q refs/heads/missing
'

test_expect_success 'stdin -z verify treats no value as missing' '
	printf $F "verify refs/heads/missing" "" >stdin &&
	git update-ref -z --stdin <stdin &&
	test_must_fail git rev-parse --verify -q refs/heads/missing
'

test_expect_success 'stdin -z verify fails for wrong value' '
	git rev-parse $m >expect &&
	printf $F "verify $m" "$m~1" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z verify fails for mistaken null value' '
	git rev-parse $m >expect &&
	printf $F "verify $m" "$Z" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z verify fails for mistaken empty value' '
	M=$(git rev-parse $m) &&
	test_when_finished "git update-ref $m $M" &&
	git rev-parse $m >expect &&
	printf $F "verify $m" "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z update refs works with identity updates' '
	printf $F "update $a" "$m" "$m" "update $b" "$m" "$m" "update $c" "$Z" "" >stdin &&
	git update-ref -z --stdin <stdin &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z update refs fails with wrong old value' '
	git update-ref $c $m &&
	printf $F "update $a" "$m" "$m" "update $b" "$m" "$m" "update $c" "$m" "$Z" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: cannot lock ref '"'"'$c'"'"'" err &&
	git rev-parse $m >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual &&
	git rev-parse $b >actual &&
	test_cmp expect actual &&
	git rev-parse $c >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin -z delete refs works with packed and loose refs' '
	git pack-refs --all &&
	git update-ref $c $m~1 &&
	printf $F "delete $a" "$m" "update $b" "$Z" "$m" "update $c" "" "$m~1" >stdin &&
	git update-ref -z --stdin <stdin &&
	test_must_fail git rev-parse --verify -q $a &&
	test_must_fail git rev-parse --verify -q $b &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'fails with duplicate HEAD update' '
	git branch target1 $A &&
	git checkout target1 &&
	cat >stdin <<-EOF &&
	update refs/heads/target1 $C
	option no-deref
	update HEAD $B
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: multiple updates for '\''HEAD'\'' (including one via its referent .refs/heads/target1.) are not allowed" err &&
	echo "refs/heads/target1" >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	echo "$A" >expect &&
	git rev-parse refs/heads/target1 >actual &&
	test_cmp expect actual
'

test_expect_success 'fails with duplicate ref update via symref' '
	git branch target2 $A &&
	git symbolic-ref refs/heads/symref2 refs/heads/target2 &&
	cat >stdin <<-EOF &&
	update refs/heads/target2 $C
	update refs/heads/symref2 $B
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: multiple updates for '\''refs/heads/target2'\'' (including one via symref .refs/heads/symref2.) are not allowed" err &&
	echo "refs/heads/target2" >expect &&
	git symbolic-ref refs/heads/symref2 >actual &&
	test_cmp expect actual &&
	echo "$A" >expect &&
	git rev-parse refs/heads/target2 >actual &&
	test_cmp expect actual
'

run_with_limited_open_files () {
	(ulimit -n 32 && "$@")
}

test_lazy_prereq ULIMIT_FILE_DESCRIPTORS 'run_with_limited_open_files true'

test_expect_success ULIMIT_FILE_DESCRIPTORS 'large transaction creating branches does not burst open file limit' '
(
	for i in $(test_seq 33)
	do
		echo "create refs/heads/$i HEAD"
	done >large_input &&
	run_with_limited_open_files git update-ref --stdin <large_input &&
	git rev-parse --verify -q refs/heads/33
)
'

test_expect_success ULIMIT_FILE_DESCRIPTORS 'large transaction deleting branches does not burst open file limit' '
(
	for i in $(test_seq 33)
	do
		echo "delete refs/heads/$i HEAD"
	done >large_input &&
	run_with_limited_open_files git update-ref --stdin <large_input &&
	test_must_fail git rev-parse --verify -q refs/heads/33
)
'

test_expect_success 'handle per-worktree refs in refs/bisect' '
	git commit --allow-empty -m "initial commit" &&
	git worktree add -b branch worktree &&
	(
		cd worktree &&
		git commit --allow-empty -m "test commit"  &&
		git for-each-ref >for-each-ref.out &&
		! grep refs/bisect for-each-ref.out &&
		git update-ref refs/bisect/something HEAD &&
		git rev-parse refs/bisect/something >../worktree-head &&
		git for-each-ref | grep refs/bisect/something
	) &&
	test_path_is_missing .git/refs/bisect &&
	test_must_fail git rev-parse refs/bisect/something &&
	git update-ref refs/bisect/something HEAD &&
	git rev-parse refs/bisect/something >main-head &&
	! test_cmp main-head worktree-head
'

test_done
