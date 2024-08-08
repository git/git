#!/bin/sh

test_description='submodules handle mixed ref storage formats'

. ./test-lib.sh

test_ref_format () {
	echo "$2" >expect &&
	git -C "$1" rev-parse --show-ref-format >actual &&
	test_cmp expect actual
}

for OTHER_FORMAT in files reftable
do
	if test "$OTHER_FORMAT" = "$GIT_DEFAULT_REF_FORMAT"
	then
		continue
	fi

test_expect_success 'setup' '
	git config set --global protocol.file.allow always
'

test_expect_success 'clone submodules with different ref storage format' '
	test_when_finished "rm -rf submodule upstream downstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	git -C upstream submodule add "file://$(pwd)/submodule" &&
	git -C upstream commit -m "upstream submodule" &&

	git clone --no-recurse-submodules "file://$(pwd)/upstream" downstream &&
	test_ref_format downstream "$GIT_DEFAULT_REF_FORMAT" &&
	git -C downstream submodule update --init --ref-format=$OTHER_FORMAT &&
	test_ref_format downstream/submodule "$OTHER_FORMAT"
'

done

test_done
