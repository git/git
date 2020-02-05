#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test WebDAV http-push

This test runs various sanity checks on http-push.'

. ./test-lib.sh

if git http-push > /dev/null 2>&1 || [ $? -eq 128 ]
then
	skip_all="skipping test, USE_CURL_MULTI is not defined"
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
	git init &&
	: >path1 &&
	git add path1 &&
	test_tick &&
	git commit -m initial &&
	cd - &&
	git clone --bare test_repo test_repo.git &&
	cd test_repo.git &&
	git --bare update-server-info &&
	mv hooks/post-update.sample hooks/post-update &&
	ORIG_HEAD=$(git rev-parse --verify HEAD) &&
	cd - &&
	mv test_repo.git "$HTTPD_DOCUMENT_ROOT_PATH"
'

test_expect_success 'create password-protected repository' '
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb" &&
	cp -Rf "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" \
	       "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.git"
'

setup_askpass_helper

test_expect_success 'clone remote repository' '
	cd "$ROOT_PATH" &&
	git clone $HTTPD_URL/dumb/test_repo.git test_repo_clone
'

test_expect_success 'push to remote repository with packed refs' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	git add path2 &&
	test_tick &&
	git commit -m path2 &&
	HEAD=$(git rev-parse --verify HEAD) &&
	git push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

test_expect_success 'push already up-to-date' '
	git push
'

test_expect_success 'push to remote repository with unpacked refs' '
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 rm packed-refs &&
	 git update-ref refs/heads/master $ORIG_HEAD &&
	 git --bare update-server-info) &&
	git push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

test_expect_success 'http-push fetches unpacked objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
		"$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_unpacked.git &&

	git clone $HTTPD_URL/dumb/test_repo_unpacked.git \
		"$ROOT_PATH"/fetch_unpacked &&

	# By reset, we force git to retrieve the object
	(cd "$ROOT_PATH"/fetch_unpacked &&
	 git reset --hard HEAD^ &&
	 git remote rm origin &&
	 git reflog expire --expire=0 --all &&
	 git prune &&
	 git push -f -v $HTTPD_URL/dumb/test_repo_unpacked.git master)
'

test_expect_success 'http-push fetches packed objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
		"$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_packed.git &&

	git clone $HTTPD_URL/dumb/test_repo_packed.git \
		"$ROOT_PATH"/test_repo_clone_packed &&

	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo_packed.git &&
	 git --bare repack &&
	 git --bare prune-packed) &&

	# By reset, we force git to retrieve the packed object
	(cd "$ROOT_PATH"/test_repo_clone_packed &&
	 git reset --hard HEAD^ &&
	 git remote remove origin &&
	 git reflog expire --expire=0 --all &&
	 git prune &&
	 git push -f -v $HTTPD_URL/dumb/test_repo_packed.git master)
'

test_expect_success 'create and delete remote branch' '
	cd "$ROOT_PATH"/test_repo_clone &&
	git checkout -b dev &&
	: >path3 &&
	git add path3 &&
	test_tick &&
	git commit -m dev &&
	git push origin dev &&
	git push origin :dev &&
	test_must_fail git show-ref --verify refs/remotes/origin/dev
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

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
	"$ROOT_PATH"/test_repo_clone master

test_expect_success 'push to password-protected repository (user in URL)' '
	test_commit pw-user &&
	set_askpass user@host pass@host &&
	git push "$HTTPD_URL_USER/auth/dumb/test_repo.git" HEAD &&
	git rev-parse --verify HEAD >expect &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.git" \
		rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_failure 'user was prompted only once for password' '
	expect_askpass pass user@host
'

test_expect_failure 'push to password-protected repository (no user in URL)' '
	test_commit pw-nouser &&
	set_askpass user@host pass@host &&
	git push "$HTTPD_URL/auth/dumb/test_repo.git" HEAD &&
	expect_askpass both user@host &&
	git rev-parse --verify HEAD >expect &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/test_repo.git" \
		rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_done
