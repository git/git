#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-checkout hook.'
. ./test-lib.sh

test_expect_success setup '
	echo Data for commit0. >a &&
	echo Data for commit0. >b &&
	git update-index --add a &&
	git update-index --add b &&
	tree0=$(git write-tree) &&
	commit0=$(echo setup | git commit-tree $tree0) &&
	git update-ref refs/heads/master $commit0 &&
	git clone ./. clone1 &&
	git clone ./. clone2 &&
	GIT_DIR=clone2/.git git branch new2 &&
	echo Data for commit1. >clone2/b &&
	GIT_DIR=clone2/.git git add clone2/b &&
	GIT_DIR=clone2/.git git commit -m new2
'

for clone in 1 2; do
    cat >clone${clone}/.git/hooks/post-checkout <<'EOF'
#!/bin/sh
echo $@ > $GIT_DIR/post-checkout.args
EOF
    chmod u+x clone${clone}/.git/hooks/post-checkout
done

test_expect_success 'post-checkout runs as expected ' '
	GIT_DIR=clone1/.git git checkout master &&
	test -e clone1/.git/post-checkout.args
'

test_expect_success 'post-checkout receives the right arguments with HEAD unchanged ' '
	old=$(awk "{print \$1}" clone1/.git/post-checkout.args) &&
	new=$(awk "{print \$2}" clone1/.git/post-checkout.args) &&
	flag=$(awk "{print \$3}" clone1/.git/post-checkout.args) &&
	test $old = $new -a $flag = 1
'

test_expect_success 'post-checkout runs as expected ' '
	GIT_DIR=clone1/.git git checkout master &&
	test -e clone1/.git/post-checkout.args
'

test_expect_success 'post-checkout args are correct with git checkout -b ' '
	GIT_DIR=clone1/.git git checkout -b new1 &&
	old=$(awk "{print \$1}" clone1/.git/post-checkout.args) &&
	new=$(awk "{print \$2}" clone1/.git/post-checkout.args) &&
	flag=$(awk "{print \$3}" clone1/.git/post-checkout.args) &&
	test $old = $new -a $flag = 1
'

test_expect_success 'post-checkout receives the right args with HEAD changed ' '
	GIT_DIR=clone2/.git git checkout new2 &&
	old=$(awk "{print \$1}" clone2/.git/post-checkout.args) &&
	new=$(awk "{print \$2}" clone2/.git/post-checkout.args) &&
	flag=$(awk "{print \$3}" clone2/.git/post-checkout.args) &&
	test $old != $new -a $flag = 1
'

test_expect_success 'post-checkout receives the right args when not switching branches ' '
	GIT_DIR=clone2/.git git checkout master b &&
	old=$(awk "{print \$1}" clone2/.git/post-checkout.args) &&
	new=$(awk "{print \$2}" clone2/.git/post-checkout.args) &&
	flag=$(awk "{print \$3}" clone2/.git/post-checkout.args) &&
	test $old = $new -a $flag = 0
'

if test "$(git config --bool core.filemode)" = true; then
mkdir -p templates/hooks
cat >templates/hooks/post-checkout <<'EOF'
#!/bin/sh
echo $@ > $GIT_DIR/post-checkout.args
EOF
chmod +x templates/hooks/post-checkout

test_expect_success 'post-checkout hook is triggered by clone' '
	git clone --template=templates . clone3 &&
	test -f clone3/.git/post-checkout.args
'
fi

test_done
