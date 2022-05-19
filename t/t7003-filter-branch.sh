#!/bin/sh

test_description='but filter-branch'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success 'setup' '
	test_cummit A &&
	GIT_CUMMITTER_DATE="@0 +0000" GIT_AUTHOR_DATE="@0 +0000" &&
	test_cummit --notick B &&
	but checkout -b branch B &&
	test_cummit D &&
	mkdir dir &&
	test_cummit dir/D &&
	test_cummit E &&
	but checkout main &&
	test_cummit C &&
	but checkout branch &&
	but merge C &&
	but tag F &&
	test_cummit G &&
	test_commit H
'
# * (HEAD, branch) H
# * G
# *   Merge cummit 'C' into branch
# |\
# | * (main) C
# * | E
# * | dir/D
# * | D
# |/
# * B
# * A


H=$(but rev-parse H)

test_expect_success 'rewrite identically' '
	but filter-branch branch
'
test_expect_success 'result is really identical' '
	test $H = $(but rev-parse HEAD)
'

test_expect_success 'rewrite bare repository identically' '
	(but config core.bare true && cd .but &&
	 but filter-branch branch > filter-output 2>&1 &&
	! fgrep fatal filter-output)
'
but config core.bare false
test_expect_success 'result is really identical' '
	test $H = $(but rev-parse HEAD)
'

TRASHDIR=$(pwd)
test_expect_success 'correct GIT_DIR while using -d' '
	mkdir drepo &&
	( cd drepo &&
	but init &&
	test_cummit drepo &&
	but filter-branch -d "$TRASHDIR/dfoo" \
		--index-filter "cp \"$TRASHDIR\"/dfoo/backup-refs \"$TRASHDIR\"" \
	) &&
	grep drepo "$TRASHDIR/backup-refs"
'

test_expect_success 'tree-filter works with -d' '
	but init drepo-tree &&
	(
		cd drepo-tree &&
		test_cummit one &&
		but filter-branch -d "$TRASHDIR/dfoo" \
			--tree-filter "echo changed >one.t" &&
		echo changed >expect &&
		but cat-file blob HEAD:one.t >actual &&
		test_cmp expect actual &&
		test_cmp one.t actual
	)
'

test_expect_success 'Fail if cummit filter fails' '
	test_must_fail but filter-branch -f --cummit-filter "exit 1" HEAD
'

test_expect_success 'rewrite, renaming a specific file' '
	but filter-branch -f --tree-filter "mv D.t doh || :" HEAD
'

test_expect_success 'test that the file was renamed' '
	test D = "$(but show HEAD:doh --)" &&
	! test -f D.t &&
	test -f doh &&
	test D = "$(cat doh)"
'

test_expect_success 'rewrite, renaming a specific directory' '
	but filter-branch -f --tree-filter "mv dir diroh || :" HEAD
'

test_expect_success 'test that the directory was renamed' '
	test dir/D = "$(but show HEAD:diroh/D.t --)" &&
	! test -d dir &&
	test -d diroh &&
	! test -d diroh/dir &&
	test -f diroh/D.t &&
	test dir/D = "$(cat diroh/D.t)"
'

V=$(but rev-parse HEAD)

test_expect_success 'populate --state-branch' '
	but filter-branch --state-branch state -f --tree-filter "touch file || :" HEAD
'

W=$(but rev-parse HEAD)

test_expect_success 'using --state-branch to skip already rewritten cummits' '
	test_when_finished but reset --hard $V &&
	but reset --hard $V &&
	but filter-branch --state-branch state -f --tree-filter "touch file || :" HEAD &&
	test_cmp_rev $W HEAD
'

but tag oldD HEAD~4
test_expect_success 'rewrite one branch, keeping a side branch' '
	but branch modD oldD &&
	but filter-branch -f --tree-filter "mv B.t boh || :" D..modD
'

test_expect_success 'common ancestor is still common (unchanged)' '
	test "$(but merge-base modD D)" = "$(but rev-parse B)"
'

test_expect_success 'filter subdirectory only' '
	mkdir subdir &&
	touch subdir/new &&
	but add subdir/new &&
	test_tick &&
	but cummit -m "subdir" &&
	echo H > A.t &&
	test_tick &&
	but cummit -m "not subdir" A.t &&
	echo A > subdir/new &&
	test_tick &&
	but cummit -m "again subdir" subdir/new &&
	but rm A.t &&
	test_tick &&
	but cummit -m "again not subdir" &&
	but branch sub &&
	but branch sub-earlier HEAD~2 &&
	but filter-branch -f --subdirectory-filter subdir \
		refs/heads/sub refs/heads/sub-earlier
