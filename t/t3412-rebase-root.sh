#!/bin/sh

test_description='git rebase --root

Tests if git rebase --root --onto <newparent> can rebase the root commit.
'
. ./test-lib.sh

log_with_names () {
	git rev-list --topo-order --parents --pretty="tformat:%s" HEAD |
	git name-rev --stdin --name-only --refs=refs/heads/$1
}


test_expect_success 'prepare repository' '
	test_commit 1 A &&
	test_commit 2 A &&
	git symbolic-ref HEAD refs/heads/other &&
	rm .git/index &&
	test_commit 3 B &&
	test_commit 1b A 1 &&
	test_commit 4 B
'

test_expect_success 'rebase --root fails with too many args' '
	git checkout -B fail other &&
	test_must_fail git rebase --onto master --root fail fail
'

test_expect_success 'setup pre-rebase hook' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rebase <<EOF &&
#!$SHELL_PATH
echo "\$1,\$2" >.git/PRE-REBASE-INPUT
EOF
	chmod +x .git/hooks/pre-rebase
'
cat > expect <<EOF
4
3
2
1
EOF

test_expect_success 'rebase --root --onto <newbase>' '
	git checkout -b work other &&
	git rebase --root --onto master &&
	git log --pretty=tformat:"%s" > rebased &&
	test_cmp expect rebased
'

test_expect_success 'pre-rebase got correct input (1)' '
	test "z$(cat .git/PRE-REBASE-INPUT)" = z--root,
'

test_expect_success 'rebase --root --onto <newbase> <branch>' '
	git branch work2 other &&
	git rebase --root --onto master work2 &&
	git log --pretty=tformat:"%s" > rebased2 &&
	test_cmp expect rebased2
'

test_expect_success 'pre-rebase got correct input (2)' '
	test "z$(cat .git/PRE-REBASE-INPUT)" = z--root,work2
'

test_expect_success 'rebase -i --root --onto <newbase>' '
	git checkout -b work3 other &&
	git rebase -i --root --onto master &&
	git log --pretty=tformat:"%s" > rebased3 &&
	test_cmp expect rebased3
'

test_expect_success 'pre-rebase got correct input (3)' '
	test "z$(cat .git/PRE-REBASE-INPUT)" = z--root,
'

test_expect_success 'rebase -i --root --onto <newbase> <branch>' '
	git branch work4 other &&
	git rebase -i --root --onto master work4 &&
	git log --pretty=tformat:"%s" > rebased4 &&
	test_cmp expect rebased4
'

test_expect_success 'pre-rebase got correct input (4)' '
	test "z$(cat .git/PRE-REBASE-INPUT)" = z--root,work4
'

test_expect_success REBASE_P 'rebase -i -p with linear history' '
	git checkout -b work5 other &&
	git rebase -i -p --root --onto master &&
	git log --pretty=tformat:"%s" > rebased5 &&
	test_cmp expect rebased5
'

test_expect_success REBASE_P 'pre-rebase got correct input (5)' '
	test "z$(cat .git/PRE-REBASE-INPUT)" = z--root,
'

test_expect_success 'set up merge history' '
	git checkout other^ &&
	git checkout -b side &&
	test_commit 5 C &&
	git checkout other &&
	git merge side
'

cat > expect-side <<'EOF'
commit work6 work6~1 work6^2
Merge branch 'side' into other
commit work6^2 work6~2
5
commit work6~1 work6~2
4
commit work6~2 work6~3
3
commit work6~3 work6~4
2
commit work6~4
1
EOF

test_expect_success REBASE_P 'rebase -i -p with merge' '
	git checkout -b work6 other &&
	git rebase -i -p --root --onto master &&
	log_with_names work6 > rebased6 &&
	test_cmp expect-side rebased6
'

test_expect_success 'set up second root and merge' '
	git symbolic-ref HEAD refs/heads/third &&
	rm .git/index &&
	rm A B C &&
	test_commit 6 D &&
	git checkout other &&
	git merge --allow-unrelated-histories third
'

cat > expect-third <<'EOF'
commit work7 work7~1 work7^2
Merge branch 'third' into other
commit work7^2 work7~4
6
commit work7~1 work7~2 work7~1^2
Merge branch 'side' into other
commit work7~1^2 work7~3
5
commit work7~2 work7~3
4
commit work7~3 work7~4
3
commit work7~4 work7~5
2
commit work7~5
1
EOF

test_expect_success REBASE_P 'rebase -i -p with two roots' '
	git checkout -b work7 other &&
	git rebase -i -p --root --onto master &&
	log_with_names work7 > rebased7 &&
	test_cmp expect-third rebased7
'

test_expect_success 'setup pre-rebase hook that fails' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rebase <<EOF &&
#!$SHELL_PATH
false
EOF
	chmod +x .git/hooks/pre-rebase
'

test_expect_success 'pre-rebase hook stops rebase' '
	git checkout -b stops1 other &&
	test_must_fail git rebase --root --onto master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/stops1 &&
	test 0 = $(git rev-list other...stops1 | wc -l)
'

test_expect_success 'pre-rebase hook stops rebase -i' '
	git checkout -b stops2 other &&
	test_must_fail git rebase --root --onto master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/stops2 &&
	test 0 = $(git rev-list other...stops2 | wc -l)
'

test_expect_success 'remove pre-rebase hook' '
	rm -f .git/hooks/pre-rebase
'

test_expect_success 'set up a conflict' '
	git checkout master &&
	echo conflict > B &&
	git add B &&
	git commit -m conflict
'

test_expect_success 'rebase --root with conflict (first part)' '
	git checkout -b conflict1 other &&
	test_must_fail git rebase --root --onto master &&
	git ls-files -u | grep "B$"
'

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	git add B
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
	git rebase --continue &&
	git log --pretty=tformat:"%s" > conflict1 &&
	test_cmp expect-conflict conflict1
'

test_expect_success 'rebase -i --root with conflict (first part)' '
	git checkout -b conflict2 other &&
	test_must_fail git rebase -i --root --onto master &&
	git ls-files -u | grep "B$"
'

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	git add B
'

test_expect_success 'rebase -i --root with conflict (second part)' '
	git rebase --continue &&
	git log --pretty=tformat:"%s" > conflict2 &&
	test_cmp expect-conflict conflict2
'

cat >expect-conflict-p <<\EOF
commit conflict3 conflict3~1 conflict3^2
Merge branch 'third' into other
commit conflict3^2 conflict3~4
6
commit conflict3~1 conflict3~2 conflict3~1^2
Merge branch 'side' into other
commit conflict3~1^2 conflict3~3
5
commit conflict3~2 conflict3~3
4
commit conflict3~3 conflict3~4
3
commit conflict3~4 conflict3~5
conflict
commit conflict3~5 conflict3~6
2
commit conflict3~6
1
EOF

test_expect_success REBASE_P 'rebase -i -p --root with conflict (first part)' '
	git checkout -b conflict3 other &&
	test_must_fail git rebase -i -p --root --onto master &&
	git ls-files -u | grep "B$"
'

test_expect_success 'fix the conflict' '
	echo 3 > B &&
	git add B
'

test_expect_success REBASE_P 'rebase -i -p --root with conflict (second part)' '
	git rebase --continue &&
	log_with_names conflict3 >out &&
	test_cmp expect-conflict-p out
'

test_done
