#!/bin/sh
#
# Copyright (c) 2006 Shawn O. Pearce
#

test_description='Test the update hook infrastructure.'
. ./test-lib.sh

test_expect_success setup '
	echo This is a test. >a &&
	but update-index --add a &&
	tree0=$(but write-tree) &&
	cummit0=$(echo setup | but cummit-tree $tree0) &&
	echo We hope it works. >a &&
	but update-index a &&
	tree1=$(but write-tree) &&
	cummit1=$(echo modify | but cummit-tree $tree1 -p $cummit0) &&
	but update-ref refs/heads/main $cummit0 &&
	but update-ref refs/heads/tofail $cummit1 &&
	but clone --bare ./. victim.but &&
	GIT_DIR=victim.but but update-ref refs/heads/tofail $cummit1 &&
	but update-ref refs/heads/main $cummit1 &&
	but update-ref refs/heads/tofail $cummit0 &&

	test_hook --setup -C victim.but pre-receive <<-\EOF &&
	printf %s "$@" >>$GIT_DIR/pre-receive.args
	cat - >$GIT_DIR/pre-receive.stdin
	echo STDOUT pre-receive
	echo STDERR pre-receive >&2
	EOF

	test_hook --setup -C victim.but update <<-\EOF &&
	echo "$@" >>$GIT_DIR/update.args
	read x; printf %s "$x" >$GIT_DIR/update.stdin
	echo STDOUT update $1
	echo STDERR update $1 >&2
	test "$1" = refs/heads/main || exit
	EOF

	test_hook --setup -C victim.but post-receive <<-\EOF &&
	printf %s "$@" >>$GIT_DIR/post-receive.args
	cat - >$GIT_DIR/post-receive.stdin
	echo STDOUT post-receive
	echo STDERR post-receive >&2
	EOF

	test_hook --setup -C victim.but post-update <<-\EOF
	echo "$@" >>$GIT_DIR/post-update.args
	read x; printf %s "$x" >$GIT_DIR/post-update.stdin
	echo STDOUT post-update
	echo STDERR post-update >&2
	EOF
'

test_expect_success push '
	test_must_fail but send-pack --force ./victim.but \
		main tofail >send.out 2>send.err
'

test_expect_success 'updated as expected' '
	test $(GIT_DIR=victim.but but rev-parse main) = $cummit1 &&
	test $(GIT_DIR=victim.but but rev-parse tofail) = $cummit1
'

test_expect_success 'hooks ran' '
	test -f victim.but/pre-receive.args &&
	test -f victim.but/pre-receive.stdin &&
	test -f victim.but/update.args &&
	test -f victim.but/update.stdin &&
	test -f victim.but/post-receive.args &&
	test -f victim.but/post-receive.stdin &&
	test -f victim.but/post-update.args &&
	test -f victim.but/post-update.stdin
'

test_expect_success 'pre-receive hook input' '
	(echo $cummit0 $cummit1 refs/heads/main &&
	 echo $cummit1 $cummit0 refs/heads/tofail
	) | test_cmp - victim.but/pre-receive.stdin
'

test_expect_success 'update hook arguments' '
	(echo refs/heads/main $cummit0 $cummit1 &&
	 echo refs/heads/tofail $cummit1 $cummit0
	) | test_cmp - victim.but/update.args
'

test_expect_success 'post-receive hook input' '
	echo $cummit0 $cummit1 refs/heads/main |
	test_cmp - victim.but/post-receive.stdin
'

test_expect_success 'post-update hook arguments' '
	echo refs/heads/main |
	test_cmp - victim.but/post-update.args
'

test_expect_success 'all hook stdin is /dev/null' '
	test_must_be_empty victim.but/update.stdin &&
	test_must_be_empty victim.but/post-update.stdin
'

test_expect_success 'all *-receive hook args are empty' '
	test_must_be_empty victim.but/pre-receive.args &&
	test_must_be_empty victim.but/post-receive.args
'

test_expect_success 'send-pack produced no output' '
	test_must_be_empty send.out
'

cat <<EOF >expect
remote: STDOUT pre-receive
remote: STDERR pre-receive
remote: STDOUT update refs/heads/main
remote: STDERR update refs/heads/main
remote: STDOUT update refs/heads/tofail
remote: STDERR update refs/heads/tofail
remote: error: hook declined to update refs/heads/tofail
remote: STDOUT post-receive
remote: STDERR post-receive
remote: STDOUT post-update
remote: STDERR post-update
EOF
test_expect_success 'send-pack stderr contains hook messages' '
	grep ^remote: send.err | sed "s/ *\$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'pre-receive hook that forgets to read its input' '
	test_hook --clobber -C victim.but pre-receive <<-\EOF &&
	exit 0
	EOF
	rm -f victim.but/hooks/update victim.but/hooks/post-update &&

	for v in $(test_seq 100 999)
	do
		but branch branch_$v main || return
	done &&
	but push ./victim.but "+refs/heads/*:refs/heads/*"
'

test_done
