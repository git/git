#!/bin/sh

test_description='git filter-branch'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success 'setup' '
	test_commit A &&
	GIT_COMMITTER_DATE="@0 +0000" GIT_AUTHOR_DATE="@0 +0000" &&
	test_commit --notick B &&
	git checkout -b branch B &&
	test_commit D &&
	mkdir dir &&
	test_commit dir/D &&
	test_commit E &&
	git checkout main &&
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
# | * (main) C
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
	! grep fatal filter-output)
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

V=$(git rev-parse HEAD)

test_expect_success 'populate --state-branch' '
	git filter-branch --state-branch state -f --tree-filter "touch file || :" HEAD
'

W=$(git rev-parse HEAD)

test_expect_success 'using --state-branch to skip already rewritten commits' '
	test_when_finished git reset --hard $V &&
	git reset --hard $V &&
	git filter-branch --state-branch state -f --tree-filter "touch file || :" HEAD &&
	test_cmp_rev $W HEAD
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
	git checkout main &&
	mkdir subdir &&
	echo A > subdir/new &&
	git add subdir/new &&
	test_tick &&
	git commit -m "subdir on main" subdir/new &&
	git rm A.t &&
	test_tick &&
	git commit -m "again subdir on main" &&
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
			test \$GIT_COMMIT != $(git rev-parse main) || \
			echo Hallo" \
		preserved-author) &&
	git rev-list --author="B V Uips" preserved-author >actual &&
	test_line_count = 1 actual
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
	cnt1=$(git rev-list main | wc -l) &&
	cnt2=$(git rev-list removed-author | wc -l) &&
	test $cnt1 -eq $(($cnt2 + 1)) &&
	git rev-list --author="B V Uips" removed-author >actual &&
	test_line_count = 0 actual
'

test_expect_success 'barf on invalid name' '
	test_must_fail git filter-branch -f main xy-problem &&
	test_must_fail git filter-branch -f HEAD^
'

test_expect_success '"map" works in commit filter' '
	git filter-branch -f --commit-filter "\
		parent=\$(git rev-parse \$GIT_COMMIT^) &&
		mapped=\$(map \$parent) &&
		actual=\$(echo \"\$@\" | sed \"s/^.*-p //\") &&
		test \$mapped = \$actual &&
		git commit-tree \"\$@\";" main~2..main &&
	git rev-parse --verify main
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
	git checkout main &&

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
	git rev-list main >actual &&
	test_line_count = 3 actual
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

test_expect_success GPG 'Filtering retains message of gpg signed commit' '
	mkdir gpg &&
	touch gpg/foo &&
	git add gpg &&
	test_tick &&
	git commit -S -m "Adding gpg" &&

	git log -1 --format="%s" > expect &&
	git filter-branch -f --msg-filter "cat" &&
	git log -1 --format="%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'Tag name filtering allows slashes in tag names' '
	git tag -m tag-with-slash X/1 &&
	git cat-file tag X/1 | sed -e s,X/1,X/2, > expect &&
	git filter-branch -f --tag-name-filter "echo X/2" &&
	git cat-file tag X/2 > actual &&
	test_cmp expect actual
'
test_expect_success 'setup --prune-empty comparisons' '
	git checkout --orphan main-no-a &&
	git rm -rf . &&
	unset test_tick &&
	test_tick &&
	GIT_COMMITTER_DATE="@0 +0000" GIT_AUTHOR_DATE="@0 +0000" &&
	test_commit --notick B B.t B Bx &&
	git checkout -b branch-no-a Bx &&
	test_commit D D.t D Dx &&
	mkdir dir &&
	test_commit dir/D dir/D.t dir/D dir/Dx &&
	test_commit E E.t E Ex &&
	git checkout main-no-a &&
	test_commit C C.t C Cx &&
	git checkout branch-no-a &&
	git merge Cx -m "Merge tag '\''C'\'' into branch" &&
	git tag Fx &&
	test_commit G G.t G Gx &&
	test_commit H H.t H Hx &&
	git checkout branch
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

test_expect_success 'prune empty works even without index/tree filters' '
	git rev-list HEAD >expect &&
	git commit --allow-empty -m empty &&
	git filter-branch -f --prune-empty HEAD &&
	git rev-list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--prune-empty is able to prune root commit' '
	git rev-list branch-no-a >expect &&
	git branch testing H &&
	git filter-branch -f --prune-empty --index-filter "git update-index --remove A.t" testing &&
	git rev-list testing >actual &&
	git branch -D testing &&
	test_cmp expect actual
'

test_expect_success '--prune-empty is able to prune entire branch' '
	git branch prune-entire B &&
	git filter-branch -f --prune-empty --index-filter "git update-index --remove A.t B.t" prune-entire &&
	test_must_fail git rev-parse refs/heads/prune-entire &&
	if test_have_prereq REFFILES
	then
		test_must_fail git reflog exists refs/heads/prune-entire
	fi
'

test_expect_success '--remap-to-ancestor with filename filters' '
	git checkout main &&
	git reset --hard A &&
	test_commit add-foo foo 1 &&
	git branch moved-foo &&
	test_commit add-bar bar a &&
	git branch invariant &&
	orig_invariant=$(git rev-parse invariant) &&
	git branch moved-bar &&
	test_commit change-foo foo 2 &&
	git filter-branch -f --remap-to-ancestor \
		moved-foo moved-bar A..main \
		-- -- foo &&
	test $(git rev-parse moved-foo) = $(git rev-parse moved-bar) &&
	test $(git rev-parse moved-foo) = $(git rev-parse main^) &&
	test $orig_invariant = $(git rev-parse invariant)
'

test_expect_success 'automatic remapping to ancestor with filename filters' '
	git checkout main &&
	git reset --hard A &&
	test_commit add-foo2 foo 1 &&
	git branch moved-foo2 &&
	test_commit add-bar2 bar a &&
	git branch invariant2 &&
	orig_invariant=$(git rev-parse invariant2) &&
	git branch moved-bar2 &&
	test_commit change-foo2 foo 2 &&
	git filter-branch -f \
		moved-foo2 moved-bar2 A..main \
		-- -- foo &&
	test $(git rev-parse moved-foo2) = $(git rev-parse moved-bar2) &&
	test $(git rev-parse moved-foo2) = $(git rev-parse main^) &&
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

orig_head=$(git show-ref --hash --head HEAD)

test_expect_success 'rewrite submodule with another content' '
	git filter-branch --tree-filter "test -d submod && {
					 rm -rf submod &&
					 git rm -rf --quiet submod &&
					 mkdir submod &&
					 : > submod/file
					 } || :" HEAD &&
	test $orig_head != $(git show-ref --hash --head HEAD)
