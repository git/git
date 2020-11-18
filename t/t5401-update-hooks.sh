#!/bin/sh
#
# Copyright (c) 2006 Shawn O. Pearce
#

test_description='Test the update hook infrastructure.'
. ./test-lib.sh

test_expect_success setup '
	echo This is a test. >a &&
	git update-index --add a &&
	tree0=$(git write-tree) &&
	commit0=$(echo setup | git commit-tree $tree0) &&
	echo We hope it works. >a &&
	git update-index a &&
	tree1=$(git write-tree) &&
	commit1=$(echo modify | git commit-tree $tree1 -p $commit0) &&
	git update-ref refs/heads/main $commit0 &&
	git update-ref refs/heads/tofail $commit1 &&
	git clone --bare ./. victim.git &&
	GIT_DIR=victim.git git update-ref refs/heads/tofail $commit1 &&
	git update-ref refs/heads/main $commit1 &&
	git update-ref refs/heads/tofail $commit0
'

cat >victim.git/hooks/pre-receive <<'EOF'
#!/bin/sh
printf %s "$@" >>$GIT_DIR/pre-receive.args
cat - >$GIT_DIR/pre-receive.stdin
echo STDOUT pre-receive
echo STDERR pre-receive >&2
EOF
chmod u+x victim.git/hooks/pre-receive

cat >victim.git/hooks/update <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/update.args
read x; printf %s "$x" >$GIT_DIR/update.stdin
echo STDOUT update $1
echo STDERR update $1 >&2
test "$1" = refs/heads/main || exit
EOF
chmod u+x victim.git/hooks/update

cat >victim.git/hooks/post-receive <<'EOF'
#!/bin/sh
printf %s "$@" >>$GIT_DIR/post-receive.args
cat - >$GIT_DIR/post-receive.stdin
echo STDOUT post-receive
echo STDERR post-receive >&2
EOF
chmod u+x victim.git/hooks/post-receive

cat >victim.git/hooks/post-update <<'EOF'
#!/bin/sh
echo "$@" >>$GIT_DIR/post-update.args
read x; printf %s "$x" >$GIT_DIR/post-update.stdin
echo STDOUT post-update
echo STDERR post-update >&2
EOF
chmod u+x victim.git/hooks/post-update

test_expect_success push '
	test_must_fail git send-pack --force ./victim.git \
		main tofail >send.out 2>send.err
'

test_expect_success 'updated as expected' '
	test $(GIT_DIR=victim.git git rev-parse main) = $commit1 &&
	test $(GIT_DIR=victim.git git rev-parse tofail) = $commit1
'

test_expect_success 'hooks ran' '
	test -f victim.git/pre-receive.args &&
	test -f victim.git/pre-receive.stdin &&
	test -f victim.git/update.args &&
	test -f victim.git/update.stdin &&
	test -f victim.git/post-receive.args &&
	test -f victim.git/post-receive.stdin &&
	test -f victim.git/post-update.args &&
	test -f victim.git/post-update.stdin
'

test_expect_success 'pre-receive hook input' '
	(echo $commit0 $commit1 refs/heads/main &&
	 echo $commit1 $commit0 refs/heads/tofail
	) | test_cmp - victim.git/pre-receive.stdin
'

test_expect_success 'update hook arguments' '
	(echo refs/heads/main $commit0 $commit1 &&
	 echo refs/heads/tofail $commit1 $commit0
	) | test_cmp - victim.git/update.args
'

test_expect_success 'post-receive hook input' '
	echo $commit0 $commit1 refs/heads/main |
	test_cmp - victim.git/post-receive.stdin
'

test_expect_success 'post-update hook arguments' '
	echo refs/heads/main |
	test_cmp - victim.git/post-update.args
'

test_expect_success 'all hook stdin is /dev/null' '
	test_must_be_empty victim.git/update.stdin &&
	test_must_be_empty victim.git/post-update.stdin
'

test_expect_success 'all *-receive hook args are empty' '
	test_must_be_empty victim.git/pre-receive.args &&
	test_must_be_empty victim.git/post-receive.args
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
	write_script victim.git/hooks/pre-receive <<-\EOF &&
	exit 0
	EOF
	rm -f victim.git/hooks/update victim.git/hooks/post-update &&

	for v in $(test_seq 100 999)
	do
		git branch branch_$v main || return
	done &&
	git push ./victim.git "+refs/heads/*:refs/heads/*"
'

test_done
