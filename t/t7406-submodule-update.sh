#!/bin/sh
#
# Copyright (c) 2009 Red Hat, Inc.
#

test_description='Test updating submodules

This test verifies that "git submodule update" detaches the HEAD of the
submodule and "git submodule update --rebase/--merge" does not detach the HEAD.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


compare_head()
{
    sha_main=$(git rev-list --max-count=1 main)
    sha_head=$(git rev-list --max-count=1 HEAD)

    test "$sha_main" = "$sha_head"
}


test_expect_success 'setup a submodule tree' '
	git config --global protocol.file.allow always &&
	echo file > file &&
	git add file &&
	test_tick &&
	git commit -m upstream &&
	git clone . super &&
	git clone super submodule &&
	git clone super rebasing &&
	git clone super merging &&
	git clone super none &&
	(cd super &&
	 git submodule add ../submodule submodule &&
	 test_tick &&
	 git commit -m "submodule" &&
	 git submodule init submodule
	) &&
	(cd submodule &&
	echo "line2" > file &&
	git add file &&
	git commit -m "Commit 2"
	) &&
	(cd super &&
	 (cd submodule &&
	  git pull --rebase origin
	 ) &&
	 git add submodule &&
	 git commit -m "submodule update"
	) &&
	(cd super &&
	 git submodule add ../rebasing rebasing &&
	 test_tick &&
	 git commit -m "rebasing"
	) &&
	(cd super &&
	 git submodule add ../merging merging &&
	 test_tick &&
	 git commit -m "rebasing"
	) &&
	(cd super &&
	 git submodule add ../none none &&
	 test_tick &&
	 git commit -m "none"
	) &&
	git clone . recursivesuper &&
	( cd recursivesuper &&
	 git submodule add ../super super
	)
'

test_expect_success 'update --remote falls back to using HEAD' '
	test_create_repo main-branch-submodule &&
	test_commit -C main-branch-submodule initial &&

	test_create_repo main-branch &&
	git -C main-branch submodule add ../main-branch-submodule &&
	git -C main-branch commit -m add-submodule &&

	git -C main-branch-submodule switch -c hello &&
	test_commit -C main-branch-submodule world &&

	git clone --recursive main-branch main-branch-clone &&
	git -C main-branch-clone submodule update --remote main-branch-submodule &&
	test_path_exists main-branch-clone/main-branch-submodule/world.t
'

