#!/bin/sh
#
# Copyright (c) 2009 Johan Herland
#

test_description='Test "git submodule foreach"

This test verifies that "git submodule foreach" correctly visits all submodules
that are currently checked out.
'

. ./test-lib.sh


test_expect_success 'setup a submodule tree' '
	echo file > file &&
	git add file &&
	test_tick &&
	git commit -m upstream
	git clone . super &&
	git clone super submodule &&
	(
		cd super &&
		git submodule add ../submodule sub1 &&
		git submodule add ../submodule sub2 &&
		git submodule add ../submodule sub3 &&
		git config -f .gitmodules --rename-section \
			submodule.sub1 submodule.foo1 &&
		git config -f .gitmodules --rename-section \
			submodule.sub2 submodule.foo2 &&
		git config -f .gitmodules --rename-section \
			submodule.sub3 submodule.foo3 &&
		git add .gitmodules
		test_tick &&
		git commit -m "submodules" &&
		git submodule init sub1 &&
		git submodule init sub2 &&
		git submodule init sub3
	) &&
	(
		cd submodule &&
		echo different > file &&
		git add file &&
		test_tick &&
		git commit -m "different"
	) &&
	(
		cd super &&
		(
			cd sub3 &&
			git pull
		) &&
		git add sub3 &&
		test_tick &&
		git commit -m "update sub3"
	)
'

sub1sha1=$(cd super/sub1 && git rev-parse HEAD)
sub3sha1=$(cd super/sub3 && git rev-parse HEAD)

cat > expect <<EOF
Entering 'sub1'
foo1-sub1-$sub1sha1
Entering 'sub3'
foo3-sub3-$sub3sha1
EOF

test_expect_success 'test basic "submodule foreach" usage' '
	git clone super clone &&
	(
		cd clone &&
		git submodule update --init -- sub1 sub3 &&
		git submodule foreach "echo \$name-\$path-\$sha1" > ../actual
	) &&
	test_cmp expect actual
'

test_done