'

test_expect_success 'subdirectory filter result looks okay' '
	test 2 = $(but rev-list sub | wc -l) &&
	but show sub:new &&
	test_must_fail but show sub:subdir &&
	but show sub-earlier:new &&
	test_must_fail but show sub-earlier:subdir
'

test_expect_success 'more setup' '
	but checkout main &&
	mkdir subdir &&
	echo A > subdir/new &&
	but add subdir/new &&
	test_tick &&
	but cummit -m "subdir on main" subdir/new &&
	but rm A.t &&
	test_tick &&
	but cummit -m "again subdir on main" &&
	but merge branch
'

test_expect_success 'use index-filter to move into a subdirectory' '
	but branch directorymoved &&
	but filter-branch -f --index-filter \
		 "but ls-files -s | sed \"s-	-&newsubdir/-\" |
	          GIT_INDEX_FILE=\$GIT_INDEX_FILE.new \
			but update-index --index-info &&
		  mv \"\$GIT_INDEX_FILE.new\" \"\$GIT_INDEX_FILE\"" directorymoved &&
	but diff --exit-code HEAD directorymoved:newsubdir
'

test_expect_success 'stops when msg filter fails' '
	old=$(but rev-parse HEAD) &&
	test_must_fail but filter-branch -f --msg-filter false HEAD &&
	test $old = $(but rev-parse HEAD) &&
	rm -rf .but-rewrite
'

test_expect_success 'author information is preserved' '
	: > i &&
	but add i &&
	test_tick &&
	GIT_AUTHOR_NAME="B V Uips" but cummit -m bvuips &&
	but branch preserved-author &&
	(sane_unset GIT_AUTHOR_NAME &&
	 but filter-branch -f --msg-filter "cat; \
			test \$GIT_CUMMIT != $(but rev-parse main) || \
			echo Hallo" \
		preserved-author) &&
	but rev-list --author="B V Uips" preserved-author >actual &&
	test_line_count = 1 actual
'

test_expect_success "remove a certain author's cummits" '
	echo i > i &&
	test_tick &&
	but cummit -m i i &&
	but branch removed-author &&
	but filter-branch -f --cummit-filter "\
		if [ \"\$GIT_AUTHOR_NAME\" = \"B V Uips\" ];\
		then\
			skip_cummit \"\$@\";
		else\
			but cummit-tree \"\$@\";\
		fi" removed-author &&
	cnt1=$(but rev-list main | wc -l) &&
	cnt2=$(but rev-list removed-author | wc -l) &&
	test $cnt1 -eq $(($cnt2 + 1)) &&
	but rev-list --author="B V Uips" removed-author >actual &&
	test_line_count = 0 actual
'

test_expect_success 'barf on invalid name' '
	test_must_fail but filter-branch -f main xy-problem &&
	test_must_fail but filter-branch -f HEAD^
'

