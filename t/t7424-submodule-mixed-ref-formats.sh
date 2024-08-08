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

test_expect_success 'recursive clone propagates ref storage format' '
	test_when_finished "rm -rf submodule upstream downstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	git -C upstream submodule add "file://$(pwd)/submodule" &&
	git -C upstream commit -am "add submodule" &&

	# The upstream repository and its submodule should be using the default
	# ref format.
	test_ref_format upstream "$GIT_DEFAULT_REF_FORMAT" &&
	test_ref_format upstream/submodule "$GIT_DEFAULT_REF_FORMAT" &&

	# The cloned repositories should use the other ref format that we have
	# specified via `--ref-format`. The option should propagate to cloned
	# submodules.
	git clone --ref-format=$OTHER_FORMAT --recurse-submodules \
		upstream downstream &&
	test_ref_format downstream "$OTHER_FORMAT" &&
	test_ref_format downstream/submodule "$OTHER_FORMAT"
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