'

test_expect_success 'replace submodule revision' '
	invalid=$(test_oid numeric) &&
	git reset --hard original &&
	git filter-branch -f --tree-filter \
	    "if git ls-files --error-unmatch -- submod > /dev/null 2>&1
	     then git update-index --cacheinfo 160000 $invalid submod
	     fi" HEAD &&
	test $orig_head != $(git show-ref --hash --head HEAD)
'

test_expect_success 'filter commit message without trailing newline' '
	git reset --hard original &&
	commit=$(printf "no newline" | git commit-tree HEAD^{tree}) &&
	git update-ref refs/heads/no-newline $commit &&
	git filter-branch -f refs/heads/no-newline &&
	echo $commit >expect &&
	git rev-parse refs/heads/no-newline >actual &&
	test_cmp expect actual
'

test_expect_success 'tree-filter deals with object name vs pathname ambiguity' '
	test_when_finished "git reset --hard original" &&
	ambiguous=$(git rev-list -1 HEAD) &&
	git filter-branch --tree-filter "mv file.t $ambiguous" HEAD^.. &&
	git show HEAD:$ambiguous
'

test_expect_success 'rewrite repository including refs that point at non-commit object' '
	test_when_finished "git reset --hard original" &&
	tree=$(git rev-parse HEAD^{tree}) &&
	test_when_finished "git replace -d $tree" &&
	echo A >new &&
	git add new &&
	new_tree=$(git write-tree) &&
	git replace $tree $new_tree &&
	git tag -a -m "tag to a tree" treetag $new_tree &&
	git reset --hard HEAD &&
	git filter-branch -f -- --all >filter-output 2>&1 &&
	! grep fatal filter-output
'

test_expect_success 'filter-branch handles ref deletion' '
	git switch --orphan empty-commit &&
	git commit --allow-empty -m "empty commit" &&
	git tag empty &&
	git branch to-delete &&
	git filter-branch -f --prune-empty to-delete >out 2>&1 &&
	grep "to-delete.*was deleted" out &&
	test_must_fail git rev-parse --verify to-delete
'

test_expect_success 'filter-branch handles ref rewrite' '
	git checkout empty &&
	test_commit to-drop &&
	git branch rewrite &&
	git filter-branch -f \
		--index-filter "git rm --ignore-unmatch --cached to-drop.t" \
		 rewrite >out 2>&1 &&
	grep "rewrite.*was rewritten" out &&
	! grep -i warning out &&
	git diff-tree empty rewrite
'

test_expect_success 'filter-branch handles ancestor rewrite' '
	test_commit to-exclude &&
	git branch ancestor &&
	git filter-branch -f ancestor -- :^to-exclude.t >out 2>&1 &&
	grep "ancestor.*was rewritten" out &&
	! grep -i warning out &&
	git diff-tree HEAD^ ancestor
'

test_done
