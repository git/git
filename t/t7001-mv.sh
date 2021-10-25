#!/bin/sh

test_description='git mv in subdirs'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-data.sh

test_expect_success 'prepare reference tree' '
	mkdir path0 path1 &&
	COPYING_test_data >path0/COPYING &&
	git add path0/COPYING &&
	git commit -m add -a
'

test_expect_success 'moving the file out of subdirectory' '
	git -C path0 mv COPYING ../path1/COPYING
'

# in path0 currently
test_expect_success 'commiting the change' '
	git commit -m move-out -a
'

test_expect_success 'checking the commit' '
	git diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path0/COPYING..*path1/COPYING" actual
'

test_expect_success 'moving the file back into subdirectory' '
	git -C path0 mv ../path1/COPYING COPYING
'

# in path0 currently
test_expect_success 'commiting the change' '
	git commit -m move-in -a
'

test_expect_success 'checking the commit' '
	git diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path1/COPYING..*path0/COPYING" actual
'

test_expect_success 'mv --dry-run does not move file' '
	git mv -n path0/COPYING MOVED &&
	test -f path0/COPYING &&
	test ! -f MOVED
'

test_expect_success 'checking -k on non-existing file' '
	git mv -k idontexist path0
'

test_expect_success 'checking -k on untracked file' '
	>untracked1 &&
	git mv -k untracked1 path0 &&
	test -f untracked1 &&
	test ! -f path0/untracked1
'

test_expect_success 'checking -k on multiple untracked files' '
	>untracked2 &&
	git mv -k untracked1 untracked2 path0 &&
	test -f untracked1 &&
	test -f untracked2 &&
	test ! -f path0/untracked1 &&
	test ! -f path0/untracked2
'

test_expect_success 'checking -f on untracked file with existing target' '
	>path0/untracked1 &&
	test_must_fail git mv -f untracked1 path0 &&
	test ! -f .git/index.lock &&
	test -f untracked1 &&
	test -f path0/untracked1
'

# clean up the mess in case bad things happen
rm -f idontexist untracked1 untracked2 \
     path0/idontexist path0/untracked1 path0/untracked2 \
     .git/index.lock
rmdir path1

test_expect_success 'moving to absent target with trailing slash' '
	test_must_fail git mv path0/COPYING no-such-dir/ &&
	test_must_fail git mv path0/COPYING no-such-dir// &&
	git mv path0/ no-such-dir/ &&
	test_path_is_dir no-such-dir
'

test_expect_success 'clean up' '
	git reset --hard
'

test_expect_success 'moving to existing untracked target with trailing slash' '
	mkdir path1 &&
	git mv path0/ path1/ &&
	test_path_is_dir path1/path0/
'

test_expect_success 'moving to existing tracked target with trailing slash' '
	mkdir path2 &&
	>path2/file && git add path2/file &&
	git mv path1/path0/ path2/ &&
	test_path_is_dir path2/path0/
'

test_expect_success 'clean up' '
	git reset --hard
'

test_expect_success 'adding another file' '
	COPYING_test_data | tr A-Za-z N-ZA-Mn-za-m >path0/README &&
	git add path0/README &&
	git commit -m add2 -a
'

test_expect_success 'moving whole subdirectory' '
	git mv path0 path2
'

test_expect_success 'commiting the change' '
	git commit -m dir-move -a
'

test_expect_success 'checking the commit' '
	git diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path0/COPYING..*path2/COPYING" actual &&
	grep "^R100..*path0/README..*path2/README" actual
'

test_expect_success 'succeed when source is a prefix of destination' '
	git mv path2/COPYING path2/COPYING-renamed
'

test_expect_success 'moving whole subdirectory into subdirectory' '
	git mv path2 path1
'

test_expect_success 'commiting the change' '
	git commit -m dir-move -a
'

test_expect_success 'checking the commit' '
	git diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path2/COPYING..*path1/path2/COPYING" actual &&
	grep "^R100..*path2/README..*path1/path2/README" actual
'

test_expect_success 'do not move directory over existing directory' '
	mkdir path0 &&
	mkdir path0/path2 &&
	test_must_fail git mv path2 path0
'

test_expect_success 'move into "."' '
	git mv path1/path2/ .
'

test_expect_success "Michael Cassar's test case" '
	rm -fr .git papers partA &&
	git init &&
	mkdir -p papers/unsorted papers/all-papers partA &&
	echo a >papers/unsorted/Thesis.pdf &&
	echo b >partA/outline.txt &&
	echo c >papers/unsorted/_another &&
	git add papers partA &&
	T1=$(git write-tree) &&

	git mv papers/unsorted/Thesis.pdf papers/all-papers/moo-blah.pdf &&

	T=$(git write-tree) &&
	git ls-tree -r $T | verbose grep partA/outline.txt
