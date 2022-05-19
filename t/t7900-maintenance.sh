#!/bin/sh

test_description='but maintenance builtin'

. ./test-lib.sh

BUT_TEST_CUMMIT_GRAPH=0
BUT_TEST_MULTI_PACK_INDEX=0

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
	test_expect_code 129 but maintenance -h 2>err &&
	test_i18ngrep "usage: but maintenance <subcommand>" err &&
	test_expect_code 128 but maintenance barf 2>err &&
	test_i18ngrep "invalid subcommand: barf" err &&
	test_expect_code 129 but maintenance 2>err &&
	test_i18ngrep "usage: but maintenance" err
'

test_expect_success 'run [--auto|--quiet]' '
	BUT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" \
		but maintenance run 2>/dev/null &&
	BUT_TRACE2_EVENT="$(pwd)/run-auto.txt" \
		but maintenance run --auto 2>/dev/null &&
	BUT_TRACE2_EVENT="$(pwd)/run-no-quiet.txt" \
		but maintenance run --no-quiet 2>/dev/null &&
	test_subcommand but gc --quiet <run-no-auto.txt &&
	test_subcommand ! but gc --auto --quiet <run-auto.txt &&
	test_subcommand but gc --no-quiet <run-no-quiet.txt
'

test_expect_success 'maintenance.auto config option' '
	BUT_TRACE2_EVENT="$(pwd)/default" but cummit --quiet --allow-empty -m 1 &&
	test_subcommand but maintenance run --auto --quiet <default &&
	BUT_TRACE2_EVENT="$(pwd)/true" \
		but -c maintenance.auto=true \
		cummit --quiet --allow-empty -m 2 &&
	test_subcommand but maintenance run --auto --quiet  <true &&
	BUT_TRACE2_EVENT="$(pwd)/false" \
		but -c maintenance.auto=false \
		cummit --quiet --allow-empty -m 3 &&
	test_subcommand ! but maintenance run --auto --quiet  <false
'

test_expect_success 'maintenance.<task>.enabled' '
	but config maintenance.gc.enabled false &&
	but config maintenance.cummit-graph.enabled true &&
	BUT_TRACE2_EVENT="$(pwd)/run-config.txt" but maintenance run 2>err &&
	test_subcommand ! but gc --quiet <run-config.txt &&
	test_subcommand but cummit-graph write --split --reachable --no-progress <run-config.txt
'

test_expect_success 'run --task=<task>' '
	BUT_TRACE2_EVENT="$(pwd)/run-cummit-graph.txt" \
		but maintenance run --task=cummit-graph 2>/dev/null &&
	BUT_TRACE2_EVENT="$(pwd)/run-gc.txt" \
		but maintenance run --task=gc 2>/dev/null &&
	BUT_TRACE2_EVENT="$(pwd)/run-cummit-graph.txt" \
		but maintenance run --task=cummit-graph 2>/dev/null &&
	BUT_TRACE2_EVENT="$(pwd)/run-both.txt" \
		but maintenance run --task=cummit-graph --task=gc 2>/dev/null &&
	test_subcommand ! but gc --quiet <run-cummit-graph.txt &&
	test_subcommand but gc --quiet <run-gc.txt &&
	test_subcommand but gc --quiet <run-both.txt &&
	test_subcommand but cummit-graph write --split --reachable --no-progress <run-cummit-graph.txt &&
	test_subcommand ! but cummit-graph write --split --reachable --no-progress <run-gc.txt &&
	test_subcommand but cummit-graph write --split --reachable --no-progress <run-both.txt
'

test_expect_success 'core.cummitGraph=false prevents write process' '
	BUT_TRACE2_EVENT="$(pwd)/no-cummit-graph.txt" \
		but -c core.cummitGraph=false maintenance run \
		--task=cummit-graph 2>/dev/null &&
	test_subcommand ! but cummit-graph write --split --reachable --no-progress \
		<no-cummit-graph.txt
'

