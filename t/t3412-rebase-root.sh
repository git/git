#!/bin/sh

test_description='but rebase --root

Tests if but rebase --root --onto <newparent> can rebase the root cummit.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

log_with_names () {
	but rev-list --topo-order --parents --pretty="tformat:%s" HEAD |
	but name-rev --annotate-stdin --name-only --refs=refs/heads/$1
}


test_expect_success 'prepare repository' '
	test_cummit 1 A &&
	test_cummit 2 A &&
	but symbolic-ref HEAD refs/heads/other &&
	rm .but/index &&
	test_cummit 3 B &&
	test_cummit 1b A 1 &&
	test_cummit 4 B
'

test_expect_success 'rebase --root fails with too many args' '
	but checkout -B fail other &&
	test_must_fail but rebase --onto main --root fail fail
'

test_expect_success 'setup pre-rebase hook' '
	test_hook --setup pre-rebase <<-\EOF
	echo "$1,$2" >.but/PRE-REBASE-INPUT
	EOF
'
cat > expect <<EOF
4
3
2
1
EOF

test_expect_success 'rebase --root --onto <newbase>' '
	but checkout -b work other &&
	but rebase --root --onto main &&
	but log --pretty=tformat:"%s" > rebased &&
	test_cmp expect rebased
'

test_expect_success 'pre-rebase got correct input (1)' '
	test "z$(cat .but/PRE-REBASE-INPUT)" = z--root,
'

test_expect_success 'rebase --root --onto <newbase> <branch>' '
	but branch work2 other &&
	but rebase --root --onto main work2 &&
	but log --pretty=tformat:"%s" > rebased2 &&
	test_cmp expect rebased2
'

test_expect_success 'pre-rebase got correct input (2)' '
	test "z$(cat .but/PRE-REBASE-INPUT)" = z--root,work2
'

test_expect_success 'rebase -i --root --onto <newbase>' '
	but checkout -b work3 other &&
	but rebase -i --root --onto main &&
	but log --pretty=tformat:"%s" > rebased3 &&
	test_cmp expect rebased3
'

test_expect_success 'pre-rebase got correct input (3)' '
	test "z$(cat .but/PRE-REBASE-INPUT)" = z--root,
'

test_expect_success 'rebase -i --root --onto <newbase> <branch>' '
	but branch work4 other &&
	but rebase -i --root --onto main work4 &&
	but log --pretty=tformat:"%s" > rebased4 &&
	test_cmp expect rebased4
'

test_expect_success 'pre-rebase got correct input (4)' '
	test "z$(cat .but/PRE-REBASE-INPUT)" = z--root,work4
'

test_expect_success 'set up merge history' '
	but checkout other^ &&
	but checkout -b side &&
	test_cummit 5 C &&
	but checkout other &&
	but merge side
'

cat > expect-side <<'EOF'
cummit work6 work6~1 work6^2
Merge branch 'side' into other
cummit work6^2 work6~2
5
cummit work6~1 work6~2
4
cummit work6~2 work6~3
3
cummit work6~3 work6~4
2
cummit work6~4
1
EOF

test_expect_success 'set up second root and merge' '
	but symbolic-ref HEAD refs/heads/third &&
	rm .but/index &&
	rm A B C &&
	test_cummit 6 D &&
	but checkout other &&
	but merge --allow-unrelated-histories third
'

cat > expect-third <<'EOF'
cummit work7 work7~1 work7^2
Merge branch 'third' into other
cummit work7^2 work7~4
6
cummit work7~1 work7~2 work7~1^2
Merge branch 'side' into other
cummit work7~1^2 work7~3
5
cummit work7~2 work7~3
4
cummit work7~3 work7~4
3
cummit work7~4 work7~5
2
cummit work7~5
1
EOF

test_expect_success 'setup pre-rebase hook that fails' '
	test_hook --setup --clobber pre-rebase <<-\EOF
	false
	EOF
'

test_expect_success 'pre-rebase hook stops rebase' '
	but checkout -b stops1 other &&
	test_must_fail but rebase --root --onto main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/stops1 &&
	test 0 = $(but rev-list other...stops1 | wc -l)
'

test_expect_success 'pre-rebase hook stops rebase -i' '
	but checkout -b stops2 other &&
	test_must_fail but rebase --root --onto main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/stops2 &&
	test 0 = $(but rev-list other...stops2 | wc -l)
'

test_expect_success 'remove pre-rebase hook' '
	rm -f .but/hooks/pre-rebase
'

test_expect_success 'set up a conflict' '
	but checkout main &&
	echo conflict > B &&
	but add B &&
	but cummit -m conflict
'

test_expect_success 'rebase --root with conflict (first part)' '
	but checkout -b conflict1 other &&
	test_must_fail but rebase --root --onto main &&
	but ls-files -u | grep "B$"
'

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	but add B
'

cat > expect-conflict <<EOF
6
5
4
3
conflict
2
1
EOF

test_expect_success 'rebase --root with conflict (second part)' '
	but rebase --continue &&
	but log --pretty=tformat:"%s" > conflict1 &&
	test_cmp expect-conflict conflict1
'

test_expect_success 'rebase -i --root with conflict (first part)' '
	but checkout -b conflict2 other &&
	test_must_fail but rebase -i --root --onto main &&
	but ls-files -u | grep "B$"
'

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	but add B
'

test_expect_success 'rebase -i --root with conflict (second part)' '
	but rebase --continue &&
	but log --pretty=tformat:"%s" > conflict2 &&
	test_cmp expect-conflict conflict2
'

cat >expect-conflict-p <<\EOF
cummit conflict3 conflict3~1 conflict3^2
Merge branch 'third' into other
cummit conflict3^2 conflict3~4
6
cummit conflict3~1 conflict3~2 conflict3~1^2
Merge branch 'side' into other
cummit conflict3~1^2 conflict3~3
5
cummit conflict3~2 conflict3~3
4
cummit conflict3~3 conflict3~4
3
cummit conflict3~4 conflict3~5
conflict
cummit conflict3~5 conflict3~6
2
cummit conflict3~6
1
EOF

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	but add B
'

test_done
