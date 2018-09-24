#!/bin/sh

test_description='check handling of .gitmodule url with dash'
. ./test-lib.sh

test_expect_success 'create submodule with protected dash in url' '
	git init upstream &&
	git -C upstream commit --allow-empty -m base &&
	mv upstream ./-upstream &&
	git submodule add ./-upstream sub &&
	git add sub .gitmodules &&
	git commit -m submodule
'

test_expect_success 'clone can recurse submodule' '
	test_when_finished "rm -rf dst" &&
	git clone --recurse-submodules . dst &&
	echo base >expect &&
	git -C dst/sub log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'fsck accepts protected dash' '
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	git push dst HEAD
'

test_expect_success 'remove ./ protection from .gitmodules url' '
	perl -i -pe "s{\./}{}" .gitmodules &&
	git commit -am "drop protection"
'

test_expect_success 'clone rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	test_must_fail git clone --recurse-submodules . dst 2>err &&
	test_i18ngrep ignoring err
'

test_expect_success 'fsck rejects unprotected dash' '
	test_when_finished "rm -rf dst" &&
	git init --bare dst &&
	git -C dst config transfer.fsckObjects true &&
	test_must_fail git push dst HEAD 2>err &&
	grep gitmodulesUrl err
'

test_done
