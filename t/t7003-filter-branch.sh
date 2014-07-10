#!/bin/sh

test_description='git filter-branch'
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit A &&
	GIT_COMMITTER_DATE="@0 +0000" GIT_AUTHOR_DATE="@0 +0000" &&
	test_commit --notick B &&
	git checkout -b branch B &&
	test_commit D &&
	mkdir dir &&
	test_commit dir/D &&
	test_commit E &&
	git checkout master &&
	test_commit C &&
	git checkout branch &&
	git merge C &&
	git tag F &&
	test_commit G &&
	test_commit H
'
# * (HEAD, branch) H
# * G
# *   Merge commit 'C' into branch
# |\
# | * (master) C
# * | E
# * | dir/D
# * | D
# |/
# * B
# * A


H=$(git rev-parse H)

test_expect_success 'rewrite identically' '
	git filter-branch branch
'
test_expect_success 'result is really identical' '
	test $H = $(git rev-parse HEAD)
'

test_expect_success 'rewrite bare repository identically' '
	(git config core.bare true && cd .git &&
	 git filter-branch branch > filter-output 2>&1 &&
	! fgrep fatal filter-output)
'
git config core.bare false
test_expect_success 'result is really identical' '
	test $H = $(git rev-parse HEAD)
'

TRASHDIR=$(pwd)
test_expect_success 'correct GIT_DIR while using -d' '
	mkdir drepo &&
	( cd drepo &&
	git init &&
	test_commit drepo &&
	git filter-branch -d "$TRASHDIR/dfoo" \
		--index-filter "cp \"$TRASHDIR\"/dfoo/backup-refs \"$TRASHDIR\"" \
	) &&
	grep drepo "$TRASHDIR/backup-refs"
'

test_expect_success 'tree-filter works with -d' '
	git init drepo-tree &&
	(
		cd drepo-tree &&
		test_commit one &&
		git filter-branch -d "$TRASHDIR/dfoo" \
			--tree-filter "echo changed >one.t" &&
		echo changed >expect &&
		git cat-file blob HEAD:one.t >actual &&
		test_cmp expect actual &&
		test_cmp one.t actual
	)
'

test_expect_success 'Fail if commit filter fails' '
	test_must_fail git filter-branch -f --commit-filter "exit 1" HEAD
'

test_expect_success 'rewrite, renaming a specific file' '
	git filter-branch -f --tree-filter "mv D.t doh || :" HEAD
'

test_expect_success 'test that the file was renamed' '
	test D = "$(git show HEAD:doh --)" &&
	! test -f D.t &&
	test -f doh &&
	test D = "$(cat doh)"
'

test_expect_success 'rewrite, renaming a specific directory' '
	git filter-branch -f --tree-filter "mv dir diroh || :" HEAD
'

test_expect_success 'test that the directory was renamed' '
	test dir/D = "$(git show HEAD:diroh/D.t --)" &&
	! test -d dir &&
	test -d diroh &&
	! test -d diroh/dir &&
	test -f diroh/D.t &&
	test dir/D = "$(cat diroh/D.t)"
'

git tag oldD HEAD~4
test_expect_success 'rewrite one branch, keeping a side branch' '
	git branch modD oldD &&
	git filter-branch -f --tree-filter "mv B.t boh || :" D..modD
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
	echo H > A.t &&
	test_tick &&
	git commit -m "not subdir" A.t &&
	echo A > subdir/new &&
	test_tick &&
	git commit -m "again subdir" subdir/new &&
	git rm A.t &&
	test_tick &&
	git commit -m "again not subdir" &&
	git branch sub &&
	git branch sub-earlier HEAD~2 &&
	git filter-branch -f --subdirectory-filter subdir \
		refs/heads/sub refs/heads/sub-earlier
'

test_expect_success 'subdirectory filter result looks okay' '
	test 2 = $(git rev-list sub | wc -l) &&
	git show sub:new &&
	test_must_fail git show sub:subdir &&
	git show sub-earlier:new &&
	test_must_fail git show sub-earlier:subdir
'

test_expect_success 'more setup' '
	git checkout master &&
	mkdir subdir &&
	echo A > subdir/new &&
	git add subdir/new &&
	test_tick &&
	git commit -m "subdir on master" subdir/new &&
	git rm A.t &&
	test_tick &&
	git commit -m "again subdir on master" &&
	git merge branch
'

test_expect_success 'use index-filter to move into a subdirectory' '
	git branch directorymoved &&
	git filter-branch -f --index-filter \
		 "git ls-files -s | sed \"s-	-&newsubdir/-\" |
	          GIT_INDEX_FILE=\$GIT_INDEX_FILE.new \
			git update-index --index-info &&
		  mv \"\$GIT_INDEX_FILE.new\" \"\$GIT_INDEX_FILE\"" directorymoved &&
	git diff --exit-code HEAD directorymoved:newsubdir
'

test_expect_success 'stops when msg filter fails' '
	old=$(git rev-parse HEAD) &&
	test_must_fail git filter-branch -f --msg-filter false HEAD &&
	test $old = $(git rev-parse HEAD) &&
	rm -rf .git-rewrite
'

test_expect_success 'author information is preserved' '
	: > i &&
	git add i &&
	test_tick &&
	GIT_AUTHOR_NAME="B V Uips" git commit -m bvuips &&
	git branch preserved-author &&
	(sane_unset GIT_AUTHOR_NAME &&
	 git filter-branch -f --msg-filter "cat; \
			test \$GIT_COMMIT != $(git rev-parse master) || \
			echo Hallo" \
		preserved-author) &&
	test 1 = $(git rev-list --author="B V Uips" preserved-author | wc -l)
