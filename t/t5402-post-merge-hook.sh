#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-merge hook.'
. ./test-lib.sh

test_expect_success setup '
	echo Data for commit0. >a &&
	git update-index --add a &&
	tree0=$(git write-tree) &&
	commit0=$(echo setup | git commit-tree $tree0) &&
	echo Changed data for commit1. >a &&
	git update-index a &&
	tree1=$(git write-tree) &&
	commit1=$(echo modify | git commit-tree $tree1 -p $commit0) &&
        git update-ref refs/heads/master $commit0 &&
	git-clone ./. clone1 &&
	GIT_DIR=clone1/.git git update-index --add a &&
	git-clone ./. clone2 &&
	GIT_DIR=clone2/.git git update-index --add a
'

for clone in 1 2; do
    cat >clone${clone}/.git/hooks/post-merge <<'EOF'
#!/bin/sh
echo $@ >> $GIT_DIR/post-merge.args
EOF
    chmod u+x clone${clone}/.git/hooks/post-merge
done

test_expect_failure 'post-merge does not run for up-to-date ' '
        GIT_DIR=clone1/.git git merge $commit0 &&
	test -e clone1/.git/post-merge.args
'

test_expect_success 'post-merge runs as expected ' '
        GIT_DIR=clone1/.git git merge $commit1 &&
	test -e clone1/.git/post-merge.args
'

test_expect_success 'post-merge from normal merge receives the right argument ' '
        grep 0 clone1/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge runs as expected ' '
        GIT_DIR=clone2/.git git merge --squash $commit1 &&
	test -e clone2/.git/post-merge.args
'

test_expect_success 'post-merge from squash merge receives the right argument ' '
        grep 1 clone2/.git/post-merge.args
'

test_done
