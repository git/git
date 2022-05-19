#!/bin/sh
#
# Copyright (c) 2009 Red Hat, Inc.
#

test_description='Test updating submodules

This test verifies that "but submodule update" detaches the HEAD of the
submodule and "but submodule update --rebase/--merge" does not detach the HEAD.
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


compare_head()
{
    sha_main=$(but rev-list --max-count=1 main)
    sha_head=$(but rev-list --max-count=1 HEAD)

    test "$sha_main" = "$sha_head"
}


test_expect_success 'setup a submodule tree' '
	echo file > file &&
	but add file &&
	test_tick &&
	but cummit -m upstream &&
	but clone . super &&
	but clone super submodule &&
	but clone super rebasing &&
	but clone super merging &&
	but clone super none &&
	(cd super &&
	 but submodule add ../submodule submodule &&
	 test_tick &&
	 but cummit -m "submodule" &&
	 but submodule init submodule
	) &&
	(cd submodule &&
	echo "line2" > file &&
	but add file &&
	but cummit -m "cummit 2"
	) &&
	(cd super &&
	 (cd submodule &&
	  but pull --rebase origin
	 ) &&
	 but add submodule &&
	 but cummit -m "submodule update"
	) &&
	(cd super &&
	 but submodule add ../rebasing rebasing &&
	 test_tick &&
	 but cummit -m "rebasing"
	) &&
	(cd super &&
	 but submodule add ../merging merging &&
	 test_tick &&
	 but cummit -m "rebasing"
	) &&
	(cd super &&
	 but submodule add ../none none &&
	 test_tick &&
	 but cummit -m "none"
	) &&
	but clone . recursivesuper &&
	( cd recursivesuper &&
	 but submodule add ../super super
	)
'

test_expect_success 'update --remote falls back to using HEAD' '
	test_create_repo main-branch-submodule &&
	test_cummit -C main-branch-submodule initial &&

	test_create_repo main-branch &&
	but -C main-branch submodule add ../main-branch-submodule &&
	but -C main-branch cummit -m add-submodule &&

	but -C main-branch-submodule switch -c hello &&
	test_cummit -C main-branch-submodule world &&

	but clone --recursive main-branch main-branch-clone &&
	but -C main-branch-clone submodule update --remote main-branch-submodule &&
	test_path_exists main-branch-clone/main-branch-submodule/world.t
'

