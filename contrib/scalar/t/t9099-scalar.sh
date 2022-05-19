#!/bin/sh

test_description='test the `scalar` command'

TEST_DIRECTORY=$PWD/../../../t
export TEST_DIRECTORY

# Make it work with --no-bin-wrappers
PATH=$PWD/..:$PATH

. ../../../t/test-lib.sh

GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab ../cron.txt,launchctl:true,schtasks:true"
export GIT_TEST_MAINT_SCHEDULER

test_expect_success 'scalar shows a usage' '
	test_expect_code 129 scalar -h
'

test_expect_success 'scalar unregister' '
	but init vanish/src &&
	scalar register vanish/src &&
	but config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	grep -F "$(pwd)/vanish/src" scalar.repos &&
	rm -rf vanish/src/.but &&
	scalar unregister vanish &&
	test_must_fail but config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	! grep -F "$(pwd)/vanish/src" scalar.repos
'

test_expect_success 'set up repository to clone' '
	test_cummit first &&
	test_cummit second &&
	test_cummit third &&
	but switch -c parallel first &&
	mkdir -p 1/2 &&
	test_cummit 1/2/3 &&
	but config uploadPack.allowFilter true &&
	but config uploadPack.allowAnySHA1InWant true
'

test_expect_success 'scalar clone' '
	second=$(but rev-parse --verify second:second.t) &&
	scalar clone "file://$(pwd)" cloned --single-branch &&
	(
		cd cloned/src &&

		but config --get --global --fixed-value maintenance.repo \
			"$(pwd)" &&

		but for-each-ref --format="%(refname)" refs/remotes/origin/ >actual &&
		echo "refs/remotes/origin/parallel" >expect &&
		test_cmp expect actual &&

		test_path_is_missing 1/2 &&
		test_must_fail but rev-list --missing=print $second &&
		but rev-list $second &&
		but cat-file blob $second >actual &&
		echo "second" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'scalar reconfigure' '
	but init one/src &&
	scalar register one &&
	but -C one/src config core.preloadIndex false &&
	scalar reconfigure one &&
	test true = "$(but -C one/src config core.preloadIndex)" &&
	but -C one/src config core.preloadIndex false &&
	scalar reconfigure -a &&
	test true = "$(but -C one/src config core.preloadIndex)"
'

test_expect_success 'scalar delete without enlistment shows a usage' '
	test_expect_code 129 scalar delete
'

test_expect_success 'scalar delete with enlistment' '
	scalar delete cloned &&
	test_path_is_missing cloned
'

test_expect_success 'scalar supports -c/-C' '
	test_when_finished "scalar delete sub" &&
	but init sub &&
	scalar -C sub -c status.aheadBehind=bogus register &&
	test -z "$(but -C sub config --local status.aheadBehind)" &&
	test true = "$(but -C sub config core.preloadIndex)"
'

test_done
