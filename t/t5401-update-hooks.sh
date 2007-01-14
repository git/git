#!/bin/sh
#
# Copyright (c) 2006 Shawn O. Pearce
#

test_description='Test the update hook infrastructure.'
. ./test-lib.sh

test_expect_success setup '
	echo This is a test. >a &&
	git-update-index --add a &&
	tree0=$(git-write-tree) &&
	commit0=$(echo setup | git-commit-tree $tree0) &&
	git-update-ref HEAD $commit0 &&
	git-clone ./. victim &&
	echo We hope it works. >a &&
	git-update-index a &&
	tree1=$(git-write-tree) &&
	commit1=$(echo modify | git-commit-tree $tree1 -p $commit0) &&
	git-update-ref HEAD $commit1
'

cat >victim/.git/hooks/update <<'EOF'
#!/bin/sh
echo "$@" >$GIT_DIR/update.args
read x; echo -n "$x" >$GIT_DIR/update.stdin
echo STDOUT update
echo STDERR update >&2
EOF
chmod u+x victim/.git/hooks/update

cat >victim/.git/hooks/post-update <<'EOF'
#!/bin/sh
echo "$@" >$GIT_DIR/post-update.args
read x; echo -n "$x" >$GIT_DIR/post-update.stdin
echo STDOUT post-update
echo STDERR post-update >&2
EOF
chmod u+x victim/.git/hooks/post-update

test_expect_success push '
	git-send-pack ./victim/.git/ master >send.out 2>send.err
'

test_expect_success 'hooks ran' '
	test -f victim/.git/update.args &&
	test -f victim/.git/update.stdin &&
	test -f victim/.git/post-update.args &&
	test -f victim/.git/post-update.stdin
'

test_expect_success 'update hook arguments' '
	echo refs/heads/master $commit0 $commit1 |
	diff -u - victim/.git/update.args
'

test_expect_success 'post-update hook arguments' '
	echo refs/heads/master |
	diff -u - victim/.git/post-update.args
'

test_expect_failure 'update hook stdin is /dev/null' '
	test -s victim/.git/update.stdin
'

test_expect_failure 'post-update hook stdin is /dev/null' '
	test -s victim/.git/post-update.stdin
'

test_expect_failure 'send-pack produced no output' '
	test -s send.out
'

test_expect_success 'send-pack stderr contains hook messages' '
	grep "STDOUT update" send.err &&
	grep "STDERR update" send.err &&
	grep "STDOUT post-update" send.err &&
	grep "STDERR post-update" send.err
'

test_done