test_expect_success 'submodule update detaching the HEAD ' '
	(cd super/submodule &&
	 but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update from subdirectory' '
	(cd super/submodule &&
	 but reset --hard HEAD~1
	) &&
	mkdir super/sub &&
	(cd super/sub &&
	 (cd ../submodule &&
	  compare_head
	 ) &&
	 but submodule update ../submodule &&
	 cd ../submodule &&
	 ! compare_head
	)
'

supersha1=$(but -C super rev-parse HEAD)
mergingsha1=$(but -C super/merging rev-parse HEAD)
nonesha1=$(but -C super/none rev-parse HEAD)
rebasingsha1=$(but -C super/rebasing rev-parse HEAD)
submodulesha1=$(but -C super/submodule rev-parse HEAD)
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
	but -C recursivesuper/super reset --hard HEAD^ &&
	(cd recursivesuper &&
	 mkdir tmp &&
	 cd tmp &&
	 but submodule update --init --recursive ../super >../../actual 2>../../actual2
	) &&
	test_cmp expect actual &&
	sort actual2 >actual2.sorted &&
	test_cmp expect2 actual2.sorted
'

cat <<EOF >expect2
Submodule 'foo/sub' ($pwd/withsubs/../rebasing) registered for path 'sub'
EOF

test_expect_success 'submodule update --init from and of subdirectory' '
	but init withsubs &&
	(cd withsubs &&
	 mkdir foo &&
	 but submodule add "$(pwd)/../rebasing" foo/sub &&
	 (cd foo &&
	  but submodule deinit -f sub &&
	  but submodule update --init sub 2>../../actual2
	 )
	) &&
	test_cmp expect2 actual2
'

test_expect_success 'submodule update does not fetch already present cummits' '
	(cd submodule &&
	  echo line3 >> file &&
	  but add file &&
	  test_tick &&
	  but cummit -m "upstream line3"
	) &&
	(cd super/submodule &&
	  head=$(but rev-parse --verify HEAD) &&
	  echo "Submodule path ${SQ}submodule$SQ: checked out $SQ$head$SQ" > ../../expected &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	  but submodule update > ../actual 2> ../actual.err
	) &&
	test_cmp expected actual &&
	test_must_be_empty actual.err
'

test_expect_success 'submodule update should fail due to local changes' '
	(cd super/submodule &&
	 but reset --hard HEAD~1 &&
	 echo "local change" > file
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 test_must_fail but submodule update submodule 2>../actual.raw
	) &&
	sed "s/^> //" >expect <<-\EOF &&
	> error: Your local changes to the following files would be overwritten by checkout:
	> 	file
	> Please cummit your changes or stash them before you switch branches.
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
	 but submodule update --force submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update --force forcibly checks out submodules' '
	(cd super &&
	 (cd submodule &&
	  rm -f file
	 ) &&
	 but submodule update --force submodule &&
	 (cd submodule &&
	  test "$(but status -s file)" = ""
	 )
	)
'

test_expect_success 'submodule update --remote should fetch upstream changes' '
	(cd submodule &&
	 echo line4 >> file &&
	 but add file &&
	 test_tick &&
	 but cummit -m "upstream line4"
	) &&
	(cd super &&
	 but submodule update --remote --force submodule &&
	 cd submodule &&
	 test "$(but log -1 --oneline)" = "$(BUT_DIR=../../submodule/.but but log -1 --oneline)"
	)
'

test_expect_success 'submodule update --remote should fetch upstream changes with .' '
	(
		cd super &&
		but config -f .butmodules submodule."submodule".branch "." &&
		but add .butmodules &&
		but cummit -m "submodules: update from the respective superproject branch"
	) &&
	(
		cd submodule &&
		echo line4a >> file &&
		but add file &&
		test_tick &&
		but cummit -m "upstream line4a" &&
		but checkout -b test-branch &&
		test_cummit on-test-branch
	) &&
	(
		cd super &&
		but submodule update --remote --force submodule &&
		but -C submodule log -1 --oneline >actual &&
		but -C ../submodule log -1 --oneline main >expect &&
		test_cmp expect actual &&
		but checkout -b test-branch &&
		but submodule update --remote --force submodule &&
		but -C submodule log -1 --oneline >actual &&
		but -C ../submodule log -1 --oneline test-branch >expect &&
		test_cmp expect actual &&
		but checkout main &&
		but branch -d test-branch &&
		but reset --hard HEAD^
	)
'

test_expect_success 'local config should override .butmodules branch' '
	(cd submodule &&
	 but checkout test-branch &&
	 echo line5 >> file &&
	 but add file &&
	 test_tick &&
	 but cummit -m "upstream line5" &&
	 but checkout main
	) &&
	(cd super &&
	 but config submodule.submodule.branch test-branch &&
	 but submodule update --remote --force submodule &&
	 cd submodule &&
	 test "$(but log -1 --oneline)" = "$(BUT_DIR=../../submodule/.but but log -1 --oneline test-branch)"
	)
'

test_expect_success 'submodule update --rebase staying on main' '
	(cd super/submodule &&
	  but checkout main
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update --rebase submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update --merge staying on main' '
	(cd super/submodule &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update --merge submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - rebase in .but/config' '
	(cd super &&
	 but config submodule.submodule.update rebase
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .but/config but --rebase given' '
	(cd super &&
	 but config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update --rebase submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - merge in .but/config' '
	(cd super &&
	 but config submodule.submodule.update merge
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .but/config but --merge given' '
	(cd super &&
	 but config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD~1
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update --merge submodule &&
	 cd submodule &&
	 compare_head
	)
'

test_expect_success 'submodule update - checkout in .but/config' '
	(cd super &&
	 but config submodule.submodule.update checkout
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD^
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update - command in .but/config' '
	(cd super &&
	 but config submodule.submodule.update "!but checkout"
	) &&
	(cd super/submodule &&
	  but reset --hard HEAD^
	) &&
	(cd super &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but submodule update submodule &&
	 cd submodule &&
	 ! compare_head
	)
'

test_expect_success 'submodule update - command in .butmodules is rejected' '
	test_when_finished "but -C super reset --hard HEAD^" &&
	but -C super config -f .butmodules submodule.submodule.update "!false" &&
	but -C super cummit -a -m "add command to .butmodules file" &&
	but -C super/submodule reset --hard $submodulesha1^ &&
	test_must_fail but -C super submodule update submodule
'

test_expect_success 'fsck detects command in .butmodules' '
	but init command-in-butmodules &&
	(
		cd command-in-butmodules &&
		but submodule add ../submodule submodule &&
		test_cummit adding-submodule &&

		but config -f .butmodules submodule.submodule.update "!false" &&
		but add .butmodules &&
		test_cummit configuring-update &&
		test_must_fail but fsck
	)
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path 'submodule'
EOF

test_expect_success 'submodule update - command in .but/config catches failure' '
	(cd super &&
	 but config submodule.submodule.update "!false"
	) &&
	(cd super/submodule &&
	  but reset --hard $submodulesha1^
	) &&
	(cd super &&
	 test_must_fail but submodule update submodule 2>../actual
	) &&
	test_cmp actual expect
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path '../submodule'
EOF

test_expect_success 'submodule update - command in .but/config catches failure -- subdirectory' '
	(cd super &&
	 but config submodule.submodule.update "!false"
	) &&
	(cd super/submodule &&
	  but reset --hard $submodulesha1^
	) &&
	(cd super &&
	 mkdir tmp && cd tmp &&
	 test_must_fail but submodule update ../submodule 2>../../actual
	) &&
	test_cmp actual expect
'

test_expect_success 'submodule update - command run for initial population of submodule' '
	cat >expect <<-EOF &&
	fatal: Execution of '\''false $submodulesha1'\'' failed in submodule path '\''submodule'\''
	EOF
	rm -rf super/submodule &&
	test_must_fail but -C super submodule update 2>actual &&
	test_cmp expect actual &&
	but -C super submodule update --checkout
'

cat << EOF >expect
fatal: Execution of 'false $submodulesha1' failed in submodule path '../super/submodule'
fatal: Failed to recurse into submodule path '../super'
EOF

test_expect_success 'recursive submodule update - command in .but/config catches failure -- subdirectory' '
	(cd recursivesuper &&
	 but submodule update --remote super &&
	 but add super &&
	 but cummit -m "update to latest to have more than one cummit in submodules"
	) &&
	but -C recursivesuper/super config submodule.submodule.update "!false" &&
	but -C recursivesuper/super/submodule reset --hard $submodulesha1^ &&
	(cd recursivesuper &&
	 mkdir -p tmp && cd tmp &&
	 test_must_fail but submodule update --recursive ../super 2>../../actual
	) &&
	test_cmp actual expect
'

test_expect_success 'submodule init does not copy command into .but/config' '
	test_when_finished "but -C super update-index --force-remove submodule1" &&
	test_when_finished but config -f super/.butmodules \
		--remove-section submodule.submodule1 &&
	(cd super &&
	 but ls-files -s submodule >out &&
	 H=$(cut -d" " -f2 out) &&
	 mkdir submodule1 &&
	 but update-index --add --cacheinfo 160000 $H submodule1 &&
	 but config -f .butmodules submodule.submodule1.path submodule1 &&
	 but config -f .butmodules submodule.submodule1.url ../submodule &&
	 but config -f .butmodules submodule.submodule1.update !false &&
	 test_must_fail but submodule init submodule1 &&
	 test_expect_code 1 but config submodule.submodule1.update >actual &&
	 test_must_be_empty actual
	)
'

test_expect_success 'submodule init picks up rebase' '
	(cd super &&
	 but config -f .butmodules submodule.rebasing.update rebase &&
	 but submodule init rebasing &&
	 test "rebase" = "$(but config submodule.rebasing.update)"
	)
'

test_expect_success 'submodule init picks up merge' '
	(cd super &&
	 but config -f .butmodules submodule.merging.update merge &&
	 but submodule init merging &&
	 test "merge" = "$(but config submodule.merging.update)"
	)
'

test_expect_success 'submodule update --merge  - ignores --merge  for new submodules' '
	test_config -C super submodule.submodule.update checkout &&
	(cd super &&
	 rm -rf submodule &&
	 but submodule update submodule &&
	 but status -s submodule >expect &&
	 rm -rf submodule &&
	 but submodule update --merge submodule &&
	 but status -s submodule >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update --rebase - ignores --rebase for new submodules' '
	test_config -C super submodule.submodule.update checkout &&
	(cd super &&
	 rm -rf submodule &&
	 but submodule update submodule &&
	 but status -s submodule >expect &&
	 rm -rf submodule &&
	 but submodule update --rebase submodule &&
	 but status -s submodule >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update ignores update=merge config for new submodules' '
	(cd super &&
	 rm -rf submodule &&
	 but submodule update submodule &&
	 but status -s submodule >expect &&
	 rm -rf submodule &&
	 but config submodule.submodule.update merge &&
	 but submodule update submodule &&
	 but status -s submodule >actual &&
	 but config --unset submodule.submodule.update &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update ignores update=rebase config for new submodules' '
	(cd super &&
	 rm -rf submodule &&
	 but submodule update submodule &&
	 but status -s submodule >expect &&
	 rm -rf submodule &&
	 but config submodule.submodule.update rebase &&
	 but submodule update submodule &&
	 but status -s submodule >actual &&
	 but config --unset submodule.submodule.update &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule init picks up update=none' '
	(cd super &&
	 but config -f .butmodules submodule.none.update none &&
	 but submodule init none &&
	 test "none" = "$(but config submodule.none.update)"
	)
'

test_expect_success 'submodule update - update=none in .but/config' '
	(cd super &&
	 but config submodule.submodule.update none &&
	 (cd submodule &&
	  but checkout main &&
	  compare_head
	 ) &&
	 but diff --name-only >out &&
	 grep ^submodule$ out &&
	 but submodule update &&
	 but diff --name-only >out &&
	 grep ^submodule$ out &&
	 (cd submodule &&
	  compare_head
	 ) &&
	 but config --unset submodule.submodule.update &&
	 but submodule update submodule
	)
'

test_expect_success 'submodule update - update=none in .but/config but --checkout given' '
	(cd super &&
	 but config submodule.submodule.update none &&
	 (cd submodule &&
	  but checkout main &&
	  compare_head
	 ) &&
	 but diff --name-only >out &&
	 grep ^submodule$ out &&
	 but submodule update --checkout &&
	 but diff --name-only >out &&
	 ! grep ^submodule$ out &&
	 (cd submodule &&
	  ! compare_head
	 ) &&
	 but config --unset submodule.submodule.update
	)
'

test_expect_success 'submodule update --init skips submodule with update=none' '
	(cd super &&
	 but add .butmodules &&
	 but cummit -m ".butmodules"
	) &&
	but clone super cloned &&
	(cd cloned &&
	 but submodule update --init &&
	 test_path_exists submodule/.but &&
	 test_path_is_missing none/.but
	)
'

test_expect_success 'submodule update with pathspec warns against uninitialized ones' '
	test_when_finished "rm -fr selective" &&
	but clone super selective &&
	(
		cd selective &&
		but submodule init submodule &&

		but submodule update submodule 2>err &&
		! grep "Submodule path .* not initialized" err &&

		but submodule update rebasing 2>err &&
		grep "Submodule path .rebasing. not initialized" err &&

		test_path_exists submodule/.but &&
		test_path_is_missing rebasing/.but
	)

'

test_expect_success 'submodule update without pathspec updates only initialized ones' '
	test_when_finished "rm -fr selective" &&
	but clone super selective &&
	(
		cd selective &&
		but submodule init submodule &&
		but submodule update 2>err &&
		test_path_exists submodule/.but &&
		test_path_is_missing rebasing/.but &&
		! grep "Submodule path .* not initialized" err
	)

'

test_expect_success 'submodule update continues after checkout error' '
	(cd super &&
	 but reset --hard HEAD &&
	 but submodule add ../submodule submodule2 &&
	 but submodule init &&
	 but cummit -am "new_submodule" &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../expect
	 ) &&
	 (cd submodule &&
	  test_cummit "update_submodule" file
	 ) &&
	 (cd submodule2 &&
	  test_cummit "update_submodule2" file
	 ) &&
	 but add submodule &&
	 but add submodule2 &&
	 but cummit -m "two_new_submodule_cummits" &&
	 (cd submodule &&
	  echo "" > file
	 ) &&
	 but checkout HEAD^ &&
	 test_must_fail but submodule update &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'
test_expect_success 'submodule update continues after recursive checkout error' '
	(cd super &&
	 but reset --hard HEAD &&
	 but checkout main &&
	 but submodule update &&
	 (cd submodule &&
	  but submodule add ../submodule subsubmodule &&
	  but submodule init &&
	  but cummit -m "new_subsubmodule"
	 ) &&
	 but add submodule &&
	 but cummit -m "update_submodule" &&
	 (cd submodule &&
	  (cd subsubmodule &&
	   test_cummit "update_subsubmodule" file
	  ) &&
	  but add subsubmodule &&
	  test_cummit "update_submodule_again" file &&
	  (cd subsubmodule &&
	   test_cummit "update_subsubmodule_again" file
	  ) &&
	  test_cummit "update_submodule_again_again" file
	 ) &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../expect &&
	  test_cummit "update_submodule2_again" file
	 ) &&
	 but add submodule &&
	 but add submodule2 &&
	 but cummit -m "new_cummits" &&
	 but checkout HEAD^ &&
	 (cd submodule &&
	  but checkout HEAD^ &&
	  (cd subsubmodule &&
	   echo "" > file
	  )
	 ) &&
	 test_must_fail but submodule update --recursive &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update exit immediately in case of merge conflict' '
	(cd super &&
	 but checkout main &&
	 but reset --hard HEAD &&
	 (cd submodule &&
	  (cd subsubmodule &&
	   but reset --hard HEAD
	  )
	 ) &&
	 but submodule update --recursive &&
	 (cd submodule &&
	  test_cummit "update_submodule_2" file
	 ) &&
	 (cd submodule2 &&
	  test_cummit "update_submodule2_2" file
	 ) &&
	 but add submodule &&
	 but add submodule2 &&
	 but cummit -m "two_new_submodule_cummits" &&
	 (cd submodule &&
	  but checkout main &&
	  test_cummit "conflict" file &&
	  echo "conflict" > file
	 ) &&
	 but checkout HEAD^ &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../expect
	 ) &&
	 but config submodule.submodule.update merge &&
	 test_must_fail but submodule update &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update exit immediately after recursive rebase error' '
	(cd super &&
	 but checkout main &&
	 but reset --hard HEAD &&
	 (cd submodule &&
	  but reset --hard HEAD &&
	  but submodule update --recursive
	 ) &&
	 (cd submodule &&
	  test_cummit "update_submodule_3" file
	 ) &&
	 (cd submodule2 &&
	  test_cummit "update_submodule2_3" file
	 ) &&
	 but add submodule &&
	 but add submodule2 &&
	 but cummit -m "two_new_submodule_cummits" &&
	 (cd submodule &&
	  but checkout main &&
	  test_cummit "conflict2" file &&
	  echo "conflict" > file
	 ) &&
	 but checkout HEAD^ &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../expect
	 ) &&
	 but config submodule.submodule.update rebase &&
	 test_must_fail but submodule update &&
	 (cd submodule2 &&
	  but rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'add different submodules to the same path' '
	(cd super &&
	 but submodule add ../submodule s1 &&
	 test_must_fail but submodule add ../merging s1
	)
'

test_expect_success 'submodule add places but-dir in superprojects but-dir' '
	(cd super &&
	 mkdir deeper &&
	 but submodule add ../submodule deeper/submodule &&
	 (cd deeper/submodule &&
	  but log > ../../expected
	 ) &&
	 (cd .but/modules/deeper/submodule &&
	  but log > ../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule update places but-dir in superprojects but-dir' '
	(cd super &&
	 but cummit -m "added submodule"
	) &&
	but clone super super2 &&
	(cd super2 &&
	 but submodule init deeper/submodule &&
	 but submodule update &&
	 (cd deeper/submodule &&
	  but log > ../../expected
	 ) &&
	 (cd .but/modules/deeper/submodule &&
	  but log > ../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule add places but-dir in superprojects but-dir recursive' '
	(cd super2 &&
	 (cd deeper/submodule &&
	  but submodule add ../submodule subsubmodule &&
	  (cd subsubmodule &&
	   but log > ../../../expected
	  ) &&
	  but cummit -m "added subsubmodule" &&
	  but push origin :
	 ) &&
	 (cd .but/modules/deeper/submodule/modules/subsubmodule &&
	  but log > ../../../../../actual
	 ) &&
	 but add deeper/submodule &&
	 but cummit -m "update submodule" &&
	 but push origin : &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule update places but-dir in superprojects but-dir recursive' '
	mkdir super_update_r &&
	(cd super_update_r &&
	 but init --bare
	) &&
	mkdir subsuper_update_r &&
	(cd subsuper_update_r &&
	 but init --bare
	) &&
	mkdir subsubsuper_update_r &&
	(cd subsubsuper_update_r &&
	 but init --bare
	) &&
	but clone subsubsuper_update_r subsubsuper_update_r2 &&
	(cd subsubsuper_update_r2 &&
	 test_cummit "update_subsubsuper" file &&
	 but push origin main
	) &&
	but clone subsuper_update_r subsuper_update_r2 &&
	(cd subsuper_update_r2 &&
	 test_cummit "update_subsuper" file &&
	 but submodule add ../subsubsuper_update_r subsubmodule &&
	 but cummit -am "subsubmodule" &&
	 but push origin main
	) &&
	but clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 test_cummit "update_super" file &&
	 but submodule add ../subsuper_update_r submodule &&
	 but cummit -am "submodule" &&
	 but push origin main
	) &&
	rm -rf super_update_r2 &&
	but clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 but submodule update --init --recursive >actual &&
	 test_i18ngrep "Submodule path .submodule/subsubmodule.: checked out" actual &&
	 (cd submodule/subsubmodule &&
	  but log > ../../expected
	 ) &&
	 (cd .but/modules/submodule/modules/subsubmodule &&
	  but log > ../../../../../actual
	 ) &&
	 test_cmp expected actual
	)
'

test_expect_success 'submodule add properly re-creates deeper level submodules' '
	(cd super &&
	 but reset --hard main &&
	 rm -rf deeper/ &&
	 but submodule add --force ../submodule deeper/submodule
	)
'

test_expect_success 'submodule update properly revives a moved submodule' '
	(cd super &&
	 H=$(but rev-parse --short HEAD) &&
	 but cummit -am "pre move" &&
	 H2=$(but rev-parse --short HEAD) &&
	 but status >out &&
	 sed "s/$H/XXX/" out >expect &&
	 H=$(cd submodule2 && but rev-parse HEAD) &&
	 but rm --cached submodule2 &&
	 rm -rf submodule2 &&
	 mkdir -p "moved/sub module" &&
	 but update-index --add --cacheinfo 160000 $H "moved/sub module" &&
	 but config -f .butmodules submodule.submodule2.path "moved/sub module" &&
	 but cummit -am "post move" &&
	 but submodule update &&
	 but status > out &&
	 sed "s/$H2/XXX/" out >actual &&
	 test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'submodule update can handle symbolic links in pwd' '
	mkdir -p linked/dir &&
	ln -s linked/dir linkto &&
	(cd linkto &&
	 but clone "$TRASH_DIRECTORY"/super_update_r2 super &&
	 (cd super &&
	  but submodule update --init --recursive
	 )
	)
'

test_expect_success 'submodule update clone shallow submodule' '
	test_when_finished "rm -rf super3" &&
	first=$(but -C cloned rev-parse HEAD:submodule) &&
	second=$(but -C submodule rev-parse HEAD) &&
	cummit_count=$(but -C submodule rev-list --count $first^..$second) &&
	but clone cloned super3 &&
	pwd=$(pwd) &&
	(
		cd super3 &&
		sed -e "s#url = ../#url = file://$pwd/#" <.butmodules >.butmodules.tmp &&
		mv -f .butmodules.tmp .butmodules &&
		but submodule update --init --depth=$cummit_count &&
		but -C submodule log --oneline >out &&
		test_line_count = 1 out
	)
'

test_expect_success 'submodule update clone shallow submodule outside of depth' '
	test_when_finished "rm -rf super3" &&
	but clone cloned super3 &&
	pwd=$(pwd) &&
	(
		cd super3 &&
		sed -e "s#url = ../#url = file://$pwd/#" <.butmodules >.butmodules.tmp &&
		mv -f .butmodules.tmp .butmodules &&
		# Some protocol versions (e.g. 2) support fetching
		# unadvertised objects, so restrict this test to v0.
		test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
			but submodule update --init --depth=1 2>actual &&
		test_i18ngrep "Direct fetching of that cummit failed." actual &&
		but -C ../submodule config uploadpack.allowReachableSHA1InWant true &&
		but submodule update --init --depth=1 >actual &&
		but -C submodule log --oneline >out &&
		test_line_count = 1 out
	)
'

test_expect_success 'submodule update --recursive drops module name before recursing' '
	(cd super2 &&
	 (cd deeper/submodule/subsubmodule &&
	  but checkout HEAD^
	 ) &&
	 but submodule update --recursive deeper/submodule >actual &&
	 test_i18ngrep "Submodule path .deeper/submodule/subsubmodule.: checked out" actual
	)
'

test_expect_success 'submodule update can be run in parallel' '
	(cd super2 &&
	 BUT_TRACE=$(pwd)/trace.out but submodule update --jobs 7 &&
	 grep "7 tasks" trace.out &&
	 but config submodule.fetchJobs 8 &&
	 BUT_TRACE=$(pwd)/trace.out but submodule update &&
	 grep "8 tasks" trace.out &&
	 BUT_TRACE=$(pwd)/trace.out but submodule update --jobs 9 &&
	 grep "9 tasks" trace.out
	)
'

test_expect_success 'but clone passes the parallel jobs config on to submodules' '
	test_when_finished "rm -rf super4" &&
	BUT_TRACE=$(pwd)/trace.out but clone --recurse-submodules --jobs 7 . super4 &&
	grep "7 tasks" trace.out &&
	rm -rf super4 &&
	but config --global submodule.fetchJobs 8 &&
	BUT_TRACE=$(pwd)/trace.out but clone --recurse-submodules . super4 &&
	grep "8 tasks" trace.out &&
	rm -rf super4 &&
	BUT_TRACE=$(pwd)/trace.out but clone --recurse-submodules --jobs 9 . super4 &&
	grep "9 tasks" trace.out &&
	rm -rf super4
'

test_expect_success 'submodule update --quiet passes quietness to merge/rebase' '
	(cd super &&
	 test_cummit -C rebasing message &&
	 but submodule update --rebase --quiet >out 2>err &&
	 test_must_be_empty out &&
	 test_must_be_empty err &&
	 but submodule update --rebase -v >out 2>err &&
	 test_file_not_empty out &&
	 test_must_be_empty err
	)
'

test_expect_success 'submodule update --quiet passes quietness to fetch with a shallow clone' '
	test_when_finished "rm -rf super4 super5 super6" &&
	but clone . super4 &&
	(cd super4 &&
	 but submodule add --quiet file://"$TRASH_DIRECTORY"/submodule submodule3 &&
	 but cummit -am "setup submodule3"
	) &&
	(cd submodule &&
	  test_cummit line6 file
	) &&
	but clone super4 super5 &&
	(cd super5 &&
	 but submodule update --quiet --init --depth=1 submodule3 >out 2>err &&
	 test_must_be_empty out &&
	 test_must_be_empty err
	) &&
	but clone super4 super6 &&
	(cd super6 &&
	 but submodule update --init --depth=1 submodule3 >out 2>err &&
	 test_file_not_empty out &&
	 test_file_not_empty err
	)
'

test_expect_success 'submodule update --filter requires --init' '
	test_expect_code 129 but -C super submodule update --filter blob:none
'

test_expect_success 'submodule update --filter sets partial clone settings' '
	test_when_finished "rm -rf super-filter" &&
	but clone cloned super-filter &&
	but -C super-filter submodule update --init --filter blob:none &&
	test_cmp_config -C super-filter/submodule true remote.origin.promisor &&
	test_cmp_config -C super-filter/submodule blob:none remote.origin.partialclonefilter
'

test_done
