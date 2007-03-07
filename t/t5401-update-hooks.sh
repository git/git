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
	echo We hope it works. >a &&
	git-update-index a &&
	tree1=$(git-write-tree) &&
	commit1=$(echo modify | git-commit-tree $tree1 -p $commit0) &&
	git-update-ref refs/heads/master $commit0 &&
	git-update-ref refs/heads/tofail $commit1 &&
	git-clone ./. victim &&
	GIT_DIR=victim/.git git-update-ref refs/heads/tofail $commit1 &&
	git-update-ref refs/heads/master $commit1 &&
	git-update-ref refs/heads/tofail $commit0
'

cat >victim/.git/hooks/pre-receive <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/pre-receive.args
read x; printf "$x" >$GIT_DIR/pre-receive.stdin
echo STDOUT pre-receive
echo STDERR pre-receive >&2
EOF
chmod u+x victim/.git/hooks/pre-receive

cat >victim/.git/hooks/update <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/update.args
read x; printf "$x" >$GIT_DIR/update.stdin
echo STDOUT update $1
echo STDERR update $1 >&2
test "$1" = refs/heads/master || exit
EOF
chmod u+x victim/.git/hooks/update

cat >victim/.git/hooks/post-receive <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/post-receive.args
read x; printf "$x" >$GIT_DIR/post-receive.stdin
echo STDOUT post-receive
echo STDERR post-receive >&2
EOF
chmod u+x victim/.git/hooks/post-receive

cat >victim/.git/hooks/post-update <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/post-update.args
read x; printf "$x" >$GIT_DIR/post-update.stdin
echo STDOUT post-update
echo STDERR post-update >&2
EOF
chmod u+x victim/.git/hooks/post-update

test_expect_failure push '
	git-send-pack --force ./victim/.git master tofail >send.out 2>send.err
'

test_expect_success 'updated as expected' '
	test $(GIT_DIR=victim/.git git-rev-parse master) = $commit1 &&
	test $(GIT_DIR=victim/.git git-rev-parse tofail) = $commit1
'

test_expect_success 'hooks ran' '
	test -f victim/.git/pre-receive.args &&
	test -f victim/.git/pre-receive.stdin &&
	test -f victim/.git/update.args &&
	test -f victim/.git/update.stdin &&
	test -f victim/.git/post-receive.args &&
	test -f victim/.git/post-receive.stdin &&
	test -f victim/.git/post-update.args &&
	test -f victim/.git/post-update.stdin
'

test_expect_success 'pre-receive hook arguments' '
	echo \
	 refs/heads/master $commit0 $commit1 \
	 refs/heads/tofail $commit1 $commit0 \
	| diff - victim/.git/pre-receive.args
'

test_expect_success 'update hook arguments' '
	(echo refs/heads/master $commit0 $commit1;
	 echo refs/heads/tofail $commit1 $commit0
	) | diff - victim/.git/update.args
'

test_expect_success 'post-receive hook arguments' '
	echo refs/heads/master $commit0 $commit1 |
	diff - victim/.git/post-receive.args
'

test_expect_success 'post-update hook arguments' '
	echo refs/heads/master |
	diff -u - victim/.git/post-update.args
'

test_expect_success 'all hook stdin is /dev/null' '
	! test -s victim/.git/pre-receive.stdin &&
	! test -s victim/.git/update.stdin &&
	! test -s victim/.git/post-receive.stdin &&
	! test -s victim/.git/post-update.stdin
'

test_expect_failure 'send-pack produced no output' '
	test -s send.out
'

cat <<EOF >expect
STDOUT pre-receive
STDERR pre-receive
STDOUT update refs/heads/master
STDERR update refs/heads/master
STDOUT update refs/heads/tofail
STDERR update refs/heads/tofail
STDOUT post-receive
STDERR post-receive
STDOUT post-update
STDERR post-update
EOF
test_expect_success 'send-pack stderr contains hook messages' '
	egrep ^STD send.err >actual &&
	diff - actual <expect
'

test_done
