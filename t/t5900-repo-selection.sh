#!/bin/sh

test_description='selecting remote repo in ambiguous cases'
. ./test-lib.sh

reset() {
	rm -rf foo foo.but fetch clone
}

make_tree() {
	but init "$1" &&
	(cd "$1" && test_cummit "$1")
}

make_bare() {
	but init --bare "$1" &&
	(cd "$1" &&
	 tree=$(but hash-object -w -t tree /dev/null) &&
	 cummit=$(echo "$1" | but cummit-tree $tree) &&
	 but update-ref HEAD $cummit
	)
}

get() {
	but init --bare fetch &&
	(cd fetch && but fetch "../$1") &&
	but clone "$1" clone
}

check() {
	echo "$1" >expect &&
	(cd fetch && but log -1 --format=%s FETCH_HEAD) >actual.fetch &&
	(cd clone && but log -1 --format=%s HEAD) >actual.clone &&
	test_cmp expect actual.fetch &&
	test_cmp expect actual.clone
}

test_expect_success 'find .but dir in worktree' '
	reset &&
	make_tree foo &&
	get foo &&
	check foo
'

test_expect_success 'automagically add .but suffix' '
	reset &&
	make_bare foo.but &&
	get foo &&
	check foo.but
'

test_expect_success 'automagically add .but suffix to worktree' '
	reset &&
	make_tree foo.but &&
	get foo &&
	check foo.but
'

test_expect_success 'prefer worktree foo over bare foo.but' '
	reset &&
	make_tree foo &&
	make_bare foo.but &&
	get foo &&
	check foo
'

test_expect_success 'prefer bare foo over bare foo.but' '
	reset &&
	make_bare foo &&
	make_bare foo.but &&
	get foo &&
	check foo
'

test_expect_success 'disambiguate with full foo.but' '
	reset &&
	make_bare foo &&
	make_bare foo.but &&
	get foo.but &&
	check foo.but
'

test_expect_success 'we are not fooled by non-but foo directory' '
	reset &&
	make_bare foo.but &&
	mkdir foo &&
	get foo &&
	check foo.but
'

test_expect_success 'prefer inner .but over outer bare' '
	reset &&
	make_tree foo &&
	make_bare foo.but &&
	mv foo/.but foo.but &&
	get foo.but &&
	check foo
'

test_done
