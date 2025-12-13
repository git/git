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
	test_grep "usage: git maintenance <subcommand>" actual &&
	test_expect_code 129 git maintenance barf 2>err &&
	test_grep "unknown subcommand: \`barf'\''" err &&
	test_grep "usage: git maintenance" err &&
	test_expect_code 129 git maintenance 2>err &&
	test_grep "error: need a subcommand" err &&
	test_grep "usage: git maintenance" err
'

test_expect_success 'run [--auto|--quiet]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" \
		git maintenance run 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" \
		git maintenance run --auto 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-no-quiet.txt" \
		git maintenance run --no-quiet 2>/dev/null &&
	git maintenance is-needed &&
	test_subcommand git gc --quiet --no-detach --skip-foreground-tasks <run-no-auto.txt &&
	! git maintenance is-needed --auto &&
	test_subcommand ! git gc --auto --quiet --no-detach --skip-foreground-tasks <run-auto.txt &&
	test_subcommand git gc --no-quiet --no-detach --skip-foreground-tasks <run-no-quiet.txt
'

test_expect_success 'maintenance.auto config option' '
	GIT_TRACE2_EVENT="$(pwd)/default" git commit --quiet --allow-empty -m 1 &&
	test_subcommand git maintenance run --auto --quiet --detach <default &&
	GIT_TRACE2_EVENT="$(pwd)/true" \
		git -c maintenance.auto=true \
		commit --quiet --allow-empty -m 2 &&
	test_subcommand git maintenance run --auto --quiet --detach <true &&
	GIT_TRACE2_EVENT="$(pwd)/false" \
		git -c maintenance.auto=false \
		commit --quiet --allow-empty -m 3 &&
	test_subcommand ! git maintenance run --auto --quiet --detach <false
'

for cfg in maintenance.autoDetach gc.autoDetach
do
	test_expect_success "$cfg=true config option" '
		test_when_finished "rm -f trace" &&
		test_config $cfg true &&
		GIT_TRACE2_EVENT="$(pwd)/trace" git commit --quiet --allow-empty -m 1 &&
		test_subcommand git maintenance run --auto --quiet --detach <trace
	'

	test_expect_success "$cfg=false config option" '
		test_when_finished "rm -f trace" &&
		test_config $cfg false &&
		GIT_TRACE2_EVENT="$(pwd)/trace" git commit --quiet --allow-empty -m 1 &&
		test_subcommand git maintenance run --auto --quiet --no-detach <trace
	'
done

test_expect_success "maintenance.autoDetach overrides gc.autoDetach" '
	test_when_finished "rm -f trace" &&
	test_config maintenance.autoDetach false &&
	test_config gc.autoDetach true &&
	GIT_TRACE2_EVENT="$(pwd)/trace" git commit --quiet --allow-empty -m 1 &&
	test_subcommand git maintenance run --auto --quiet --no-detach <trace
'

