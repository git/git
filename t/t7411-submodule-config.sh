#!/bin/sh
#
# Copyright (c) 2014 Heiko Voigt
#

test_description='Test submodules config cache infrastructure

This test verifies that parsing .gitmodules configurations directly
from the database and from the worktree works.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(cd submodule &&
		git init &&
		echo a >a &&
		git add . &&
		git commit -ma
	) &&
	mkdir super &&
	(cd super &&
		git init &&
		git submodule add ../submodule &&
		git submodule add ../submodule a &&
		git commit -m "add as submodule and as a" &&
		git mv a b &&
		git commit -m "move a to b"
	)
'

cat >super/expect <<EOF
Submodule name: 'a' for path 'a'
Submodule name: 'a' for path 'b'
Submodule name: 'submodule' for path 'submodule'
Submodule name: 'submodule' for path 'submodule'
EOF

test_expect_success 'test parsing and lookup of submodule config by path' '
	(cd super &&
		test-submodule-config \
			HEAD^ a \
			HEAD b \
			HEAD^ submodule \
			HEAD submodule \
				>actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test parsing and lookup of submodule config by name' '
	(cd super &&
		test-submodule-config --name \
			HEAD^ a \
			HEAD a \
			HEAD^ submodule \
			HEAD submodule \
				>actual &&
		test_cmp expect actual
	)
'

cat >super/expect_error <<EOF
Submodule name: 'a' for path 'b'
Submodule name: 'submodule' for path 'submodule'
EOF

test_expect_success 'error in one submodule config lets continue' '
	(cd super &&
		cp .gitmodules .gitmodules.bak &&
		echo "	value = \"" >>.gitmodules &&
		git add .gitmodules &&
		mv .gitmodules.bak .gitmodules &&
		git commit -m "add error" &&
		test-submodule-config \
			HEAD b \
			HEAD submodule \
				>actual &&
		test_cmp expect_error actual
	)
'

cat >super/expect_url <<EOF
Submodule url: 'git@somewhere.else.net:a.git' for path 'b'
Submodule url: 'git@somewhere.else.net:submodule.git' for path 'submodule'
EOF

cat >super/expect_local_path <<EOF
Submodule name: 'a' for path 'c'
Submodule name: 'submodule' for path 'submodule'
EOF

test_expect_success 'reading of local configuration' '
	(cd super &&
		old_a=$(git config submodule.a.url) &&
		old_submodule=$(git config submodule.submodule.url) &&
		git config submodule.a.url git@somewhere.else.net:a.git &&
		git config submodule.submodule.url git@somewhere.else.net:submodule.git &&
		test-submodule-config --url \
			"" b \
			"" submodule \
				>actual &&
		test_cmp expect_url actual &&
		git config submodule.a.path c &&
		test-submodule-config \
			"" c \
			"" submodule \
				>actual &&
		test_cmp expect_local_path actual &&
		git config submodule.a.url $old_a &&
		git config submodule.submodule.url $old_submodule &&
		git config --unset submodule.a.path c
	)
'

test_done
