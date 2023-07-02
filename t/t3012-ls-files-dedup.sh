#!/bin/sh

test_description='git ls-files --deduplicate test'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	>a.txt &&
	>b.txt &&
	>delete.txt &&
	git add a.txt b.txt delete.txt &&
	git commit -m base &&
	echo a >a.txt &&
	echo b >b.txt &&
	echo delete >delete.txt &&
	git add a.txt b.txt delete.txt &&
	git commit -m tip &&
	git tag tip &&
	git reset --hard HEAD^ &&
	echo change >a.txt &&
	git commit -a -m side &&
	git tag side
'

test_expect_success 'git ls-files --deduplicate to show unique unmerged path' '
	test_must_fail git merge tip &&
	git ls-files --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	git merge --abort
'

test_expect_success 'git ls-files -d -m --deduplicate with different display options' '
	git reset --hard side &&
	test_must_fail git merge tip &&
	rm delete.txt &&
	git ls-files -d -m --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	git ls-files -d -m -t --deduplicate >actual &&
	cat >expect <<-\EOF &&
	C a.txt
	C a.txt
	C a.txt
	R delete.txt
	C delete.txt
	EOF
	test_cmp expect actual &&
	git ls-files -d -m -c --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	git merge --abort
'

test_done