test_expect_success 'register uses XDG_CONFIG_HOME config if it exists' '
	test_when_finished rm -r .config/git/config &&
	(
		XDG_CONFIG_HOME=.config &&
		export XDG_CONFIG_HOME &&
		mkdir -p $XDG_CONFIG_HOME/git &&
		>$XDG_CONFIG_HOME/git/config &&
		git maintenance register &&
		git config --file=$XDG_CONFIG_HOME/git/config --get maintenance.repo >actual &&
		pwd >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'register does not need XDG_CONFIG_HOME config to exist' '
	test_when_finished git maintenance unregister &&
	test_path_is_missing $XDG_CONFIG_HOME/git/config &&
	git maintenance register &&
	git config --global --get maintenance.repo >actual &&
	pwd >expect &&
	test_cmp expect actual
'

test_expect_success 'unregister uses XDG_CONFIG_HOME config if it exists' '
	test_when_finished rm -r .config/git/config &&
	(
		XDG_CONFIG_HOME=.config &&
		export XDG_CONFIG_HOME &&
		mkdir -p $XDG_CONFIG_HOME/git &&
		>$XDG_CONFIG_HOME/git/config &&
		git maintenance register &&
		git maintenance unregister &&
		test_must_fail git config --file=$XDG_CONFIG_HOME/git/config --get maintenance.repo >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'unregister does not need XDG_CONFIG_HOME config to exist' '
	test_path_is_missing $XDG_CONFIG_HOME/git/config &&
	git maintenance register &&
	git maintenance unregister &&
	test_must_fail git config --global --get maintenance.repo >actual &&
	test_must_be_empty actual
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
	test_subcommand ! git gc --quiet --no-detach --skip-foreground-tasks <run-commit-graph.txt &&
	test_subcommand git gc --quiet --no-detach --skip-foreground-tasks <run-gc.txt &&
	test_subcommand git gc --quiet --no-detach --skip-foreground-tasks <run-both.txt &&
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

	! git -c maintenance.commit-graph.auto=0 \
		maintenance is-needed --auto --task=commit-graph &&
	git -c maintenance.commit-graph.auto=1 \
		maintenance is-needed --auto --task=commit-graph &&

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

test_expect_success 'commit-graph auto condition with merges' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git config set maintenance.auto false &&
		git commit --allow-empty -m initial &&
		git switch --create feature &&
		git commit --allow-empty -m feature-1 &&
		git commit --allow-empty -m feature-2 &&
		git switch - &&
		git commit --allow-empty -m main-1 &&
		git commit --allow-empty -m main-2 &&
		git merge feature &&

		# We have 6 commit, none of which are covered by a commit
		# graph. So this must be the boundary at which we start to
		# perform maintenance.
		test_must_fail git -c maintenance.commit-graph.auto=7 \
			maintenance is-needed --auto --task=commit-graph &&
		git -c maintenance.commit-graph.auto=6 \
			maintenance is-needed --auto --task=commit-graph
	)
'

test_expect_success 'run --task=bogus' '
	test_must_fail git maintenance run --task=bogus 2>err &&
	test_grep "is not a valid task" err
'

test_expect_success 'run --task duplicate' '
	test_must_fail git maintenance run --task=gc --task=gc 2>err &&
	test_grep "cannot be selected multiple times" err
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
	git for-each-ref refs/remotes >actual &&
	test_must_be_empty actual &&
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
	! git -c maintenance.loose-objects.auto=2 \
		maintenance is-needed --auto --task=loose-objects &&
	GIT_TRACE2_EVENT="$(pwd)/trace-loA" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand ! git prune-packed --quiet <trace-loA &&

	printf data-B | git hash-object -t blob --stdin -w &&
	git -c maintenance.loose-objects.auto=2 \
		maintenance is-needed --auto --task=loose-objects &&
	GIT_TRACE2_EVENT="$(pwd)/trace-loB" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand git prune-packed --quiet <trace-loB &&

	GIT_TRACE2_EVENT="$(pwd)/trace-loC" \
		git -c maintenance.loose-objects.auto=2 \
		maintenance run --auto --task=loose-objects 2>/dev/null &&
	test_subcommand git prune-packed --quiet <trace-loC
'

test_expect_success 'maintenance.loose-objects.batchSize' '
	git init loose-batch &&

	# This creates three objects per commit.
	test_commit_bulk -C loose-batch 34 &&
	pack=$(ls loose-batch/.git/objects/pack/pack-*.pack) &&
	index="${pack%pack}idx" &&
	rm "$index" &&
	git -C loose-batch unpack-objects <"$pack" &&
	git -C loose-batch config maintenance.loose-objects.batchSize 50 &&

	GIT_PROGRESS_DELAY=0 \
	git -C loose-batch maintenance run --no-quiet --task=loose-objects 2>err &&
	grep "Enumerating objects: 50, done." err &&

	GIT_PROGRESS_DELAY=0 \
	git -C loose-batch maintenance run --no-quiet --task=loose-objects 2>err &&
	grep "Enumerating objects: 50, done." err &&

	GIT_PROGRESS_DELAY=0 \
	git -C loose-batch maintenance run --no-quiet --task=loose-objects 2>err &&
	grep "Enumerating objects: 2, done." err &&

	GIT_PROGRESS_DELAY=0 \
	git -C loose-batch maintenance run --no-quiet --task=loose-objects 2>err &&
	test_must_be_empty err
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
		refs/prefetch refs/tags refs/remotes \
		--exclude=refs/remotes/*/HEAD >refs &&
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
	! git -c maintenance.incremental-repack.auto=1 \
		maintenance is-needed --auto --task=incremental-repack &&
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
	git -c maintenance.incremental-repack.auto=2 \
		maintenance is-needed --auto --task=incremental-repack &&
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

run_and_verify_geometric_pack () {
	EXPECTED_PACKS="$1" &&

	# Verify that we perform a geometric repack.
	rm -f "trace2.txt" &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		git maintenance run --task=geometric-repack 2>/dev/null &&
	test_subcommand git repack -d -l --geometric=2 \
		--quiet --write-midx <trace2.txt &&

	# Verify that the number of packfiles matches our expectation.
	ls -l .git/objects/pack/*.pack >packfiles &&
	test_line_count = "$EXPECTED_PACKS" packfiles &&

	# And verify that there are no loose objects anymore.
	git count-objects -v >count &&
	test_grep '^count: 0$' count
}

test_expect_success 'geometric repacking task' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git config set maintenance.auto false &&
		test_commit initial &&

		# The initial repack causes an all-into-one repack.
		GIT_TRACE2_EVENT="$(pwd)/initial-repack.txt" \
			git maintenance run --task=geometric-repack 2>/dev/null &&
		test_subcommand git repack -d -l --cruft --cruft-expiration=2.weeks.ago \
			--quiet --write-midx <initial-repack.txt &&

		# Repacking should now cause a no-op geometric repack because
		# no packfiles need to be combined.
		ls -l .git/objects/pack/*.pack >before &&
		run_and_verify_geometric_pack 1 &&
		ls -l .git/objects/pack/*.pack >after &&
		test_cmp before after &&

		# This incremental change creates a new packfile that only
		# soaks up loose objects. The packfiles are not getting merged
		# at this point.
		test_commit loose &&
		run_and_verify_geometric_pack 2 &&

		# Both packfiles have 3 objects, so the next run would cause us
		# to merge all packfiles together. This should be turned into
		# an all-into-one-repack.
		GIT_TRACE2_EVENT="$(pwd)/all-into-one-repack.txt" \
			git maintenance run --task=geometric-repack 2>/dev/null &&
		test_subcommand git repack -d -l --cruft --cruft-expiration=2.weeks.ago \
			--quiet --write-midx <all-into-one-repack.txt &&

		# The geometric repack soaks up unreachable objects.
		echo blob-1 | git hash-object -w --stdin -t blob &&
		run_and_verify_geometric_pack 2 &&

		# A second unreachable object should be written into another packfile.
		echo blob-2 | git hash-object -w --stdin -t blob &&
		run_and_verify_geometric_pack 3 &&

		# And these two small packs should now be merged via the
		# geometric repack. The large packfile should remain intact.
		run_and_verify_geometric_pack 2 &&

		# If we now add two more objects and repack twice we should
		# then see another all-into-one repack. This time around
		# though, as we have unreachable objects, we should also see a
		# cruft pack.
		echo blob-3 | git hash-object -w --stdin -t blob &&
		echo blob-4 | git hash-object -w --stdin -t blob &&
		run_and_verify_geometric_pack 3 &&
		GIT_TRACE2_EVENT="$(pwd)/cruft-repack.txt" \
			git maintenance run --task=geometric-repack 2>/dev/null &&
		test_subcommand git repack -d -l --cruft --cruft-expiration=2.weeks.ago \
			--quiet --write-midx <cruft-repack.txt &&
		ls .git/objects/pack/*.pack >packs &&
		test_line_count = 2 packs &&
		ls .git/objects/pack/*.mtimes >cruft &&
		test_line_count = 1 cruft
	)
'

test_geometric_repack_needed () {
	NEEDED="$1"
	GEOMETRIC_CONFIG="$2" &&
	rm -f trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		git ${GEOMETRIC_CONFIG:+-c maintenance.geometric-repack.$GEOMETRIC_CONFIG} \
		maintenance run --auto --task=geometric-repack 2>/dev/null &&
	case "$NEEDED" in
	true)
		test_grep "\[\"git\",\"repack\"," trace2.txt;;
	false)
		! test_grep "\[\"git\",\"repack\"," trace2.txt;;
	*)
		BUG "invalid parameter: $NEEDED";;
	esac
}

test_expect_success 'geometric repacking with --auto' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# An empty repository does not need repacking, except when
		# explicitly told to do it.
		test_geometric_repack_needed false &&
		test_geometric_repack_needed false auto=0 &&
		test_geometric_repack_needed false auto=1 &&
		test_geometric_repack_needed true auto=-1 &&

		test_oid_init &&

		# Loose objects cause a repack when crossing the limit. Note
		# that the number of objects gets extrapolated by having a look
		# at the "objects/17/" shard.
		test_commit "$(test_oid blob17_1)" &&
		test_geometric_repack_needed false &&
		test_commit "$(test_oid blob17_2)" &&
		test_geometric_repack_needed false auto=257 &&
		test_geometric_repack_needed true auto=256 &&

		# Force another repack.
		test_commit first &&
		test_commit second &&
		test_geometric_repack_needed true auto=-1 &&

		# We now have two packfiles that would be merged together. As
		# such, the repack should always happen unless the user has
		# disabled the auto task.
		test_geometric_repack_needed false auto=0 &&
		test_geometric_repack_needed true auto=9000
	)
'

test_expect_success 'geometric repacking honors configured split factor' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git config set maintenance.auto false &&

		# Create three different packs with 9, 2 and 1 object, respectively.
		# This is done so that only a subset of packs would be merged
		# together so that we can verify that `git repack` receives the
		# correct geometric factor.
		for i in $(test_seq 9)
		do
			echo first-$i | git hash-object -w --stdin -t blob || return 1
		done &&
		git repack --geometric=2 -d &&

		for i in $(test_seq 2)
		do
			echo second-$i | git hash-object -w --stdin -t blob || return 1
		done &&
		git repack --geometric=2 -d &&

		echo third | git hash-object -w --stdin -t blob &&
		git repack --geometric=2 -d &&

		test_geometric_repack_needed false splitFactor=2 &&
		test_geometric_repack_needed true splitFactor=3 &&
		test_subcommand git repack -d -l --geometric=3 --quiet --write-midx <trace2.txt
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

test_expect_success 'reflog-expire task' '
	GIT_TRACE2_EVENT="$(pwd)/reflog-expire.txt" \
		git maintenance run --task=reflog-expire &&
	test_subcommand git reflog expire --all <reflog-expire.txt
'

test_expect_success 'reflog-expire task --auto only packs when exceeding limits' '
	git reflog expire --all --expire=now &&
	test_commit reflog-one &&
	test_commit reflog-two &&

	! git -c maintenance.reflog-expire.auto=3 \
		maintenance is-needed --auto --task=reflog-expire &&
	GIT_TRACE2_EVENT="$(pwd)/reflog-expire-auto.txt" \
		git -c maintenance.reflog-expire.auto=3 maintenance run --auto --task=reflog-expire &&
	test_subcommand ! git reflog expire --all <reflog-expire-auto.txt &&

	git -c maintenance.reflog-expire.auto=2 \
		maintenance is-needed --auto --task=reflog-expire &&
	GIT_TRACE2_EVENT="$(pwd)/reflog-expire-auto.txt" \
		git -c maintenance.reflog-expire.auto=2 maintenance run --auto --task=reflog-expire &&
	test_subcommand git reflog expire --all <reflog-expire-auto.txt
'

test_expect_worktree_prune () {
	negate=
	if test "$1" = "!"
	then
		negate="!"
		shift
	fi

	rm -f "worktree-prune.txt" &&
	GIT_TRACE2_EVENT="$(pwd)/worktree-prune.txt" "$@" &&
	test_subcommand $negate git worktree prune --expire 3.months.ago <worktree-prune.txt
}

test_expect_success 'worktree-prune task without --auto always prunes' '
	test_expect_worktree_prune git maintenance run --task=worktree-prune
'

test_expect_success 'worktree-prune task --auto only prunes with prunable worktree' '
	test_expect_worktree_prune ! git maintenance run --auto --task=worktree-prune &&
	mkdir .git/worktrees &&
	: >.git/worktrees/abc &&
	git maintenance is-needed --auto --task=worktree-prune &&
	test_expect_worktree_prune git maintenance run --auto --task=worktree-prune
'

test_expect_success 'worktree-prune task with --auto honors maintenance.worktree-prune.auto' '
	# A negative value should always prune.
	test_expect_worktree_prune git -c maintenance.worktree-prune.auto=-1 maintenance run --auto --task=worktree-prune &&

	mkdir .git/worktrees &&
	: >.git/worktrees/first &&
	: >.git/worktrees/second &&
	: >.git/worktrees/third &&

	# Zero should never prune.
	test_expect_worktree_prune ! git -c maintenance.worktree-prune.auto=0 maintenance run --auto --task=worktree-prune &&
	# A positive value should require at least this many prunable worktrees.
	test_expect_worktree_prune ! git -c maintenance.worktree-prune.auto=4 maintenance run --auto --task=worktree-prune &&
	git -c maintenance.worktree-prune.auto=3 maintenance is-needed --auto --task=worktree-prune &&
	test_expect_worktree_prune git -c maintenance.worktree-prune.auto=3 maintenance run --auto --task=worktree-prune
'

test_expect_success 'worktree-prune task honors gc.worktreePruneExpire' '
	git worktree add worktree &&
	rm -rf worktree &&

	rm -f worktree-prune.txt &&
	! git -c gc.worktreePruneExpire=1.week.ago maintenance is-needed --auto --task=worktree-prune &&
	GIT_TRACE2_EVENT="$(pwd)/worktree-prune.txt" git -c gc.worktreePruneExpire=1.week.ago maintenance run --auto --task=worktree-prune &&
	test_subcommand ! git worktree prune --expire 1.week.ago <worktree-prune.txt &&
	test_path_is_dir .git/worktrees/worktree &&

	rm -f worktree-prune.txt &&
	git -c gc.worktreePruneExpire=now maintenance is-needed --auto --task=worktree-prune &&
	GIT_TRACE2_EVENT="$(pwd)/worktree-prune.txt" git -c gc.worktreePruneExpire=now maintenance run --auto --task=worktree-prune &&
	test_subcommand git worktree prune --expire now <worktree-prune.txt &&
	test_path_is_missing .git/worktrees/worktree
'

test_expect_rerere_gc () {
	negate=
	if test "$1" = "!"
	then
		negate="!"
		shift
	fi

	rm -f "rerere-gc.txt" &&
	GIT_TRACE2_EVENT="$(pwd)/rerere-gc.txt" "$@" &&
	test_subcommand $negate git rerere gc <rerere-gc.txt
}

test_expect_success 'rerere-gc task without --auto always collects garbage' '
	test_expect_rerere_gc git maintenance run --task=rerere-gc
'

test_expect_success 'rerere-gc task with --auto only prunes with prunable entries' '
	test_when_finished "rm -rf .git/rr-cache" &&
	! git maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc ! git maintenance run --auto --task=rerere-gc &&
	mkdir .git/rr-cache &&
	! git maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc ! git maintenance run --auto --task=rerere-gc &&
	: >.git/rr-cache/entry &&
	git maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc git maintenance run --auto --task=rerere-gc
'

test_expect_success 'rerere-gc task with --auto honors maintenance.rerere-gc.auto' '
	test_when_finished "rm -rf .git/rr-cache" &&

	# A negative value should always prune.
	git -c maintenance.rerere-gc.auto=-1 maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc git -c maintenance.rerere-gc.auto=-1 maintenance run --auto --task=rerere-gc &&

	# A positive value prunes when there is at least one entry.
	! git -c maintenance.rerere-gc.auto=9000 maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc ! git -c maintenance.rerere-gc.auto=9000 maintenance run --auto --task=rerere-gc &&
	mkdir .git/rr-cache &&
	! git -c maintenance.rerere-gc.auto=9000 maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc ! git -c maintenance.rerere-gc.auto=9000 maintenance run --auto --task=rerere-gc &&
	: >.git/rr-cache/entry-1 &&
	git -c maintenance.rerere-gc.auto=9000 maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc git -c maintenance.rerere-gc.auto=9000 maintenance run --auto --task=rerere-gc &&

	# Zero should never prune.
	: >.git/rr-cache/entry-1 &&
	! git -c maintenance.rerere-gc.auto=0 maintenance is-needed --auto --task=rerere-gc &&
	test_expect_rerere_gc ! git -c maintenance.rerere-gc.auto=0 maintenance run --auto --task=rerere-gc
'

test_expect_success '--auto and --schedule incompatible' '
	test_must_fail git maintenance run --auto --schedule=daily 2>err &&
	test_grep "cannot be used together" err
'

test_expect_success '--task and --schedule incompatible' '
	test_must_fail git maintenance run --task=pack-refs --schedule=daily 2>err &&
	test_grep "cannot be used together" err
'

test_expect_success 'invalid --schedule value' '
	test_must_fail git maintenance run --schedule=annually 2>err &&
	test_grep "unrecognized --schedule" err
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

test_strategy () {
	STRATEGY="$1"
	shift

	cat >expect &&
	rm -f trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		git -c maintenance.strategy=$STRATEGY maintenance run --quiet "$@" &&
	sed -n 's/{"event":"child_start","sid":"[^/"]*",.*,"argv":\["\(.*\)\"]}/\1/p' <trace2.txt |
		sed 's/","/ /g'  >actual
	test_cmp expect actual
}

test_expect_success 'maintenance.strategy is respected' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&

		test_must_fail git -c maintenance.strategy=unknown maintenance run 2>err &&
		test_grep "unknown maintenance strategy: .unknown." err &&

		test_strategy incremental <<-\EOF &&
		git pack-refs --all --prune
		git reflog expire --all
		git gc --quiet --no-detach --skip-foreground-tasks
		EOF

		test_strategy incremental --schedule=weekly <<-\EOF &&
		git pack-refs --all --prune
		git prune-packed --quiet
		git multi-pack-index write --no-progress
		git multi-pack-index expire --no-progress
		git multi-pack-index repack --no-progress --batch-size=1
		git commit-graph write --split --reachable --no-progress
		EOF

		test_strategy gc <<-\EOF &&
		git pack-refs --all --prune
		git reflog expire --all
		git gc --quiet --no-detach --skip-foreground-tasks
		EOF

		test_strategy gc --schedule=weekly <<-\EOF &&
		git pack-refs --all --prune
		git reflog expire --all
		git gc --quiet --no-detach --skip-foreground-tasks
		EOF

		test_strategy geometric <<-\EOF &&
		git pack-refs --all --prune
		git reflog expire --all
		git repack -d -l --geometric=2 --quiet --write-midx
		git commit-graph write --split --reachable --no-progress
		git worktree prune --expire 3.months.ago
		git rerere gc
		EOF

		test_strategy geometric --schedule=weekly <<-\EOF
		git pack-refs --all --prune
		git reflog expire --all
		git repack -d -l --geometric=2 --quiet --write-midx
		git commit-graph write --split --reachable --no-progress
		git worktree prune --expire 3.months.ago
		git rerere gc
		EOF
	)
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

test_expect_success 'start without GIT_TEST_MAINT_SCHEDULER' '
	test_when_finished "rm -rf systemctl.log script repo" &&
	mkdir script &&
	write_script script/systemctl <<-\EOF &&
	echo "$*" >>../systemctl.log
	EOF
	git init repo &&
	(
		cd repo &&
		sane_unset GIT_TEST_MAINT_SCHEDULER &&
		PATH="$PWD/../script:$PATH" git maintenance start --scheduler=systemd
	) &&
	test_grep -- "--user list-timers" systemctl.log &&
	test_grep -- "enable --now git-maintenance@" systemctl.log
'

test_expect_success 'start --scheduler=<scheduler>' '
	test_expect_code 129 git maintenance start --scheduler=foo 2>err &&
	test_grep "unrecognized --scheduler argument" err &&

	test_expect_code 129 git maintenance start --no-scheduler 2>err &&
	test_grep "unknown option" err &&

	test_expect_code 128 \
		env GIT_TEST_MAINT_SCHEDULER="launchctl:true,schtasks:true" \
		git maintenance start --scheduler=crontab 2>err &&
	test_grep "fatal: crontab scheduler is not available" err
'

test_expect_success 'start from empty cron table' '
	GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt" git maintenance start --scheduler=crontab &&

	# start registers the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	grep "for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=daily" cron.txt &&
	grep "for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=hourly" cron.txt &&
	grep "for-each-repo --keep-going --config=maintenance.repo maintenance run --schedule=weekly" cron.txt
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

	for schedule in hourly daily weekly
	do
		test_path_is_file "systemd/user/git-maintenance@$schedule.timer" || return 1
	done &&
	test_path_is_file "systemd/user/git-maintenance@.service" &&

	test_systemd_analyze_verify "systemd/user/git-maintenance@hourly.service" &&
	test_systemd_analyze_verify "systemd/user/git-maintenance@daily.service" &&
	test_systemd_analyze_verify "systemd/user/git-maintenance@weekly.service" &&

	grep "core.askPass=true" "systemd/user/git-maintenance@.service" &&
	grep "credential.interactive=false" "systemd/user/git-maintenance@.service" &&

	printf -- "--user enable --now git-maintenance@%s.timer\n" hourly daily weekly >expect &&
	test_cmp expect args &&

	rm -f args &&
	GIT_TEST_MAINT_SCHEDULER="systemctl:./print-args" git maintenance stop &&

	# stop does not unregister the repo
	git config --get --global --fixed-value maintenance.repo "$(pwd)" &&

	for schedule in hourly daily weekly
	do
		test_path_is_missing "systemd/user/git-maintenance@$schedule.timer" || return 1
	done &&
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

test_expect_success 'fails when configured to use an invalid strategy' '
	test_must_fail git -c maintenance.strategy=invalid maintenance run --schedule=hourly 2>err &&
	test_grep "unknown maintenance strategy: .invalid." err
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

test_expect_success 'failed schedule prevents config change' '
	git init --bare failcase &&

	for scheduler in crontab launchctl schtasks systemctl
	do
		GIT_TEST_MAINT_SCHEDULER="$scheduler:false" &&
		export GIT_TEST_MAINT_SCHEDULER &&
		test_must_fail \
			git -C failcase maintenance start &&
		test_must_fail git -C failcase config maintenance.auto || return 1
	done
'

test_expect_success '--no-detach causes maintenance to not run in background' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# Prepare the repository such that git-maintenance(1) ends up
		# outputting something.
		test_commit something &&
		git config set maintenance.gc.enabled false &&
		git config set maintenance.loose-objects.enabled true &&
		git config set maintenance.loose-objects.auto 1 &&
		git config set maintenance.incremental-repack.enabled true &&

		GIT_TRACE2_EVENT="$(pwd)/trace.txt" \
			git maintenance run --no-detach >out 2>&1 &&
		! test_region maintenance detach trace.txt
	)
'

test_expect_success '--detach causes maintenance to run in background' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit something &&
		git config set maintenance.gc.enabled false &&
		git config set maintenance.loose-objects.enabled true &&
		git config set maintenance.loose-objects.auto 1 &&
		git config set maintenance.incremental-repack.enabled true &&

		# The extra file descriptor gets inherited to the child
		# process, and by reading stdout we thus essentially wait for
		# that descriptor to get closed, which indicates that the child
		# is done, too.
		does_not_matter=$(GIT_TRACE2_EVENT="$(pwd)/trace.txt" \
			git maintenance run --detach 9>&1) &&
		test_region maintenance detach trace.txt
	)
'

test_expect_success 'repacking loose objects is quiet' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit something &&
		git config set maintenance.gc.enabled false &&
		git config set maintenance.loose-objects.enabled true &&
		git config set maintenance.loose-objects.auto 1 &&

		git maintenance run --quiet >out 2>&1 &&
		test_must_be_empty out
	)
'

test_expect_success 'maintenance aborts with existing lock file' '
	test_when_finished "rm -rf repo script" &&
	mkdir script &&
	write_script script/systemctl <<-\EOF &&
	true
	EOF

	git init repo &&
	: >repo/.git/objects/schedule.lock &&
	test_must_fail env PATH="$PWD/script:$PATH" git -C repo maintenance start --scheduler=systemd 2>err &&
	test_grep "Another scheduled git-maintenance(1) process seems to be running" err
'

test_done
