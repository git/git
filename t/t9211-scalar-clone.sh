#!/bin/sh

test_description='test the `scalar clone` subcommand'

. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-terminal.sh"

GIT_TEST_MAINT_SCHEDULER="crontab:test-tool crontab cron.txt,launchctl:true,schtasks:true"
export GIT_TEST_MAINT_SCHEDULER

test_expect_success 'set up repository to clone' '
	rm -rf .git &&
	git init to-clone &&
	(
		cd to-clone &&
		git branch -m base &&

		test_commit first &&
		test_commit second &&
		test_commit third &&

		git switch -c parallel first &&
		mkdir -p 1/2 &&
		test_commit 1/2/3 &&

		git switch base &&

		# By default, permit
		git config uploadpack.allowfilter true &&
		git config uploadpack.allowanysha1inwant true
	)
'

cleanup_clone () {
	rm -rf "$1"
}

test_expect_success 'creates content in enlistment root' '
	enlistment=cloned &&

	scalar clone "file://$(pwd)/to-clone" $enlistment &&
	ls -A $enlistment >enlistment-root &&
	test_line_count = 1 enlistment-root &&
	test_path_is_dir $enlistment/src &&
	test_path_is_dir $enlistment/src/.git &&

	cleanup_clone $enlistment
'

test_expect_success 'with spaces' '
	enlistment="cloned with space" &&

	scalar clone "file://$(pwd)/to-clone" "$enlistment" &&
	test_path_is_dir "$enlistment" &&
	test_path_is_dir "$enlistment/src" &&
	test_path_is_dir "$enlistment/src/.git" &&

	cleanup_clone "$enlistment"
'

test_expect_success 'partial clone if supported by server' '
	enlistment=partial-clone &&

	scalar clone "file://$(pwd)/to-clone" $enlistment &&

	(
		cd $enlistment/src &&

		# Two promisor packs: one for refs, the other for blobs
		ls .git/objects/pack/pack-*.promisor >promisorlist &&
		test_line_count = 2 promisorlist
	) &&

	cleanup_clone $enlistment
'

test_expect_success 'fall back on full clone if partial unsupported' '
	enlistment=no-partial-support &&

	test_config -C to-clone uploadpack.allowfilter false &&
	test_config -C to-clone uploadpack.allowanysha1inwant false &&

	scalar clone "file://$(pwd)/to-clone" $enlistment 2>err &&
	grep "filtering not recognized by server, ignoring" err &&

	(
		cd $enlistment/src &&

		# Still get a refs promisor file, but none for blobs
		ls .git/objects/pack/pack-*.promisor >promisorlist &&
		test_line_count = 1 promisorlist
	) &&

	cleanup_clone $enlistment
'

test_expect_success 'initializes sparse-checkout by default' '
	enlistment=sparse &&

	scalar clone "file://$(pwd)/to-clone" $enlistment &&
	(
		cd $enlistment/src &&
		test_cmp_config true core.sparseCheckout &&
		test_cmp_config true core.sparseCheckoutCone
	) &&

	cleanup_clone $enlistment
'

test_expect_success '--full-clone does not create sparse-checkout' '
	enlistment=full-clone &&

	scalar clone --full-clone "file://$(pwd)/to-clone" $enlistment &&
	(
		cd $enlistment/src &&
		test_cmp_config "" --default "" core.sparseCheckout &&
		test_cmp_config "" --default "" core.sparseCheckoutCone
	) &&

	cleanup_clone $enlistment
'

test_expect_success '--single-branch clones HEAD only' '
	enlistment=single-branch &&

	scalar clone --single-branch "file://$(pwd)/to-clone" $enlistment &&
	(
		cd $enlistment/src &&
		git for-each-ref refs/remotes/origin >out &&
		test_line_count = 1 out &&
		grep "refs/remotes/origin/base" out
	) &&

	cleanup_clone $enlistment
'

test_expect_success '--no-single-branch clones all branches' '
	enlistment=no-single-branch &&

	scalar clone --no-single-branch "file://$(pwd)/to-clone" $enlistment &&
	(
		cd $enlistment/src &&
		git for-each-ref refs/remotes/origin >out &&
		test_line_count = 2 out &&
		grep "refs/remotes/origin/base" out &&
		grep "refs/remotes/origin/parallel" out
	) &&

	cleanup_clone $enlistment
'

test_expect_success TTY 'progress with tty' '
	enlistment=progress1 &&

	test_config -C to-clone uploadpack.allowfilter true &&
	test_config -C to-clone uploadpack.allowanysha1inwant true &&

	test_terminal env GIT_PROGRESS_DELAY=0 \
		scalar clone "file://$(pwd)/to-clone" "$enlistment" 2>stderr &&
	grep "Enumerating objects" stderr >actual &&
	test_line_count = 2 actual &&
	cleanup_clone $enlistment
'

test_expect_success 'progress without tty' '
	enlistment=progress2 &&

	test_config -C to-clone uploadpack.allowfilter true &&
	test_config -C to-clone uploadpack.allowanysha1inwant true &&

	GIT_PROGRESS_DELAY=0 scalar clone "file://$(pwd)/to-clone" "$enlistment" 2>stderr &&
	! grep "Enumerating objects" stderr &&
	! grep "Updating files" stderr &&
	cleanup_clone $enlistment
'

test_expect_success 'scalar clone warns when background maintenance fails' '
	GIT_TEST_MAINT_SCHEDULER="crontab:false,launchctl:false,schtasks:false" \
		scalar clone "file://$(pwd)/to-clone" maint-fail 2>err &&
	grep "could not turn on maintenance" err
'

test_done
