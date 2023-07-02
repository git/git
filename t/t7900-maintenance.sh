#!/bin/sh

test_description='git maintenance builtin'

. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH=0
GIT_TEST_MULTI_PACK_INDEX=0

test_lazy_prereq XMLLINT '
	xmllint --version
'

test_xmllint () {
	if test_have_prereq XMLLINT
	then
		xmllint --noout "$@"
	else
		true
	fi
}

test_lazy_prereq SYSTEMD_ANALYZE '
	systemd-analyze verify /lib/systemd/system/basic.target
'

test_systemd_analyze_verify () {
	if test_have_prereq SYSTEMD_ANALYZE
	then
		systemd-analyze verify "$@"
	fi
}

test_expect_success 'help text' '
	test_expect_code 129 git maintenance -h >actual &&
	test_i18ngrep "usage: git maintenance <subcommand>" actual &&
	test_expect_code 129 git maintenance barf 2>err &&
	test_i18ngrep "unknown subcommand: \`barf'\''" err &&
	test_i18ngrep "usage: git maintenance" err &&
	test_expect_code 129 git maintenance 2>err &&
	test_i18ngrep "error: need a subcommand" err &&
	test_i18ngrep "usage: git maintenance" err
'

test_expect_success 'run [--auto|--quiet]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" \
		git maintenance run 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" \
		git maintenance run --auto 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-no-quiet.txt" \
		git maintenance run --no-quiet 2>/dev/null &&
	test_subcommand git gc --quiet <run-no-auto.txt &&
	test_subcommand ! git gc --auto --quiet <run-auto.txt &&
	test_subcommand git gc --no-quiet <run-no-quiet.txt
'

test_expect_success 'maintenance.auto config option' '
	GIT_TRACE2_EVENT="$(pwd)/default" git commit --quiet --allow-empty -m 1 &&
	test_subcommand git maintenance run --auto --quiet <default &&
	GIT_TRACE2_EVENT="$(pwd)/true" \
		git -c maintenance.auto=true \
		commit --quiet --allow-empty -m 2 &&
	test_subcommand git maintenance run --auto --quiet  <true &&
	GIT_TRACE2_EVENT="$(pwd)/false" \
		git -c maintenance.auto=false \
		commit --quiet --allow-empty -m 3 &&
	test_subcommand ! git maintenance run --auto --quiet  <false
'

test_expect_success 'maintenance.<task>.enabled' '
	git config maintenance.gc.enabled false &&
	git config maintenance.commit-graph.enabled true &&
	GIT_TRACE2_EVENT="$(pwd)/run-config.txt" git maintenance run 2>err &&
	test_subcommand ! git gc --quiet <run-config.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-config.txt
'

test_expect_success 'run --task=<task>' '
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" \
		git maintenance run --task=commit-graph 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-gc.txt" \
		git maintenance run --task=gc 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" \
		git maintenance run --task=commit-graph 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-both.txt" \
		git maintenance run --task=commit-graph --task=gc 2>/dev/null &&
	test_subcommand ! git gc --quiet <run-commit-graph.txt &&
	test_subcommand git gc --quiet <run-gc.txt &&
	test_subcommand git gc --quiet <run-both.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-commit-graph.txt &&
	test_subcommand ! git commit-graph write --split --reachable --no-progress <run-gc.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-both.txt
'

test_expect_success 'core.commitGraph=false prevents write process' '
	GIT_TRACE2_EVENT="$(pwd)/no-commit-graph.txt" \
		git -c core.commitGraph=false maintenance run \
		--task=commit-graph 2>/dev/null &&
	test_subcommand ! git commit-graph write --split --reachable --no-progress \
		<no-commit-graph.txt
'

