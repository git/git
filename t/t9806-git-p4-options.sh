#!/bin/sh

test_description='but p4 options'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1" &&
		echo file2 >file2 &&
		p4 add file2 &&
		p4 submit -d "change 2" &&
		echo file3 >file3 &&
		p4 add file3 &&
		p4 submit -d "change 3"
	)
'

test_expect_success 'clone no --but-dir' '
	test_must_fail but p4 clone --but-dir=xx //depot
'

test_expect_success 'clone --branch should checkout main' '
	but p4 clone --branch=refs/remotes/p4/sb --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but rev-parse refs/remotes/p4/sb >sb &&
		but rev-parse refs/heads/main >main &&
		test_cmp sb main &&
		but rev-parse HEAD >head &&
		test_cmp sb head
	)
'

test_expect_success 'sync when no master branch prints a nice error' '
	test_when_finished cleanup_but &&
	but p4 clone --branch=refs/remotes/p4/sb --dest="$but" //depot@2 &&
	(
		cd "$but" &&
		test_must_fail but p4 sync 2>err &&
		grep "Error: no branch refs/remotes/p4/master" err
	)
'

test_expect_success 'sync --branch builds the full ref name correctly' '
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but init &&

		but p4 sync --branch=b1 //depot &&
		but rev-parse --verify refs/remotes/p4/b1 &&
		but p4 sync --branch=p4/b2 //depot &&
		but rev-parse --verify refs/remotes/p4/b2 &&

		but p4 sync --import-local --branch=h1 //depot &&
		but rev-parse --verify refs/heads/p4/h1 &&
		but p4 sync --import-local --branch=p4/h2 //depot &&
		but rev-parse --verify refs/heads/p4/h2 &&

		but p4 sync --branch=refs/stuff //depot &&
		but rev-parse --verify refs/stuff
	)
'

# engages --detect-branches code, which will do filename filtering so
# no sync to either b1 or b2
test_expect_success 'sync when two branches but no master should noop' '
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but init &&
		but p4 sync --branch=refs/remotes/p4/b1 //depot@2 &&
		but p4 sync --branch=refs/remotes/p4/b2 //depot@2 &&
		but p4 sync &&
		but show -s --format=%s refs/remotes/p4/b1 >show &&
		grep "Initial import" show &&
		but show -s --format=%s refs/remotes/p4/b2 >show &&
		grep "Initial import" show
	)
'

test_expect_success 'sync --branch updates specific branch, no detection' '
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but init &&
		but p4 sync --branch=b1 //depot@2 &&
		but p4 sync --branch=b2 //depot@2 &&
		but p4 sync --branch=b2 &&
		but show -s --format=%s refs/remotes/p4/b1 >show &&
		grep "Initial import" show &&
		but show -s --format=%s refs/remotes/p4/b2 >show &&
		grep "change 3" show
	)
'

# allows using the refname "p4" as a short name for p4/master
test_expect_success 'clone creates HEAD symbolic reference' '
	but p4 clone --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but rev-parse --verify refs/remotes/p4/master >master &&
		but rev-parse --verify p4 >p4 &&
		test_cmp master p4
	)
'

test_expect_success 'clone --branch creates HEAD symbolic reference' '
	but p4 clone --branch=refs/remotes/p4/sb --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but rev-parse --verify refs/remotes/p4/sb >sb &&
		but rev-parse --verify p4 >p4 &&
		test_cmp sb p4
	)
'

test_expect_success 'clone --changesfile' '
	test_when_finished "rm cf" &&
	printf "1\n3\n" >cf &&
	but p4 clone --changesfile="$TRASH_DIRECTORY/cf" --dest="$but" //depot &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but log --oneline p4/master >lines &&
		test_line_count = 2 lines &&
		test_path_is_file file1 &&
		test_path_is_missing file2 &&
		test_path_is_file file3
	)
'

test_expect_success 'clone --changesfile, @all' '
	test_when_finished "rm cf" &&
	printf "1\n3\n" >cf &&
	test_must_fail but p4 clone --changesfile="$TRASH_DIRECTORY/cf" --dest="$but" //depot@all
'

