#!/bin/sh

test_description='but mv in subdirs'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-data.sh

test_expect_success 'mv -f refreshes updated index entry' '
	echo test >bar &&
	but add bar &&
	but cummit -m test &&

	echo foo >foo &&
	but add foo &&

	# Wait one second to ensure ctime of rename will differ from original
	# file creation ctime.
	sleep 1 &&
	but mv -f foo bar &&
	but reset --merge HEAD &&

	# Verify the index has been reset
	but diff-files >out &&
	test_must_be_empty out
'

test_expect_success 'prepare reference tree' '
	mkdir path0 path1 &&
	COPYING_test_data >path0/COPYING &&
	but add path0/COPYING &&
	but cummit -m add -a
'

test_expect_success 'moving the file out of subdirectory' '
	but -C path0 mv COPYING ../path1/COPYING
'

# in path0 currently
test_expect_success 'cummiting the change' '
	but cummit -m move-out -a
'

test_expect_success 'checking the cummit' '
	but diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path0/COPYING..*path1/COPYING" actual
'

test_expect_success 'moving the file back into subdirectory' '
	but -C path0 mv ../path1/COPYING COPYING
'

# in path0 currently
test_expect_success 'cummiting the change' '
	but cummit -m move-in -a
'

test_expect_success 'checking the cummit' '
	but diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path1/COPYING..*path0/COPYING" actual
'

test_expect_success 'mv --dry-run does not move file' '
	but mv -n path0/COPYING MOVED &&
	test -f path0/COPYING &&
	test ! -f MOVED
'

test_expect_success 'checking -k on non-existing file' '
	but mv -k idontexist path0
'

test_expect_success 'checking -k on untracked file' '
	>untracked1 &&
	but mv -k untracked1 path0 &&
	test -f untracked1 &&
	test ! -f path0/untracked1
'

test_expect_success 'checking -k on multiple untracked files' '
	>untracked2 &&
	but mv -k untracked1 untracked2 path0 &&
	test -f untracked1 &&
	test -f untracked2 &&
	test ! -f path0/untracked1 &&
	test ! -f path0/untracked2
'

test_expect_success 'checking -f on untracked file with existing target' '
	>path0/untracked1 &&
	test_must_fail but mv -f untracked1 path0 &&
	test ! -f .but/index.lock &&
	test -f untracked1 &&
	test -f path0/untracked1
'

# clean up the mess in case bad things happen
rm -f idontexist untracked1 untracked2 \
     path0/idontexist path0/untracked1 path0/untracked2 \
     .but/index.lock
rmdir path1

test_expect_success 'moving to absent target with trailing slash' '
	test_must_fail but mv path0/COPYING no-such-dir/ &&
	test_must_fail but mv path0/COPYING no-such-dir// &&
	but mv path0/ no-such-dir/ &&
	test_path_is_dir no-such-dir
'

test_expect_success 'clean up' '
	but reset --hard
'

test_expect_success 'moving to existing untracked target with trailing slash' '
	mkdir path1 &&
	but mv path0/ path1/ &&
	test_path_is_dir path1/path0/
'

test_expect_success 'moving to existing tracked target with trailing slash' '
	mkdir path2 &&
	>path2/file && but add path2/file &&
	but mv path1/path0/ path2/ &&
	test_path_is_dir path2/path0/
'

test_expect_success 'clean up' '
	but reset --hard
'

test_expect_success 'adding another file' '
	COPYING_test_data | tr A-Za-z N-ZA-Mn-za-m >path0/README &&
	but add path0/README &&
	but cummit -m add2 -a
'

test_expect_success 'moving whole subdirectory' '
	but mv path0 path2
'

test_expect_success 'cummiting the change' '
	but cummit -m dir-move -a
'

test_expect_success 'checking the cummit' '
	but diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path0/COPYING..*path2/COPYING" actual &&
	grep "^R100..*path0/README..*path2/README" actual
'

test_expect_success 'succeed when source is a prefix of destination' '
	but mv path2/COPYING path2/COPYING-renamed
'

test_expect_success 'moving whole subdirectory into subdirectory' '
	but mv path2 path1
'

test_expect_success 'cummiting the change' '
	but cummit -m dir-move -a
'

test_expect_success 'checking the cummit' '
	but diff-tree -r -M --name-status  HEAD^ HEAD >actual &&
	grep "^R100..*path2/COPYING..*path1/path2/COPYING" actual &&
	grep "^R100..*path2/README..*path1/path2/README" actual
'

test_expect_success 'do not move directory over existing directory' '
	mkdir path0 &&
	mkdir path0/path2 &&
	test_must_fail but mv path2 path0
