#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='Test git update-ref and basic ref logging'

. ./test-lib.sh

Z=$ZERO_OID

m=refs/heads/main
outside=refs/foo
bare=bare-repo

create_test_commits ()
{
	prfx="$1"
	for name in A B C D E F
	do
		test_tick &&
		T=$(git write-tree) &&
		sha1=$(echo $name | git commit-tree $T) &&
		eval $prfx$name=$sha1
	done
}

test_expect_success setup '
	git checkout --orphan main &&
	create_test_commits "" &&
	mkdir $bare &&
	cd $bare &&
	git init --bare -b main &&
	create_test_commits "bare" &&
	cd -
'

test_expect_success "create $m" '
	git update-ref $m $A &&
	test $A = $(git show-ref -s --verify $m)
'
test_expect_success "create $m with oldvalue verification" '
	git update-ref $m $B $A &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "fail to delete $m with stale ref" '
	test_must_fail git update-ref -d $m $A &&
	test $B = "$(git show-ref -s --verify $m)"
'
test_expect_success "delete $m" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref -d $m $B &&
	test_must_fail git show-ref --verify -q $m
'

test_expect_success "delete $m without oldvalue verification" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref $m $A &&
	test $A = $(git show-ref -s --verify $m) &&
	git update-ref -d $m &&
	test_must_fail git show-ref --verify -q $m
'

test_expect_success "fail to create $n due to file/directory conflict" '
	test_when_finished "git update-ref -d refs/heads/gu" &&
	git update-ref refs/heads/gu $A &&
	test_must_fail git update-ref refs/heads/gu/fixes $A
'

test_expect_success "create $m (by HEAD)" '
	git update-ref HEAD $A &&
	test $A = $(git show-ref -s --verify $m)
'
test_expect_success "create $m (by HEAD) with oldvalue verification" '
	git update-ref HEAD $B $A &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "fail to delete $m (by HEAD) with stale ref" '
	test_must_fail git update-ref -d HEAD $A &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "delete $m (by HEAD)" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref -d HEAD $B &&
	test_must_fail git show-ref --verify -q $m
'

test_expect_success "deleting current branch adds message to HEAD's log" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref $m $A &&
	git symbolic-ref HEAD $m &&
	git update-ref -m delete-$m -d $m &&
	test_must_fail git show-ref --verify -q $m &&
	test-tool ref-store main for-each-reflog-ent HEAD >actual &&
	grep "delete-$m$" actual
'

test_expect_success "deleting by HEAD adds message to HEAD's log" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref $m $A &&
	git symbolic-ref HEAD $m &&
	git update-ref -m delete-by-head -d HEAD &&
	test_must_fail git show-ref --verify -q $m &&
	test-tool ref-store main for-each-reflog-ent HEAD >actual &&
	grep "delete-by-head$" actual
'

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

test_expect_success 'creates no reflog in bare repository' '
	git -C $bare update-ref $m $bareA &&
	git -C $bare rev-parse $bareA >expect &&
	git -C $bare rev-parse $m >actual &&
	test_cmp expect actual &&
	test_must_fail git -C $bare reflog exists $m
'

test_expect_success 'core.logAllRefUpdates=true creates reflog in bare repository' '
	test_when_finished "git -C $bare config --unset core.logAllRefUpdates && \
		test-tool ref-store main delete-reflog $m" &&
	git -C $bare config core.logAllRefUpdates true &&
	git -C $bare update-ref $m $bareB &&
	git -C $bare rev-parse $bareB >expect &&
	git -C $bare rev-parse $m >actual &&
	test_cmp expect actual &&
	git -C $bare reflog exists $m
'

test_expect_success 'core.logAllRefUpdates=true does not create reflog by default' '
	test_config core.logAllRefUpdates true &&
	test_when_finished "git update-ref -d $outside" &&
	git update-ref $outside $A &&
	git rev-parse $A >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	test_must_fail git reflog exists $outside
'

test_expect_success 'core.logAllRefUpdates=always creates reflog by default' '
	test_config core.logAllRefUpdates always &&
	test_when_finished "git update-ref -d $outside" &&
	git update-ref $outside $A &&
	git rev-parse $A >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	git reflog exists $outside
'

test_expect_success 'core.logAllRefUpdates=always creates reflog for ORIG_HEAD' '
	test_config core.logAllRefUpdates always &&
	git update-ref ORIG_HEAD $A &&
	git reflog exists ORIG_HEAD
'

test_expect_success '--no-create-reflog overrides core.logAllRefUpdates=always' '
	test_config core.logAllRefUpdates true &&
	test_when_finished "git update-ref -d $outside" &&
	git update-ref --no-create-reflog $outside $A &&
	git rev-parse $A >expect &&
	git rev-parse $outside >actual &&
	test_cmp expect actual &&
	test_must_fail git reflog exists $outside
'

test_expect_success "create $m (by HEAD)" '
	git update-ref HEAD $A &&
	test $A = $(git show-ref -s --verify $m)
'
test_expect_success 'pack refs' '
	git pack-refs --all
'
test_expect_success "move $m (by HEAD)" '
	git update-ref HEAD $B $A &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "delete $m (by HEAD) should remove both packed and loose $m" '
	test_when_finished "git update-ref -d $m" &&
	git update-ref -d HEAD $B &&
	! grep "$m" .git/packed-refs &&
	test_must_fail git show-ref --verify -q $m
'

