#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='Test git-update-ref and basic ref logging'
. ./test-lib.sh

Z=0000000000000000000000000000000000000000
A=1111111111111111111111111111111111111111
B=2222222222222222222222222222222222222222
m=refs/heads/master

test_expect_success \
	"create $m" \
	'git-update-ref $m $A &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"create $m" \
	'git-update-ref $m $B $A &&
	 test $B = $(cat .git/$m)'
rm -f .git/$m

test_expect_success \
	"create $m (by HEAD)" \
	'git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"create $m (by HEAD)" \
	'git-update-ref HEAD $B $A &&
	 test $B = $(cat .git/$m)'
rm -f .git/$m

test_expect_failure \
	'(not) create HEAD with old sha1' \
	'git-update-ref HEAD $A $B'
test_expect_failure \
	"(not) prior created .git/$m" \
	'test -f .git/$m'
rm -f .git/$m

test_expect_success \
	"create HEAD" \
	'git-update-ref HEAD $A'
test_expect_failure \
	'(not) change HEAD with wrong SHA1' \
	'git-update-ref HEAD $B $Z'
test_expect_failure \
	"(not) changed .git/$m" \
	'test $B = $(cat .git/$m)'
rm -f .git/$m

mkdir -p .git/logs/refs/heads
touch .git/logs/refs/heads/master
test_expect_success \
	"create $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
	 git-update-ref HEAD $A -m "Initial Creation" &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"update $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:31" \
	 git-update-ref HEAD $B $A -m "Switch" &&
	 test $B = $(cat .git/$m)'
test_expect_success \
	"set $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:41" \
	 git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150260 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	'diff expect .git/logs/$m'
rm -rf .git/$m .git/logs expect

test_expect_success \
	'enable core.logAllRefUpdates' \
	'git-repo-config core.logAllRefUpdates true &&
	 test true = $(git-repo-config --bool --get core.logAllRefUpdates)'

test_expect_success \
	"create $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:32" \
	 git-update-ref HEAD $A -m "Initial Creation" &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"update $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:33" \
	 git-update-ref HEAD $B $A -m "Switch" &&
	 test $B = $(cat .git/$m)'
test_expect_success \
	"set $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:43" \
	 git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	'diff expect .git/logs/$m'
rm -f .git/$m .git/logs/$m expect

test_done