'

rm -fr papers partA path?

test_expect_success "Sergey Vlasov's test case" '
	rm -fr .git &&
	git init &&
	mkdir ab &&
	date >ab.c &&
	date >ab/d &&
	git add ab.c ab &&
	git commit -m "initial" &&
	git mv ab a
'

test_expect_success 'absolute pathname' '
	(
		rm -fr mine &&
		mkdir mine &&
		cd mine &&
		test_create_repo one &&
		cd one &&
		mkdir sub &&
		>sub/file &&
		git add sub/file &&

		git mv sub "$(pwd)/in" &&
		! test -d sub &&
		test -d in &&
		git ls-files --error-unmatch in/file
	)
'

test_expect_success 'absolute pathname outside should fail' '
	(
		rm -fr mine &&
		mkdir mine &&
		cd mine &&
		out=$(pwd) &&
		test_create_repo one &&
		cd one &&
		mkdir sub &&
		>sub/file &&
		git add sub/file &&

		test_must_fail git mv sub "$out/out" &&
		test -d sub &&
		! test -d ../in &&
		git ls-files --error-unmatch sub/file
	)
'

test_expect_success 'git mv to move multiple sources into a directory' '
	rm -fr .git && git init &&
	mkdir dir other &&
	>dir/a.txt &&
	>dir/b.txt &&
	git add dir/?.txt &&
	git mv dir/a.txt dir/b.txt other &&
	git ls-files >actual &&
	cat >expect <<-\EOF &&
	other/a.txt
	other/b.txt
	EOF
	test_cmp expect actual
'

test_expect_success 'git mv should not change sha1 of moved cache entry' '
	rm -fr .git &&
	git init &&
	echo 1 >dirty &&
	git add dirty &&
	entry="$(git ls-files --stage dirty | cut -f 1)" &&
	git mv dirty dirty2 &&
	test "$entry" = "$(git ls-files --stage dirty2 | cut -f 1)" &&
	echo 2 >dirty2 &&
	git mv dirty2 dirty &&
	test "$entry" = "$(git ls-files --stage dirty | cut -f 1)"
'

rm -f dirty dirty2

# NB: This test is about the error message
# as well as the failure.
test_expect_success 'git mv error on conflicted file' '
	rm -fr .git &&
	git init &&
	>conflict &&
	test_when_finished "rm -f conflict" &&
	cfhash=$(git hash-object -w conflict) &&
	q_to_tab <<-EOF | git update-index --index-info &&
	0 $cfhash 0Qconflict
	100644 $cfhash 1Qconflict
	EOF

	test_must_fail git mv conflict newname 2>actual &&
	test_i18ngrep "conflicted" actual
'

test_expect_success 'git mv should overwrite symlink to a file' '
	rm -fr .git &&
	git init &&
	echo 1 >moved &&
	test_ln_s_add moved symlink &&
	git add moved &&
	test_must_fail git mv moved symlink &&
	git mv -f moved symlink &&
	! test -e moved &&
	test -f symlink &&
	test "$(cat symlink)" = 1 &&
	git update-index --refresh &&
	git diff-files --quiet
'

rm -f moved symlink

test_expect_success 'git mv should overwrite file with a symlink' '
	rm -fr .git &&
	git init &&
	echo 1 >moved &&
	test_ln_s_add moved symlink &&
	git add moved &&
	test_must_fail git mv symlink moved &&
	git mv -f symlink moved &&
	! test -e symlink &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success SYMLINKS 'check moved symlink' '
	test -h moved
'

rm -f moved symlink

test_expect_success 'setup submodule' '
	git commit -m initial &&
	git reset --hard &&
	git submodule add ./. sub &&
	echo content >file &&
	git add file &&
	git commit -m "added sub and file" &&
	mkdir -p deep/directory/hierarchy &&
	git submodule add ./. deep/directory/hierarchy/sub &&
	git commit -m "added another submodule" &&
	git branch submodule
'

test_expect_success 'git mv cannot move a submodule in a file' '
	test_must_fail git mv sub file
'

