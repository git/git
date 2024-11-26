#!/bin/sh

test_description='verify safe.directory checks'

. ./test-lib.sh

GIT_TEST_ASSUME_DIFFERENT_OWNER=1
export GIT_TEST_ASSUME_DIFFERENT_OWNER

expect_rejected_dir () {
	test_must_fail git status 2>err &&
	grep "dubious ownership" err
}

test_expect_success 'safe.directory is not set' '
	expect_rejected_dir
'

test_expect_success 'safe.directory on the command line' '
	git -c safe.directory="$(pwd)" status
'

test_expect_success 'safe.directory in the environment' '
	env GIT_CONFIG_COUNT=1 \
	    GIT_CONFIG_KEY_0="safe.directory" \
	    GIT_CONFIG_VALUE_0="$(pwd)" \
	    git status
'

test_expect_success 'safe.directory in GIT_CONFIG_PARAMETERS' '
	env GIT_CONFIG_PARAMETERS="${SQ}safe.directory${SQ}=${SQ}$(pwd)${SQ}" \
	    git status
'

test_expect_success 'ignoring safe.directory in repo config' '
	(
		unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config safe.directory "$(pwd)"
	) &&
	expect_rejected_dir
'

test_expect_success 'safe.directory does not match' '
	git config --global safe.directory bogus &&
	expect_rejected_dir
'

test_expect_success 'path exist as different key' '
	git config --global foo.bar "$(pwd)" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory matches' '
	git config --global --add safe.directory "$(pwd)" &&
	git status
'

test_expect_success 'safe.directory matches, but is reset' '
	git config --global --add safe.directory "" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory=*' '
	git config --global --add safe.directory "*" &&
	git status
'

test_expect_success 'safe.directory=*, but is reset' '
	git config --global --add safe.directory "" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory with matching glob' '
	git config --global --unset-all safe.directory &&
	p=$(pwd) &&
	git config --global safe.directory "${p%/*}/*" &&
	git status
'

test_expect_success 'safe.directory with unmatching glob' '
	git config --global --unset-all safe.directory &&
	p=$(pwd) &&
	git config --global safe.directory "${p%/*}no/*" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory in included file' '
	git config --global --unset-all safe.directory &&
	cat >gitconfig-include <<-EOF &&
	[safe]
		directory = "$(pwd)"
	EOF
	git config --global --add include.path "$(pwd)/gitconfig-include" &&
	git status
'

test_expect_success 'local clone of unowned repo refused in unsafe directory' '
	test_when_finished "rm -rf source" &&
	git init source &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit -C source initial
	) &&
	test_must_fail git clone --local source target &&
	test_path_is_missing target
'

test_expect_success 'local clone of unowned repo accepted in safe directory' '
	test_when_finished "rm -rf source" &&
	git init source &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit -C source initial
	) &&
	test_must_fail git clone --local source target &&
	git config --global --add safe.directory "$(pwd)/source/.git" &&
	git clone --local source target &&
	test_path_is_dir target
'

test_expect_success SYMLINKS 'checked paths are normalized' '
	test_when_finished "rm -rf repository; rm -f repo" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	git init repository &&
	ln -s repository repo &&
	(
		cd repository &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "$(pwd)/repository"
	) &&
	git -C repository for-each-ref &&
	git -C repository/ for-each-ref &&
	git -C repo for-each-ref &&
	git -C repo/ for-each-ref &&
	test_must_fail git -C repository/.git for-each-ref &&
	test_must_fail git -C repository/.git/ for-each-ref &&
	test_must_fail git -C repo/.git for-each-ref &&
	test_must_fail git -C repo/.git/ for-each-ref
'

test_expect_success SYMLINKS 'checked leading paths are normalized' '
	test_when_finished "rm -rf repository; rm -f repo" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	mkdir -p repository &&
	git init repository/s &&
	ln -s repository repo &&
	(
		cd repository/s &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "$(pwd)/repository/*"
	) &&
	git -C repository/s for-each-ref &&
	git -C repository/s/ for-each-ref &&
	git -C repo/s for-each-ref &&
	git -C repo/s/ for-each-ref &&
	git -C repository/s/.git for-each-ref &&
	git -C repository/s/.git/ for-each-ref &&
	git -C repo/s/.git for-each-ref &&
	git -C repo/s/.git/ for-each-ref
'

test_expect_success SYMLINKS 'configured paths are normalized' '
	test_when_finished "rm -rf repository; rm -f repo" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	git init repository &&
	ln -s repository repo &&
	(
		cd repository &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "$(pwd)/repo"
	) &&
	git -C repository for-each-ref &&
	git -C repository/ for-each-ref &&
	git -C repo for-each-ref &&
	git -C repo/ for-each-ref &&
	test_must_fail git -C repository/.git for-each-ref &&
	test_must_fail git -C repository/.git/ for-each-ref &&
	test_must_fail git -C repo/.git for-each-ref &&
	test_must_fail git -C repo/.git/ for-each-ref
'

test_expect_success SYMLINKS 'configured leading paths are normalized' '
	test_when_finished "rm -rf repository; rm -f repo" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	mkdir -p repository &&
	git init repository/s &&
	ln -s repository repo &&
	(
		cd repository/s &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "$(pwd)/repo/*"
	) &&
	git -C repository/s for-each-ref &&
	git -C repository/s/ for-each-ref &&
	git -C repository/s/.git for-each-ref &&
	git -C repository/s/.git/ for-each-ref &&
	git -C repo/s for-each-ref &&
	git -C repo/s/ for-each-ref &&
	git -C repo/s/.git for-each-ref &&
	git -C repo/s/.git/ for-each-ref
'

test_expect_success 'safe.directory set to a dot' '
	test_when_finished "rm -rf repository" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	mkdir -p repository/subdir &&
	git init repository &&
	(
		cd repository &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "."
	) &&
	git -C repository for-each-ref &&
	git -C repository/ for-each-ref &&
	git -C repository/.git for-each-ref &&
	git -C repository/.git/ for-each-ref &&

	# What is allowed is repository/subdir but the repository
	# path is repository.
	test_must_fail git -C repository/subdir for-each-ref &&

	# Likewise, repository .git/refs is allowed with "." but
	# repository/.git that is accessed is not allowed.
	test_must_fail git -C repository/.git/refs for-each-ref
'

test_expect_success 'safe.directory set to asterisk' '
	test_when_finished "rm -rf repository" &&
	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global --unset-all safe.directory
	) &&
	mkdir -p repository/subdir &&
	git init repository &&
	(
		cd repository &&
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		test_commit sample
	) &&

	(
		sane_unset GIT_TEST_ASSUME_DIFFERENT_OWNER &&
		git config --global safe.directory "*"
	) &&
	# these are trivial
	git -C repository for-each-ref &&
	git -C repository/ for-each-ref &&
	git -C repository/.git for-each-ref &&
	git -C repository/.git/ for-each-ref &&

	# With "*", everything is allowed, and the repository is
	# discovered, which is different behaviour from "." above.
	git -C repository/subdir for-each-ref &&

	# Likewise.
	git -C repository/.git/refs for-each-ref
'

test_done