test_expect_success 'delete symref without dereference' '
	test_when_finished "git update-ref -d $m" &&
	echo foo >foo.c &&
	git add foo.c &&
	git commit -m foo &&
	git symbolic-ref SYMREF $m &&
	git update-ref --no-deref -d SYMREF &&
	git show-ref --verify -q $m &&
	test_must_fail git show-ref --verify -q SYMREF &&
	test_must_fail git symbolic-ref SYMREF
'

test_expect_success 'delete symref without dereference when the referred ref is packed' '
	test_when_finished "git update-ref -d $m" &&
	echo foo >foo.c &&
	git add foo.c &&
	git commit -m foo &&
	git symbolic-ref SYMREF $m &&
	git pack-refs --all &&
	git update-ref --no-deref -d SYMREF &&
	git show-ref --verify -q $m &&
	test_must_fail git show-ref --verify -q SYMREF &&
	test_must_fail git symbolic-ref SYMREF
'

test_expect_success 'update-ref -d is not confused by self-reference' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF refs/heads/self" &&
	git symbolic-ref refs/heads/self refs/heads/self &&
	git symbolic-ref --no-recurse refs/heads/self &&
	test_must_fail git update-ref -d refs/heads/self &&
	git symbolic-ref --no-recurse refs/heads/self
'

test_expect_success 'update-ref --no-deref -d can delete self-reference' '
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF refs/heads/self" &&
	git symbolic-ref refs/heads/self refs/heads/self &&
	git symbolic-ref --no-recurse refs/heads/self &&
	git update-ref --no-deref -d refs/heads/self &&
	test_must_fail git show-ref --verify -q refs/heads/self
'

test_expect_success REFFILES 'update-ref --no-deref -d can delete reference to bad ref' '
	>.git/refs/heads/bad &&
	test_when_finished "rm -f .git/refs/heads/bad" &&
	git symbolic-ref refs/heads/ref-to-bad refs/heads/bad &&
	test_when_finished "git update-ref -d refs/heads/ref-to-bad" &&
	git symbolic-ref --no-recurse refs/heads/ref-to-bad &&
	git update-ref --no-deref -d refs/heads/ref-to-bad &&
	test_must_fail git show-ref --verify -q refs/heads/ref-to-bad
'

test_expect_success '(not) create HEAD with old sha1' '
	test_must_fail git update-ref HEAD $A $B
'
test_expect_success "(not) prior created .git/$m" '
	test_when_finished "git update-ref -d $m" &&
	test_must_fail git show-ref --verify -q $m
'

test_expect_success 'create HEAD' '
	git update-ref HEAD $A
'
test_expect_success '(not) change HEAD with wrong SHA1' '
	test_must_fail git update-ref HEAD $B $Z
'
test_expect_success "(not) changed .git/$m" '
	test_when_finished "git update-ref -d $m" &&
	! test $B = $(git show-ref -s --verify $m)
'

test_expect_success "clean up reflog" '
	test-tool ref-store main delete-reflog $m
'

test_expect_success "create $m (logged by touch)" '
	test_config core.logAllRefUpdates false &&
	GIT_COMMITTER_DATE="2005-05-26 23:30" \
	git update-ref --create-reflog HEAD $A -m "Initial Creation" &&
	test $A = $(git show-ref -s --verify $m)
'
test_expect_success "update $m (logged by touch)" '
	test_config core.logAllRefUpdates false &&
	GIT_COMMITTER_DATE="2005-05-26 23:31" \
	git update-ref HEAD $B $A -m "Switch" &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "set $m (logged by touch)" '
	test_config core.logAllRefUpdates false &&
	GIT_COMMITTER_DATE="2005-05-26 23:41" \
	git update-ref HEAD $A &&
	test $A = $(git show-ref -s --verify $m)
'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150260 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000
EOF
test_expect_success "verifying $m's log (logged by touch)" '
	test_when_finished "git update-ref -d $m && git reflog expire --expire=all --all && rm -rf actual expect" &&
	test-tool ref-store main for-each-reflog-ent $m >actual &&
	test_cmp actual expect
'

test_expect_success "create $m (logged by config)" '
	test_config core.logAllRefUpdates true &&
	GIT_COMMITTER_DATE="2005-05-26 23:32" \
	git update-ref HEAD $A -m "Initial Creation" &&
	test $A = $(git show-ref -s --verify $m)
'
test_expect_success "update $m (logged by config)" '
	test_config core.logAllRefUpdates true &&
	GIT_COMMITTER_DATE="2005-05-26 23:33" \
	git update-ref HEAD $B $A -m "Switch" &&
	test $B = $(git show-ref -s --verify $m)
'
test_expect_success "set $m (logged by config)" '
	test_config core.logAllRefUpdates true &&
	GIT_COMMITTER_DATE="2005-05-26 23:43" \
	git update-ref HEAD $A &&
	test $A = $(git show-ref -s --verify $m)
'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 +0000
EOF
test_expect_success "verifying $m's log (logged by config)" '
	test_when_finished "git update-ref -d $m && git reflog expire --expire=all --all && rm -rf actual expect" &&
	test-tool ref-store main for-each-reflog-ent $m >actual &&
	test_cmp actual expect
'

test_expect_success 'set up for querying the reflog' '
	git update-ref -d $m &&
	test-tool ref-store main delete-reflog $m &&

	GIT_COMMITTER_DATE="1117150320 -0500" git update-ref $m $C &&
	GIT_COMMITTER_DATE="1117150350 -0500" git update-ref $m $A &&
	GIT_COMMITTER_DATE="1117150380 -0500" git update-ref $m $B &&
	GIT_COMMITTER_DATE="1117150680 -0500" git update-ref $m $F &&
	GIT_COMMITTER_DATE="1117150980 -0500" git update-ref $m $E &&
	git update-ref $m $D &&
	# Delete the last reflog entry so that the tip of m and the reflog for
	# it disagree.
	git reflog delete $m@{0} &&

	cat >expect <<-EOF &&
	$Z $C $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 -0500
	$C $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150350 -0500
	$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 -0500
	$B $F $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150680 -0500
	$F $E $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 -0500
	EOF
	test-tool ref-store main for-each-reflog-ent $m >actual &&
	test_cmp expect actual
