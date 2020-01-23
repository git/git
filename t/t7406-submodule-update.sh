#!/bin/sh
#
# Copyright (c) 2009 Red Hat, Inc.
#

test_description='Test updating submodules

This test verifies that "git submodule update" detaches the HEAD of the
submodule and "git submodule update --rebase/--merge" does not detach the HEAD.
'

. ./test-lib.sh


compare_head()
{
    sha_master=$(git rev-list --max-count=1 master)
    sha_head=$(git rev-list --max-count=1 HEAD)

    test "$sha_master" = "$sha_head"
}


test_expect_success 'setup a submodule tree' '
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
	test_i18ncmp expect actual &&
	sort actual2 >actual2.sorted &&
	test_i18ncmp expect2 actual2.sorted
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
	test_i18ncmp expect2 actual2
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
	test_i18ncmp expected actual &&
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
	 test_must_fail git submodule update submodule
	)
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
		git -C ../submodule log -1 --oneline master >expect &&
		test_cmp expect actual &&
		git checkout -b test-branch &&
		git submodule update --remote --force submodule &&
		git -C submodule log -1 --oneline >actual &&
		git -C ../submodule log -1 --oneline test-branch >expect &&
		test_cmp expect actual &&
		git checkout master &&
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
	 git checkout master
	) &&
	(cd super &&
	 git config submodule.submodule.branch test-branch &&
	 git submodule update --remote --force submodule &&
	 cd submodule &&
	 test "$(git log -1 --oneline)" = "$(GIT_DIR=../../submodule/.git git log -1 --oneline test-branch)"
	)
'

test_expect_success 'submodule update --rebase staying on master' '
	(cd super/submodule &&
	  git checkout master
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

test_expect_success 'submodule update --merge staying on master' '
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
Execution of 'false $submodulesha1' failed in submodule path 'submodule'
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
	test_i18ncmp actual expect
'

cat << EOF >expect
Execution of 'false $submodulesha1' failed in submodule path '../submodule'
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
	test_i18ncmp actual expect
'

test_expect_success 'submodule update - command run for initial population of submodule' '
	cat >expect <<-EOF &&
	Execution of '\''false $submodulesha1'\'' failed in submodule path '\''submodule'\''
	EOF
	rm -rf super/submodule &&
	test_must_fail git -C super submodule update 2>actual &&
	test_i18ncmp expect actual &&
	git -C super submodule update --checkout
'

cat << EOF >expect
Execution of 'false $submodulesha1' failed in submodule path '../super/submodule'
Failed to recurse into submodule path '../super'
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
	test_i18ncmp actual expect
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
	  git checkout master &&
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
	  git checkout master &&
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
	 git checkout master &&
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
	 test_must_fail git submodule update --recursive &&
	 (cd submodule2 &&
	  git rev-parse --verify HEAD >../actual
	 ) &&
	 test_cmp expect actual
	)
'

test_expect_success 'submodule update exit immediately in case of merge conflict' '
	(cd super &&
	 git checkout master &&
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
	  git checkout master &&
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
	 git checkout master &&
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
	  git checkout master &&
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
	 git push origin master
	) &&
	git clone subsuper_update_r subsuper_update_r2 &&
	(cd subsuper_update_r2 &&
	 test_commit "update_subsuper" file &&
	 git submodule add ../subsubsuper_update_r subsubmodule &&
	 git commit -am "subsubmodule" &&
	 git push origin master
	) &&
	git clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 test_commit "update_super" file &&
	 git submodule add ../subsuper_update_r submodule &&
	 git commit -am "submodule" &&
	 git push origin master
	) &&
	rm -rf super_update_r2 &&
	git clone super_update_r super_update_r2 &&
	(cd super_update_r2 &&
	 git submodule update --init --recursive >actual &&
	 test_i18ngrep "Submodule path .submodule/subsubmodule.: checked out" actual &&
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
	 git reset --hard master &&
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
		test_i18ngrep "Direct fetching of that commit failed." actual &&
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
	 test_i18ngrep "Submodule path .deeper/submodule/subsubmodule.: checked out" actual
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

test_done