test_expect_success 'commit-graph auto condition' '
	COMMAND="maintenance run --task=commit-graph --auto --quiet" &&

	GIT_TRACE2_EVENT="$(pwd)/cg-no.txt" \
		git -c maintenance.commit-graph.auto=1 $COMMAND &&
	GIT_TRACE2_EVENT="$(pwd)/cg-negative-means-yes.txt" \
		git -c maintenance.commit-graph.auto="-1" $COMMAND &&

	test_commit first &&

	GIT_TRACE2_EVENT="$(pwd)/cg-zero-means-no.txt" \
		git -c maintenance.commit-graph.auto=0 $COMMAND &&
	GIT_TRACE2_EVENT="$(pwd)/cg-one-satisfied.txt" \
		git -c maintenance.commit-graph.auto=1 $COMMAND &&

	git commit --allow-empty -m "second" &&
	git commit --allow-empty -m "third" &&

	GIT_TRACE2_EVENT="$(pwd)/cg-two-satisfied.txt" \
		git -c maintenance.commit-graph.auto=2 $COMMAND &&

	COMMIT_GRAPH_WRITE="git commit-graph write --split --reachable --no-progress" &&
	test_subcommand ! $COMMIT_GRAPH_WRITE <cg-no.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-negative-means-yes.txt &&
	test_subcommand ! $COMMIT_GRAPH_WRITE <cg-zero-means-no.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-one-satisfied.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-two-satisfied.txt
'

test_expect_success 'run --task=bogus' '
	test_must_fail git maintenance run --task=bogus 2>err &&
	test_i18ngrep "is not a valid task" err
'

test_expect_success 'run --task duplicate' '
	test_must_fail git maintenance run --task=gc --task=gc 2>err &&
	test_i18ngrep "cannot be selected multiple times" err
'

test_expect_success 'run --task=prefetch with no remotes' '
	git maintenance run --task=prefetch 2>err &&
	test_must_be_empty err
'

test_expect_success 'prefetch multiple remotes' '
	git clone . clone1 &&
	git clone . clone2 &&
	git remote add remote1 "file://$(pwd)/clone1" &&
	git remote add remote2 "file://$(pwd)/clone2" &&
	git -C clone1 switch -c one &&
	git -C clone2 switch -c two &&
	test_commit -C clone1 one &&
	test_commit -C clone2 two &&
	GIT_TRACE2_EVENT="$(pwd)/run-prefetch.txt" git maintenance run --task=prefetch 2>/dev/null &&
	fetchargs="--prefetch --prune --no-tags --no-write-fetch-head --recurse-submodules=no --quiet" &&
	test_subcommand git fetch remote1 $fetchargs <run-prefetch.txt &&
	test_subcommand git fetch remote2 $fetchargs <run-prefetch.txt &&
	test_path_is_missing .git/refs/remotes &&
	git log prefetch/remotes/remote1/one &&
	git log prefetch/remotes/remote2/two &&
	git fetch --all &&
	test_cmp_rev refs/remotes/remote1/one refs/prefetch/remotes/remote1/one &&
	test_cmp_rev refs/remotes/remote2/two refs/prefetch/remotes/remote2/two &&

	git log --oneline --decorate --all >log &&
	! grep "prefetch" log &&

	test_when_finished git config --unset remote.remote1.skipFetchAll &&
	git config remote.remote1.skipFetchAll true &&
	GIT_TRACE2_EVENT="$(pwd)/skip-remote1.txt" git maintenance run --task=prefetch 2>/dev/null &&
	test_subcommand ! git fetch remote1 $fetchargs <skip-remote1.txt &&
	test_subcommand git fetch remote2 $fetchargs <skip-remote1.txt
'