test_expect_success 'git mv moves a submodule with a .git directory and no .gitmodules' '
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	git rm .gitmodules &&
	(
		cd sub &&
		rm -f .git &&
		cp -R -P -p ../.git/modules/sub .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	) &&
	mkdir mod &&
	git mv sub mod/sub &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'git mv moves a submodule with a .git directory and .gitmodules' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	(
		cd sub &&
		rm -f .git &&
		cp -R -P -p ../.git/modules/sub .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	) &&
	mkdir mod &&
	git mv sub mod/sub &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	echo mod/sub >expected &&
	git config -f .gitmodules submodule.sub.path >actual &&
	test_cmp expected actual &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'git mv moves a submodule with gitfile' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	git -C mod mv ../sub/ . &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	echo mod/sub >expected &&
	git config -f .gitmodules submodule.sub.path >actual &&
	test_cmp expected actual &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'mv does not complain when no .gitmodules file is found' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	git rm .gitmodules &&
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	git mv sub mod/sub 2>actual.err &&
	test_must_be_empty actual.err &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'mv will error out on a modified .gitmodules file unless staged' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	git config -f .gitmodules foo.bar true &&
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	test_must_fail git mv sub mod/sub 2>actual.err &&
	test -s actual.err &&
	test -e sub &&
	git diff-files --quiet -- sub &&
	git add .gitmodules &&
	git mv sub mod/sub 2>actual.err &&
	test_must_be_empty actual.err &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'mv issues a warning when section is not found in .gitmodules' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	git config -f .gitmodules --remove-section submodule.sub &&
	git add .gitmodules &&
	entry="$(git ls-files --stage sub | cut -f 1)" &&
	echo "warning: Could not find section in .gitmodules where path=sub" >expect.err &&
	mkdir mod &&
	git mv sub mod/sub 2>actual.err &&
	test_cmp expect.err actual.err &&
	! test -e sub &&
	test "$entry" = "$(git ls-files --stage mod/sub | cut -f 1)" &&
	git -C mod/sub status &&
	git update-index --refresh &&
	git diff-files --quiet
'

test_expect_success 'mv --dry-run does not touch the submodule or .gitmodules' '
	rm -rf mod &&
	git reset --hard &&
	git submodule update &&
	mkdir mod &&
	git mv -n sub mod/sub 2>actual.err &&
	test -f sub/.git &&
	git diff-index --exit-code HEAD &&
	git update-index --refresh &&
	git diff-files --quiet -- sub .gitmodules
'

test_expect_success 'checking out a commit before submodule moved needs manual updates' '
	git mv sub sub2 &&
	git commit -m "moved sub to sub2" &&
	git checkout -q HEAD^ 2>actual &&
	test_i18ngrep "^warning: unable to rmdir '\''sub2'\'':" actual &&
	git status -s sub2 >actual &&
	echo "?? sub2/" >expected &&
	test_cmp expected actual &&
	! test -f sub/.git &&
	test -f sub2/.git &&
	git submodule update &&
	test -f sub/.git &&
	rm -rf sub2 &&
	git diff-index --exit-code HEAD &&
	git update-index --refresh &&
	git diff-files --quiet -- sub .gitmodules &&
	git status -s sub2 >actual &&
	test_must_be_empty actual
'

test_expect_success 'mv -k does not accidentally destroy submodules' '
	git checkout submodule &&
	mkdir dummy dest &&
	git mv -k dummy sub dest &&
	git status --porcelain >actual &&
	grep "^R  sub -> dest/sub" actual &&
	git reset --hard &&
	git checkout .
'

test_expect_success 'moving a submodule in nested directories' '
	(
		cd deep &&
		git mv directory ../ &&
		# git status would fail if the update of linking git dir to
		# work dir of the submodule failed.
		git status &&
		git config -f ../.gitmodules submodule.deep/directory/hierarchy/sub.path >../actual &&
		echo "directory/hierarchy/sub" >../expect
	) &&
	test_cmp expect actual
'

test_expect_success 'moving nested submodules' '
	git commit -am "cleanup commit" &&
	mkdir sub_nested_nested &&
	(
		cd sub_nested_nested &&
		>nested_level2 &&
		git init &&
		git add . &&
		git commit -m "nested level 2"
	) &&
	mkdir sub_nested &&
	(
		cd sub_nested &&
		>nested_level1 &&
		git init &&
		git add . &&
		git commit -m "nested level 1" &&
		git submodule add ../sub_nested_nested &&
		git commit -m "add nested level 2"
	) &&
	git submodule add ./sub_nested nested_move &&
	git commit -m "add nested_move" &&
	git submodule update --init --recursive &&
	git mv nested_move sub_nested_moved &&
	git status
'

test_done
