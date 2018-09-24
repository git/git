#!/bin/sh

test_description='check handling of .gitmodule path with dash'
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

test_done
