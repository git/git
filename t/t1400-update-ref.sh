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

: a repository with working tree always has reflog these days...
: >.git/logs/refs/heads/master
test_expect_success \
	"create $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
	 git update-ref HEAD '"$A"' -m "Initial Creation" &&
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
	 test "warning: Log .git/logs/'"$m has gap after $gd"'." = "$(cat e)"'
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
	 test "warning: Log .git/logs/'"$m unexpectedly ended on $ld"'." = "$(cat e)"'


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

test_done