test_expect_success 'cummit-graph auto condition' '
	COMMAND="maintenance run --task=cummit-graph --auto --quiet" &&

	BUT_TRACE2_EVENT="$(pwd)/cg-no.txt" \
		but -c maintenance.cummit-graph.auto=1 $COMMAND &&
	BUT_TRACE2_EVENT="$(pwd)/cg-negative-means-yes.txt" \
		but -c maintenance.cummit-graph.auto="-1" $COMMAND &&

	test_cummit first &&

	BUT_TRACE2_EVENT="$(pwd)/cg-zero-means-no.txt" \
		but -c maintenance.cummit-graph.auto=0 $COMMAND &&
	BUT_TRACE2_EVENT="$(pwd)/cg-one-satisfied.txt" \
		but -c maintenance.cummit-graph.auto=1 $COMMAND &&

	but cummit --allow-empty -m "second" &&
	but cummit --allow-empty -m "third" &&

	BUT_TRACE2_EVENT="$(pwd)/cg-two-satisfied.txt" \
		but -c maintenance.cummit-graph.auto=2 $COMMAND &&

	CUMMIT_GRAPH_WRITE="but cummit-graph write --split --reachable --no-progress" &&
	test_subcommand ! $CUMMIT_GRAPH_WRITE <cg-no.txt &&
	test_subcommand $CUMMIT_GRAPH_WRITE <cg-negative-means-yes.txt &&
	test_subcommand ! $CUMMIT_GRAPH_WRITE <cg-zero-means-no.txt &&
	test_subcommand $CUMMIT_GRAPH_WRITE <cg-one-satisfied.txt &&
	test_subcommand $CUMMIT_GRAPH_WRITE <cg-two-satisfied.txt
'

test_expect_success 'run --task=bogus' '
	test_must_fail but maintenance run --task=bogus 2>err &&
	test_i18ngrep "is not a valid task" err
'

test_expect_success 'run --task duplicate' '
	test_must_fail but maintenance run --task=gc --task=gc 2>err &&
	test_i18ngrep "cannot be selected multiple times" err
'

test_expect_success 'run --task=prefetch with no remotes' '
	but maintenance run --task=prefetch 2>err &&
	test_must_be_empty err
'

test_expect_success 'prefetch multiple remotes' '
	but clone . clone1 &&
	but clone . clone2 &&
	but remote add remote1 "file://$(pwd)/clone1" &&
	but remote add remote2 "file://$(pwd)/clone2" &&
	but -C clone1 switch -c one &&
	but -C clone2 switch -c two &&
	test_cummit -C clone1 one &&
	test_cummit -C clone2 two &&
	BUT_TRACE2_EVENT="$(pwd)/run-prefetch.txt" but maintenance run --task=prefetch 2>/dev/null &&
	fetchargs="--prefetch --prune --no-tags --no-write-fetch-head --recurse-submodules=no --quiet" &&
	test_subcommand but fetch remote1 $fetchargs <run-prefetch.txt &&
	test_subcommand but fetch remote2 $fetchargs <run-prefetch.txt &&
	test_path_is_missing .but/refs/remotes &&
	but log prefetch/remotes/remote1/one &&
	but log prefetch/remotes/remote2/two &&
	but fetch --all &&
	test_cmp_rev refs/remotes/remote1/one refs/prefetch/remotes/remote1/one &&
	test_cmp_rev refs/remotes/remote2/two refs/prefetch/remotes/remote2/two &&

	test_cmp_config refs/prefetch/ log.excludedecoration &&
	but log --oneline --decorate --all >log &&
	! grep "prefetch" log &&

	test_when_finished but config --unset remote.remote1.skipFetchAll &&
	but config remote.remote1.skipFetchAll true &&
	BUT_TRACE2_EVENT="$(pwd)/skip-remote1.txt" but maintenance run --task=prefetch 2>/dev/null &&
	test_subcommand ! but fetch remote1 $fetchargs <skip-remote1.txt &&
	test_subcommand but fetch remote2 $fetchargs <skip-remote1.txt
'

