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
	git config set --global protocol.file.allow always &&
	# Some tests migrate the ref storage format, which does not work with
	# reflogs at the time of writing these tests.
	git config set --global core.logAllRefUpdates false
'

test_expect_success 'add existing repository with different ref storage format' '
	test_when_finished "rm -rf parent" &&

	git init parent &&
	(
		cd parent &&
		test_commit parent &&
		git init --ref-format=$OTHER_FORMAT submodule &&
		test_commit -C submodule submodule &&
		git submodule add ./submodule
	)
'

test_expect_success 'add submodules with different ref storage format' '
	test_when_finished "rm -rf submodule upstream" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	test_ref_format upstream "$GIT_DEFAULT_REF_FORMAT" &&
	git -C upstream submodule add --ref-format="$OTHER_FORMAT" "file://$(pwd)/submodule" &&
	test_ref_format upstream/submodule "$OTHER_FORMAT"
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

test_expect_success 'status with mixed submodule ref storages' '
	test_when_finished "rm -rf submodule main" &&

	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init main &&
	git -C main submodule add "file://$(pwd)/submodule" &&
	git -C main commit -m "add submodule" &&
	git -C main/submodule refs migrate --ref-format=$OTHER_FORMAT &&

	# The main repository should use the default ref format now, whereas
	# the submodule should use the other format.
	test_ref_format main "$GIT_DEFAULT_REF_FORMAT" &&
	test_ref_format main/submodule "$OTHER_FORMAT" &&

	cat >expect <<-EOF &&
	 $(git -C main/submodule rev-parse HEAD) submodule (submodule-initial)
	EOF
	git -C main submodule status >actual &&
	test_cmp expect actual
'

test_expect_success 'recursive pull with mixed formats' '
	test_when_finished "rm -rf submodule upstream downstream" &&

	# Set up the initial structure with an upstream repository that has a
	# submodule, as well as a downstream clone of the upstream repository.
	git init submodule &&
	test_commit -C submodule submodule-initial &&
	git init upstream &&
	git -C upstream submodule add "file://$(pwd)/submodule" &&
	git -C upstream commit -m "upstream submodule" &&

	# Clone the upstream repository such that the main repo and its
	# submodules have different formats.
	git clone --no-recurse-submodules "file://$(pwd)/upstream" downstream &&
	git -C downstream submodule update --init --ref-format=$OTHER_FORMAT &&
	test_ref_format downstream "$GIT_DEFAULT_REF_FORMAT" &&
	test_ref_format downstream/submodule "$OTHER_FORMAT" &&

	# Update the upstream submodule as well as the owning repository such
	# that we can do a recursive pull.
	test_commit -C submodule submodule-update &&
	git -C upstream/submodule pull &&
	git -C upstream commit -am "update the submodule" &&

	git -C downstream pull --recurse-submodules &&
	git -C upstream/submodule rev-parse HEAD >expect &&
	git -C downstream/submodule rev-parse HEAD >actual &&
	test_cmp expect actual
'

done

test_done