'

test_expect_success "remove a certain author's commits" '
	echo i > i &&
	test_tick &&
	git commit -m i i &&
	git branch removed-author &&
	git filter-branch -f --commit-filter "\
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
	test_must_fail git filter-branch -f master xy-problem &&
	test_must_fail git filter-branch -f HEAD^
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
	test_must_fail git ls-files --error-unmatch "foo/$name" &&
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

test_expect_success 'Tag name filtering retains tag message' '
	git tag -m atag T &&
	git cat-file tag T > expect &&
	git filter-branch -f --tag-name-filter cat &&
	git cat-file tag T > actual &&
	test_cmp expect actual
'

faux_gpg_tag='object XXXXXX
type commit
tag S
tagger T A Gger <tagger@example.com> 1206026339 -0500

This is a faux gpg signed tag.
-----BEGIN PGP SIGNATURE-----
Version: FauxGPG v0.0.0 (FAUX/Linux)

gdsfoewhxu/6l06f1kxyxhKdZkrcbaiOMtkJUA9ITAc1mlamh0ooasxkH1XwMbYQ
acmwXaWET20H0GeAGP+7vow=
=agpO
-----END PGP SIGNATURE-----
'
test_expect_success 'Tag name filtering strips gpg signature' '
	sha1=$(git rev-parse HEAD) &&
	sha1t=$(echo "$faux_gpg_tag" | sed -e s/XXXXXX/$sha1/ | git mktag) &&
	git update-ref "refs/tags/S" "$sha1t" &&
	echo "$faux_gpg_tag" | sed -e s/XXXXXX/$sha1/ | head -n 6 > expect &&
	git filter-branch -f --tag-name-filter cat &&
	git cat-file tag S > actual &&
	test_cmp expect actual
'

test_expect_success 'Tag name filtering allows slashes in tag names' '
	git tag -m tag-with-slash X/1 &&
	git cat-file tag X/1 | sed -e s,X/1,X/2, > expect &&
	git filter-branch -f --tag-name-filter "echo X/2" &&
	git cat-file tag X/2 > actual &&
	test_cmp expect actual
'

test_expect_success 'Prune empty commits' '
	git rev-list HEAD > expect &&
	test_commit to_remove &&
	git filter-branch -f --index-filter "git update-index --remove to_remove.t" --prune-empty HEAD &&
	git rev-list HEAD > actual &&
	test_cmp expect actual
'

test_expect_success 'prune empty collapsed merges' '
	test_config merge.ff false &&
	git rev-list HEAD >expect &&
	test_commit to_remove_2 &&
	git reset --hard HEAD^ &&
	test_merge non-ff to_remove_2 &&
	git filter-branch -f --index-filter "git update-index --remove to_remove_2.t" --prune-empty HEAD &&
	git rev-list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--remap-to-ancestor with filename filters' '
	git checkout master &&
	git reset --hard A &&
	test_commit add-foo foo 1 &&
	git branch moved-foo &&
	test_commit add-bar bar a &&
	git branch invariant &&
	orig_invariant=$(git rev-parse invariant) &&
	git branch moved-bar &&
	test_commit change-foo foo 2 &&
	git filter-branch -f --remap-to-ancestor \
		moved-foo moved-bar A..master \
		-- -- foo &&
	test $(git rev-parse moved-foo) = $(git rev-parse moved-bar) &&
	test $(git rev-parse moved-foo) = $(git rev-parse master^) &&
	test $orig_invariant = $(git rev-parse invariant)
'

test_expect_success 'automatic remapping to ancestor with filename filters' '
	git checkout master &&
	git reset --hard A &&
	test_commit add-foo2 foo 1 &&
	git branch moved-foo2 &&
	test_commit add-bar2 bar a &&
	git branch invariant2 &&
	orig_invariant=$(git rev-parse invariant2) &&
	git branch moved-bar2 &&
	test_commit change-foo2 foo 2 &&
	git filter-branch -f \
		moved-foo2 moved-bar2 A..master \
		-- -- foo &&
	test $(git rev-parse moved-foo2) = $(git rev-parse moved-bar2) &&
	test $(git rev-parse moved-foo2) = $(git rev-parse master^) &&
	test $orig_invariant = $(git rev-parse invariant2)
'

test_expect_success 'setup submodule' '
	rm -fr ?* .git &&
	git init &&
	test_commit file &&
	mkdir submod &&
	submodurl="$PWD/submod" &&
	( cd submod &&
	  git init &&
	  test_commit file-in-submod ) &&
	git submodule add "$submodurl" &&
	git commit -m "added submodule" &&
	test_commit add-file &&
	( cd submod && test_commit add-in-submodule ) &&
	git add submod &&
	git commit -m "changed submodule" &&
	git branch original HEAD
'

orig_head=`git show-ref --hash --head HEAD`

test_expect_success 'rewrite submodule with another content' '
	git filter-branch --tree-filter "test -d submod && {
					 rm -rf submod &&
					 git rm -rf --quiet submod &&
					 mkdir submod &&
					 : > submod/file
					 } || :" HEAD &&
	test $orig_head != `git show-ref --hash --head HEAD`
'

test_expect_success 'replace submodule revision' '
	git reset --hard original &&
	git filter-branch -f --tree-filter \
	    "if git ls-files --error-unmatch -- submod > /dev/null 2>&1
	     then git update-index --cacheinfo 160000 0123456789012345678901234567890123456789 submod
	     fi" HEAD &&
	test $orig_head != `git show-ref --hash --head HEAD`
'

test_done
