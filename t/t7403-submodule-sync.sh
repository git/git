#!/bin/sh
#
# Copyright (c) 2008 David Aguilar
#

test_description='git submodule sync

These tests exercise the "git submodule sync" subcommand.
'

. ./test-lib.sh

test_expect_success setup '
	echo file > file &&
	git add file &&
	test_tick &&
	git commit -m upstream &&
	git clone . super &&
	git clone super submodule &&
	(cd super &&
	 git submodule add ../submodule submodule &&
	 test_tick &&
	 git commit -m "submodule"
	) &&
	git clone super super-clone &&
	(cd super-clone && git submodule update --init) &&
	git clone super empty-clone &&
	(cd empty-clone && git submodule init) &&
	git clone super top-only-clone
'

test_expect_success 'change submodule' '
	(cd submodule &&
	 echo second line >> file &&
	 test_tick &&
	 git commit -a -m "change submodule"
	)
'

test_expect_success 'change submodule url' '
	(cd super &&
	 cd submodule &&
	 git checkout master &&
	 git pull
	) &&
	mv submodule moved-submodule &&
	(cd super &&
	 git config -f .gitmodules submodule.submodule.url ../moved-submodule &&
	 test_tick &&
	 git commit -a -m moved-submodule
	)
'

test_expect_success '"git submodule sync" should update submodule URLs' '
	(cd super-clone &&
	 git pull --no-recurse-submodules &&
	 git submodule sync
	) &&
	test -d "$(cd super-clone/submodule &&
	 git config remote.origin.url
	)" &&
	(cd super-clone/submodule &&
	 git checkout master &&
	 git pull
	) &&
	(cd super-clone &&
	 test -d "$(git config submodule.submodule.url)"
	)
'

test_expect_success '"git submodule sync" should update known submodule URLs' '
	(cd empty-clone &&
	 git pull &&
	 git submodule sync &&
	 test -d "$(git config submodule.submodule.url)"
	)
'

test_expect_success '"git submodule sync" should not vivify uninteresting submodule' '
	(cd top-only-clone &&
	 git pull &&
	 git submodule sync &&
	 test -z "$(git config submodule.submodule.url)" &&
	 git submodule sync submodule &&
	 test -z "$(git config submodule.submodule.url)"
	)
'

test_done