'

ed="Thu, 26 May 2005 18:32:00 -0500"
gd="Thu, 26 May 2005 18:33:00 -0500"
ld="Thu, 26 May 2005 18:43:00 -0500"
test_expect_success 'Query "main@{May 25 2005}" (before history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{May 25 2005}" >o 2>e &&
	echo "$C" >expect &&
	test_cmp expect o &&
	echo "warning: log for '\''main'\'' only goes back to $ed" >expect &&
	test_cmp expect e
'
test_expect_success 'Query main@{2005-05-25} (before history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify main@{2005-05-25} >o 2>e &&
	echo "$C" >expect &&
	test_cmp expect o &&
	echo "warning: log for '\''main'\'' only goes back to $ed" >expect &&
	test_cmp expect e
'
test_expect_success 'Query "main@{May 26 2005 23:31:59}" (1 second before history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{May 26 2005 23:31:59}" >o 2>e &&
	echo "$C" >expect &&
	test_cmp expect o &&
	echo "warning: log for '\''main'\'' only goes back to $ed" >expect &&
	test_cmp expect e
'
test_expect_success 'Query "main@{May 26 2005 23:32:00}" (exactly history start)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{May 26 2005 23:32:00}" >o 2>e &&
	echo "$C" >expect &&
	test_cmp expect o &&
	test_must_be_empty e
'
test_expect_success 'Query "main@{May 26 2005 23:32:30}" (first non-creation change)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{May 26 2005 23:32:30}" >o 2>e &&
	echo "$A" >expect &&
	test_cmp expect o &&
	test_must_be_empty e
'
test_expect_success 'Query "main@{2005-05-26 23:33:01}" (middle of history with gap)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{2005-05-26 23:33:01}" >o 2>e &&
	echo "$B" >expect &&
	test_cmp expect o
'
test_expect_success 'Query "main@{2005-05-26 23:38:00}" (middle of history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{2005-05-26 23:38:00}" >o 2>e &&
	echo "$F" >expect &&
	test_cmp expect o &&
	test_must_be_empty e
'
test_expect_success 'Query "main@{2005-05-26 23:43:00}" (exact end of history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{2005-05-26 23:43:00}" >o 2>e &&
	echo "$E" >expect &&
	test_cmp expect o &&
	test_must_be_empty e
'
test_expect_success 'Query "main@{2005-05-28}" (past end of history)' '
	test_when_finished "rm -f o e" &&
	git rev-parse --verify "main@{2005-05-28}" >o 2>e &&
	echo "$D" >expect &&
	test_cmp expect o &&
	test_grep -F "warning: log for ref $m unexpectedly ended on $ld" e
'

rm -f expect
git update-ref -d $m

test_expect_success 'query reflog with gap' '
	test_when_finished "git update-ref -d $m" &&

	GIT_COMMITTER_DATE="1117150320 -0500" git update-ref $m $A &&
	GIT_COMMITTER_DATE="1117150380 -0500" git update-ref $m $B &&
	GIT_COMMITTER_DATE="1117150480 -0500" git update-ref $m $C &&
	GIT_COMMITTER_DATE="1117150580 -0500" git update-ref $m $D &&
	GIT_COMMITTER_DATE="1117150680 -0500" git update-ref $m $F &&
	git reflog delete $m@{2} &&

	git rev-parse --verify "main@{2005-05-26 23:33:01}" >actual 2>stderr &&
	echo "$B" >expect &&
	test_cmp expect actual &&
	test_grep -F "warning: log for ref $m has gap after $gd" stderr
'

test_expect_success 'creating initial files' '
	test_when_finished rm -f M &&
	echo TEST >F &&
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
	h_MERGED=$(git rev-parse --verify HEAD)
'

cat >expect <<EOF
$Z $h_TEST $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	commit (initial): add
$h_TEST $h_OTHER $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000	commit: The other day this did not work.
$h_OTHER $h_FIXED $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117151040 +0000	commit (amend): The other day this did not work.
$h_FIXED $h_MERGED $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117151100 +0000	commit (merge): Merged initial commit and a later commit.
EOF
test_expect_success 'git commit logged updates' '
	test-tool ref-store main for-each-reflog-ent $m >actual &&
	test_cmp expect actual
'
unset h_TEST h_OTHER h_FIXED h_MERGED

test_expect_success 'git cat-file blob main:F (expect OTHER)' '
	test OTHER = $(git cat-file blob main:F)
'
test_expect_success 'git cat-file blob main@{2005-05-26 23:30}:F (expect TEST)' '
	test TEST = $(git cat-file blob "main@{2005-05-26 23:30}:F")
'
test_expect_success 'git cat-file blob main@{2005-05-26 23:42}:F (expect OTHER)' '
	test OTHER = $(git cat-file blob "main@{2005-05-26 23:42}:F")
'

# Test adding and deleting pseudorefs

test_expect_success 'given old value for missing pseudoref, do not create' '
	test_must_fail git update-ref PSEUDOREF $A $B 2>err &&
	test_must_fail git rev-parse PSEUDOREF &&
	test_grep "unable to resolve reference" err
'

