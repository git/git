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
    sha_master=`git rev-list --max-count=1 master`
    sha_head=`git rev-list --max-count=1 HEAD`

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
	)
	(cd super &&
	 git submodule add ../none none &&
	 test_tick &&
	 git commit -m "none"
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

apos="'";
test_expect_success 'submodule update does not fetch already present commits' '
	(cd submodule &&
	  echo line3 >> file &&
	  git add file &&
	  test_tick &&
	  git commit -m "upstream line3"
	) &&
	(cd super/submodule &&
	  head=$(git rev-parse --verify HEAD) &&
	  echo "Submodule path ${apos}submodule$apos: checked out $apos$head$apos" > ../../expected &&
	  git reset --hard HEAD~1
	) &&
	(cd super &&
	  git submodule update > ../actual 2> ../actual.err
	) &&
	test_i18ncmp expected actual &&
	! test -s actual.err
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
	 git diff --raw | grep "	submodule" &&
	 git submodule update &&
	 git diff --raw | grep "	submodule" &&
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
	 git diff --raw | grep "	submodule" &&
	 git submodule update --checkout &&
	 test_must_fail git diff --raw \| grep "	submodule" &&
	 (cd submodule &&
	  test_must_fail compare_head
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
	 test -e submodule/.git &&
	 test_must_fail test -e none/.git
	)
'

test_expect_success 'submodule update continues after checkout error' '
	(cd super &&
	 git reset --hard HEAD &&
	 git submodule add ../submodule submodule2 &&
	 git submodule init &&
	 git commit -am "new_submodule" &&
	 (cd submodule2 &&
	  git rev-parse --max-count=1 HEAD > ../expect
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
	  git rev-parse --max-count=1 HEAD > ../actual
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
	  git rev-parse --max-count=1 HEAD > ../expect &&
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
	  git rev-parse --max-count=1 HEAD > ../actual
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
	  git rev-parse --max-count=1 HEAD > ../expect
	 ) &&
	 git config submodule.submodule.update merge &&
	 test_must_fail git submodule update &&
	 (cd submodule2 &&
	  git rev-parse --max-count=1 HEAD > ../actual
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
	  git rev-parse --max-count=1 HEAD > ../expect
	 ) &&
	 git config submodule.submodule.update rebase &&
	 test_must_fail git submodule update &&
	 (cd submodule2 &&
	  git rev-parse --max-count=1 HEAD > ../actual
	 ) &&
	 test_cmp expect actual
	)
'
test_done
