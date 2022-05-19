#!/bin/sh

test_description='check handling of .butmodule path with dash'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'create submodule with dash in path' '
	but init upstream &&
	but -C upstream cummit --allow-empty -m base &&
	but submodule add ./upstream sub &&
	but mv sub ./-sub &&
	but cummit -m submodule
'

test_expect_success 'clone rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	but clone --recurse-submodules . dst 2>err &&
	test_i18ngrep ignoring err
'

test_expect_success 'fsck rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	but -C dst config transfer.fsckObjects true &&
	test_must_fail but push dst HEAD 2>err &&
	grep butmodulesPath err
'

test_expect_success MINGW 'submodule paths disallows trailing spaces' '
	but init super &&
	test_must_fail but -C super submodule add ../upstream "sub " &&

	: add "sub", then rename "sub" to "sub ", the hard way &&
	but -C super submodule add ../upstream sub &&
	tree=$(but -C super write-tree) &&
	but -C super ls-tree $tree >tree &&
	sed "s/sub/sub /" <tree >tree.new &&
	tree=$(but -C super mktree <tree.new) &&
	cummit=$(echo with space | but -C super cummit-tree $tree) &&
	but -C super update-ref refs/heads/main $cummit &&

	test_must_fail but clone --recurse-submodules super dst 2>err &&
	test_i18ngrep "sub " err
'

test_done