'

test_expect_success 'move into "."' '
	but mv path1/path2/ .
'

test_expect_success "Michael Cassar's test case" '
	rm -fr .but papers partA &&
	but init &&
	mkdir -p papers/unsorted papers/all-papers partA &&
	echo a >papers/unsorted/Thesis.pdf &&
	echo b >partA/outline.txt &&
	echo c >papers/unsorted/_another &&
	but add papers partA &&
	T1=$(but write-tree) &&

	but mv papers/unsorted/Thesis.pdf papers/all-papers/moo-blah.pdf &&

	T=$(but write-tree) &&
	but ls-tree -r $T | verbose grep partA/outline.txt
'

rm -fr papers partA path?

test_expect_success "Sergey Vlasov's test case" '
	rm -fr .but &&
	but init &&
	mkdir ab &&
	date >ab.c &&
	date >ab/d &&
	but add ab.c ab &&
	but cummit -m "initial" &&
	but mv ab a
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
		but add sub/file &&

		but mv sub "$(pwd)/in" &&
		! test -d sub &&
		test -d in &&
		but ls-files --error-unmatch in/file
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
		but add sub/file &&

		test_must_fail but mv sub "$out/out" &&
		test -d sub &&
		! test -d ../in &&
		but ls-files --error-unmatch sub/file
	)
'

test_expect_success 'but mv to move multiple sources into a directory' '
	rm -fr .but && but init &&
	mkdir dir other &&
	>dir/a.txt &&
	>dir/b.txt &&
	but add dir/?.txt &&
	but mv dir/a.txt dir/b.txt other &&
	but ls-files >actual &&
	cat >expect <<-\EOF &&
	other/a.txt
	other/b.txt
	EOF
	test_cmp expect actual
'

test_expect_success 'but mv should not change sha1 of moved cache entry' '
	rm -fr .but &&
	but init &&
	echo 1 >dirty &&
	but add dirty &&
	entry="$(but ls-files --stage dirty | cut -f 1)" &&
	but mv dirty dirty2 &&
	test "$entry" = "$(but ls-files --stage dirty2 | cut -f 1)" &&
	echo 2 >dirty2 &&
	but mv dirty2 dirty &&
	test "$entry" = "$(but ls-files --stage dirty | cut -f 1)"
'

rm -f dirty dirty2

# NB: This test is about the error message
# as well as the failure.
test_expect_success 'but mv error on conflicted file' '
	rm -fr .but &&
	but init &&
	>conflict &&
	test_when_finished "rm -f conflict" &&
	cfhash=$(but hash-object -w conflict) &&
	q_to_tab <<-EOF | but update-index --index-info &&
	0 $cfhash 0Qconflict
	100644 $cfhash 1Qconflict
	EOF

	test_must_fail but mv conflict newname 2>actual &&
	test_i18ngrep "conflicted" actual
'

test_expect_success 'but mv should overwrite symlink to a file' '
	rm -fr .but &&
	but init &&
	echo 1 >moved &&
	test_ln_s_add moved symlink &&
	but add moved &&
	test_must_fail but mv moved symlink &&
	but mv -f moved symlink &&
	! test -e moved &&
	test -f symlink &&
	test "$(cat symlink)" = 1 &&
	but update-index --refresh &&
	but diff-files --quiet
'

rm -f moved symlink

test_expect_success 'but mv should overwrite file with a symlink' '
	rm -fr .but &&
	but init &&
	echo 1 >moved &&
	test_ln_s_add moved symlink &&
	but add moved &&
	test_must_fail but mv symlink moved &&
	but mv -f symlink moved &&
	! test -e symlink &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success SYMLINKS 'check moved symlink' '
	test -h moved
'

rm -f moved symlink

test_expect_success 'setup submodule' '
	but cummit -m initial &&
	but reset --hard &&
	but submodule add ./. sub &&
	echo content >file &&
	but add file &&
	but cummit -m "added sub and file" &&
	mkdir -p deep/directory/hierarchy &&
	but submodule add ./. deep/directory/hierarchy/sub &&
	but cummit -m "added another submodule" &&
	but branch submodule
'

test_expect_success 'but mv cannot move a submodule in a file' '
	test_must_fail but mv sub file
'

