#!/bin/sh
#
# Copyright (c) 2022 Sun Chao
#

test_description='Test hide-refs hook'

. ./test-lib.sh
. "$TEST_DIRECTORY"/t1419/common-functions.sh

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

setup_test_repos () {
	test_expect_success "setup bare_repo and work_repo" '
		rm -rf bare_repo.git &&
		rm -rf work_repo &&
		git init --bare bare_repo.git &&
		git init work_repo &&

		# create new commits and references
		create_commits_in work_repo A B C D &&
		(
			cd work_repo &&
			git config --local core.abbrev 7 &&
			git update-ref refs/heads/main $A &&
			git update-ref refs/heads/dev $B &&
			git update-ref refs/pull-requests/1/head $C &&
			git tag -m "v123" v123 $D &&
			git push ../bare_repo.git +refs/heads/*:refs/heads/* &&
			git push ../bare_repo.git +refs/tags/*:refs/tags/* &&
			git push ../bare_repo.git +refs/pull-requests/*:refs/pull-requests/*
		) &&
		TAG=$(git -C work_repo rev-parse v123) &&

		# config transfer.hiderefs values with "hook:" prefix
		(
			git -C bare_repo.git config --local http.receivepack true &&
			git -C bare_repo.git config --add transfer.hiderefs hook:
		)
	'
}

setup_httpd() {
	ROOT_PATH="$PWD"
	. "$TEST_DIRECTORY"/lib-gpg.sh
	. "$TEST_DIRECTORY"/lib-httpd.sh
	. "$TEST_DIRECTORY"/lib-terminal.sh

	start_httpd
	set_askpass user@host pass@host
	setup_askpass_helper
}

# Run test cases when hide-refs hook exit abnormally
run_tests_for_abnormal_hook() {
	GIT_TEST_PROTOCOL_VERSION=$1
	BAREREPO_GIT_DIR="$(pwd)/bare_repo.git"

	for t in  "$TEST_DIRECTORY"/t1419/abnormal-*.sh
	do
		setup_test_repos

		. "$t"
	done
}

# Run test cases under local/HTTP protocol
run_tests_for_normal_hook() {
	for t in  "$TEST_DIRECTORY"/t1419/test-*.sh
	do
		setup_test_repos
		case $1 in
			http)
				PROTOCOL="HTTP protocol"

				# bare_repo.git need move to httpd sever root path
				BAREREPO_GIT_DIR="$HTTPD_DOCUMENT_ROOT_PATH/bare_repo.git"
				rm -rf "$BAREREPO_GIT_DIR"
				mv bare_repo.git "$BAREREPO_GIT_DIR"

				# setup the repository service URL address of http protocol
				BAREREPO_PREFIX="$HTTPD_URL"/smart
				BAREREPO_URL="$BAREREPO_PREFIX/bare_repo.git"
				;;
			local)
				PROTOCOL="builtin protocol"
				BAREREPO_GIT_DIR="$(pwd)/bare_repo.git"

				# setup the repository service address of builtin protocol
				BAREREPO_PREFIX="$(pwd)"
				BAREREPO_URL="$BAREREPO_PREFIX/bare_repo.git"
				;;
		esac

		GIT_TEST_PROTOCOL_VERSION=$2
		git -C work_repo remote add origin "$BAREREPO_URL"

		. "$t"
	done
}

setup_httpd
for protocol in 1 2
do
	run_tests_for_abnormal_hook $protocol
	run_tests_for_normal_hook local $protocol
	run_tests_for_normal_hook http $protocol
done

test_done