test_expect_success 'prefetch and existing log.excludeDecoration values' '
	but config --unset-all log.excludeDecoration &&
	but config log.excludeDecoration refs/remotes/remote1/ &&
	but maintenance run --task=prefetch &&

	but config --get-all log.excludeDecoration >out &&
	grep refs/remotes/remote1/ out &&
	grep refs/prefetch/ out &&

	but log --oneline --decorate --all >log &&
	! grep "prefetch" log &&
	! grep "remote1" log &&
	grep "remote2" log &&

	# a second run does not change the config
	but maintenance run --task=prefetch &&
	but log --oneline --decorate --all >log2 &&
	test_cmp log log2
'

test_expect_success 'loose-objects task' '
	# Repack everything so we know the state of the object dir
	but repack -adk &&

	# Hack to stop maintenance from running during "but cummit"
	echo in use >.but/objects/maintenance.lock &&

	# Assuming that "but cummit" creates at least one loose object
	test_cummit create-loose-object &&
	rm .but/objects/maintenance.lock &&

	ls .but/objects >obj-dir-before &&
	test_file_not_empty obj-dir-before &&
	ls .but/objects/pack/*.pack >packs-before &&
	test_line_count = 1 packs-before &&

	# The first run creates a pack-file
	# but does not delete loose objects.
	but maintenance run --task=loose-objects &&
	ls .but/objects >obj-dir-between &&
	test_cmp obj-dir-before obj-dir-between &&
	ls .but/objects/pack/*.pack >packs-between &&
	test_line_count = 2 packs-between &&
	ls .but/objects/pack/loose-*.pack >loose-packs &&
	test_line_count = 1 loose-packs &&

	# The second run deletes loose objects
	# but does not create a pack-file.
	but maintenance run --task=loose-objects &&
	ls .but/objects >obj-dir-after &&
	cat >expect <<-\EOF &&
	info
	pack
	EOF
	test_cmp expect obj-dir-after &&
	ls .but/objects/pack/*.pack >packs-after &&
	test_cmp packs-between packs-after
'

test_expect_success 'maintenance.loose-objects.auto' '
	but repack -adk &&
	BUT_TRACE2_EVENT="$(pwd)/trace-lo1.txt" \
		but -c maintenance.loose-objects.auto=1 maintenance \
		run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand ! but prune-packed --quiet <trace-lo1.txt &&
	printf data-A | but hash-object -t blob --stdin -w &&
	BUT_TRACE2_EVENT="$(pwd)/trace-loA" \
		but -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand ! but prune-packed --quiet <trace-loA &&
	printf data-B | but hash-object -t blob --stdin -w &&
	BUT_TRACE2_EVENT="$(pwd)/trace-loB" \
		but -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand but prune-packed --quiet <trace-loB &&
	BUT_TRACE2_EVENT="$(pwd)/trace-loC" \
		but -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand but prune-packed --quiet <trace-loC
'

test_expect_success 'incremental-repack task' '
	packDir=.but/objects/pack &&
	for i in $(test_seq 1 5)
	do
		test_cummit $i || return 1
	done &&

	# Create three disjoint pack-files with size BIG, small, small.
	echo HEAD~2 | but pack-objects --revs $packDir/test-1 &&
	test_tick &&
	but pack-objects --revs $packDir/test-2 <<-\EOF &&
	HEAD~1
	^HEAD~2
	EOF
	test_tick &&
	but pack-objects --revs $packDir/test-3 <<-\EOF &&
	HEAD
	^HEAD~1
	EOF

	# Delete refs that have not been repacked in these packs.
	but for-each-ref --format="delete %(refname)" \
		refs/prefetch refs/tags refs/remotes >refs &&
	but update-ref --stdin <refs &&

	# Replace the object directory with this pack layout.
	rm -f $packDir/pack-* &&
	rm -f $packDir/loose-* &&
	ls $packDir/*.pack >packs-before &&
	test_line_count = 3 packs-before &&

	# make sure we do not have any broken refs that were
	# missed in the deletion above
	but for-each-ref &&

	# the job repacks the two into a new pack, but does not
	# delete the old ones.
	but maintenance run --task=incremental-repack &&
	ls $packDir/*.pack >packs-between &&
	test_line_count = 4 packs-between &&

	# the job deletes the two old packs, and does not write
	# a new one because the batch size is not high enough to
	# pack the largest pack-file.
	but maintenance run --task=incremental-repack &&
	ls .but/objects/pack/*.pack >packs-after &&
	test_line_count = 2 packs-after
'

test_expect_success EXPENSIVE 'incremental-repack 2g limit' '
	test_config core.compression 0 &&

	for i in $(test_seq 1 5)
	do
		test-tool genrandom foo$i $((512 * 1024 * 1024 + 1)) >>big ||
		return 1
	done &&
	but add big &&
	but cummit -qm "Add big file (1)" &&

	# ensure any possible loose objects are in a pack-file
	but maintenance run --task=loose-objects &&

	rm big &&
	for i in $(test_seq 6 10)
	do
		test-tool genrandom foo$i $((512 * 1024 * 1024 + 1)) >>big ||
		return 1
	done &&
	but add big &&
	but cummit -qm "Add big file (2)" &&

	# ensure any possible loose objects are in a pack-file
	but maintenance run --task=loose-objects &&

	# Now run the incremental-repack task and check the batch-size
	BUT_TRACE2_EVENT="$(pwd)/run-2g.txt" but maintenance run \
		--task=incremental-repack 2>/dev/null &&
	test_subcommand but multi-pack-index repack \
		 --no-progress --batch-size=2147483647 <run-2g.txt
'

run_incremental_repack_and_verify () {
	test_cummit A &&
	but repack -adk &&
	but multi-pack-index write &&
	BUT_TRACE2_EVENT="$(pwd)/midx-init.txt" but \
		-c maintenance.incremental-repack.auto=1 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand ! but multi-pack-index write --no-progress <midx-init.txt &&
	test_cummit B &&
	but pack-objects --revs .but/objects/pack/pack <<-\EOF &&
	HEAD
	^HEAD~1
	EOF
	BUT_TRACE2_EVENT=$(pwd)/trace-A but \
		-c maintenance.incremental-repack.auto=2 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand ! but multi-pack-index write --no-progress <trace-A &&
	test_cummit C &&
	but pack-objects --revs .but/objects/pack/pack <<-\EOF &&
	HEAD
	^HEAD~1
	EOF
	BUT_TRACE2_EVENT=$(pwd)/trace-B but \
		-c maintenance.incremental-repack.auto=2 \
		maintenance run --auto --task=incremental-repack 2>/dev/null &&
	test_subcommand but multi-pack-index write --no-progress <trace-B
}

test_expect_success 'maintenance.incremental-repack.auto' '
	rm -rf incremental-repack-true &&
	but init incremental-repack-true &&
	(
		cd incremental-repack-true &&
		but config core.multiPackIndex true &&
		run_incremental_repack_and_verify
	)
'

test_expect_success 'maintenance.incremental-repack.auto (when config is unset)' '
	rm -rf incremental-repack-unset &&
	but init incremental-repack-unset &&
	(
		cd incremental-repack-unset &&
		test_unconfig core.multiPackIndex &&
		run_incremental_repack_and_verify
	)
'

test_expect_success 'pack-refs task' '
	for n in $(test_seq 1 5)
	do
		but branch -f to-pack/$n HEAD || return 1
	done &&
	BUT_TRACE2_EVENT="$(pwd)/pack-refs.txt" \
		but maintenance run --task=pack-refs &&
	test_subcommand but pack-refs --all --prune <pack-refs.txt
'

test_expect_success '--auto and --schedule incompatible' '
	test_must_fail but maintenance run --auto --schedule=daily 2>err &&
	test_i18ngrep "at most one" err
'

test_expect_success 'invalid --schedule value' '
	test_must_fail but maintenance run --schedule=annually 2>err &&
	test_i18ngrep "unrecognized --schedule" err
'

test_expect_success '--schedule inheritance weekly -> daily -> hourly' '
	but config maintenance.loose-objects.enabled true &&
	but config maintenance.loose-objects.schedule hourly &&
	but config maintenance.cummit-graph.enabled true &&
	but config maintenance.cummit-graph.schedule daily &&
	but config maintenance.incremental-repack.enabled true &&
	but config maintenance.incremental-repack.schedule weekly &&

	BUT_TRACE2_EVENT="$(pwd)/hourly.txt" \
		but maintenance run --schedule=hourly 2>/dev/null &&
	test_subcommand but prune-packed --quiet <hourly.txt &&
	test_subcommand ! but cummit-graph write --split --reachable \
		--no-progress <hourly.txt &&
	test_subcommand ! but multi-pack-index write --no-progress <hourly.txt &&

	BUT_TRACE2_EVENT="$(pwd)/daily.txt" \
		but maintenance run --schedule=daily 2>/dev/null &&
	test_subcommand but prune-packed --quiet <daily.txt &&
	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <daily.txt &&
	test_subcommand ! but multi-pack-index write --no-progress <daily.txt &&

	BUT_TRACE2_EVENT="$(pwd)/weekly.txt" \
		but maintenance run --schedule=weekly 2>/dev/null &&
	test_subcommand but prune-packed --quiet <weekly.txt &&
	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <weekly.txt &&
	test_subcommand but multi-pack-index write --no-progress <weekly.txt
'

test_expect_success 'maintenance.strategy inheritance' '
	for task in cummit-graph loose-objects incremental-repack
	do
		but config --unset maintenance.$task.schedule || return 1
	done &&

	test_when_finished but config --unset maintenance.strategy &&
	but config maintenance.strategy incremental &&

	BUT_TRACE2_EVENT="$(pwd)/incremental-hourly.txt" \
		but maintenance run --schedule=hourly --quiet &&
	BUT_TRACE2_EVENT="$(pwd)/incremental-daily.txt" \
		but maintenance run --schedule=daily --quiet &&
	BUT_TRACE2_EVENT="$(pwd)/incremental-weekly.txt" \
		but maintenance run --schedule=weekly --quiet &&

	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <incremental-hourly.txt &&
	test_subcommand ! but prune-packed --quiet <incremental-hourly.txt &&
	test_subcommand ! but multi-pack-index write --no-progress \
		<incremental-hourly.txt &&
	test_subcommand ! but pack-refs --all --prune \
		<incremental-hourly.txt &&

	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <incremental-daily.txt &&
	test_subcommand but prune-packed --quiet <incremental-daily.txt &&
	test_subcommand but multi-pack-index write --no-progress \
		<incremental-daily.txt &&
	test_subcommand ! but pack-refs --all --prune \
		<incremental-daily.txt &&

	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <incremental-weekly.txt &&
	test_subcommand but prune-packed --quiet <incremental-weekly.txt &&
	test_subcommand but multi-pack-index write --no-progress \
		<incremental-weekly.txt &&
	test_subcommand but pack-refs --all --prune \
		<incremental-weekly.txt &&

	# Modify defaults
	but config maintenance.cummit-graph.schedule daily &&
	but config maintenance.loose-objects.schedule hourly &&
	but config maintenance.incremental-repack.enabled false &&

	BUT_TRACE2_EVENT="$(pwd)/modified-hourly.txt" \
		but maintenance run --schedule=hourly --quiet &&
	BUT_TRACE2_EVENT="$(pwd)/modified-daily.txt" \
		but maintenance run --schedule=daily --quiet &&

	test_subcommand ! but cummit-graph write --split --reachable \
		--no-progress <modified-hourly.txt &&
	test_subcommand but prune-packed --quiet <modified-hourly.txt &&
	test_subcommand ! but multi-pack-index write --no-progress \
		<modified-hourly.txt &&

	test_subcommand but cummit-graph write --split --reachable \
		--no-progress <modified-daily.txt &&
	test_subcommand but prune-packed --quiet <modified-daily.txt &&
	test_subcommand ! but multi-pack-index write --no-progress \
		<modified-daily.txt
'

test_expect_success 'register and unregister' '
	test_when_finished but config --global --unset-all maintenance.repo &&
	but config --global --add maintenance.repo /existing1 &&
	but config --global --add maintenance.repo /existing2 &&
	but config --global --get-all maintenance.repo >before &&

	but maintenance register &&
	test_cmp_config false maintenance.auto &&
	but config --global --get-all maintenance.repo >between &&
	cp before expect &&
	pwd >>expect &&
	test_cmp expect between &&

	but maintenance unregister &&
	but config --global --get-all maintenance.repo >actual &&
	test_cmp before actual
'

test_expect_success !MINGW 'register and unregister with regex metacharacters' '
	META="a+b*c" &&
	but init "$META" &&
	but -C "$META" maintenance register &&
	but config --get-all --show-origin maintenance.repo &&
	but config --get-all --global --fixed-value \
		maintenance.repo "$(pwd)/$META" &&
	but -C "$META" maintenance unregister &&
	test_must_fail but config --get-all --global --fixed-value \
		maintenance.repo "$(pwd)/$META"
'

test_expect_success 'start --scheduler=<scheduler>' '
	test_expect_code 129 but maintenance start --scheduler=foo 2>err &&
	test_i18ngrep "unrecognized --scheduler argument" err &&

	test_expect_code 129 but maintenance start --no-scheduler 2>err &&
	test_i18ngrep "unknown option" err &&

	test_expect_code 128 \
		env BUT_TEST_MAINT_SCHEDULER="launchctl:true,schtasks:true" \
		but maintenance start --scheduler=crontab 2>err &&
	test_i18ngrep "fatal: crontab scheduler is not available" err
'

test_expect_success 'start from empty cron table' '
	BUT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" but maintenance start --scheduler=crontab &&

	# start registers the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=daily" cron.txt &&
	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=hourly" cron.txt &&
	grep "for-each-repo --config=maintenance.repo maintenance run --schedule=weekly" cron.txt
'

test_expect_success 'stop from existing schedule' '
	BUT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" but maintenance stop &&

	# stop does not unregister the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	# Operation is idempotent
	BUT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" but maintenance stop &&
	test_must_be_empty cron.txt
'

test_expect_success 'start preserves existing schedule' '
	echo "Important information!" >cron.txt &&
	BUT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" but maintenance start --scheduler=crontab &&
	grep "Important information!" cron.txt
'

test_expect_success 'magic markers are correct' '
	grep "BUT MAINTENANCE SCHEDULE" cron.txt >actual &&
	cat >expect <<-\EOF &&
	# BEGIN BUT MAINTENANCE SCHEDULE
	# END BUT MAINTENANCE SCHEDULE
	EOF
	test_cmp actual expect
'

test_expect_success 'stop preserves surrounding schedule' '
	echo "Crucial information!" >>cron.txt &&
	BUT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" but maintenance stop &&
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
	BUT_TEST_MAINT_SCHEDULER=launchctl:./print-args but maintenance start --scheduler=launchctl &&

	# start registers the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	ls "$HOME/Library/LaunchAgents" >actual &&
	cat >expect <<-\EOF &&
	org.but-scm.but.daily.plist
	org.but-scm.but.hourly.plist
	org.but-scm.but.weekly.plist
	EOF
	test_cmp expect actual &&

	rm -f expect &&
	for frequency in hourly daily weekly
	do
		PLIST="$pfx/Library/LaunchAgents/org.but-scm.but.$frequency.plist" &&
		test_xmllint "$PLIST" &&
		grep schedule=$frequency "$PLIST" &&
		echo "bootout gui/[UID] $PLIST" >>expect &&
		echo "bootstrap gui/[UID] $PLIST" >>expect || return 1
	done &&
	test_cmp expect args &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER=launchctl:./print-args but maintenance stop &&

	# stop does not unregister the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	printf "bootout gui/[UID] $pfx/Library/LaunchAgents/org.but-scm.but.%s.plist\n" \
		hourly daily weekly >expect &&
	test_cmp expect args &&
	ls "$HOME/Library/LaunchAgents" >actual &&
	test_line_count = 0 actual
'

test_expect_success 'use launchctl list to prevent extra work' '
	# ensure we are registered
	BUT_TEST_MAINT_SCHEDULER=launchctl:./print-args but maintenance start --scheduler=launchctl &&

	# do it again on a fresh args file
	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER=launchctl:./print-args but maintenance start --scheduler=launchctl &&

	ls "$HOME/Library/LaunchAgents" >actual &&
	cat >expect <<-\EOF &&
	list org.but-scm.but.hourly
	list org.but-scm.but.daily
	list org.but-scm.but.weekly
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
	BUT_TEST_MAINT_SCHEDULER="schtasks:./print-args" but maintenance start --scheduler=schtasks &&

	# start registers the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	for frequency in hourly daily weekly
	do
		grep "/create /tn Git Maintenance ($frequency) /f /xml" args &&
		file=$(ls .but/schedule_${frequency}*.xml) &&
		test_xmllint "$file" || return 1
	done &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="schtasks:./print-args" but maintenance stop &&

	# stop does not unregister the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

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
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args" but maintenance start --scheduler=systemd-timer &&

	# start registers the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	test_systemd_analyze_verify "systemd/user/but-maintenance@.service" &&

	printf -- "--user enable --now but-maintenance@%s.timer\n" hourly daily weekly >expect &&
	test_cmp expect args &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args" but maintenance stop &&

	# stop does not unregister the repo
	but config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	test_path_is_missing "systemd/user/but-maintenance@.timer" &&
	test_path_is_missing "systemd/user/but-maintenance@.service" &&

	printf -- "--user disable --now but-maintenance@%s.timer\n" hourly daily weekly >expect &&
	test_cmp expect args
'

test_expect_success 'start and stop when several schedulers are available' '
	write_script print-args <<-\EOF &&
	printf "%s\n" "$*" | sed "s:gui/[0-9][0-9]*:gui/[UID]:; s:\(schtasks /create .* /xml\).*:\1:;" >>args
	EOF

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" but maintenance start --scheduler=systemd-timer &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.but-scm.but.%s.plist\n" \
		hourly daily weekly >expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	printf -- "systemctl --user enable --now but-maintenance@%s.timer\n" hourly daily weekly >>expect &&
	test_cmp expect args &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" but maintenance start --scheduler=launchctl &&
	printf -- "systemctl --user disable --now but-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	for frequency in hourly daily weekly
	do
		PLIST="$pfx/Library/LaunchAgents/org.but-scm.but.$frequency.plist" &&
		echo "launchctl bootout gui/[UID] $PLIST" >>expect &&
		echo "launchctl bootstrap gui/[UID] $PLIST" >>expect || return 1
	done &&
	test_cmp expect args &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" but maintenance start --scheduler=schtasks &&
	printf -- "systemctl --user disable --now but-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.but-scm.but.%s.plist\n" \
		hourly daily weekly >>expect &&
	printf "schtasks /create /tn Git Maintenance (%s) /f /xml\n" \
		hourly daily weekly >>expect &&
	test_cmp expect args &&

	rm -f args &&
	BUT_TEST_MAINT_SCHEDULER="systemctl:./print-args systemctl,launchctl:./print-args launchctl,schtasks:./print-args schtasks" but maintenance stop &&
	printf -- "systemctl --user disable --now but-maintenance@%s.timer\n" hourly daily weekly >expect &&
	printf "launchctl bootout gui/[UID] $pfx/Library/LaunchAgents/org.but-scm.but.%s.plist\n" \
		hourly daily weekly >>expect &&
	printf "schtasks /delete /tn Git Maintenance (%s) /f\n" \
		hourly daily weekly >>expect &&
	test_cmp expect args
'

test_expect_success 'register preserves existing strategy' '
	but config maintenance.strategy none &&
	but maintenance register &&
	test_config maintenance.strategy none &&
	but config --unset maintenance.strategy &&
	but maintenance register &&
	test_config maintenance.strategy incremental
'

test_expect_success 'fails when running outside of a repository' '
	nonbut test_must_fail but maintenance run &&
	nonbut test_must_fail but maintenance stop &&
	nonbut test_must_fail but maintenance start &&
	nonbut test_must_fail but maintenance register &&
	nonbut test_must_fail but maintenance unregister
'

test_expect_success 'register and unregister bare repo' '
	test_when_finished "but config --global --unset-all maintenance.repo || :" &&
	test_might_fail but config --global --unset-all maintenance.repo &&
	but init --bare barerepo &&
	(
		cd barerepo &&
		but maintenance register &&
		but config --get --global --fixed-value maintenance.repo "$(pwd)" &&
		but maintenance unregister &&
		test_must_fail but config --global --get-all maintenance.repo
	)
'

test_done
