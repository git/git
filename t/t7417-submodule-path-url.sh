#!/bin/sh

test_description='check handling of .gitmodule path with dash'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'create submodule with dash in path' '
	git init upstream &&
	git -C upstream commit --allow-empty -m base &&
	git submodule add ./upstream sub &&
	git mv sub ./-sub &&
	git commit -m submodule
'

test_expect_success 'clone rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	git clone --recurse-submodules . dst 2>err &&
	test_i18ngrep ignoring err
'

test_expect_success 'fsck rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesPath err
'

test_expect_success MINGW 'submodule paths disallows trailing spaces' '
	git init super &&
	test_must_fail git -C super submodule add ../upstream "sub " &&

	: add "sub", then rename "sub" to "sub ", the hard way &&
	git -C super submodule add ../upstream sub &&
	tree=$(git -C super write-tree) &&
	git -C super ls-tree $tree >tree &&
	sed "s/sub/sub /" <tree >tree.new &&
	tree=$(git -C super mktree <tree.new) &&
	commit=$(echo with space | git -C super commit-tree $tree) &&
	git -C super update-ref refs/heads/main $commit &&

	test_must_fail git clone --recurse-submodules super dst 2>err &&
	test_i18ngrep "sub " err
'

test_done