# imports both main and p4/master in refs/heads
# requires --import-local on sync to find p4 refs/heads
# does not update main on sync, just p4/master
test_expect_success 'clone/sync --import-local' '
	but p4 clone --import-local --dest="$but" //depot@1,2 &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but log --oneline refs/heads/main >lines &&
		test_line_count = 2 lines &&
		but log --oneline refs/heads/p4/master >lines &&
		test_line_count = 2 lines &&
		test_must_fail but p4 sync &&

		but p4 sync --import-local &&
		but log --oneline refs/heads/main >lines &&
		test_line_count = 2 lines &&
		but log --oneline refs/heads/p4/master >lines &&
		test_line_count = 3 lines
	)
'

test_expect_success 'clone --max-changes' '
	but p4 clone --dest="$but" --max-changes 2 //depot@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but log --oneline refs/heads/main >lines &&
		test_line_count = 2 lines
	)
'

test_expect_success 'clone --keep-path' '
	(
		cd "$cli" &&
		mkdir -p sub/dir &&
		echo f4 >sub/dir/f4 &&
		p4 add sub/dir/f4 &&
		p4 submit -d "change 4"
	) &&
	but p4 clone --dest="$but" --keep-path //depot/sub/dir@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		test_path_is_missing f4 &&
		test_path_is_file sub/dir/f4
	) &&
	cleanup_but &&
	but p4 clone --dest="$but" //depot/sub/dir@all &&
	(
		cd "$but" &&
		test_path_is_file f4 &&
		test_path_is_missing sub/dir/f4
	)
'

# clone --use-client-spec must still specify a depot path
# if given, it should rearrange files according to client spec
# when it has view lines that match the depot path
# XXX: should clone/sync just use the client spec exactly, rather
# than needing depot paths?
test_expect_success 'clone --use-client-spec' '
	(
		# big usage message
		exec >/dev/null &&
		test_must_fail but p4 clone --dest="$but" --use-client-spec
	) &&
	# build a different client
	cli2="$TRASH_DIRECTORY/cli2" &&
	mkdir -p "$cli2" &&
	test_when_finished "rmdir \"$cli2\"" &&
	test_when_finished cleanup_but &&
	(
		# group P4CLIENT and cli changes in a sub-shell
		P4CLIENT=client2 &&
		cli="$cli2" &&
		client_view "//depot/sub/... //client2/bus/..." &&
		but p4 clone --dest="$but" --use-client-spec //depot/... &&
		(
			cd "$but" &&
			test_path_is_file bus/dir/f4 &&
			test_path_is_missing file1
		) &&
		cleanup_but &&
		# same thing again, this time with variable instead of option
		(
			cd "$but" &&
			but init &&
			but config but-p4.useClientSpec true &&
			but p4 sync //depot/... &&
			but checkout -b main p4/master &&
			test_path_is_file bus/dir/f4 &&
			test_path_is_missing file1
		)
	)
'

test_expect_success 'submit works with no p4/master' '
	test_when_finished cleanup_but &&
	but p4 clone --branch=b1 //depot@1,2 --destination="$but" &&
	(
		cd "$but" &&
		test_cummit submit-1-branch &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --branch=b1
	)
'

# The sync/rebase part post-submit will engage detect-branches
# machinery which will not do anything in this particular test.
test_expect_success 'submit works with two branches' '
	test_when_finished cleanup_but &&
	but p4 clone --branch=b1 //depot@1,2 --destination="$but" &&
	(
		cd "$but" &&
		but p4 sync --branch=b2 //depot@1,3 &&
		test_cummit submit-2-branches &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	)
'

test_expect_success 'use --but-dir option and BUT_DIR' '
	test_when_finished cleanup_but &&
	but p4 clone //depot --destination="$but" &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		test_cummit first-change &&
		but p4 submit --but-dir "$but"
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		test_path_is_file first-change.t &&
		echo "cli_file" >cli_file.t &&
		p4 add cli_file.t &&
		p4 submit -d "cli change"
	) &&
	(but --but-dir "$but" p4 sync) &&
	(cd "$but" && but checkout -q p4/master) &&
	test_path_is_file "$but"/cli_file.t &&
	(
		cd "$cli" &&
		echo "cli_file2" >cli_file2.t &&
		p4 add cli_file2.t  &&
		p4 submit -d "cli change2"
	) &&
	(BUT_DIR="$but" but p4 sync) &&
	(cd "$but" && but checkout -q p4/master) &&
	test_path_is_file "$but"/cli_file2.t
'

test_done
