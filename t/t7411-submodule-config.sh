#!/bin/sh
#
# Copyright (c) 2014 Heiko Voigt
#

test_description='Test submodules config cache infrastructure

This test verifies that parsing .butmodules configurations directly
from the database and from the worktree works.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(cd submodule &&
		but init &&
		echo a >a &&
		but add . &&
		but cummit -ma
	) &&
	mkdir super &&
	(cd super &&
		but init &&
		but submodule add ../submodule &&
		but submodule add ../submodule a &&
		but cummit -m "add as submodule and as a" &&
		but mv a b &&
		but cummit -m "move a to b"
	)
'

test_expect_success 'configuration parsing with error' '
	test_when_finished "rm -rf repo" &&
	test_create_repo repo &&
	cat >repo/.butmodules <<-\EOF &&
	[submodule "s"]
		path
		ignore
	EOF
	(
		cd repo &&
		test_must_fail test-tool submodule-config "" s 2>actual &&
		test_i18ngrep "bad config" actual
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
		test-tool submodule-config \
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
		test-tool submodule-config --name \
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

test_expect_success 'error in history of one submodule config lets continue, stderr message contains blob ref' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		cp .butmodules .butmodules.bak &&
		echo "	value = \"" >>.butmodules &&
		but add .butmodules &&
		mv .butmodules.bak .butmodules &&
		but cummit -m "add error" &&
		sha1=$(but rev-parse HEAD) &&
		test-tool submodule-config \
			HEAD b \
			HEAD submodule \
				>actual \
				2>actual_stderr &&
		test_cmp expect_error actual &&
		test_i18ngrep "submodule-blob $sha1:.butmodules" actual_stderr >/dev/null
	)
'

test_expect_success 'using different treeishs works' '
	(
		cd super &&
		but tag new_tag &&
		tree=$(but rev-parse HEAD^{tree}) &&
		cummit=$(but rev-parse HEAD^{cummit}) &&
		test-tool submodule-config $cummit b >expect &&
		test-tool submodule-config $tree b >actual.1 &&
		test-tool submodule-config new_tag b >actual.2 &&
		test_cmp expect actual.1 &&
		test_cmp expect actual.2
	)
'

test_expect_success 'error in history in fetchrecursesubmodule lets continue' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		but config -f .butmodules \
			submodule.submodule.fetchrecursesubmodules blabla &&
		but add .butmodules &&
		but config --unset -f .butmodules \
			submodule.submodule.fetchrecursesubmodules &&
		but cummit -m "add error in fetchrecursesubmodules" &&
		test-tool submodule-config \
			HEAD b \
			HEAD submodule \
				>actual &&
		test_cmp expect_error actual
	)
'

test_expect_success 'reading submodules config from the working tree with "submodule--helper config"' '
	(cd super &&
		echo "../submodule" >expect &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'unsetting submodules config from the working tree with "submodule--helper config --unset"' '
	(cd super &&
		but submodule--helper config --unset submodule.submodule.url &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_must_be_empty actual
	)
'


test_expect_success 'writing submodules config with "submodule--helper config"' '
	(cd super &&
		echo "new_url" >expect &&
		but submodule--helper config submodule.submodule.url "new_url" &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'overwriting unstaged submodules config with "submodule--helper config"' '
	test_when_finished "but -C super checkout .butmodules" &&
	(cd super &&
		echo "newer_url" >expect &&
		but submodule--helper config submodule.submodule.url "newer_url" &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'writeable .butmodules when it is in the working tree' '
	but -C super submodule--helper config --check-writeable
'

test_expect_success 'writeable .butmodules when it is nowhere in the repository' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		but rm .butmodules &&
		but cummit -m "remove .butmodules from the current branch" &&
		but submodule--helper config --check-writeable
	)
'

test_expect_success 'non-writeable .butmodules when it is in the index but not in the working tree' '
	test_when_finished "but -C super checkout .butmodules" &&
	(cd super &&
		rm -f .butmodules &&
		test_must_fail but submodule--helper config --check-writeable
	)
'

test_expect_success 'non-writeable .butmodules when it is in the current branch but not in the index' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		but rm .butmodules &&
		test_must_fail but submodule--helper config --check-writeable
	)
'

test_expect_success 'reading submodules config from the index when .butmodules is not in the working tree' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		but submodule--helper config submodule.submodule.url "staged_url" &&
		but add .butmodules &&
		rm -f .butmodules &&
		echo "staged_url" >expect &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reading submodules config from the current branch when .butmodules is not in the index' '
	ORIG=$(but -C super rev-parse HEAD) &&
	test_when_finished "but -C super reset --hard $ORIG" &&
	(cd super &&
		but rm .butmodules &&
		echo "../submodule" >expect &&
		but submodule--helper config submodule.submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reading nested submodules config' '
	(cd super &&
		but init submodule/nested_submodule &&
		echo "a" >submodule/nested_submodule/a &&
		but -C submodule/nested_submodule add a &&
		but -C submodule/nested_submodule cummit -m "add a" &&
		but -C submodule submodule add ./nested_submodule &&
		but -C submodule add nested_submodule &&
		but -C submodule cummit -m "added nested_submodule" &&
		but add submodule &&
		but cummit -m "updated submodule" &&
		echo "./nested_submodule" >expect &&
		test-tool submodule-nested-repo-config \
			submodule submodule.nested_submodule.url >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reading nested submodules config when .butmodules is not in the working tree' '
	test_when_finished "but -C super/submodule checkout .butmodules" &&
	(cd super &&
		echo "./nested_submodule" >expect &&
		rm submodule/.butmodules &&
		test-tool submodule-nested-repo-config \
			submodule submodule.nested_submodule.url >actual 2>warning &&
		test_must_be_empty warning &&
		test_cmp expect actual
	)
'

test_done
