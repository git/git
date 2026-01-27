#!/bin/sh

test_description='last-modified tests'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit 1 file &&
	mkdir a &&
	test_commit 2 a/file &&
	mkdir a/b &&
	test_commit 3 a/b/file
'

test_expect_success 'cannot run last-modified on two trees' '
	test_must_fail git last-modified HEAD HEAD~1
'

check_last_modified() {
	local indir= &&
	while test $# != 0
	do
		case "$1" in
		-C)
			indir="$2"
			shift
			;;
		*)
			break
			;;
		esac &&
		shift
	done &&

	cat >expect &&
	git ${indir:+-C "$indir"} last-modified "$@" >tmp.1 &&
	git name-rev --annotate-stdin --name-only --tags \
		<tmp.1 >tmp.2 &&
	tr '\t' ' ' <tmp.2 >actual &&
	test_cmp expect actual
}

test_expect_success 'last-modified non-recursive' '
	check_last_modified <<-\EOF
	3 a
	1 file
	EOF
'

test_expect_success 'last-modified recursive' '
	check_last_modified -r <<-\EOF
	3 a/b/file
	2 a/file
	1 file
	EOF
'

test_expect_success 'last-modified recursive with show-trees' '
	check_last_modified -r -t <<-\EOF
	3 a/b
	3 a/b/file
	3 a
	2 a/file
	1 file
	EOF
'

test_expect_success 'last-modified non-recursive with show-trees' '
	check_last_modified -t <<-\EOF
	3 a
	1 file
	EOF
'

test_expect_success 'last-modified subdir' '
	check_last_modified a <<-\EOF
	3 a
	EOF
'

test_expect_success 'last-modified in sparse checkout' '
	test_when_finished "git sparse-checkout disable" &&
	git sparse-checkout set b &&
	check_last_modified -- a <<-\EOF
	3 a
	EOF
'

test_expect_success 'last-modified subdir recursive' '
	check_last_modified -r a <<-\EOF
	3 a/b/file
	2 a/file
	EOF
'

test_expect_success 'last-modified subdir non-recursive' '
	check_last_modified a <<-\EOF
	3 a
	EOF
'

test_expect_success 'last-modified path in subdir non-recursive' '
	check_last_modified a/file <<-\EOF
	2 a/file
	EOF
'

test_expect_success 'last-modified subdir with wildcard non-recursive' '
	check_last_modified a/* <<-\EOF
	3 a/b
	2 a/file
	EOF
'

test_expect_success 'last-modified with negative max-depth' '
	check_last_modified --max-depth=-1 <<-\EOF
	3 a/b/file
	2 a/file
	1 file
	EOF
'

test_expect_success 'last-modified with max-depth of 1' '
	check_last_modified --max-depth=1 <<-\EOF
	3 a/b
	2 a/file
	1 file
	EOF
'

test_expect_success 'last-modified from non-HEAD commit' '
	check_last_modified HEAD^ <<-\EOF
	2 a
	1 file
	EOF
'

test_expect_success 'last-modified from subdir defaults to root' '
	check_last_modified -C a <<-\EOF
	3 a
	1 file
	EOF
'

test_expect_success 'last-modified from subdir uses relative pathspecs' '
	check_last_modified -C a -r b <<-\EOF
	3 a/b/file
	EOF
'

test_expect_success 'limit last-modified traversal by count' '
	check_last_modified -1 <<-\EOF
	3 a
	^2 file
	EOF
'

test_expect_success 'limit last-modified traversal by commit' '
	check_last_modified HEAD~2..HEAD <<-\EOF
	3 a
	^1 file
	EOF
'

test_expect_success 'only last-modified files in the current tree' '
	git rm -rf a &&
	git commit -m "remove a" &&
	check_last_modified <<-\EOF
	1 file
	EOF
'

test_expect_success 'subdirectory modified via merge' '
	test_when_finished rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git switch --create left &&
		mkdir subdir &&
		test_commit left subdir/left &&
		git switch --create right base &&
		mkdir subdir &&
		test_commit right subdir/right &&
		git switch - &&
		test_merge merge right &&
		check_last_modified <<-\EOF
		merge subdir
		base base.t
		EOF
	)
'

test_expect_success 'cross merge boundaries in blaming' '
	git checkout HEAD^0 &&
	git rm -rf . &&
	test_commit m1 &&
	git checkout HEAD^ &&
	git rm -rf . &&
	test_commit m2 &&
	git merge m1 &&
	check_last_modified <<-\EOF
	m2 m2.t
	m1 m1.t
	EOF
'

test_expect_success 'last-modified merge for resolved conflicts' '
	git checkout HEAD^0 &&
	git rm -rf . &&
	test_commit c1 conflict &&
	git checkout HEAD^ &&
	git rm -rf . &&
	test_commit c2 conflict &&
	test_must_fail git merge c1 &&
	test_commit resolved conflict &&
	check_last_modified conflict <<-\EOF
	resolved conflict
	EOF
'


# Consider `file` with this content through history:
#
# A---B---B-------B---B
#          \     /
#           C---D
test_expect_success 'last-modified merge ignores content from branch' '
	git checkout HEAD^0 &&
	git rm -rf . &&
	test_commit a1 file A &&
	test_commit a2 file B &&
	test_commit a3 file C &&
	test_commit a4 file D &&
	git checkout a2 &&
	git merge --no-commit --no-ff a4 &&
	git checkout a2 -- file &&
	git merge --continue &&
	check_last_modified <<-\EOF
	a2 file
	EOF
'

# Consider `file` with this content through history:
#
#  A---B---B---C---D---B---B
#           \         /
#            B-------B
test_expect_success 'last-modified merge undoes changes' '
	git checkout HEAD^0 &&
	git rm -rf . &&
	test_commit b1 file A &&
	test_commit b2 file B &&
	test_commit b3 file C &&
	test_commit b4 file D &&
	git checkout b2 &&
	test_commit b5 file2 2 &&
	git checkout b4 &&
	git merge --no-commit --no-ff b5 &&
	git checkout b2 -- file &&
	git merge --continue &&
	check_last_modified <<-\EOF
	b5 file2
	b2 file
	EOF
'

test_expect_success 'last-modified complains about unknown arguments' '
	test_must_fail git last-modified --foo 2>err &&
	grep "unknown last-modified argument: --foo" err
'

test_done