test_expect_success 'loose-objects task' '
	# Repack everything so we know the state of the object dir
	git repack -adk &&

	# Hack to stop maintenance from running during "git commit"
	echo in use >.git/objects/maintenance.lock &&

	# Assuming that "git commit" creates at least one loose object
	test_commit create-loose-object &&
	rm .git/objects/maintenance.lock &&

	ls .git/objects >obj-dir-before &&
	test_file_not_empty obj-dir-before &&
	ls .git/objects/pack/*.pack >packs-before &&
	test_line_count = 1 packs-before &&

	# The first run creates a pack-file
	# but does not delete loose objects.
	git maintenance run --task=loose-objects &&
	ls .git/objects >obj-dir-between &&
	test_cmp obj-dir-before obj-dir-between &&
	ls .git/objects/pack/*.pack >packs-between &&
	test_line_count = 2 packs-between &&
	ls .git/objects/pack/loose-*.pack >loose-packs &&
	test_line_count = 1 loose-packs &&

	# The second run deletes loose objects
	# but does not create a pack-file.
	git maintenance run --task=loose-objects &&
	ls .git/objects >obj-dir-after &&
	cat >expect <<-\EOF &&
	info
	pack
	EOF
	test_cmp expect obj-dir-after &&
	ls .git/objects/pack/*.pack >packs-after &&
	test_cmp packs-between packs-after
'

test_expect_success 'maintenance.loose-objects.auto' '
	git repack -adk &&
	GIT_TRACE2_EVENT="$(pwd)/trace-lo1.txt" \
		git -c maintenance.loose-objects.auto=1 maintenance \
		run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand ! git prune-packed --quiet <trace-lo1.txt &&
	printf data-A | git hash-object -t blob --stdin -w &&
	GIT_TRACE2_EVENT="$(pwd)/trace-loA" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand ! git prune-packed --quiet <trace-loA &&
	printf data-B | git hash-object -t blob --stdin -w &&
	GIT_TRACE2_EVENT="$(pwd)/trace-loB" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand git prune-packed --quiet <trace-loB &&
	GIT_TRACE2_EVENT="$(pwd)/trace-loC" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand git prune-packed --quiet <trace-loC
'

test_expect_success 'incremental-repack task' '
	packDir=.git/objects/pack &&
	for i in $(test_seq 1 5)
	do
		test_commit $i || return 1
	done &&

	# Create three disjoint pack-files with size BIG, small, small.
	echo HEAD~2 | git pack-objects --revs $packDir/test-1 &&
	test_tick &&
	git pack-objects --revs $packDir/test-2 <<-\EOF &&
	HEAD~1
	^HEAD~2
	EOF
	test_tick &&
	git pack-objects --revs $packDir/test-3 <<-\EOF &&
	HEAD
	^HEAD~1
	EOF

	# Delete refs that have not been repacked in these packs.
	git for-each-ref --format="delete %(refname)" \
		refs/prefetch refs/tags refs/remotes >refs &&
	git update-ref --stdin <refs &&

	# Replace the object directory with this pack layout.
	rm -f $packDir/pack-* &&
	rm -f $packDir/loose-* &&
	ls $packDir/*.pack >packs-before &&
	test_line_count = 3 packs-before &&

	# make sure we do not have any broken refs that were
	# missed in the deletion above
	git for-each-ref &&

	# the job repacks the two into a new pack, but does not
	# delete the old ones.
	git maintenance run --task=incremental-repack &&
	ls $packDir/*.pack >packs-between &&
	test_line_count = 4 packs-between &&

	# the job deletes the two old packs, and does not write
	# a new one because the batch size is not high enough to
	# pack the largest pack-file.
	git maintenance run --task=incremental-repack &&
	ls .git/objects/pack/*.pack >packs-after &&
	test_line_count = 2 packs-after
'

test_expect_success EXPENSIVE 'incremental-repack 2g limit' '
	test_config core.compression 0 &&

	for i in $(test_seq 1 5)
	do
		test-tool genrandom foo$i $((512 * 1024 * 1024 + 1)) >>big ||
		return 1
	done &&
	git add big &&
	git commit -qm "Add big file (1)" &&

	# ensure any possible loose objects are in a pack-file
	git maintenance run --task=loose-objects &&

	rm big &&
	for i in $(test_seq 6 10)
	do
		test-tool genrandom foo$i $((512 * 1024 * 1024 + 1)) >>big ||
		return 1
	done &&
	git add big &&
	git commit -qm "Add big file (2)" &&

	# ensure any possible loose objects are in a pack-file
	git maintenance run --task=loose-objects &&

	# Now run the incremental-repack task and check the batch-size
	GIT_TRACE2_EVENT="$(pwd)/run-2g.txt" git maintenance run \
		--task=incremental-repack 2>/dev/null &&
	test_subcommand git multi-pack-index repack \
		 --no-progress --batch-size=2147483647 <run-2g.txt
'

run_incremental_repack_and_verify () {
	test_commit A &&
	git repack -adk &&
	git multi-pack-index write &&
	GIT_TRACE2_EVENT="$(pwd)/midx-init.txt" git \
		-c maintenance.incremental-repack.auto=1 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand ! git multi-pack-index write --no-progress <midx-init.txt &&
	test_commit B &&
	git pack-objects --revs .git/objects/pack/pack <<-\EOF &&
	HEAD
	^HEAD~1
	EOF
	GIT_TRACE2_EVENT=$(pwd)/trace-A git \
		-c maintenance.incremental-repack.auto=2 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand ! git multi-pack-index write --no-progress <trace-A &&
	test_commit C &&
	git pack-objects --revs .git/objects/pack/pack <<-\EOF &&
	HEAD
	^HEAD~1
	EOF
	GIT_TRACE2_EVENT=$(pwd)/trace-B git \
		-c maintenance.incremental-repack.auto=2 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand git multi-pack-index write --no-progress <trace-B
}

test_expect_success 'maintenance.incremental-repack.auto' '
	rm -rf incremental-repack-true &&
	git init incremental-repack-true &&
	(
		cd incremental-repack-true &&
		git config core.multiPackIndex true &&
		run_incremental_repack_and_verify
	)
'

test_expect_success 'maintenance.incremental-repack.auto (when config is unset)' '
	rm -rf incremental-repack-unset &&
	git init incremental-repack-unset &&
	(
		cd incremental-repack-unset &&
		test_unconfig core.multiPackIndex &&
		run_incremental_repack_and_verify
	)
'

test_expect_success 'pack-refs task' '
	for n in $(test_seq 1 5)
	do
		git branch -f to-pack/$n HEAD || return 1
	done &&
	GIT_TRACE2_EVENT="$(pwd)/pack-refs.txt" \
		git maintenance run --task=pack-refs &&
	test_subcommand git pack-refs --all --prune <pack-refs.txt
'

test_expect_success '--auto and --schedule incompatible' '
	test_must_fail git maintenance run --auto --schedule=daily 2>err &&
	test_i18ngrep "at most one" err
'

test_expect_success 'invalid --schedule value' '
	test_must_fail git maintenance run --schedule=annually 2>err &&
	test_i18ngrep "unrecognized --schedule" err
'

test_expect_success '--schedule inheritance weekly -> daily -> hourly' '
	git config maintenance.loose-objects.enabled true &&
	git config maintenance.loose-objects.schedule hourly &&
	git config maintenance.commit-graph.enabled true &&
	git config maintenance.commit-graph.schedule daily &&
	git config maintenance.incremental-repack.enabled true &&
	git config maintenance.incremental-repack.schedule weekly &&

	GIT_TRACE2_EVENT="$(pwd)/hourly.txt" \
		git maintenance run --schedule=hourly 2>/dev/null &&
	test_subcommand git prune-packed --quiet <hourly.txt &&
	test_subcommand ! git commit-graph write --split --reachable \
		--no-progress <hourly.txt &&
	test_subcommand ! git multi-pack-index write --no-progress <hourly.txt &&

	GIT_TRACE2_EVENT="$(pwd)/daily.txt" \
		git maintenance run --schedule=daily 2>/dev/null &&
	test_subcommand git prune-packed --quiet <daily.txt &&
	test_subcommand git commit-graph write --split --reachable \
		--no-progress <daily.txt &&
	test_subcommand ! git multi-pack-index write --no-progress <daily.txt &&

	GIT_TRACE2_EVENT="$(pwd)/weekly.txt" \
		git maintenance run --schedule=weekly 2>/dev/null &&
	test_subcommand git prune-packed --quiet <weekly.txt &&
	test_subcommand git commit-graph write --split --reachable \
		--no-progress <weekly.txt &&
	test_subcommand git multi-pack-index write --no-progress <weekly.txt
'

test_expect_success 'maintenance.strategy inheritance' '
	for task in commit-graph loose-objects incremental-repack
	do
		git config --unset maintenance.$task.schedule || return 1
	done &&

	test_when_finished git config --unset maintenance.strategy &&
	git config maintenance.strategy incremental &&

	GIT_TRACE2_EVENT="$(pwd)/incremental-hourly.txt" \
		git maintenance run --schedule=hourly --quiet &&
	GIT_TRACE2_EVENT="$(pwd)/incremental-daily.txt" \
		git maintenance run --schedule=daily --quiet &&
	GIT_TRACE2_EVENT="$(pwd)/incremental-weekly.txt" \
		git maintenance run --schedule=weekly --quiet &&

	test_subcommand git commit-graph write --split --reachable \
		--no-progress <incremental-hourly.txt &&
	test_subcommand ! git prune-packed --quiet <incremental-hourly.txt &&
	test_subcommand ! git multi-pack-index write --no-progress \
		<incremental-hourly.txt &&
	test_subcommand ! git pack-refs --all --prune \
		<incremental-hourly.txt &&

	test_subcommand git commit-graph write --split --reachable \
		--no-progress <incremental-daily.txt &&
	test_subcommand git prune-packed --quiet <incremental-daily.txt &&
	test_subcommand git multi-pack-index write --no-progress \
		<incremental-daily.txt &&
	test_subcommand ! git pack-refs --all --prune \
		<incremental-daily.txt &&

	test_subcommand git commit-graph write --split --reachable \
		--no-progress <incremental-weekly.txt &&
	test_subcommand git prune-packed --quiet <incremental-weekly.txt &&
	test_subcommand git multi-pack-index write --no-progress \
		<incremental-weekly.txt &&
	test_subcommand git pack-refs --all --prune \
		<incremental-weekly.txt &&

	# Modify defaults
	git config maintenance.commit-graph.schedule daily &&
	git config maintenance.loose-objects.schedule hourly &&
	git config maintenance.incremental-repack.enabled false &&

	GIT_TRACE2_EVENT="$(pwd)/modified-hourly.txt" \
		git maintenance run --schedule=hourly --quiet &&
	GIT_TRACE2_EVENT="$(pwd)/modified-daily.txt" \
		git maintenance run --schedule=daily --quiet &&

	test_subcommand ! git commit-graph write --split --reachable \
		--no-progress <modified-hourly.txt &&
	test_subcommand git prune-packed --quiet <modified-hourly.txt &&
	test_subcommand ! git multi-pack-index write --no-progress \
		<modified-hourly.txt &&

	test_subcommand git commit-graph write --split --reachable \
		--no-progress <modified-daily.txt &&
	test_subcommand git prune-packed --quiet <modified-daily.txt &&
	test_subcommand ! git multi-pack-index write --no-progress \
		<modified-daily.txt
'

test_expect_success 'register and unregister' '
	test_when_finished git config --global --unset-all maintenance.repo &&

	test_must_fail git maintenance unregister 2>err &&
	grep "is not registered" err &&
	git maintenance unregister --force &&

	git config --global --add maintenance.repo /existing1 &&
	git config --global --add maintenance.repo /existing2 &&
	git config --global --get-all maintenance.repo >before &&

	git maintenance register &&
	test_cmp_config false maintenance.auto &&
	git config --global --get-all maintenance.repo >between &&
	cp before expect &&
	pwd >>expect &&
	test_cmp expect between &&

	git maintenance unregister &&
	git config --global --get-all maintenance.repo >actual &&
	test_cmp before actual &&

	git config --file ./other --add maintenance.repo /existing1 &&
	git config --file ./other --add maintenance.repo /existing2 &&
	git config --file ./other --get-all maintenance.repo >before &&

	git maintenance register --config-file ./other &&
	test_cmp_config false maintenance.auto &&
	git config --file ./other --get-all maintenance.repo >between &&
	cp before expect &&
	pwd >>expect &&
	test_cmp expect between &&

	git maintenance unregister --config-file ./other &&
	git config --file ./other --get-all maintenance.repo >actual &&
	test_cmp before actual &&

	test_must_fail git maintenance unregister 2>err &&
	grep "is not registered" err &&
	git maintenance unregister --force &&

	test_must_fail git maintenance unregister --config-file ./other 2>err &&
	grep "is not registered" err &&
	git maintenance unregister --config-file ./other --force
'

test_expect_success 'register with no value for maintenance.repo' '
	cp .git/config .git/config.orig &&
	test_when_finished mv .git/config.orig .git/config &&

	cat >>.git/config <<-\EOF &&
	[maintenance]
		repo
	EOF
	cat >expect <<-\EOF &&
	error: missing value for '\''maintenance.repo'\''
	EOF
	git maintenance register 2>actual &&
	test_cmp expect actual &&
	git config maintenance.repo
'

test_expect_success 'unregister with no value for maintenance.repo' '
	cp .git/config .git/config.orig &&
	test_when_finished mv .git/config.orig .git/config &&

	cat >>.git/config <<-\EOF &&
	[maintenance]
		repo
	EOF
	cat >expect <<-\EOF &&
	error: missing value for '\''maintenance.repo'\''
	EOF
	test_expect_code 128 git maintenance unregister 2>actual.raw &&
	grep ^error actual.raw >actual &&
	test_cmp expect actual &&
	git config maintenance.repo &&

	git maintenance unregister --force 2>actual.raw &&
	grep ^error actual.raw >actual &&
	test_cmp expect actual &&
	git config maintenance.repo
'

test_expect_success !MINGW 'register and unregister with regex metacharacters' '
	META="a+b*c" &&
	git init "$META" &&
	git -C "$META" maintenance register &&
	git config --get-all --show-origin maintenance.repo &&
	git config --get-all --global --fixed-value \
		maintenance.repo "$(pwd)/$META" &&
	git -C "$META" maintenance unregister &&
	test_must_fail git config --get-all --global --fixed-value \
		maintenance.repo "$(pwd)/$META"
'

test_expect_success 'start --scheduler=<scheduler>' '
	test_expect_code 129 git maintenance start --scheduler=foo 2>err &&
	test_i18ngrep "unrecognized --scheduler argument" err &&

	test_expect_code 129 git maintenance start --no-scheduler 2>err &&
	test_i18ngrep "unknown option" err &&

	test_expect_code 128 \
		env GIT_TEST_MAINT_SCHEDULER="launchctl:true,schtasks:true" \
		git maintenance start --scheduler=crontab 2>err &&
	test_i18ngrep "fatal: crontab scheduler is not available" err
'

test_expect_success 'start from empty cron table' '
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance start --scheduler=crontab &&

	# start registers the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=daily" cron.txt &&
	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=hourly" cron.txt &&
	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=weekly" cron.txt
'

test_expect_success 'stop from existing schedule' '
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance stop &&

	# stop does not unregister the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	# Operation is idempotent
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance stop &&
	test_must_be_empty cron.txt
'

test_expect_success 'start preserves existing schedule' '
	echo "Important information!" >cron.txt &&
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance start --scheduler=crontab &&
	grep "Important information!" cron.txt
'

test_expect_success 'magic markers are correct' '
	grep "GIT MAINTENANCE SCHEDULE" cron.txt >actual &&
	cat >expect <<-\EOF &&
	# BEGIN GIT MAINTENANCE SCHEDULE
	# END GIT MAINTENANCE SCHEDULE
	EOF
	test_cmp actual expect
'

test_expect_success 'stop preserves surrounding schedule' '
	echo "Crucial information!" >>cron.txt &&
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance stop &&
	grep "Important information!" cron.txt &&
	grep "Crucial information!" cron.txt
'

test_expect_success 'start and stop macOS maintenance' '
	# ensure $HOME can be compared against hook arguments on all platforms
	pfx=$(cd "$HOME" && pwd) &&

	write_script print-args <<-\EOF &&
	echo $* | sed "s:gui/[0-9][0-9]*:gui/[UID]:" >>args
	EOF

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER=launchctl:./print-args git maintenance start --scheduler=launchctl &&

	# start registers the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	ls "$HOME/Library/LaunchAgents" >actual &&
	cat >expect <<-\EOF &&
	org.git-scm.git.daily.plist
	org.git-scm.git.hourly.plist
	org.git-scm.git.weekly.plist
	EOF
	test_cmp expect actual &&

	rm -f expect &&
	for frequency in hourly daily weekly
	do
		PLIST="$pfx/Library/LaunchAgents/org.git-scm.git.$frequency.plist" &&
		test_xmllint "$PLIST" &&
		grep schedule=$frequency "$PLIST" &&
		echo "bootout gui/[UID] $PLIST" >>expect &&
		echo "bootstrap gui/[UID] $PLIST" >>expect || return 1
	done &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER=launchctl:./print-args git maintenance stop &&

	# stop does not unregister the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	printf "bootout gui/[UID] $pfx/Library/LaunchAgents/org.git-scm.git.%s.plist\n" \
		hourly daily weekly >expect &&
	test_cmp expect args &&
	ls "$HOME/Library/LaunchAgents" >actual &&
	test_line_count = 0 actual
'

test_expect_success 'use launchctl list to prevent extra work' '
	# ensure we are registered
	GIT_TEST_MAINT_SCHEDULER=launchctl:./print-args git maintenance start --scheduler=launchctl &&

	# do it again on a fresh args file
	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER=launchctl:./print-args git maintenance start --scheduler=launchctl &&

	ls "$HOME/Library/LaunchAgents" >actual &&
	cat >expect <<-\EOF &&
	list org.git-scm.git.hourly
	list org.git-scm.git.daily
	list org.git-scm.git.weekly
	EOF
	test_cmp expect args
'

test_expect_success 'start and stop Windows maintenance' '
	write_script print-args <<-\EOF &&
	echo $* >>args
	while test $# -gt 0
	do
		case "$1" in
		/xml) shift; xmlfile=$1; break ;;
		*) shift ;;
		esac
	done
	test -z "$xmlfile" || cp "$xmlfile" "$xmlfile.xml"
	EOF

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="schtasks:./print-args" git maintenance start --scheduler=schtasks &&

	# start registers the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	for frequency in hourly daily weekly
	do
		grep "/create /tn Git Maintenance ($frequency) /f /xml" args &&
		file=$(ls .git/schedule_${frequency}*.xml) &&
		test_xmllint "$file" || return 1
	done &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="schtasks:./print-args" git maintenance stop &&

	# stop does not unregister the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	printf "/delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >expect &&
	test_cmp expect args
'

test_expect_success 'start and stop Linux/systemd maintenance' '
	write_script print-args <<-\EOF &&
	printf "%s\n" "$*" >>args
	EOF

	XDG_CONFIG_HOME="$PWD" &&
	export XDG_CONFIG_HOME &&
	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args" git maintenance start --scheduler=systemd-timer &&

	# start registers the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	test_systemd_analyze_verify "systemd/user/git-maintenance@.service" &&

	printf -- "--user enable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args" git maintenance stop &&

	# stop does not unregister the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	test_path_is_missing "systemd/user/git-maintenance@.timer" &&
	test_path_is_missing "systemd/user/git-maintenance@.service" &&

	printf -- "--user disable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	test_cmp expect args
'

test_expect_success 'start and stop when several schedulers are available' '
	write_script print-args <<-\EOF &&
	printf "%s\n" "$*" | sed "s:gui/[0-9][0-9]*:gui/[UID]:; s:\(schtasks /create .* /xml\).*:\1:;" >>args
	EOF

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" git maintenance start --scheduler=systemd-timer &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.git-scm.git.%s.plist\n" \
		hourly daily weekly >expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	printf -- "systemctl --user enable --now git-maintenance@%s.timer\n" hourly daily weekly >>expect &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" git maintenance start --scheduler=launchctl &&
	printf -- "systemctl --user disable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	for frequency in hourly daily weekly
	do
		PLIST="$pfx/Library/LaunchAgents/org.git-scm.git.$frequency.plist" &&
		echo "launchctl bootout gui/[UID] $PLIST" >>expect &&
		echo "launchctl bootstrap gui/[UID] $PLIST" >>expect || return 1
	done &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" git maintenance start --scheduler=schtasks &&
	printf -- "systemctl --user disable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.git-scm.git.%s.plist\n" \
		hourly daily weekly >>expect &&
	printf "schtasks /create /tn Git Maintenance (%s) /f /xml\n" \
		hourly daily weekly >>expect &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" git maintenance stop &&
	printf -- "systemctl --user disable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.git-scm.git.%s.plist\n" \
		hourly daily weekly >>expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	test_cmp expect args
'

test_expect_success 'register preserves existing strategy' '
	git config maintenance.strategy none &&
	git maintenance register &&
	test_config maintenance.strategy none &&
	git config --unset maintenance.strategy &&
	git maintenance register &&
	test_config maintenance.strategy incremental
'

test_expect_success 'fails when running outside of a repository' '
	nongit test_must_fail git maintenance run &&
	nongit test_must_fail git maintenance stop &&
	nongit test_must_fail git maintenance start &&
	nongit test_must_fail git maintenance register &&
	nongit test_must_fail git maintenance unregister
'

test_expect_success 'register and unregister bare repo' '
	test_when_finished "git config --global --unset-all maintenance.repo || :" &&
	test_might_fail git config --global --unset-all maintenance.repo &&
	git init --bare barerepo &&
	(
		cd barerepo &&
		git maintenance register &&
		git config --get --global --fixed-value maintenance.repo "$(pwd)" &&
		git maintenance unregister &&
		test_must_fail git config --global --get-all maintenance.repo
	)
'

test_done
