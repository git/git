#!/bin/sh

test_description='git rebase --root

Tests if git rebase --root --onto <newparent> can rebase the root commit.
'
. ./test-lib.sh

test_expect_success 'prepare repository' '
	echo 1 > A &&
	git add A &&
	git commit -m 1 &&
	echo 2 > A &&
	git add A &&
	git commit -m 2 &&
	git symbolic-ref HEAD refs/heads/other &&
	rm .git/index &&
	echo 3 > B &&
	git add B &&
	git commit -m 3 &&
	echo 1 > A &&
	git add A &&
	git commit -m 1b &&
	echo 4 > B &&
	git add B &&
	git commit -m 4
'

test_expect_success 'rebase --root expects --onto' '
	test_must_fail git rebase --root
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
	git checkout -b work &&
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
	GIT_EDITOR=: test_must_fail git rebase --root --onto master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/stops1
	test 0 = $(git rev-list other...stops1 | wc -l)
'

test_done
