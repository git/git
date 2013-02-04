#!/bin/sh

test_description='git p4 options'

. ./lib-git-p4.sh

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

test_expect_success 'clone no --git-dir' '
	test_must_fail git p4 clone --git-dir=xx //depot
'

test_expect_success 'clone --branch should checkout master' '
	git p4 clone --branch=refs/remotes/p4/sb --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git rev-parse refs/remotes/p4/sb >sb &&
		git rev-parse refs/heads/master >master &&
		test_cmp sb master &&
		git rev-parse HEAD >head &&
		test_cmp sb head
	)
'

test_expect_success 'sync when no master branch prints a nice error' '
	test_when_finished cleanup_git &&
	git p4 clone --branch=refs/remotes/p4/sb --dest="$git" //depot@2 &&
	(
		cd "$git" &&
		test_must_fail git p4 sync 2>err &&
		grep "Error: no branch refs/remotes/p4/master" err
	)
'

test_expect_success 'sync --branch builds the full ref name correctly' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init &&

		git p4 sync --branch=b1 //depot &&
		git rev-parse --verify refs/remotes/p4/b1 &&
		git p4 sync --branch=p4/b2 //depot &&
		git rev-parse --verify refs/remotes/p4/b2 &&

		git p4 sync --import-local --branch=h1 //depot &&
		git rev-parse --verify refs/heads/p4/h1 &&
		git p4 sync --import-local --branch=p4/h2 //depot &&
		git rev-parse --verify refs/heads/p4/h2 &&

		git p4 sync --branch=refs/stuff //depot &&
		git rev-parse --verify refs/stuff
	)
'

# engages --detect-branches code, which will do filename filtering so
# no sync to either b1 or b2
test_expect_success 'sync when two branches but no master should noop' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init &&
		git p4 sync --branch=refs/remotes/p4/b1 //depot@2 &&
		git p4 sync --branch=refs/remotes/p4/b2 //depot@2 &&
		git p4 sync &&
		git show -s --format=%s refs/remotes/p4/b1 >show &&
		grep "Initial import" show &&
		git show -s --format=%s refs/remotes/p4/b2 >show &&
		grep "Initial import" show
	)
'

test_expect_success 'sync --branch updates specific branch, no detection' '
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init &&
		git p4 sync --branch=b1 //depot@2 &&
		git p4 sync --branch=b2 //depot@2 &&
		git p4 sync --branch=b2 &&
		git show -s --format=%s refs/remotes/p4/b1 >show &&
		grep "Initial import" show &&
		git show -s --format=%s refs/remotes/p4/b2 >show &&
		grep "change 3" show
	)
'

# allows using the refname "p4" as a short name for p4/master
test_expect_success 'clone creates HEAD symbolic reference' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git rev-parse --verify refs/remotes/p4/master >master &&
		git rev-parse --verify p4 >p4 &&
		test_cmp master p4
	)
'

test_expect_success 'clone --branch creates HEAD symbolic reference' '
	git p4 clone --branch=refs/remotes/p4/sb --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git rev-parse --verify refs/remotes/p4/sb >sb &&
		git rev-parse --verify p4 >p4 &&
		test_cmp sb p4
	)
'

test_expect_success 'clone --changesfile' '
	test_when_finished "rm cf" &&
	printf "1\n3\n" >cf &&
	git p4 clone --changesfile="$TRASH_DIRECTORY/cf" --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git log --oneline p4/master >lines &&
		test_line_count = 2 lines
		test_path_is_file file1 &&
		test_path_is_missing file2 &&
		test_path_is_file file3
	)
'

test_expect_success 'clone --changesfile, @all' '
	test_when_finished "rm cf" &&
	printf "1\n3\n" >cf &&
	test_must_fail git p4 clone --changesfile="$TRASH_DIRECTORY/cf" --dest="$git" //depot@all
'

# imports both master and p4/master in refs/heads
# requires --import-local on sync to find p4 refs/heads
# does not update master on sync, just p4/master
test_expect_success 'clone/sync --import-local' '
	git p4 clone --import-local --dest="$git" //depot@1,2 &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git log --oneline refs/heads/master >lines &&
		test_line_count = 2 lines &&
		git log --oneline refs/heads/p4/master >lines &&
		test_line_count = 2 lines &&
		test_must_fail git p4 sync &&

		git p4 sync --import-local &&
		git log --oneline refs/heads/master >lines &&
		test_line_count = 2 lines &&
		git log --oneline refs/heads/p4/master >lines &&
		test_line_count = 3 lines
	)
'

test_expect_success 'clone --max-changes' '
	git p4 clone --dest="$git" --max-changes 2 //depot@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git log --oneline refs/heads/master >lines &&
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
	git p4 clone --dest="$git" --keep-path //depot/sub/dir@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test_path_is_missing f4 &&
		test_path_is_file sub/dir/f4
	) &&
	cleanup_git &&
	git p4 clone --dest="$git" //depot/sub/dir@all &&
	(
		cd "$git" &&
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
		test_must_fail git p4 clone --dest="$git" --use-client-spec
	) &&
	# build a different client
	cli2="$TRASH_DIRECTORY/cli2" &&
	mkdir -p "$cli2" &&
	test_when_finished "rmdir \"$cli2\"" &&
	test_when_finished cleanup_git &&
	(
		# group P4CLIENT and cli changes in a sub-shell
		P4CLIENT=client2 &&
		cli="$cli2" &&
		client_view "//depot/sub/... //client2/bus/..." &&
		git p4 clone --dest="$git" --use-client-spec //depot/... &&
		(
			cd "$git" &&
			test_path_is_file bus/dir/f4 &&
			test_path_is_missing file1
		) &&
		cleanup_git &&
		# same thing again, this time with variable instead of option
		(
			cd "$git" &&
			git init &&
			git config git-p4.useClientSpec true &&
			git p4 sync //depot/... &&
			git checkout -b master p4/master &&
			test_path_is_file bus/dir/f4 &&
			test_path_is_missing file1
		)
	)
'

test_expect_success 'submit works with no p4/master' '
	test_when_finished cleanup_git &&
	git p4 clone --branch=b1 //depot@1,2 --destination="$git" &&
	(
		cd "$git" &&
		test_commit submit-1-branch &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --branch=b1
	)
'

# The sync/rebase part post-submit will engage detect-branches
# machinery which will not do anything in this particular test.
test_expect_success 'submit works with two branches' '
	test_when_finished cleanup_git &&
	git p4 clone --branch=b1 //depot@1,2 --destination="$git" &&
	(
		cd "$git" &&
		git p4 sync --branch=b2 //depot@1,3 &&
		test_commit submit-2-branches &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
