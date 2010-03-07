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
	git commit -m upstream
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

test_done
