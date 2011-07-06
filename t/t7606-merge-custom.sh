#!/bin/sh

test_description='git merge

Testing a custom strategy.'

. ./test-lib.sh

cat >git-merge-theirs <<EOF
#!$SHELL_PATH
eval git read-tree --reset -u \\\$\$#
EOF
chmod +x git-merge-theirs
PATH=.:$PATH
export PATH

test_expect_success 'setup' '
	echo c0 >c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 >c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c1c1 >c1.c &&
	echo c2 >c2.c &&
	git add c1.c c2.c &&
	git commit -m c2 &&
	git tag c2
'

test_expect_success 'merge c2 with a custom strategy' '
	git reset --hard c1 &&
	git merge -s theirs c2 &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD^1)" &&
	test "$(git rev-parse c2)" = "$(git rev-parse HEAD^2)" &&
	test "$(git rev-parse c2^{tree})" = "$(git rev-parse HEAD^{tree})" &&
	git diff --exit-code &&
	git diff --exit-code c2 HEAD &&
	git diff --exit-code c2 &&
	test -f c0.c &&
	grep c1c1 c1.c &&
	test -f c2.c
'

test_done