test_expect_success '"map" works in cummit filter' '
	but filter-branch -f --cummit-filter "\
		parent=\$(but rev-parse \$GIT_CUMMIT^) &&
		mapped=\$(map \$parent) &&
		actual=\$(echo \"\$@\" | sed \"s/^.*-p //\") &&
		test \$mapped = \$actual &&
		but cummit-tree \"\$@\";" main~2..main &&
	but rev-parse --verify main
'

test_expect_success 'Name needing quotes' '

	but checkout -b rerere A &&
	mkdir foo &&
	name="れれれ" &&
	>foo/$name &&
	but add foo &&
	but cummit -m "Adding a file" &&
	but filter-branch --tree-filter "rm -fr foo" &&
	test_must_fail but ls-files --error-unmatch "foo/$name" &&
	test $(but rev-parse --verify rerere) != $(but rev-parse --verify A)

'

test_expect_success 'Subdirectory filter with disappearing trees' '
	but reset --hard &&
	but checkout main &&

	mkdir foo &&
	touch foo/bar &&
	but add foo &&
	test_tick &&
	but cummit -m "Adding foo" &&

	but rm -r foo &&
	test_tick &&
	but cummit -m "Removing foo" &&

	mkdir foo &&
	touch foo/bar &&
	but add foo &&
	test_tick &&
	but cummit -m "Re-adding foo" &&

	but filter-branch -f --subdirectory-filter foo &&
	but rev-list main >actual &&
	test_line_count = 3 actual
'

test_expect_success 'Tag name filtering retains tag message' '
	but tag -m atag T &&
	but cat-file tag T > expect &&
	but filter-branch -f --tag-name-filter cat &&
	but cat-file tag T > actual &&
	test_cmp expect actual
'

faux_gpg_tag='object XXXXXX
type cummit
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
	sha1=$(but rev-parse HEAD) &&
	sha1t=$(echo "$faux_gpg_tag" | sed -e s/XXXXXX/$sha1/ | but mktag) &&
	but update-ref "refs/tags/S" "$sha1t" &&
	echo "$faux_gpg_tag" | sed -e s/XXXXXX/$sha1/ | head -n 6 > expect &&
	but filter-branch -f --tag-name-filter cat &&
	but cat-file tag S > actual &&
	test_cmp expect actual
'

test_expect_success GPG 'Filtering retains message of gpg signed cummit' '
	mkdir gpg &&
	touch gpg/foo &&
	but add gpg &&
	test_tick &&
	but cummit -S -m "Adding gpg" &&

	but log -1 --format="%s" > expect &&
	but filter-branch -f --msg-filter "cat" &&
	but log -1 --format="%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'Tag name filtering allows slashes in tag names' '
	but tag -m tag-with-slash X/1 &&
	but cat-file tag X/1 | sed -e s,X/1,X/2, > expect &&
	but filter-branch -f --tag-name-filter "echo X/2" &&
	but cat-file tag X/2 > actual &&
	test_cmp expect actual
'
test_expect_success 'setup --prune-empty comparisons' '
	but checkout --orphan main-no-a &&
	but rm -rf . &&
	unset test_tick &&
	test_tick &&
	GIT_CUMMITTER_DATE="@0 +0000" GIT_AUTHOR_DATE="@0 +0000" &&
	test_cummit --notick B B.t B Bx &&
	but checkout -b branch-no-a Bx &&
	test_cummit D D.t D Dx &&
	mkdir dir &&
	test_cummit dir/D dir/D.t dir/D dir/Dx &&
	test_cummit E E.t E Ex &&
	but checkout main-no-a &&
	test_cummit C C.t C Cx &&
	but checkout branch-no-a &&
	but merge Cx -m "Merge tag '\''C'\'' into branch" &&
	but tag Fx &&
	test_cummit G G.t G Gx &&
	test_commit H H.t H Hx &&
	but checkout branch
'

test_expect_success 'Prune empty cummits' '
	but rev-list HEAD > expect &&
	test_cummit to_remove &&
	but filter-branch -f --index-filter "but update-index --remove to_remove.t" --prune-empty HEAD &&
	but rev-list HEAD > actual &&
	test_cmp expect actual
'

test_expect_success 'prune empty collapsed merges' '
	test_config merge.ff false &&
	but rev-list HEAD >expect &&
	test_cummit to_remove_2 &&
	but reset --hard HEAD^ &&
	test_merge non-ff to_remove_2 &&
	but filter-branch -f --index-filter "but update-index --remove to_remove_2.t" --prune-empty HEAD &&
	but rev-list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'prune empty works even without index/tree filters' '
	but rev-list HEAD >expect &&
	but cummit --allow-empty -m empty &&
	but filter-branch -f --prune-empty HEAD &&
	but rev-list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--prune-empty is able to prune root cummit' '
	but rev-list branch-no-a >expect &&
	but branch testing H &&
	but filter-branch -f --prune-empty --index-filter "but update-index --remove A.t" testing &&
	but rev-list testing >actual &&
	but branch -D testing &&
	test_cmp expect actual
'

test_expect_success '--prune-empty is able to prune entire branch' '
	but branch prune-entire B &&
	but filter-branch -f --prune-empty --index-filter "but update-index --remove A.t B.t" prune-entire &&
	test_must_fail but rev-parse refs/heads/prune-entire &&
	if test_have_prereq REFFILES
	then
		test_must_fail but reflog exists refs/heads/prune-entire
	fi
'

test_expect_success '--remap-to-ancestor with filename filters' '
	but checkout main &&
	but reset --hard A &&
	test_cummit add-foo foo 1 &&
	but branch moved-foo &&
	test_cummit add-bar bar a &&
	but branch invariant &&
	orig_invariant=$(but rev-parse invariant) &&
	but branch moved-bar &&
	test_cummit change-foo foo 2 &&
	but filter-branch -f --remap-to-ancestor \
		moved-foo moved-bar A..main \
		-- -- foo &&
	test $(but rev-parse moved-foo) = $(but rev-parse moved-bar) &&
	test $(but rev-parse moved-foo) = $(but rev-parse main^) &&
	test $orig_invariant = $(but rev-parse invariant)
'

test_expect_success 'automatic remapping to ancestor with filename filters' '
	but checkout main &&
	but reset --hard A &&
	test_cummit add-foo2 foo 1 &&
	but branch moved-foo2 &&
	test_cummit add-bar2 bar a &&
	but branch invariant2 &&
	orig_invariant=$(but rev-parse invariant2) &&
	but branch moved-bar2 &&
	test_cummit change-foo2 foo 2 &&
	but filter-branch -f \
		moved-foo2 moved-bar2 A..main \
		-- -- foo &&
	test $(but rev-parse moved-foo2) = $(but rev-parse moved-bar2) &&
	test $(but rev-parse moved-foo2) = $(but rev-parse main^) &&
	test $orig_invariant = $(but rev-parse invariant2)
'

test_expect_success 'setup submodule' '
	rm -fr ?* .but &&
	but init &&
	test_cummit file &&
	mkdir submod &&
	submodurl="$PWD/submod" &&
	( cd submod &&
	  but init &&
	  test_cummit file-in-submod ) &&
	but submodule add "$submodurl" &&
	but cummit -m "added submodule" &&
	test_cummit add-file &&
	( cd submod && test_cummit add-in-submodule ) &&
	but add submod &&
	but cummit -m "changed submodule" &&
	but branch original HEAD
'

orig_head=$(but show-ref --hash --head HEAD)

test_expect_success 'rewrite submodule with another content' '
	but filter-branch --tree-filter "test -d submod && {
					 rm -rf submod &&
					 but rm -rf --quiet submod &&
					 mkdir submod &&
					 : > submod/file
					 } || :" HEAD &&
	test $orig_head != $(but show-ref --hash --head HEAD)