test_expect_success 'create pseudoref' '
	git update-ref PSEUDOREF $A &&
	test $A = $(git show-ref -s --verify PSEUDOREF)
'

test_expect_success 'overwrite pseudoref with no old value given' '
	git update-ref PSEUDOREF $B &&
	test $B = $(git show-ref -s --verify PSEUDOREF)
'

test_expect_success 'overwrite pseudoref with correct old value' '
	git update-ref PSEUDOREF $C $B &&
	test $C = $(git show-ref -s --verify PSEUDOREF)
'

test_expect_success 'do not overwrite pseudoref with wrong old value' '
	test_must_fail git update-ref PSEUDOREF $D $E 2>err &&
	test $C = $(git show-ref -s --verify PSEUDOREF) &&
	test_grep "cannot lock ref.*expected" err
'

test_expect_success 'delete pseudoref' '
	git update-ref -d PSEUDOREF &&
	test_must_fail git show-ref -s --verify PSEUDOREF
'

test_expect_success 'do not delete pseudoref with wrong old value' '
	git update-ref PSEUDOREF $A &&
	test_must_fail git update-ref -d PSEUDOREF $B 2>err &&
	test $A = $(git show-ref -s --verify PSEUDOREF) &&
	test_grep "cannot lock ref.*expected" err
'

test_expect_success 'delete pseudoref with correct old value' '
	git update-ref -d PSEUDOREF $A &&
	test_must_fail git show-ref -s --verify PSEUDOREF
'

test_expect_success 'create pseudoref with old OID zero' '
	git update-ref PSEUDOREF $A $Z &&
	test $A = $(git show-ref -s --verify PSEUDOREF)
'

test_expect_success 'do not overwrite pseudoref with old OID zero' '
	test_when_finished git update-ref -d PSEUDOREF &&
	test_must_fail git update-ref PSEUDOREF $B $Z 2>err &&
	test $A = $(git show-ref -s --verify PSEUDOREF) &&
	test_grep "already exists" err
'

# Test --stdin

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
	test_grep "usage: git update-ref" err
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
	echo "create $a \"main" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: badly quoted argument: \\\"main" err
'

test_expect_success 'stdin fails on invalid escape' '
	echo "create $a \"ma\zn\"" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: badly quoted argument: \\\"ma\\\\zn\\\"" err
'

test_expect_success 'stdin fails on junk after quoted argument' '
	echo "create \"$a\"main" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: unexpected character after quoted argument: \\\"$a\\\"main" err
'

test_expect_success 'stdin fails create with no ref' '
	echo "create " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create: missing <ref>" err
'

test_expect_success 'stdin fails create with no new value' '
	echo "create $a" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $a: missing <new-oid>" err
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
	grep "fatal: update $a: missing <new-oid>" err
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
	test_grep "fatal: multiple updates for ref '"'"'$a'"'"' not allowed" err
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
	test_when_finished "git update-ref -d $outside" &&
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
	echo "create $a \"ma\\151n\"" >stdin &&
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
	grep "fatal: update $c: invalid <old-oid>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin create ref fails with bad new value' '
	echo "create $c does-not-exist" >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $c: invalid <new-oid>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin create ref fails with zero new value' '
	echo "create $c " >stdin &&
	test_must_fail git update-ref --stdin <stdin 2>err &&
	grep "fatal: create $c: zero <new-oid>" err &&
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
	grep "fatal: delete $a: zero <old-oid>" err &&
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

test_expect_success 'stdin update symref works flag --no-deref' '
	git symbolic-ref TESTSYMREFONE $b &&
	git symbolic-ref TESTSYMREFTWO $b &&
	cat >stdin <<-EOF &&
	update TESTSYMREFONE $a $b
	update TESTSYMREFTWO $a $b
	EOF
	git update-ref --no-deref --stdin <stdin &&
	git rev-parse TESTSYMREFONE TESTSYMREFTWO >expect &&
	git rev-parse $a $a >actual &&
	test_cmp expect actual &&
	git rev-parse $m~1 >expect &&
	git rev-parse $b >actual &&
	test_cmp expect actual
'

test_expect_success 'stdin delete symref works flag --no-deref' '
	git symbolic-ref TESTSYMREFONE $b &&
	git symbolic-ref TESTSYMREFTWO $b &&
	cat >stdin <<-EOF &&
	delete TESTSYMREFONE $b
	delete TESTSYMREFTWO $b
	EOF
	git update-ref --no-deref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q TESTSYMREFONE &&
	test_must_fail git rev-parse --verify -q TESTSYMREFTWO &&
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
	test-tool ref-store main for-each-reflog-ent $m >before &&
	git rev-parse $m >expect &&
	echo "verify $m $m" >stdin &&
	git update-ref --stdin <stdin &&
	git rev-parse $m >actual &&
	test_cmp expect actual &&
	test-tool ref-store main for-each-reflog-ent $m >after &&
	test_cmp before after
'

test_expect_success 'stdin verify succeeds for missing reference' '
	test-tool ref-store main for-each-reflog-ent $m >before &&
	echo "verify refs/heads/missing $Z" >stdin &&
	git update-ref --stdin <stdin &&
	test_must_fail git rev-parse --verify -q refs/heads/missing &&
	test-tool ref-store main for-each-reflog-ent $m >after &&
	test_cmp before after
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
	grep "fatal: create $a: unexpected end of input when reading <new-oid>" err
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
	grep "fatal: update $a: unexpected end of input when reading <old-oid>" err
'

test_expect_success 'stdin -z emits warning with empty new value' '
	git update-ref $a $m &&
	printf $F "update $a" "" "" >stdin &&
	git update-ref -z --stdin <stdin 2>err &&
	grep "warning: update $a: missing <new-oid>, treating as zero" err &&
	test_must_fail git rev-parse --verify -q $a
