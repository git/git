#!/bin/sh
#
# Copyright (c) 2005 Jon Seymour
#

test_description='Test rev-list --merge-order
'
. ./test-lib.sh

function do_commit
{
    git-commit-tree "$@" </dev/null
}

function check_adjacency
{
    read previous
    echo "= $previous"
    while read next
    do
        if ! (git-cat-file commit $previous | grep "^parent $next" >/dev/null)
        then
            echo "^ $next"
        else
            echo "| $next"
        fi
        previous=$next
    done
}

function sed_script
{
   for c in root a0 a1 a2 a3 a4 b1 b2 b3 b4 c1 c2 c3 l0 l1 l2 l3 l4 l5
   do
       echo -n "s/${!c}/$c/;"
   done
}

date >path0
git-update-cache --add path0
tree=$(git-write-tree)
root=$(do_commit $tree 2>/dev/null)
export GIT_COMMITTER_NAME=foobar  # to guarantee that the commit is different
l0=$(do_commit $tree -p $root)
l1=$(do_commit $tree -p $l0)
l2=$(do_commit $tree -p $l1)
a0=$(do_commit $tree -p $l2)
a1=$(do_commit $tree -p $a0)
export GIT_COMMITTER_NAME=foobar2 # to guarantee that the commit is different
b1=$(do_commit $tree -p $a0)
c1=$(do_commit $tree -p $b1)
export GIT_COMMITTER_NAME=foobar3 # to guarantee that the commit is different
b2=$(do_commit $tree -p $b1)
b3=$(do_commit $tree -p $b2)
c2=$(do_commit $tree -p $c1 -p $b2)
c3=$(do_commit $tree -p $c2)
a2=$(do_commit $tree -p $a1)
a3=$(do_commit $tree -p $a2)
b4=$(do_commit $tree -p $b3 -p $a3)
a4=$(do_commit $tree -p $a3 -p $b4 -p $c3)
l3=$(do_commit $tree -p $a4)
l4=$(do_commit $tree -p $l3)
l5=$(do_commit $tree -p $l4)
echo $l5 > .git/HEAD

git-rev-list --merge-order --show-breaks HEAD | sed "$(sed_script)" > actual-merge-order
cat > expected-merge-order <<EOF
= l5
| l4
| l3
= a4
| c3
| c2
| c1
^ b4
| b3
| b2
| b1
^ a3
| a2
| a1
= a0
| l2
| l1
| l0
= root
EOF

git-rev-list HEAD | check_adjacency | sed "$(sed_script)" > actual-default-order
normal_adjacency_count=$(git-rev-list HEAD | check_adjacency | grep -c "\^" | tr -d ' ')
merge_order_adjacency_count=$(git-rev-list --merge-order HEAD | check_adjacency | grep -c "\^" | tr -d ' ')

test_expect_success 'Testing that the rev-list has correct number of entries' '[ $(git-rev-list HEAD | wc -l) -eq 19 ]'
test_expect_success 'Testing that --merge-order produces the correct result' 'diff expected-merge-order actual-merge-order'
test_expect_success 'Testing that --merge-order produces as many or fewer discontinuities' '[ $merge_order_adjacency_count -le $normal_adjacency_count ]'

cat > expected-merge-order-1 <<EOF
c3
c2
c1
b3
b2
b1
a3
a2
a1
a0
l2
l1
l0
root
EOF

git-rev-list --merge-order $a3 $b3 $c3 | sed "$(sed_script)" > actual-merge-order-1
test_expect_success 'Testing multiple heads' 'diff expected-merge-order-1 actual-merge-order-1'

cat > expected-merge-order-2 <<EOF
c3
c2
c1
b3
b2
b1
a3
a2
EOF

git-rev-list --merge-order $a3 $b3 $c3 ^$a1 | sed "$(sed_script)" > actual-merge-order-2
test_expect_success 'Testing stop' 'diff expected-merge-order-2 actual-merge-order-2'

cat > expected-merge-order-3 <<EOF
c3
c2
c1
b3
b2
b1
a3
a2
a1
a0
l2
EOF

git-rev-list --merge-order $a3 $b3 $c3 ^$l1 | sed "$(sed_script)" > actual-merge-order-3
test_expect_success 'Testing stop in linear epoch' 'diff expected-merge-order-3 actual-merge-order-3'

cat > expected-merge-order-4 <<EOF
l5
l4
l3
a4
c3
c2
c1
b4
b3
b2
b1
a3
a2
a1
a0
l2
EOF

git-rev-list --merge-order $l5 ^$l1 | sed "$(sed_script)" > actual-merge-order-4
test_expect_success 'Testing start in linear epoch, stop after non-linear epoch' 'diff expected-merge-order-4 actual-merge-order-4'

git-rev-list --merge-order $l5 $l5 ^$l1 2>/dev/null | sed "$(sed_script)" > actual-merge-order-5
test_expect_success 'Testing duplicated start arguments' 'diff expected-merge-order-4 actual-merge-order-5'

test_expect_success 'Testing exclusion near merge' 'git-rev-list --merge-order $a4 ^$c3 2>/dev/null'

test_done
