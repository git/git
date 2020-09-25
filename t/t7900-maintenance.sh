#!/bin/sh

test_description='git maintenance builtin'

. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH=0

test_expect_success 'help text' '
	test_expect_code 129 git maintenance -h 2>err &&
	test_i18ngrep "usage: git maintenance run" err &&
	test_expect_code 128 git maintenance barf 2>err &&
	test_i18ngrep "invalid subcommand: barf" err &&
	test_expect_code 129 git maintenance 2>err &&
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
	fetchargs="--prune --no-tags --no-write-fetch-head --recurse-submodules=no --refmap= --quiet" &&
	test_subcommand git fetch remote1 $fetchargs +refs/heads/\\*:refs/prefetch/remote1/\\* <run-prefetch.txt &&
	test_subcommand git fetch remote2 $fetchargs +refs/heads/\\*:refs/prefetch/remote2/\\* <run-prefetch.txt &&
	test_path_is_missing .git/refs/remotes &&
	git log prefetch/remote1/one &&
	git log prefetch/remote2/two &&
	git fetch --all &&
	test_cmp_rev refs/remotes/remote1/one refs/prefetch/remote1/one &&
	test_cmp_rev refs/remotes/remote2/two refs/prefetch/remote2/two
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

test_done