'

test_expect_success 'stdin -z fails update with no new value' '
	printf $F "update $a" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $a: unexpected end of input when reading <new-oid>" err
'

test_expect_success 'stdin -z fails update with no old value' '
	printf $F "update $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: update $a: unexpected end of input when reading <old-oid>" err
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
	grep "fatal: delete $a: unexpected end of input when reading <old-oid>" err
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
	grep "fatal: verify $a: unexpected end of input when reading <old-oid>" err
'

test_expect_success 'stdin -z fails option with unknown name' '
	printf $F "option unknown" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: option unknown: unknown" err
'

test_expect_success 'stdin -z fails with duplicate refs' '
	printf $F "create $a" "$m" "create $b" "$m" "create $a" "$m" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	test_grep "fatal: multiple updates for ref '"'"'$a'"'"' not allowed" err
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
	grep "fatal: update $c: invalid <old-oid>: does-not-exist" err &&
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
	grep "fatal: create $c: invalid <new-oid>: does-not-exist" err &&
	test_must_fail git rev-parse --verify -q $c
'

test_expect_success 'stdin -z create ref fails with empty new value' '
	printf $F "create $c" "" >stdin &&
	test_must_fail git update-ref -z --stdin <stdin 2>err &&
	grep "fatal: create $c: missing <new-oid>" err &&
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
	grep "fatal: delete $a: zero <old-oid>" err &&
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
	test_grep "fatal: multiple updates for '\''HEAD'\'' (including one via its referent .refs/heads/target1.) are not allowed" err &&
	echo "refs/heads/target1" >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	echo "$A" >expect &&
	git rev-parse refs/heads/target1 >actual &&
	test_cmp expect actual
'

test_expect_success 'fails with duplicate ref update via symref' '
	test_when_finished "git symbolic-ref -d refs/heads/symref2" &&
	git branch target2 $A &&
	git symbolic-ref refs/heads/symref2 refs/heads/target2 &&
	cat >stdin <<-EOF &&
	update refs/heads/target2 $C
	update refs/heads/symref2 $B
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err &&
	test_grep "fatal: multiple updates for '\''refs/heads/target2'\'' (including one via symref .refs/heads/symref2.) are not allowed" err &&
	echo "refs/heads/target2" >expect &&
	git symbolic-ref refs/heads/symref2 >actual &&
	test_cmp expect actual &&
	echo "$A" >expect &&
	git rev-parse refs/heads/target2 >actual &&
	test_cmp expect actual
'

test_expect_success ULIMIT_FILE_DESCRIPTORS 'large transaction creating branches does not burst open file limit' '
(
	for i in $(test_seq 33)
	do
		echo "create refs/heads/$i HEAD" || exit 1
	done >large_input &&
	run_with_limited_open_files git update-ref --stdin <large_input &&
	git rev-parse --verify -q refs/heads/33
)
'

