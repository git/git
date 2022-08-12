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
	git init vanish/src &&
	scalar register vanish/src &&
	git config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	grep -F "$(pwd)/vanish/src" scalar.repos &&
	rm -rf vanish/src/.git &&
	scalar unregister vanish &&
	test_must_fail git config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	! grep -F "$(pwd)/vanish/src" scalar.repos
'

test_expect_success 'set up repository to clone' '
	test_commit first &&
	test_commit second &&
	test_commit third &&
	git switch -c parallel first &&
	mkdir -p 1/2 &&
	test_commit 1/2/3 &&
	git config uploadPack.allowFilter true &&
	git config uploadPack.allowAnySHA1InWant true
'

test_expect_success 'scalar clone' '
	second=$(git rev-parse --verify second:second.t) &&
	scalar clone "file://$(pwd)" cloned --single-branch &&
	(
		cd cloned/src &&

		git config --get --global --fixed-value maintenance.repo \
			"$(pwd)" &&

		git for-each-ref --format="%(refname)" refs/remotes/origin/ >actual &&
		echo "refs/remotes/origin/parallel" >expect &&
		test_cmp expect actual &&

		test_path_is_missing 1/2 &&
		test_must_fail git rev-list --missing=print $second &&
		git rev-list $second &&
		git cat-file blob $second >actual &&
		echo "second" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'scalar reconfigure' '
	git init one/src &&
	scalar register one &&
	git -C one/src config core.preloadIndex false &&
	scalar reconfigure one &&
	test true = "$(git -C one/src config core.preloadIndex)" &&
	git -C one/src config core.preloadIndex false &&
	scalar reconfigure -a &&
	test true = "$(git -C one/src config core.preloadIndex)"
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
	git init sub &&
	scalar -C sub -c status.aheadBehind=bogus register &&
	test -z "$(git -C sub config --local status.aheadBehind)" &&
	test true = "$(git -C sub config core.preloadIndex)"
'

test_expect_success '`scalar [...] <dir>` errors out when dir is missing' '
	! scalar run config cloned 2>err &&
	grep "cloned. does not exist" err
'

SQ="'"
test_expect_success UNZIP 'scalar diagnose' '
	scalar clone "file://$(pwd)" cloned --single-branch &&
	git repack &&
	echo "$(pwd)/.git/objects/" >>cloned/src/.git/objects/info/alternates &&
	test_commit -C cloned/src loose &&
	scalar diagnose cloned >out 2>err &&
	grep "Available space" out &&
	sed -n "s/.*$SQ\\(.*\\.zip\\)$SQ.*/\\1/p" <err >zip_path &&
	zip_path=$(cat zip_path) &&
	test -n "$zip_path" &&
	"$GIT_UNZIP" -v "$zip_path" &&
	folder=${zip_path%.zip} &&
	test_path_is_missing "$folder" &&
	"$GIT_UNZIP" -p "$zip_path" diagnostics.log >out &&
	test_file_not_empty out &&
	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep "$(pwd)/.git/objects" out &&
	"$GIT_UNZIP" -p "$zip_path" objects-local.txt >out &&
	grep "^Total: [1-9]" out
'

test_done
