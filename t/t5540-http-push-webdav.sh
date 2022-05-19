#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test WebDAV http-push

This test runs various sanity checks on http-push.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if but http-push > /dev/null 2>&1 || [ $? -eq 128 ]
then
	skip_all="skipping test, USE_CURL_MULTI is not defined"
	test_done
fi

if test_have_prereq !REFFILES
then
	skip_all='skipping test; dumb HTTP protocol not supported with reftable.'
	test_done
fi

LIB_HTTPD_DAV=t
. "$TEST_DIRECTORY"/lib-httpd.sh
ROOT_PATH="$PWD"
start_httpd

test_expect_success 'setup remote repository' '
	cd "$ROOT_PATH" &&
	mkdir test_repo &&
	cd test_repo &&
	but init &&
	: >path1 &&
	but add path1 &&
	test_tick &&
	but cummit -m initial &&
	cd - &&
	but clone --bare test_repo test_repo.but &&
	cd test_repo.but &&
	but --bare update-server-info &&
	test_hook --setup post-update <<-\EOF &&
	exec but update-server-info
	EOF
	ORIG_HEAD=$(but rev-parse --verify HEAD) &&
	cd - &&
	mv test_repo.but "$HTTPD_DOCUMENT_ROOT_PATH"
'

test_expect_success 'create password-protected repository' '
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb" &&
	cp -Rf "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" \
	       "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.but"
'

setup_askpass_helper

test_expect_success 'clone remote repository' '
	cd "$ROOT_PATH" &&
	but clone $HTTPD_URL/dumb/test_repo.but test_repo_clone
'

test_expect_success 'push to remote repository with packed refs' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	but add path2 &&
	test_tick &&
	but cummit -m path2 &&
	HEAD=$(but rev-parse --verify HEAD) &&
	but push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
	 test $HEAD = $(but rev-parse --verify HEAD))
'

test_expect_success 'push already up-to-date' '
	but push
'

test_expect_success 'push to remote repository with unpacked refs' '
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
	 rm packed-refs &&
	 but update-ref refs/heads/main $ORIG_HEAD &&
	 but --bare update-server-info) &&
	but push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
	 test $HEAD = $(but rev-parse --verify HEAD))
'

test_expect_success 'http-push fetches unpacked objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but \
		"$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_unpacked.but &&

	but clone $HTTPD_URL/dumb/test_repo_unpacked.but \
		"$ROOT_PATH"/fetch_unpacked &&

	# By reset, we force but to retrieve the object
	(cd "$ROOT_PATH"/fetch_unpacked &&
	 but reset --hard HEAD^ &&
	 but remote rm origin &&
	 but reflog expire --expire=0 --all &&
	 but prune &&
	 but push -f -v $HTTPD_URL/dumb/test_repo_unpacked.but main)
'

test_expect_success 'http-push fetches packed objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but \
		"$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_packed.but &&

	but clone $HTTPD_URL/dumb/test_repo_packed.but \
		"$ROOT_PATH"/test_repo_clone_packed &&

	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_packed.but &&
	 but --bare repack &&
	 but --bare prune-packed) &&

	# By reset, we force but to retrieve the packed object
	(cd "$ROOT_PATH"/test_repo_clone_packed &&
	 but reset --hard HEAD^ &&
	 but remote remove origin &&
	 but reflog expire --expire=0 --all &&
	 but prune &&
	 but push -f -v $HTTPD_URL/dumb/test_repo_packed.but main)
'

test_expect_success 'create and delete remote branch' '
	cd "$ROOT_PATH"/test_repo_clone &&
	but checkout -b dev &&
	: >path3 &&
	but add path3 &&
	test_tick &&
	but cummit -m dev &&
	but push origin dev &&
	but push origin :dev &&
	test_must_fail but show-ref --verify refs/remotes/origin/dev
'

test_expect_success 'non-force push fails if not up to date' '
	but init --bare "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_conflict.but &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_conflict.but update-server-info &&
	but clone $HTTPD_URL/dumb/test_repo_conflict.but "$ROOT_PATH"/c1 &&
	but clone $HTTPD_URL/dumb/test_repo_conflict.but "$ROOT_PATH"/c2 &&
	test_cummit -C "$ROOT_PATH/c1" path1 &&
	but -C "$ROOT_PATH/c1" push origin HEAD &&
	but -C "$ROOT_PATH/c2" pull &&
	test_cummit -C "$ROOT_PATH/c1" path2 &&
	but -C "$ROOT_PATH/c1" push origin HEAD &&
	test_cummit -C "$ROOT_PATH/c2" path3 &&
	but -C "$ROOT_PATH/c1" log --graph --all &&
	but -C "$ROOT_PATH/c2" log --graph --all &&
	test_must_fail but -C "$ROOT_PATH/c2" push origin HEAD
'

test_expect_success 'MKCOL sends directory names with trailing slashes' '

	! grep "\"MKCOL.*[^/] HTTP/[^ ]*\"" < "$HTTPD_ROOT_PATH"/access.log

'

x1="[0-9a-f]"
x2="$x1$x1"
xtrunc=$(echo $OID_REGEX | sed -e "s/\[0-9a-f\]\[0-9a-f\]//")

test_expect_success 'PUT and MOVE sends object to URLs with SHA-1 hash suffix' '
	sed \
		-e "s/PUT /OP /" \
		-e "s/MOVE /OP /" \
	    -e "s|/objects/$x2/${xtrunc}_$OID_REGEX|WANTED_PATH_REQUEST|" \
		"$HTTPD_ROOT_PATH"/access.log |
	grep -e "\"OP .*WANTED_PATH_REQUEST HTTP/[.0-9]*\" 20[0-9] "

'

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but \
	"$ROOT_PATH"/test_repo_clone main

test_expect_success 'push to password-protected repository (user in URL)' '
	test_cummit pw-user &&
	set_askpass user@host pass@host &&
	but push "$HTTPD_URL_USER/auth/dumb/test_repo.but" HEAD &&
	but rev-parse --verify HEAD >expect &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.but" \
		rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_failure 'user was prompted only once for password' '
	expect_askpass pass user@host
'

test_expect_failure 'push to password-protected repository (no user in URL)' '
	test_cummit pw-nouser &&
	set_askpass user@host pass@host &&
	but push "$HTTPD_URL/auth/dumb/test_repo.but" HEAD &&
	expect_askpass both user@host &&
	but rev-parse --verify HEAD >expect &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.but" \
		rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_done