test_expect_success 'submodule update detaching the HEAD ' '
	(cd super/submodule &&
	 git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update from subdirectory' '
	(cd super/submodule &&
	 git reset --hard HEAD~1
	) &&
	mkdir super/sub &&
	(cd super/sub &&
	 (cd ../submodule &&
	  compare_head
	 ) &&
	 git submodule update ../submodule &&
	 cd ../submodule &&
	 ! compare_head
	)
'

supersha1=$(git -C super rev-parse HEAD)
mergingsha1=$(git -C super/merging rev-parse HEAD)
nonesha1=$(git -C super/none rev-parse HEAD)
rebasingsha1=$(git -C super/rebasing rev-parse HEAD)
submodulesha1=$(git -C super/submodule rev-parse HEAD)
pwd=$(pwd)

cat <<EOF >expect
Submodule path '../super': checked out '$supersha1'
Submodule path '../super/merging': checked out '$mergingsha1'
Submodule path '../super/none': checked out '$nonesha1'
Submodule path '../super/rebasing': checked out '$rebasingsha1'
Submodule path '../super/submodule': checked out '$submodulesha1'
EOF

cat <<EOF >expect2
Cloning into '$pwd/recursivesuper/super/merging'...
Cloning into '$pwd/recursivesuper/super/none'...
Cloning into '$pwd/recursivesuper/super/rebasing'...
Cloning into '$pwd/recursivesuper/super/submodule'...
Submodule 'merging' ($pwd/merging) registered for path '../super/merging'
Submodule 'none' ($pwd/none) registered for path '../super/none'
Submodule 'rebasing' ($pwd/rebasing) registered for path '../super/rebasing'
Submodule 'submodule' ($pwd/submodule) registered for path '../super/submodule'
done.
done.
done.
done.
EOF

test_expect_success 'submodule update --init --recursive from subdirectory' '
	git -C recursivesuper/super reset --hard HEAD^ &&
	(cd recursivesuper &&
	 mkdir tmp &&
	 cd tmp &&
	 git submodule update --init --recursive ../super >../../actual 2>../../actual2
	) &&
	test_cmp expect actual &&
	sort actual2 >actual2.sorted &&
	test_cmp expect2 actual2.sorted
'

cat <<EOF >expect2
Submodule 'foo/sub' ($pwd/withsubs/../rebasing) registered for path 'sub'
EOF

test_expect_success 'submodule update --init from and of subdirectory' '
	git init withsubs &&
	(cd withsubs &&
	 mkdir foo &&
	 git submodule add "$(pwd)/../rebasing" foo/sub &&
	 (cd foo &&
	  git submodule deinit -f sub &&
	  git submodule update --init sub 2>../../actual2
	 )
	) &&
	test_cmp expect2 actual2
'

test_expect_success 'submodule update does not fetch already present commits' '
	(cd submodule &&
	  echo line3 >> file &&
	  git add file &&
	  test_tick &&
	  git commit -m "upstream line3"
	) &&
	(cd super/submodule &&
	  head=$(git rev-parse --verify HEAD) &&
	  echo "Submodule path ${SQ}submodule$SQ: checked out $SQ$head$SQ" > ../../expected &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	  git submodule update > ../actual 2> ../actual.err
	) &&
	test_cmp expected actual &&
	test_must_be_empty actual.err
'

test_expect_success 'submodule update should fail due to local changes' '
	(cd super/submodule &&
	 git reset --hard HEAD~1 &&
	 echo "local change" > file
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 test_must_fail git submodule update submodule 2>../actual.raw
	) &&
	sed "s/^> //" >expect <<-\EOF &&
	> error: Your local changes to the following files would be overwritten by checkout:
	> 	file
	> Please commit your changes or stash them before you switch branches.
	> Aborting
	> fatal: Unable to checkout OID in submodule path '\''submodule'\''
	EOF
	sed -e "s/checkout $SQ[^$SQ]*$SQ/checkout OID/" <actual.raw >actual &&
	test_cmp expect actual

'
test_expect_success 'submodule update should throw away changes with --force ' '
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update --force submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update --force forcibly checks out submodules' '
	(cd super &&
	 (cd submodule &&
	  rm -f file
	 ) &&
	 git submodule update --force submodule &&
	 (cd submodule &&
	  test "$(git status -s file)" = ""
	 )
	)
'

test_expect_success 'submodule update --remote should fetch upstream changes' '
	(cd submodule &&
	 echo line4 >> file &&
	 git add file &&
	 test_tick &&
	 git commit -m "upstream line4"
	) &&
	(cd super &&
	 git submodule update --remote --force submodule &&
	 cd submodule &&
	 test "$(git log -1 --oneline)" = "$(GIT_DIR=../../submodule/.git git log -1 --oneline)"
	)
'

test_expect_success 'submodule update --remote should fetch upstream changes with .' '
	(
		cd super &&
		git config -f .gitmodules submodule."submodule".branch "." &&
		git add .gitmodules &&
		git commit -m "submodules: update from the respective superproject branch"
	) &&
	(
		cd submodule &&
		echo line4a >> file &&
		git add file &&
		test_tick &&
		git commit -m "upstream line4a" &&
		git checkout -b test-branch &&
		test_commit on-test-branch
	) &&
	(
		cd super &&
		git submodule update --remote --force submodule &&
		git -C submodule log -1 --oneline >actual &&
		git -C ../submodule log -1 --oneline main >expect &&
		test_cmp expect actual &&
		git checkout -b test-branch &&
		git submodule update --remote --force submodule &&
		git -C submodule log -1 --oneline >actual &&
		git -C ../submodule log -1 --oneline test-branch >expect &&
		test_cmp expect actual &&
		git checkout main &&
		git branch -d test-branch &&
		git reset --hard HEAD^
	)
'

test_expect_success 'local config should override .gitmodules branch' '
	(cd submodule &&
	 git checkout test-branch &&
	 echo line5 >> file &&
	 git add file &&
	 test_tick &&
	 git commit -m "upstream line5" &&
	 git checkout main
	) &&
	(cd super &&
	 git config submodule.submodule.branch test-branch &&
	 git submodule update --remote --force submodule &&
	 cd submodule &&
	 test "$(git log -1 --oneline)" = "$(GIT_DIR=../../submodule/.git git log -1 --oneline test-branch)"
	)
'

test_expect_success 'submodule update --rebase staying on main' '
	(cd super/submodule &&
	  git checkout main
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update --rebase submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update --merge staying on main' '
	(cd super/submodule &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update --merge submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - rebase in .git/config' '
	(cd super &&
	 git config submodule.submodule.update rebase
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .git/config but --rebase given' '
	(cd super &&
	 git config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update --rebase submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - merge in .git/config' '
	(cd super &&
	 git config submodule.submodule.update merge
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .git/config but --merge given' '
	(cd super &&
	 git config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update --merge submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .git/config' '
	(cd super &&
	 git config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD^
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update - command in .git/config' '
	(cd super &&
	 git config submodule.submodule.update "!git checkout"
	) &&
	(cd super/submodule &&
	  git reset --hard HEAD^
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update - command in .gitmodules is rejected' '
	test_when_finished "git -C super reset --hard HEAD^" &&
	git -C super config -f .gitmodules submodule.submodule.update "!false" &&
	git -C super commit -a -m "add command to .gitmodules file" &&
	git -C super/submodule reset --hard $submodulesha1^ &&
	test_must_fail git -C super submodule update submodule
'

test_expect_success 'fsck detects command in .gitmodules' '
	git init command-in-gitmodules &&
	(
		cd command-in-gitmodules &&
		git submodule add ../submodule submodule &&
		test_commit adding-submodule &&

		git config -f .gitmodules submodule.submodule.update "!false" &&
		git add .gitmodules &&
		test_commit configuring-update &&
		test_must_fail git fsck
	)
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path 'submodule'
EOF

test_expect_success 'submodule update - command in .git/config catches failure' '
	(cd super &&
	 git config submodule.submodule.update "!false"
	) &&
	(cd super/submodule &&
	  git reset --hard $submodulesha1^
	) &&
	(cd super &&
	 test_must_fail git submodule update submodule 2>../actual
	) &&
	test_cmp actual expect
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path '../submodule'
EOF

test_expect_success 'submodule update - command in .git/config catches failure -- subdirectory' '
	(cd super &&
	 git config submodule.submodule.update "!false"
	) &&
	(cd super/submodule &&
	  git reset --hard $submodulesha1^
	) &&
	(cd super &&
	 mkdir tmp && cd tmp &&
	 test_must_fail git submodule update ../submodule 2>../../actual
	) &&
	test_cmp actual expect
'

test_expect_success 'submodule update - command run for initial population of submodule' '
	cat >expect <<-EOF &&
	fatal: Execution of '\''false $submodulesha1'\'' failed in submodule path '\''submodule'\''
	EOF
	rm -rf super/submodule &&
	test_must_fail git -C super submodule update 2>actual &&
	test_cmp expect actual &&
	git -C super submodule update --checkout
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path '../super/submodule'
fatal: Failed to recurse into submodule path '../super'
EOF

test_expect_success 'recursive submodule update - command in .git/config catches failure -- subdirectory' '
	(cd recursivesuper &&
	 git submodule update --remote super &&
	 git add super &&
	 git commit -m "update to latest to have more than one commit in submodules"
	) &&
	git -C recursivesuper/super config submodule.submodule.update "!false" &&
	git -C recursivesuper/super/submodule reset --hard $submodulesha1^ &&
	(cd recursivesuper &&
	 mkdir -p tmp && cd tmp &&
	 test_must_fail git submodule update --recursive ../super 2>../../actual
	) &&
	test_cmp actual expect
'

test_expect_success 'submodule init does not copy command into .git/config' '
	test_when_finished "git -C super update-index --force-remove submodule1" &&
	test_when_finished git config -f super/.gitmodules \
		--remove-section submodule.submodule1 &&
	(cd super &&
	 git ls-files -s submodule >out &&
	 H=$(cut -d" " -f2 out) &&
	 mkdir submodule1 &&
	 git update-index --add --cacheinfo 160000 $H submodule1 &&
	 git config -f .gitmodules submodule.submodule1.path submodule1 &&
	 git config -f .gitmodules submodule.submodule1.url ../submodule &&
	 git config -f .gitmodules submodule.submodule1.update !false &&
	 test_must_fail git submodule init submodule1 &&
	 test_expect_code 1 git config submodule.submodule1.update >actual &&
	 test_must_be_empty actual
	)
'

test_expect_success 'submodule init picks up rebase' '
	(cd super &&
	 git config -f .gitmodules submodule.rebasing.update rebase &&
	 git submodule init rebasing &&
	 test "rebase" = "$(git config submodule.rebasing.update)"
	)
'

test_expect_success 'submodule init picks up merge' '
	(cd super &&
	 git config -f .gitmodules submodule.merging.update merge &&
	 git submodule init merging &&
	 test "merge" = "$(git config submodule.merging.update)"
	)
'

test_expect_success 'submodule update --merge  - ignores --merge  for new submodules' '
	test_config -C super submodule.submodule.update checkout &&
	(cd super &&
	 rm -rf submodule &&
	 git submodule update submodule &&
	 git status -s submodule >expect &&
	 rm -rf submodule &&
	 git submodule update --merge submodule &&
	 git status -s submodule >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update --rebase - ignores --rebase for new submodules' '
	test_config -C super submodule.submodule.update checkout &&
	(cd super &&
	 rm -rf submodule &&
	 git submodule update submodule &&
	 git status -s submodule >expect &&
	 rm -rf submodule &&
	 git submodule update --rebase submodule &&
	 git status -s submodule >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update ignores update=merge config for new submodules' '
	(cd super &&
	 rm -rf submodule &&
	 git submodule update submodule &&
	 git status -s submodule >expect &&
	 rm -rf submodule &&
	 git config submodule.submodule.update merge &&
	 git submodule update submodule &&
	 git status -s submodule >actual &&
	 git config --unset submodule.submodule.update &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update ignores update=rebase config for new submodules' '
	(cd super &&
	 rm -rf submodule &&
	 git submodule update submodule &&
	 git status -s submodule >expect &&
	 rm -rf submodule &&
	 git config submodule.submodule.update rebase &&
	 git submodule update submodule &&
	 git status -s submodule >actual &&
	 git config --unset submodule.submodule.update &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule init picks up update=none' '
	(cd super &&
	 git config -f .gitmodules submodule.none.update none &&
	 git submodule init none &&
	 test "none" = "$(git config submodule.none.update)"
	)
'

test_expect_success 'submodule update - update=none in .git/config' '
	(cd super &&
	 git config submodule.submodule.update none &&
	 (cd submodule &&
	  git checkout main &&
	  compare_head
	 ) &&
	 git diff --name-only >out &&
	 grep ^submodule$ out &&
	 git submodule update &&
	 git diff --name-only >out &&
	 grep ^submodule$ out &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 git config --unset submodule.submodule.update &&
	 git submodule update submodule
	)
'

test_expect_success 'submodule update - update=none in .git/config but --checkout given' '
	(cd super &&
	 git config submodule.submodule.update none &&
	 (cd submodule &&
	  git checkout main &&
	  compare_head
	 ) &&
	 git diff --name-only >out &&
	 grep ^submodule$ out &&
	 git submodule update --checkout &&
	 git diff --name-only >out &&
	 ! grep ^submodule$ out &&
	 (cd submodule &&
	  ! compare_head
	 ) &&
	 git config --unset submodule.submodule.update
	)
'

test_expect_success 'submodule update --init skips submodule with update=none' '
	(cd super &&
	 git add .gitmodules &&
	 git commit -m ".gitmodules"
	) &&
	git clone super cloned &&
	(cd cloned &&
	 git submodule update --init &&
	 test_path_exists submodule/.git &&
	 test_path_is_missing none/.git
	)
'

test_expect_success 'submodule update with pathspec warns against uninitialized ones' '
	test_when_finished "rm -fr selective" &&
	git clone super selective &&
	(
		cd selective &&
		git submodule init submodule &&

		git submodule update submodule 2>err &&
		! grep "Submodule path .* not initialized" err &&

		git submodule update rebasing 2>err &&
		grep "Submodule path .rebasing. not initialized" err &&

		test_path_exists submodule/.git &&
		test_path_is_missing rebasing/.git
	)

'

test_expect_success 'submodule update without pathspec updates only initialized ones' '
	test_when_finished "rm -fr selective" &&
	git clone super selective &&
	(
		cd selective &&
		git submodule init submodule &&
		git submodule update 2>err &&
		test_path_exists submodule/.git &&
		test_path_is_missing rebasing/.git &&
		! grep "Submodule path .* not initialized" err
	)

'

test_expect_success 'submodule update continues after checkout error' '
	(cd super &&
	 git reset --hard HEAD &&
	 git submodule add ../submodule submodule2 &&
	 git submodule init &&
	 git commit -am "new_submodule" &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../expect
	 ) &&
	 (cd submodule &&
	  test_commit "update_submodule" file
	 ) &&
	 (cd submodule2 &&
	  test_commit "update_submodule2" file
	 ) &&
	 git add submodule &&
	 git add submodule2 &&
	 git commit -m "two_new_submodule_commits" &&
	 (cd submodule &&
	  echo "" > file
	 ) &&
	 git checkout HEAD^ &&
	 test_must_fail git submodule update &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'
test_expect_success 'submodule update continues after recursive checkout error' '
	(cd super &&
	 git reset --hard HEAD &&
	 git checkout main &&
	 git submodule update &&
	 (cd submodule &&
	  git submodule add ../submodule subsubmodule &&
	  git submodule init &&
	  git commit -m "new_subsubmodule"
	 ) &&
	 git add submodule &&
	 git commit -m "update_submodule" &&
	 (cd submodule &&
	  (cd subsubmodule &&
	   test_commit "update_subsubmodule" file
	  ) &&
	  git add subsubmodule &&
	  test_commit "update_submodule_again" file &&
	  (cd subsubmodule &&
	   test_commit "update_subsubmodule_again" file
	  ) &&
	  test_commit "update_submodule_again_again" file
	 ) &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../expect &&
	  test_commit "update_submodule2_again" file
	 ) &&
	 git add submodule &&
	 git add submodule2 &&
	 git commit -m "new_commits" &&
	 git checkout HEAD^ &&
	 (cd submodule &&
	  git checkout HEAD^ &&
	  (cd subsubmodule &&
	   echo "" > file
	  )
	 ) &&
	 test_expect_code 1 git submodule update --recursive &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update exit immediately in case of merge conflict' '
	(cd super &&
	 git checkout main &&
	 git reset --hard HEAD &&
	 (cd submodule &&
	  (cd subsubmodule &&
	   git reset --hard HEAD
	  )
	 ) &&
	 git submodule update --recursive &&
	 (cd submodule &&
	  test_commit "update_submodule_2" file
	 ) &&
	 (cd submodule2 &&
	  test_commit "update_submodule2_2" file
	 ) &&
	 git add submodule &&
	 git add submodule2 &&
	 git commit -m "two_new_submodule_commits" &&
	 (cd submodule &&
	  git checkout main &&
	  test_commit "conflict" file &&
	  echo "conflict" > file
	 ) &&
	 git checkout HEAD^ &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../expect
	 ) &&
	 git config submodule.submodule.update merge &&
	 test_must_fail git submodule update &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update exit immediately after recursive rebase error' '
	(cd super &&
	 git checkout main &&
	 git reset --hard HEAD &&
	 (cd submodule &&
	  git reset --hard HEAD &&
	  git submodule update --recursive
	 ) &&
	 (cd submodule &&
	  test_commit "update_submodule_3" file
	 ) &&
	 (cd submodule2 &&
	  test_commit "update_submodule2_3" file
	 ) &&
	 git add submodule &&
	 git add submodule2 &&
	 git commit -m "two_new_submodule_commits" &&
	 (cd submodule &&
	  git checkout main &&
	  test_commit "conflict2" file &&
	  echo "conflict" > file
	 ) &&
	 git checkout HEAD^ &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../expect
	 ) &&
	 git config submodule.submodule.update rebase &&
	 test_must_fail git submodule update &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'add different submodules to the same path' '
	(cd super &&
	 git submodule add ../submodule s1 &&
	 test_must_fail git submodule add ../merging s1
	)
'

test_expect_success 'submodule add places git-dir in superprojects git-dir' '
	(cd super &&
	 mkdir deeper &&
	 git submodule add ../submodule deeper/submodule &&
	 (cd deeper/submodule &&
	  git log > ../../expected
	 ) &&
	 (cd .git/modules/deeper/submodule &&
	  git log > ../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule update places git-dir in superprojects git-dir' '
	(cd super &&
	 git commit -m "added submodule"
	) &&
	git clone super super2 &&
	(cd super2 &&
	 git submodule init deeper/submodule &&
	 git submodule update &&
	 (cd deeper/submodule &&
	  git log > ../../expected
	 ) &&
	 (cd .git/modules/deeper/submodule &&
	  git log > ../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule add places git-dir in superprojects git-dir recursive' '
	(cd super2 &&
	 (cd deeper/submodule &&
	  git submodule add ../submodule subsubmodule &&
	  (cd subsubmodule &&
	   git log > ../../../expected
	  ) &&
	  git commit -m "added subsubmodule" &&
	  git push origin :
	 ) &&
	 (cd .git/modules/deeper/submodule/modules/subsubmodule &&
	  git log > ../../../../../actual
	 ) &&
	 git add deeper/submodule &&
	 git commit -m "update submodule" &&
	 git push origin : &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule update places git-dir in superprojects git-dir recursive' '
	mkdir super_update_r &&
	(cd super_update_r &&
	 git init --bare
	) &&
	mkdir subsuper_update_r &&
	(cd subsuper_update_r &&
	 git init --bare
	) &&
	mkdir subsubsuper_update_r &&
	(cd subsubsuper_update_r &&
	 git init --bare
	) &&
	git clone subsubsuper_update_r subsubsuper_update_r2 &&
	(cd subsubsuper_update_r2 &&
	 test_commit "update_subsubsuper" file &&
	 git push origin main
	) &&
	git clone subsuper_update_r subsuper_update_r2 &&
	(cd subsuper_update_r2 &&
	 test_commit "update_subsuper" file &&
	 git submodule add ../subsubsuper_update_r subsubmodule &&
	 git commit -am "subsubmodule" &&
	 git push origin main
	) &&
	git clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 test_commit "update_super" file &&
	 git submodule add ../subsuper_update_r submodule &&
	 git commit -am "submodule" &&
	 git push origin main
	) &&
	rm -rf super_update_r2 &&
	git clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 git submodule update --init --recursive >actual &&
	 test_grep "Submodule path .submodule/subsubmodule.: checked out" actual &&
	 (cd submodule/subsubmodule &&
	  git log > ../../expected
	 ) &&
	 (cd .git/modules/submodule/modules/subsubmodule &&
	  git log > ../../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule add properly re-creates deeper level submodules' '
	(cd super &&
	 git reset --hard main &&
	 rm -rf deeper/ &&
	 git submodule add --force ../submodule deeper/submodule
	)
'

test_expect_success 'submodule update properly revives a moved submodule' '
	(cd super &&
	 H=$(git rev-parse --short HEAD) &&
	 git commit -am "pre move" &&
	 H2=$(git rev-parse --short HEAD) &&
	 git status >out &&
	 sed "s/$H/XXX/" out >expect &&
	 H=$(cd submodule2 && git rev-parse HEAD) &&
	 git rm --cached submodule2 &&
	 rm -rf submodule2 &&
	 mkdir -p "moved/sub module" &&
	 git update-index --add --cacheinfo 160000 $H "moved/sub module" &&
	 git config -f .gitmodules submodule.submodule2.path "moved/sub module" &&
	 git commit -am "post move" &&
	 git submodule update &&
	 git status > out &&
	 sed "s/$H2/XXX/" out >actual &&
	 test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'submodule update can handle symbolic links in pwd' '
	mkdir -p linked/dir &&
	ln -s linked/dir linkto &&
	(cd linkto &&
	 git clone "$TRASH_DIRECTORY"/super_update_r2 super &&
	 (cd super &&
	  git submodule update --init --recursive
	 )
	)
'

test_expect_success 'submodule update clone shallow submodule' '
	test_when_finished "rm -rf super3" &&
	first=$(git -C cloned rev-parse HEAD:submodule) &&
	second=$(git -C submodule rev-parse HEAD) &&
	commit_count=$(git -C submodule rev-list --count $first^..$second) &&
	git clone cloned super3 &&
	pwd=$(pwd) &&
	(
		cd super3 &&
		sed -e "s#url = ../#url = file://$pwd/#" <.gitmodules >.gitmodules.tmp &&
		mv -f .gitmodules.tmp .gitmodules &&
		git submodule update --init --depth=$commit_count &&
		git -C submodule log --oneline >out &&
		test_line_count = 1 out
	)
'

test_expect_success 'submodule update clone shallow submodule outside of depth' '
	test_when_finished "rm -rf super3" &&
	git clone cloned super3 &&
	pwd=$(pwd) &&
	(
		cd super3 &&
		sed -e "s#url = ../#url = file://$pwd/#" <.gitmodules >.gitmodules.tmp &&
		mv -f .gitmodules.tmp .gitmodules &&
		# Some protocol versions (e.g. 2) support fetching
		# unadvertised objects, so restrict this test to v0.
		test_must_fail env GIT_TEST_PROTOCOL_VERSION=0 \
			git submodule update --init --depth=1 2>actual &&
		test_grep "Direct fetching of that commit failed." actual &&
		git -C ../submodule config uploadpack.allowReachableSHA1InWant true &&
		git submodule update --init --depth=1 >actual &&
		git -C submodule log --oneline >out &&
		test_line_count = 1 out
	)
'

test_expect_success 'submodule update --recursive drops module name before recursing' '
	(cd super2 &&
	 (cd deeper/submodule/subsubmodule &&
	  git checkout HEAD^
	 ) &&
	 git submodule update --recursive deeper/submodule >actual &&
	 test_grep "Submodule path .deeper/submodule/subsubmodule.: checked out" actual
	)
'

test_expect_success 'submodule update can be run in parallel' '
	(cd super2 &&
	 GIT_TRACE=$(pwd)/trace.out git submodule update --jobs 7 &&
	 grep "7 tasks" trace.out &&
	 git config submodule.fetchJobs 8 &&
	 GIT_TRACE=$(pwd)/trace.out git submodule update &&
	 grep "8 tasks" trace.out &&
	 GIT_TRACE=$(pwd)/trace.out git submodule update --jobs 9 &&
	 grep "9 tasks" trace.out
	)
'

test_expect_success 'git clone passes the parallel jobs config on to submodules' '
	test_when_finished "rm -rf super4" &&
	GIT_TRACE=$(pwd)/trace.out git clone --recurse-submodules --jobs 7 . super4 &&
	grep "7 tasks" trace.out &&
	rm -rf super4 &&
	git config --global submodule.fetchJobs 8 &&
	GIT_TRACE=$(pwd)/trace.out git clone --recurse-submodules . super4 &&
	grep "8 tasks" trace.out &&
	rm -rf super4 &&
	GIT_TRACE=$(pwd)/trace.out git clone --recurse-submodules --jobs 9 . super4 &&
	grep "9 tasks" trace.out &&
	rm -rf super4
'

test_expect_success 'submodule update --quiet passes quietness to merge/rebase' '
	(cd super &&
	 test_commit -C rebasing message &&
	 git submodule update --rebase --quiet >out 2>err &&
	 test_must_be_empty out &&
	 test_must_be_empty err &&
	 git submodule update --rebase >out 2>err &&
	 test_file_not_empty out &&
	 test_must_be_empty err
	)
'

test_expect_success 'submodule update --quiet passes quietness to fetch with a shallow clone' '
	test_when_finished "rm -rf super4 super5 super6" &&
	git clone . super4 &&
	(cd super4 &&
	 git submodule add --quiet file://"$TRASH_DIRECTORY"/submodule submodule3 &&
	 git commit -am "setup submodule3"
	) &&
	(cd submodule &&
	  test_commit line6 file
	) &&
	git clone super4 super5 &&
	(cd super5 &&
	 git submodule update --quiet --init --depth=1 submodule3 >out 2>err &&
	 test_must_be_empty out &&
	 test_must_be_empty err
	) &&
	git clone super4 super6 &&
	(cd super6 &&
	 git submodule update --init --depth=1 submodule3 >out 2>err &&
	 test_file_not_empty out &&
	 test_file_not_empty err
	)
'

test_expect_success 'submodule update --filter requires --init' '
	test_expect_code 129 git -C super submodule update --filter blob:none
'

test_expect_success 'submodule update --filter sets partial clone settings' '
	test_when_finished "rm -rf super-filter" &&
	git clone cloned super-filter &&
	git -C super-filter submodule update --init --filter blob:none &&
	test_cmp_config -C super-filter/submodule true remote.origin.promisor &&
	test_cmp_config -C super-filter/submodule blob:none remote.origin.partialclonefilter
'

# NEEDSWORK: Clean up the tests so that we can reuse the test setup.
# Don't reuse the existing repos because the earlier tests have
# intentionally disruptive configurations.
test_expect_success 'setup clean recursive superproject' '
	git init bottom &&
	test_commit -C bottom "bottom" &&
	git init middle &&
	git -C middle submodule add ../bottom bottom &&
	git -C middle commit -m "middle" &&
	git init top &&
	git -C top submodule add ../middle middle &&
	git -C top commit -m "top" &&
	git clone --recurse-submodules top top-clean
'

test_expect_success 'submodule update should skip unmerged submodules' '
	test_when_finished "rm -fr top-cloned" &&
	cp -r top-clean top-cloned &&

	# Create an upstream commit in each repo, starting with bottom
	test_commit -C bottom upstream_commit &&
	# Create middle commit
	git -C middle/bottom fetch &&
	git -C middle/bottom checkout -f FETCH_HEAD &&
	git -C middle add bottom &&
	git -C middle commit -m "upstream_commit" &&
	# Create top commit
	git -C top/middle fetch &&
	git -C top/middle checkout -f FETCH_HEAD &&
	git -C top add middle &&
	git -C top commit -m "upstream_commit" &&

	# Create a downstream conflict
	test_commit -C top-cloned/middle/bottom downstream_commit &&
	git -C top-cloned/middle add bottom &&
	git -C top-cloned/middle commit -m "downstream_commit" &&
	git -C top-cloned/middle fetch --recurse-submodules origin &&
	test_must_fail git -C top-cloned/middle merge origin/main &&

	# Make the update of "middle" a no-op, otherwise we error out
	# because of its unmerged state
	test_config -C top-cloned submodule.middle.update !true &&
	git -C top-cloned submodule update --recursive 2>actual.err &&
	cat >expect.err <<-\EOF &&
	Skipping unmerged submodule middle/bottom
	EOF
	test_cmp expect.err actual.err
'

test_expect_success 'submodule update --recursive skip submodules with strategy=none' '
	test_when_finished "rm -fr top-cloned" &&
	cp -r top-clean top-cloned &&

	test_commit -C top-cloned/middle/bottom downstream_commit &&
	git -C top-cloned/middle config submodule.bottom.update none &&
	git -C top-cloned submodule update --recursive 2>actual.err &&
	cat >expect.err <<-\EOF &&
	Skipping submodule '\''middle/bottom'\''
	EOF
	test_cmp expect.err actual.err
'

add_submodule_commit_and_validate () {
	HASH=$(git rev-parse HEAD) &&
	git update-index --add --cacheinfo 160000,$HASH,sub &&
	git commit -m "create submodule" &&
	echo "160000 commit $HASH	sub" >expect &&
	git ls-tree HEAD -- sub >actual &&
	test_cmp expect actual
}

test_expect_success 'commit with staged submodule change' '
	add_submodule_commit_and_validate
'

test_expect_success 'commit with staged submodule change with ignoreSubmodules dirty' '
	test_config diff.ignoreSubmodules dirty &&
	add_submodule_commit_and_validate
'

test_expect_success 'commit with staged submodule change with ignoreSubmodules all' '
	test_config diff.ignoreSubmodules all &&
	add_submodule_commit_and_validate
'

test_expect_success CASE_INSENSITIVE_FS,SYMLINKS \
	'submodule paths must not follow symlinks' '

	# This is only needed because we want to run this in a self-contained
	# test without having to spin up an HTTP server; However, it would not
	# be needed in a real-world scenario where the submodule is simply
	# hosted on a public site.
	test_config_global protocol.file.allow always &&

	# Make sure that Git tries to use symlinks on Windows
	test_config_global core.symlinks true &&

	tell_tale_path="$PWD/tell.tale" &&
	git init hook &&
	(
		cd hook &&
		mkdir -p y/hooks &&
		write_script y/hooks/post-checkout <<-EOF &&
		echo HOOK-RUN >&2
		echo hook-run >"$tell_tale_path"
		EOF
		git add y/hooks/post-checkout &&
		test_tick &&
		git commit -m post-checkout
	) &&

	hook_repo_path="$(pwd)/hook" &&
	git init captain &&
	(
		cd captain &&
		git submodule add --name x/y "$hook_repo_path" A/modules/x &&
		test_tick &&
		git commit -m add-submodule &&

		printf .git >dotgit.txt &&
		git hash-object -w --stdin <dotgit.txt >dot-git.hash &&
		printf "120000 %s 0\ta\n" "$(cat dot-git.hash)" >index.info &&
		git update-index --index-info <index.info &&
		test_tick &&
		git commit -m add-symlink
	) &&

	test_path_is_missing "$tell_tale_path" &&
	git clone --recursive captain hooked 2>err &&
	test_grep ! HOOK-RUN err &&
	test_path_is_missing "$tell_tale_path"
'

test_done