'

test_expect_success 'replace submodule revision' '
	invalid=$(test_oid numeric) &&
	but reset --hard original &&
	but filter-branch -f --tree-filter \
	    "if but ls-files --error-unmatch -- submod > /dev/null 2>&1
	     then but update-index --cacheinfo 160000 $invalid submod
	     fi" HEAD &&
	test $orig_head != $(but show-ref --hash --head HEAD)
'

test_expect_success 'filter cummit message without trailing newline' '
	but reset --hard original &&
	cummit=$(printf "no newline" | but cummit-tree HEAD^{tree}) &&
	but update-ref refs/heads/no-newline $cummit &&
	but filter-branch -f refs/heads/no-newline &&
	echo $cummit >expect &&
	but rev-parse refs/heads/no-newline >actual &&
	test_cmp expect actual
'

test_expect_success 'tree-filter deals with object name vs pathname ambiguity' '
	test_when_finished "but reset --hard original" &&
	ambiguous=$(but rev-list -1 HEAD) &&
	but filter-branch --tree-filter "mv file.t $ambiguous" HEAD^.. &&
	but show HEAD:$ambiguous
'

test_expect_success 'rewrite repository including refs that point at non-cummit object' '
	test_when_finished "but reset --hard original" &&
	tree=$(but rev-parse HEAD^{tree}) &&
	test_when_finished "but replace -d $tree" &&
	echo A >new &&
	but add new &&
	new_tree=$(but write-tree) &&
	but replace $tree $new_tree &&
	but tag -a -m "tag to a tree" treetag $new_tree &&
	but reset --hard HEAD &&
	but filter-branch -f -- --all >filter-output 2>&1 &&
	! fgrep fatal filter-output
'

test_expect_success 'filter-branch handles ref deletion' '
	but switch --orphan empty-cummit &&
	but cummit --allow-empty -m "empty cummit" &&
	but tag empty &&
	but branch to-delete &&
	but filter-branch -f --prune-empty to-delete >out 2>&1 &&
	grep "to-delete.*was deleted" out &&
	test_must_fail but rev-parse --verify to-delete
'

test_expect_success 'filter-branch handles ref rewrite' '
	but checkout empty &&
	test_cummit to-drop &&
	but branch rewrite &&
	but filter-branch -f \
		--index-filter "but rm --ignore-unmatch --cached to-drop.t" \
		 rewrite >out 2>&1 &&
	grep "rewrite.*was rewritten" out &&
	! grep -i warning out &&
	but diff-tree empty rewrite
'

test_expect_success 'filter-branch handles ancestor rewrite' '
	test_cummit to-exclude &&
	but branch ancestor &&
	but filter-branch -f ancestor -- :^to-exclude.t >out 2>&1 &&
	grep "ancestor.*was rewritten" out &&
	! grep -i warning out &&
	but diff-tree HEAD^ ancestor
'

test_done
