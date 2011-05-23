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

test_done