test_expect_success ULIMIT_FILE_DESCRIPTORS 'large transaction deleting branches does not burst open file limit' '
(
	for i in $(test_seq 33)
	do
		echo "delete refs/heads/$i HEAD" || exit 1
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
	git show-ref >actual &&
	! grep 'refs/bisect' actual &&
	test_must_fail git rev-parse refs/bisect/something &&
	git update-ref refs/bisect/something HEAD &&
	git rev-parse refs/bisect/something >main-head &&
	! test_cmp main-head worktree-head
'

test_expect_success 'transaction handles empty commit' '
	cat >stdin <<-EOF &&
	start
	prepare
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start prepare commit >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction handles empty commit with missing prepare' '
	cat >stdin <<-EOF &&
	start
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start commit >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction handles sole commit' '
	cat >stdin <<-EOF &&
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" commit >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction handles empty abort' '
	cat >stdin <<-EOF &&
	start
	prepare
	abort
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start prepare abort >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction exits on multiple aborts' '
	cat >stdin <<-EOF &&
	abort
	abort
	EOF
	test_must_fail git update-ref --stdin <stdin >actual 2>err &&
	printf "%s: ok\n" abort >expect &&
	test_cmp expect actual &&
	grep "fatal: transaction is closed" err
'

test_expect_success 'transaction exits on start after prepare' '
	cat >stdin <<-EOF &&
	prepare
	start
	EOF
	test_must_fail git update-ref --stdin <stdin 2>err >actual &&
	printf "%s: ok\n" prepare >expect &&
	test_cmp expect actual &&
	grep "fatal: prepared transactions can only be closed" err
'

test_expect_success 'transaction handles empty abort with missing prepare' '
	cat >stdin <<-EOF &&
	start
	abort
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start abort >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction handles sole abort' '
	cat >stdin <<-EOF &&
	abort
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" abort >expect &&
	test_cmp expect actual
'

test_expect_success 'transaction can handle commit' '
	cat >stdin <<-EOF &&
	start
	create $a HEAD
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start commit >expect &&
	test_cmp expect actual &&
	git rev-parse HEAD >expect &&
	git rev-parse $a >actual &&
	test_cmp expect actual
'

test_expect_success 'transaction can handle abort' '
	cat >stdin <<-EOF &&
	start
	create $b HEAD
	abort
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start abort >expect &&
	test_cmp expect actual &&
	test_must_fail git show-ref --verify -q $b
'

test_expect_success 'transaction aborts by default' '
	cat >stdin <<-EOF &&
	start
	create $b HEAD
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start >expect &&
	test_cmp expect actual &&
	test_must_fail git show-ref --verify -q $b
'

test_expect_success 'transaction with prepare aborts by default' '
	cat >stdin <<-EOF &&
	start
	create $b HEAD
	prepare
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start prepare >expect &&
	test_cmp expect actual &&
	test_must_fail git show-ref --verify -q $b
'

test_expect_success 'transaction can commit multiple times' '
	cat >stdin <<-EOF &&
	start
	create refs/heads/branch-1 $A
	commit
	start
	create refs/heads/branch-2 $B
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start commit start commit >expect &&
	test_cmp expect actual &&
	echo "$A" >expect &&
	git rev-parse refs/heads/branch-1 >actual &&
	test_cmp expect actual &&
	echo "$B" >expect &&
	git rev-parse refs/heads/branch-2 >actual &&
	test_cmp expect actual
'

test_expect_success 'transaction can create and delete' '
	cat >stdin <<-EOF &&
	start
	create refs/heads/create-and-delete $A
	commit
	start
	delete refs/heads/create-and-delete $A
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start commit start commit >expect &&
	test_cmp expect actual &&
	test_must_fail git show-ref --verify refs/heads/create-and-delete
'

test_expect_success 'transaction can commit after abort' '
	cat >stdin <<-EOF &&
	start
	create refs/heads/abort $A
	abort
	start
	create refs/heads/abort $A
	commit
	EOF
	git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start abort start commit >expect &&
	echo "$A" >expect &&
	git rev-parse refs/heads/abort >actual &&
	test_cmp expect actual
'

test_expect_success 'transaction cannot restart ongoing transaction' '
	cat >stdin <<-EOF &&
	start
	create refs/heads/restart $A
	start
	commit
	EOF
	test_must_fail git update-ref --stdin <stdin >actual &&
	printf "%s: ok\n" start >expect &&
	test_cmp expect actual &&
	test_must_fail git show-ref --verify refs/heads/restart
'

test_expect_success PIPE 'transaction flushes status updates' '
	mkfifo in out &&
	(git update-ref --stdin <in >out &) &&

	exec 9>in &&
	exec 8<out &&
	test_when_finished "exec 9>&-" &&
	test_when_finished "exec 8<&-" &&

	echo "start" >&9 &&
	echo "start: ok" >expected &&
	read line <&8 &&
	echo "$line" >actual &&
	test_cmp expected actual &&

	echo "create refs/heads/flush $A" >&9 &&

	echo prepare >&9 &&
	echo "prepare: ok" >expected &&
	read line <&8 &&
	echo "$line" >actual &&
	test_cmp expected actual &&

	# This must now fail given that we have locked the ref.
	test_must_fail git update-ref refs/heads/flush $B 2>stderr &&
	grep "fatal: update_ref failed for ref ${SQ}refs/heads/flush${SQ}: cannot lock ref" stderr &&

	echo commit >&9 &&
	echo "commit: ok" >expected &&
	read line <&8 &&
	echo "$line" >actual &&
	test_cmp expected actual
'

format_command () {
	if test "$1" = "-z"
	then
		shift
		printf "$F" "$@"
	else
		echo "$@"
	fi
}

for type in "" "-z"
do

	test_expect_success "stdin $type symref-verify fails without --no-deref" '
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-verify refs/heads/symref" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type <stdin 2>err &&
		grep "fatal: symref-verify: cannot operate with deref mode" err
	'

	test_expect_success "stdin $type symref-verify fails with too many arguments" '
		format_command $type "symref-verify refs/heads/symref" "$a" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err  &&
		if test "$type" = "-z"
		then
			grep "fatal: unknown command: $a" err
		else
			grep "fatal: symref-verify refs/heads/symref: extra input:  $a" err
		fi
	'

	test_expect_success "stdin $type symref-verify succeeds for correct value" '
		git symbolic-ref refs/heads/symref >expect &&
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >before &&
		format_command $type "symref-verify refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual &&
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >after &&
		test_cmp before after
	'

	test_expect_success "stdin $type symref-verify fails with no value" '
		git symbolic-ref refs/heads/symref >expect &&
		format_command $type "symref-verify refs/heads/symref" "" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin
	'

	test_expect_success "stdin $type symref-verify succeeds for dangling reference" '
		test_when_finished "git symbolic-ref -d refs/heads/symref2" &&
		test_must_fail git symbolic-ref refs/heads/nonexistent &&
		git symbolic-ref refs/heads/symref2 refs/heads/nonexistent &&
		format_command $type "symref-verify refs/heads/symref2" "refs/heads/nonexistent" >stdin &&
		git update-ref --stdin $type --no-deref <stdin
	'

	test_expect_success "stdin $type symref-verify fails for missing reference" '
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >before &&
		format_command $type "symref-verify refs/heads/missing" "refs/heads/unknown" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: cannot lock ref ${SQ}refs/heads/missing${SQ}: unable to resolve reference ${SQ}refs/heads/missing${SQ}" err &&
		test_must_fail git rev-parse --verify -q refs/heads/missing &&
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >after &&
		test_cmp before after
	'

	test_expect_success "stdin $type symref-verify fails for wrong value" '
		git symbolic-ref refs/heads/symref >expect &&
		format_command $type "symref-verify refs/heads/symref" "$b" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-verify fails for mistaken null value" '
		git symbolic-ref refs/heads/symref >expect &&
		format_command $type "symref-verify refs/heads/symref" "$Z" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-delete fails without --no-deref" '
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-delete refs/heads/symref" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type <stdin 2>err &&
		grep "fatal: symref-delete: cannot operate with deref mode" err
	'

	test_expect_success "stdin $type symref-delete fails with no ref" '
		format_command $type "symref-delete " >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: symref-delete: missing <ref>" err
	'

	test_expect_success "stdin $type symref-delete fails deleting regular ref" '
		test_when_finished "git update-ref -d refs/heads/regularref" &&
		git update-ref refs/heads/regularref $a &&
		format_command $type "symref-delete refs/heads/regularref" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: cannot lock ref ${SQ}refs/heads/regularref${SQ}: expected symref with target ${SQ}$a${SQ}: but is a regular ref" err
	'

	test_expect_success "stdin $type symref-delete fails with too many arguments" '
		format_command $type "symref-delete refs/heads/symref" "$a" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		if test "$type" = "-z"
		then
			grep "fatal: unknown command: $a" err
		else
			grep "fatal: symref-delete refs/heads/symref: extra input:  $a" err
		fi
	'

	test_expect_success "stdin $type symref-delete fails with wrong old value" '
		format_command $type "symref-delete refs/heads/symref" "$m" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: verifying symref target: ${SQ}refs/heads/symref${SQ}: is at $a but expected refs/heads/main" err &&
		git symbolic-ref refs/heads/symref >expect &&
		echo $a >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-delete works with right old value" '
		format_command $type "symref-delete refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		test_must_fail git rev-parse --verify -q refs/heads/symref
	'

	test_expect_success "stdin $type symref-delete works with empty old value" '
		git symbolic-ref refs/heads/symref $a >stdin &&
		format_command $type "symref-delete refs/heads/symref" "" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		test_must_fail git rev-parse --verify -q $b
	'

	test_expect_success "stdin $type symref-delete succeeds for dangling reference" '
		test_must_fail git symbolic-ref refs/heads/nonexistent &&
		git symbolic-ref refs/heads/symref2 refs/heads/nonexistent &&
		format_command $type "symref-delete refs/heads/symref2" "refs/heads/nonexistent" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		test_must_fail git symbolic-ref -d refs/heads/symref2
	'

	test_expect_success "stdin $type symref-delete deletes regular ref without target" '
		git update-ref refs/heads/regularref $a &&
		format_command $type "symref-delete refs/heads/regularref" >stdin &&
		git update-ref --stdin $type --no-deref <stdin
	'

	test_expect_success "stdin $type symref-create fails with too many arguments" '
		format_command $type "symref-create refs/heads/symref" "$a" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		if test "$type" = "-z"
		then
			grep "fatal: unknown command: $a" err
		else
			grep "fatal: symref-create refs/heads/symref: extra input:  $a" err
		fi
	'

	test_expect_success "stdin $type symref-create fails with no target" '
		format_command $type "symref-create refs/heads/symref" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin
	'

	test_expect_success "stdin $type symref-create fails with empty target" '
		format_command $type "symref-create refs/heads/symref" "" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin
	'

	test_expect_success "stdin $type symref-create works" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-create refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >expect &&
		echo $a >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-create works with --no-deref" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-create refs/heads/symref" "$a" &&
		git update-ref --stdin $type <stdin 2>err
	'

	test_expect_success "stdin $type create dangling symref ref works" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-create refs/heads/symref" "refs/heads/unknown" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >expect &&
		echo refs/heads/unknown >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-create does not create reflogs by default" '
		test_when_finished "git symbolic-ref -d refs/symref" &&
		format_command $type "symref-create refs/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/symref >expect &&
		echo $a >actual &&
		test_cmp expect actual &&
		test_must_fail git reflog exists refs/symref
	'

	test_expect_success "stdin $type symref-create reflogs with --create-reflog" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-create refs/heads/symref" "$a" >stdin &&
		git update-ref --create-reflog --stdin $type --no-deref <stdin &&
		git symbolic-ref refs/heads/symref >expect &&
		echo $a >actual &&
		test_cmp expect actual &&
		git reflog exists refs/heads/symref
	'

	test_expect_success "stdin $type symref-update fails with too many arguments" '
		format_command $type "symref-update refs/heads/symref" "$a" "ref" "$a" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		if test "$type" = "-z"
		then
			grep "fatal: unknown command: $a" err
		else
			grep "fatal: symref-update refs/heads/symref: extra input:  $a" err
		fi
	'

	test_expect_success "stdin $type symref-update fails with wrong old value argument" '
		format_command $type "symref-update refs/heads/symref" "$a" "foo" "$a" "$a" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: symref-update refs/heads/symref: invalid arg ${SQ}foo${SQ} for old value" err
	'

	test_expect_success "stdin $type symref-update creates with zero old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-update refs/heads/symref" "$a" "oid" "$Z" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $a >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update creates with no old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-update refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $a >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update creates dangling" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		test_must_fail git rev-parse refs/heads/nonexistent &&
		format_command $type "symref-update refs/heads/symref" "refs/heads/nonexistent" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo refs/heads/nonexistent >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update fails with wrong old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-update refs/heads/symref" "$m" "ref" "$b" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin 2>err &&
		grep "fatal: verifying symref target: ${SQ}refs/heads/symref${SQ}: is at $a but expected $b" err &&
		test_must_fail git rev-parse --verify -q $c
	'

	test_expect_success "stdin $type symref-update updates dangling ref" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		test_must_fail git rev-parse refs/heads/nonexistent &&
		git symbolic-ref refs/heads/symref refs/heads/nonexistent &&
		format_command $type "symref-update refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $a >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update updates dangling ref with old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		test_must_fail git rev-parse refs/heads/nonexistent &&
		git symbolic-ref refs/heads/symref refs/heads/nonexistent &&
		format_command $type "symref-update refs/heads/symref" "$a" "ref" "refs/heads/nonexistent" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $a >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update fails update dangling ref with wrong old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		test_must_fail git rev-parse refs/heads/nonexistent &&
		git symbolic-ref refs/heads/symref refs/heads/nonexistent &&
		format_command $type "symref-update refs/heads/symref" "$a" "ref" "refs/heads/wrongref" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin &&
		echo refs/heads/nonexistent >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update works with right old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-update refs/heads/symref" "$m" "ref" "$a" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $m >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update works with no old value" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-update refs/heads/symref" "$m" >stdin &&
		git update-ref --stdin $type --no-deref <stdin &&
		echo $m >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update fails with empty old ref-target" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		git symbolic-ref refs/heads/symref $a &&
		format_command $type "symref-update refs/heads/symref" "$m" "ref" "" >stdin &&
		test_must_fail git update-ref --stdin $type --no-deref <stdin &&
		echo $a >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update creates (with deref)" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		format_command $type "symref-update refs/heads/symref" "$a" >stdin &&
		git update-ref --stdin $type <stdin &&
		echo $a >expect &&
		git symbolic-ref --no-recurse refs/heads/symref >actual &&
		test_cmp expect actual &&
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >actual &&
		grep "$Z $(git rev-parse $a)" actual
	'

	test_expect_success "stdin $type symref-update regular ref to symref with correct old-oid" '
		test_when_finished "git symbolic-ref -d --no-recurse refs/heads/regularref" &&
		git update-ref --no-deref refs/heads/regularref $a &&
		format_command $type "symref-update refs/heads/regularref" "$a" "oid" "$(git rev-parse $a)" >stdin &&
		git update-ref --stdin $type <stdin &&
		echo $a >expect &&
		git symbolic-ref --no-recurse refs/heads/regularref >actual &&
		test_cmp expect actual &&
		test-tool ref-store main for-each-reflog-ent refs/heads/regularref >actual &&
		grep "$(git rev-parse $a) $(git rev-parse $a)" actual
	'

	test_expect_success "stdin $type symref-update regular ref to symref fails with wrong old-oid" '
		test_when_finished "git update-ref -d refs/heads/regularref" &&
		git update-ref --no-deref refs/heads/regularref $a &&
		format_command $type "symref-update refs/heads/regularref" "$a" "oid" "$(git rev-parse refs/heads/target2)" >stdin &&
		test_must_fail git update-ref --stdin $type <stdin 2>err &&
		grep "fatal: cannot lock ref ${SQ}refs/heads/regularref${SQ}: is at $(git rev-parse $a) but expected $(git rev-parse refs/heads/target2)" err &&
		echo $(git rev-parse $a) >expect &&
		git rev-parse refs/heads/regularref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update regular ref to symref fails with invalid old-oid" '
		test_when_finished "git update-ref -d refs/heads/regularref" &&
		git update-ref --no-deref refs/heads/regularref $a &&
		format_command $type "symref-update refs/heads/regularref" "$a" "oid" "not-a-ref-oid" >stdin &&
		test_must_fail git update-ref --stdin $type <stdin 2>err &&
		grep "fatal: symref-update refs/heads/regularref: invalid oid: not-a-ref-oid" err &&
		echo $(git rev-parse $a) >expect &&
		git rev-parse refs/heads/regularref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update existing symref with zero old-oid" '
		test_when_finished "git symbolic-ref -d --no-recurse refs/heads/symref" &&
		git symbolic-ref refs/heads/symref refs/heads/target2 &&
		format_command $type "symref-update refs/heads/symref" "$a" "oid" "$Z" >stdin &&
		test_must_fail git update-ref --stdin $type <stdin 2>err &&
		grep "fatal: cannot lock ref ${SQ}refs/heads/symref${SQ}: reference already exists" err &&
		echo refs/heads/target2 >expect &&
		git symbolic-ref refs/heads/symref >actual &&
		test_cmp expect actual
	'

	test_expect_success "stdin $type symref-update regular ref to symref (with deref)" '
		test_when_finished "git symbolic-ref -d refs/heads/symref" &&
		test_when_finished "git update-ref -d --no-deref refs/heads/symref2" &&
		git update-ref refs/heads/symref2 $a &&
		git symbolic-ref --no-recurse refs/heads/symref refs/heads/symref2 &&
		format_command $type "symref-update refs/heads/symref" "$a" >stdin &&
		git update-ref $type --stdin <stdin &&
		echo $a >expect &&
		git symbolic-ref --no-recurse refs/heads/symref2 >actual &&
		test_cmp expect actual &&
		echo refs/heads/symref2 >expect &&
		git symbolic-ref --no-recurse refs/heads/symref >actual &&
		test_cmp expect actual &&
		test-tool ref-store main for-each-reflog-ent refs/heads/symref >actual &&
		grep "$(git rev-parse $a) $(git rev-parse $a)" actual
	'

	test_expect_success "stdin $type symref-update regular ref to symref" '
		test_when_finished "git symbolic-ref -d --no-recurse refs/heads/regularref" &&
		git update-ref --no-deref refs/heads/regularref $a &&
		format_command $type "symref-update refs/heads/regularref" "$a" >stdin &&
		git update-ref $type --stdin <stdin &&
		echo $a >expect &&
		git symbolic-ref --no-recurse refs/heads/regularref >actual &&
		test_cmp expect actual &&
		test-tool ref-store main for-each-reflog-ent refs/heads/regularref >actual &&
		grep "$(git rev-parse $a) $(git rev-parse $a)" actual
	'

done

test_done
