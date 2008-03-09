#!/bin/sh

test_description='git-filter-branch'
. ./test-lib.sh

make_commit () {
	lower=$(echo $1 | tr A-Z a-z)
	echo $lower > $lower
	git add $lower
	test_tick
	git commit -m $1
	git tag $1
}

test_expect_success 'setup' '
	make_commit A
	make_commit B
	git checkout -b branch B
	make_commit D
	make_commit E
	git checkout master
	make_commit C
	git checkout branch
	git merge C
	git tag F
	make_commit G
	make_commit H
'

H=$(git rev-parse H)

test_expect_success 'rewrite identically' '
	git-filter-branch branch
'
test_expect_success 'result is really identical' '
	test $H = $(git rev-parse HEAD)
'

test_expect_success 'rewrite, renaming a specific file' '
	git-filter-branch -f --tree-filter "mv d doh || :" HEAD
'

test_expect_success 'test that the file was renamed' '
	test d = $(git show HEAD:doh) &&
	test -f doh &&
	test d = $(cat doh)
'

git tag oldD HEAD~4
test_expect_success 'rewrite one branch, keeping a side branch' '
	git branch modD oldD &&
	git-filter-branch -f --tree-filter "mv b boh || :" D..modD
'

test_expect_success 'common ancestor is still common (unchanged)' '
	test "$(git merge-base modD D)" = "$(git rev-parse B)"
'

test_expect_success 'filter subdirectory only' '
	mkdir subdir &&
	touch subdir/new &&
	git add subdir/new &&
	test_tick &&
	git commit -m "subdir" &&
	echo H > a &&
	test_tick &&
	git commit -m "not subdir" a &&
	echo A > subdir/new &&
	test_tick &&
	git commit -m "again subdir" subdir/new &&
	git rm a &&
	test_tick &&
	git commit -m "again not subdir" &&
	git branch sub &&
	git-filter-branch -f --subdirectory-filter subdir refs/heads/sub
'

test_expect_success 'subdirectory filter result looks okay' '
	test 2 = $(git rev-list sub | wc -l) &&
	git show sub:new &&
	! git show sub:subdir
'

test_expect_success 'setup and filter history that requires --full-history' '
	git checkout master &&
	mkdir subdir &&
	echo A > subdir/new &&
	git add subdir/new &&
	test_tick &&
	git commit -m "subdir on master" subdir/new &&
	git rm a &&
	test_tick &&
	git commit -m "again subdir on master" &&
	git merge branch &&
	git branch sub-master &&
	git-filter-branch -f --subdirectory-filter subdir sub-master
'

test_expect_success 'subdirectory filter result looks okay' '
	test 3 = $(git rev-list -1 --parents sub-master | wc -w) &&
	git show sub-master^:new &&
	git show sub-master^2:new &&
	! git show sub:subdir
'

test_expect_success 'use index-filter to move into a subdirectory' '
	git branch directorymoved &&
	git-filter-branch -f --index-filter \
		 "git ls-files -s | sed \"s-\\t-&newsubdir/-\" |
	          GIT_INDEX_FILE=\$GIT_INDEX_FILE.new \
			git update-index --index-info &&
		  mv \$GIT_INDEX_FILE.new \$GIT_INDEX_FILE" directorymoved &&
	test -z "$(git diff HEAD directorymoved:newsubdir)"'

test_expect_success 'stops when msg filter fails' '
	old=$(git rev-parse HEAD) &&
	! git-filter-branch -f --msg-filter false HEAD &&
	test $old = $(git rev-parse HEAD) &&
	rm -rf .git-rewrite
'

test_expect_success 'author information is preserved' '
	: > i &&
	git add i &&
	test_tick &&
	GIT_AUTHOR_NAME="B V Uips" git commit -m bvuips &&
	git branch preserved-author &&
	git-filter-branch -f --msg-filter "cat; \
			test \$GIT_COMMIT != $(git rev-parse master) || \
			echo Hallo" \
		preserved-author &&
	test 1 = $(git rev-list --author="B V Uips" preserved-author | wc -l)
'

test_expect_success "remove a certain author's commits" '
	echo i > i &&
	test_tick &&
	git commit -m i i &&
	git branch removed-author &&
	git-filter-branch -f --commit-filter "\
		if [ \"\$GIT_AUTHOR_NAME\" = \"B V Uips\" ];\
		then\
			skip_commit \"\$@\";
		else\
			git commit-tree \"\$@\";\
		fi" removed-author &&
	cnt1=$(git rev-list master | wc -l) &&
	cnt2=$(git rev-list removed-author | wc -l) &&
	test $cnt1 -eq $(($cnt2 + 1)) &&
	test 0 = $(git rev-list --author="B V Uips" removed-author | wc -l)
'

test_expect_success 'barf on invalid name' '
	! git filter-branch -f master xy-problem &&
	! git filter-branch -f HEAD^
'

test_expect_success '"map" works in commit filter' '
	git filter-branch -f --commit-filter "\
		parent=\$(git rev-parse \$GIT_COMMIT^) &&
		mapped=\$(map \$parent) &&
		actual=\$(echo \"\$@\" | sed \"s/^.*-p //\") &&
		test \$mapped = \$actual &&
		git commit-tree \"\$@\";" master~2..master &&
	git rev-parse --verify master
'

test_expect_success 'Name needing quotes' '

	git checkout -b rerere A &&
	mkdir foo &&
	name="れれれ" &&
	>foo/$name &&
	git add foo &&
	git commit -m "Adding a file" &&
	git filter-branch --tree-filter "rm -fr foo" &&
	! git ls-files --error-unmatch "foo/$name" &&
	test $(git rev-parse --verify rerere) != $(git rev-parse --verify A)

'

test_expect_success 'Subdirectory filter with disappearing trees' '
	git reset --hard &&
	git checkout master &&

	mkdir foo &&
	touch foo/bar &&
	git add foo &&
	test_tick &&
	git commit -m "Adding foo" &&

	git rm -r foo &&
	test_tick &&
	git commit -m "Removing foo" &&

	mkdir foo &&
	touch foo/bar &&
	git add foo &&
	test_tick &&
	git commit -m "Re-adding foo" &&

	git filter-branch -f --subdirectory-filter foo &&
	test $(git rev-list master | wc -l) = 3
'

test_done
