#!/bin/sh

test_description='selecting remote repo in ambiguous cases'
. ./test-lib.sh

reset() {
	rm -rf foo foo.git fetch clone
}

make_tree() {
	git init "$1" &&
	(cd "$1" && test_commit "$1")
}

make_bare() {
	git init --bare "$1" &&
	(cd "$1" &&
	 tree=`git hash-object -w -t tree /dev/null` &&
	 commit=$(echo "$1" | git commit-tree $tree) &&
	 git update-ref HEAD $commit
	)
}

get() {
	git init --bare fetch &&
	(cd fetch && git fetch "../$1") &&
	git clone "$1" clone
}

check() {
	echo "$1" >expect &&
	(cd fetch && git log -1 --format=%s FETCH_HEAD) >actual.fetch &&
	(cd clone && git log -1 --format=%s HEAD) >actual.clone &&
	test_cmp expect actual.fetch &&
	test_cmp expect actual.clone
}

test_expect_success 'find .git dir in worktree' '
	reset &&
	make_tree foo &&
	get foo &&
	check foo
'

test_expect_success 'automagically add .git suffix' '
	reset &&
	make_bare foo.git &&
	get foo &&
	check foo.git
'

test_expect_success 'automagically add .git suffix to worktree' '
	reset &&
	make_tree foo.git &&
	get foo &&
	check foo.git
'

test_expect_success 'prefer worktree foo over bare foo.git' '
	reset &&
	make_tree foo &&
	make_bare foo.git &&
	get foo &&
	check foo
'

test_expect_success 'prefer bare foo over bare foo.git' '
	reset &&
	make_bare foo &&
	make_bare foo.git &&
	get foo &&
	check foo
'

test_expect_success 'disambiguate with full foo.git' '
	reset &&
	make_bare foo &&
	make_bare foo.git &&
	get foo.git &&
	check foo.git
'

test_expect_success 'we are not fooled by non-git foo directory' '
	reset &&
	make_bare foo.git &&
	mkdir foo &&
	get foo &&
	check foo.git
'

test_expect_success 'prefer inner .git over outer bare' '
	reset &&
	make_tree foo &&
	make_bare foo.git &&
	mv foo/.git foo.git &&
	get foo.git &&
	check foo
'

test_done
