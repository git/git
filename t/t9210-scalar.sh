#!/bin/sh

test_description='test the `scalar` command'

. ./test-lib.sh

GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt,launchctl:true,schtasks:true"
export GIT_TEST_MAINT_SCHEDULER

test_expect_success 'scalar shows a usage' '
	test_expect_code 129 scalar -h
'

test_expect_success 'scalar invoked on enlistment root' '
	test_when_finished rm -rf test src deeper &&

	for enlistment_root in test src deeper/test
	do
		git init ${enlistment_root}/src &&

		# Register
		scalar register ${enlistment_root} &&
		scalar list >out &&
		grep "$(pwd)/${enlistment_root}/src\$" out &&

		# Delete (including enlistment root)
		scalar delete $enlistment_root &&
		test_path_is_missing $enlistment_root &&
		scalar list >out &&
		! grep "^$(pwd)/${enlistment_root}/src\$" out || return 1
	done
'

test_expect_success 'scalar invoked on enlistment src repo' '
	test_when_finished rm -rf test src deeper &&

	for enlistment_root in test src deeper/test
	do
		git init ${enlistment_root}/src &&

		# Register
		scalar register ${enlistment_root}/src &&
		scalar list >out &&
		grep "$(pwd)/${enlistment_root}/src\$" out &&

		# Delete (will not include enlistment root)
		scalar delete ${enlistment_root}/src &&
		test_path_is_dir $enlistment_root &&
		scalar list >out &&
		! grep "^$(pwd)/${enlistment_root}/src\$" out || return 1
	done
'

test_expect_success 'scalar invoked when enlistment root and repo are the same' '
	test_when_finished rm -rf test src deeper &&

	for enlistment_root in test src deeper/test
	do
		git init ${enlistment_root} &&

		# Register
		scalar register ${enlistment_root} &&
		scalar list >out &&
		grep "$(pwd)/${enlistment_root}\$" out &&

		# Delete (will not include enlistment root)
		scalar delete ${enlistment_root} &&
		test_path_is_missing $enlistment_root &&
		scalar list >out &&
		! grep "^$(pwd)/${enlistment_root}\$" out &&

		# Make sure we did not accidentally delete the trash dir
		test_path_is_dir "$TRASH_DIRECTORY" || return 1
	done
'

test_expect_success 'scalar repo search respects GIT_CEILING_DIRECTORIES' '
	test_when_finished rm -rf test &&

	git init test/src &&
	mkdir -p test/src/deep &&
	GIT_CEILING_DIRECTORIES="$(pwd)/test/src" &&
	! scalar register test/src/deep 2>err &&
	grep "not a git repository" err
'

test_expect_success 'scalar enlistments need a worktree' '
	test_when_finished rm -rf bare test &&

	git init --bare bare/src &&
	! scalar register bare/src 2>err &&
	grep "Scalar enlistments require a worktree" err &&

	git init test/src &&
	! scalar register test/src/.git 2>err &&
	grep "Scalar enlistments require a worktree" err
'

test_expect_success FSMONITOR_DAEMON 'scalar register starts fsmon daemon' '
	git init test/src &&
	test_must_fail git -C test/src fsmonitor--daemon status &&
	scalar register test/src &&
	git -C test/src fsmonitor--daemon status &&
	test_cmp_config -C test/src true core.fsmonitor
'

test_expect_success 'scalar register warns when background maintenance fails' '
	git init register-repo &&
	GIT_TEST_MAINT_SCHEDULER="crontab:false,launchctl:false,schtasks:false" \
		scalar register register-repo 2>err &&
	grep "could not turn on maintenance" err
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
	! grep -F "$(pwd)/vanish/src" scalar.repos &&

	# scalar unregister should be idempotent
	scalar unregister vanish
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

		# This relies on the fact that the presence of "--missing"
		# on the command line forces lazy fetching off before
		# "$second^{blob}" gets parsed.  Without "^{blob}", a
		# bare object name "$second" is taken into the queue and
		# the command may not fail with a fixed "rev-list --missing".
		test_must_fail git rev-list --missing=print "$second^{blob}" -- &&

		git rev-list $second &&
		git cat-file blob $second >actual &&
		echo "second" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'scalar clone --no-... opts' '
	# Note: redirect stderr always to avoid having a verbose test
	# run result in a difference in the --[no-]progress option.
	GIT_TRACE2_EVENT="$(pwd)/no-opt-trace" scalar clone \
		--no-tags --no-src \
		"file://$(pwd)" no-opts --single-branch 2>/dev/null &&

	test_subcommand git fetch --quiet --no-progress \
			origin --no-tags <no-opt-trace &&
	(
		cd no-opts &&

		test_cmp_config --no-tags remote.origin.tagopt &&
		git for-each-ref --format="%(refname)" refs/tags/ >tags &&
		test_line_count = 0 tags
	)
'

test_expect_success 'scalar reconfigure' '
	git init one/src &&
	scalar register one &&
	git -C one/src config core.preloadIndex false &&
	scalar reconfigure one &&
	test true = "$(git -C one/src config core.preloadIndex)" &&
	git -C one/src config core.preloadIndex false &&
	rm one/src/cron.txt &&
	GIT_TRACE2_EVENT="$(pwd)/reconfigure" scalar reconfigure -a &&
	test_path_is_file one/src/cron.txt &&
	test true = "$(git -C one/src config core.preloadIndex)" &&
	test_subcommand git maintenance start <reconfigure
'

test_expect_success 'scalar reconfigure --all with includeIf.onbranch' '
	repos="two three four" &&
	for num in $repos
	do
		git init $num/src &&
		scalar register $num/src &&
		git -C $num/src config includeif."onbranch:foo".path something &&
		git -C $num/src config core.preloadIndex false || return 1
	done &&

	scalar reconfigure --all &&

	for num in $repos
	do
		test true = "$(git -C $num/src config core.preloadIndex)" || return 1
	done
'

 test_expect_success 'scalar reconfigure --all with detached HEADs' '
	repos="two three four" &&
	for num in $repos
	do
		rm -rf $num/src &&
		git init $num/src &&
		scalar register $num/src &&
		git -C $num/src config core.preloadIndex false &&
		test_commit -C $num/src initial &&
		git -C $num/src switch --detach HEAD || return 1
	done &&

	scalar reconfigure --all &&

	for num in $repos
	do
		test true = "$(git -C $num/src config core.preloadIndex)" || return 1
	done
'

test_expect_success '`reconfigure -a` removes stale config entries' '
	git init stale/src &&
	scalar register stale &&
	scalar list >scalar.repos &&
	grep stale scalar.repos &&

	grep -v stale scalar.repos >expect &&

	rm -rf stale &&
	scalar reconfigure -a &&
	scalar list >scalar.repos &&
	test_cmp expect scalar.repos
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