test_expect_success 'but mv moves a submodule with a .but directory and no .butmodules' '
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	but rm .butmodules &&
	(
		cd sub &&
		rm -f .but &&
		cp -R -P -p ../.but/modules/sub .but &&
		GIT_WORK_TREE=. but config --unset core.worktree
	) &&
	mkdir mod &&
	but mv sub mod/sub &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'but mv moves a submodule with a .but directory and .butmodules' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	(
		cd sub &&
		rm -f .but &&
		cp -R -P -p ../.but/modules/sub .but &&
		GIT_WORK_TREE=. but config --unset core.worktree
	) &&
	mkdir mod &&
	but mv sub mod/sub &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	echo mod/sub >expected &&
	but config -f .butmodules submodule.sub.path >actual &&
	test_cmp expected actual &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'but mv moves a submodule with butfile' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	but -C mod mv ../sub/ . &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	echo mod/sub >expected &&
	but config -f .butmodules submodule.sub.path >actual &&
	test_cmp expected actual &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'mv does not complain when no .butmodules file is found' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	but rm .butmodules &&
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	but mv sub mod/sub 2>actual.err &&
	test_must_be_empty actual.err &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'mv will error out on a modified .butmodules file unless staged' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	but config -f .butmodules foo.bar true &&
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	mkdir mod &&
	test_must_fail but mv sub mod/sub 2>actual.err &&
	test -s actual.err &&
	test -e sub &&
	but diff-files --quiet -- sub &&
	but add .butmodules &&
	but mv sub mod/sub 2>actual.err &&
	test_must_be_empty actual.err &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'mv issues a warning when section is not found in .butmodules' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	but config -f .butmodules --remove-section submodule.sub &&
	but add .butmodules &&
	entry="$(but ls-files --stage sub | cut -f 1)" &&
	echo "warning: Could not find section in .butmodules where path=sub" >expect.err &&
	mkdir mod &&
	but mv sub mod/sub 2>actual.err &&
	test_cmp expect.err actual.err &&
	! test -e sub &&
	test "$entry" = "$(but ls-files --stage mod/sub | cut -f 1)" &&
	but -C mod/sub status &&
	but update-index --refresh &&
	but diff-files --quiet
'

test_expect_success 'mv --dry-run does not touch the submodule or .butmodules' '
	rm -rf mod &&
	but reset --hard &&
	but submodule update &&
	mkdir mod &&
	but mv -n sub mod/sub 2>actual.err &&
	test -f sub/.but &&
	but diff-index --exit-code HEAD &&
	but update-index --refresh &&
	but diff-files --quiet -- sub .butmodules
'

test_expect_success 'checking out a cummit before submodule moved needs manual updates' '
	but mv sub sub2 &&
	but cummit -m "moved sub to sub2" &&
	but checkout -q HEAD^ 2>actual &&
	test_i18ngrep "^warning: unable to rmdir '\''sub2'\'':" actual &&
	but status -s sub2 >actual &&
	echo "?? sub2/" >expected &&
	test_cmp expected actual &&
	! test -f sub/.but &&
	test -f sub2/.but &&
	but submodule update &&
	test -f sub/.but &&
	rm -rf sub2 &&
	but diff-index --exit-code HEAD &&
	but update-index --refresh &&
	but diff-files --quiet -- sub .butmodules &&
	but status -s sub2 >actual &&
	test_must_be_empty actual
'

test_expect_success 'mv -k does not accidentally destroy submodules' '
	but checkout submodule &&
	mkdir dummy dest &&
	but mv -k dummy sub dest &&
	but status --porcelain >actual &&
	grep "^R  sub -> dest/sub" actual &&
	but reset --hard &&
	but checkout .
'

test_expect_success 'moving a submodule in nested directories' '
	(
		cd deep &&
		but mv directory ../ &&
		# but status would fail if the update of linking but dir to
		# work dir of the submodule failed.
		but status &&
		but config -f ../.butmodules submodule.deep/directory/hierarchy/sub.path >../actual &&
		echo "directory/hierarchy/sub" >../expect
	) &&
	test_cmp expect actual
'

test_expect_success 'moving nested submodules' '
	but cummit -am "cleanup cummit" &&
	mkdir sub_nested_nested &&
	(
		cd sub_nested_nested &&
		>nested_level2 &&
		but init &&
		but add . &&
		but cummit -m "nested level 2"
	) &&
	mkdir sub_nested &&
	(
		cd sub_nested &&
		>nested_level1 &&
		but init &&
		but add . &&
		but cummit -m "nested level 1" &&
		but submodule add ../sub_nested_nested &&
		but cummit -m "add nested level 2"
	) &&
	but submodule add ./sub_nested nested_move &&
	but cummit -m "add nested_move" &&
	but submodule update --init --recursive &&
	but mv nested_move sub_nested_moved &&
	but status
'

test_done
