#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test smart pushing over http via http-backend'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	skip_all='skipping test, git built without http support'
	test_done
fi

ROOT_PATH="$PWD"
LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'5541'}
. "$TEST_DIRECTORY"/lib-httpd.sh
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
	git config http.receivepack true &&
	ORIG_HEAD=$(git rev-parse --verify HEAD) &&
	cd - &&
	mv test_repo.git "$HTTPD_DOCUMENT_ROOT_PATH"
'

cat >exp <<EOF
GET  /smart/test_repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/test_repo.git/git-upload-pack HTTP/1.1 200
EOF
test_expect_success 'no empty path components' '
	# In the URL, add a trailing slash, and see if git appends yet another
	# slash.
	cd "$ROOT_PATH" &&
	git clone $HTTPD_URL/smart/test_repo.git/ test_repo_clone &&

	sed -e "
		s/^.* \"//
		s/\"//
		s/ [1-9][0-9]*\$//
		s/^GET /GET  /
	" >act <"$HTTPD_ROOT_PATH"/access.log &&

	# Clear the log, so that it does not affect the "used receive-pack
	# service" test which reads the log too.
	#
	# We do this before the actual comparison to ensure the log is cleared.
	echo > "$HTTPD_ROOT_PATH"/access.log &&

	test_cmp exp act
'

test_expect_success 'clone remote repository' '
	rm -rf test_repo_clone &&
	git clone $HTTPD_URL/smart/test_repo.git test_repo_clone
'

test_expect_success 'push to remote repository' '
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

cat >exp <<EOF

GET  /smart/test_repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/test_repo.git/git-upload-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
EOF
test_expect_success 'used receive-pack service' '
	sed -e "
		s/^.* \"//
		s/\"//
		s/ [1-9][0-9]*\$//
		s/^GET /GET  /
	" >act <"$HTTPD_ROOT_PATH"/access.log &&
	test_cmp exp act
'

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
	"$ROOT_PATH"/test_repo_clone master

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper' '
	# create a dissimilarly-named remote ref so that git is unable to match the
	# two refs (viz. local, remote) unless an explicit refspec is provided.
	git push origin master:retsam

	echo "change changed" > path2 &&
	git commit -a -m path2 --amend &&

	# push master too; this ensures there is at least one '"'push'"' command to
	# the remote helper and triggers interaction with the helper.
	test_must_fail git push -v origin +master master:retsam >output 2>&1 &&

	grep "^ + [a-f0-9]*\.\.\.[a-f0-9]* *master -> master (forced update)$" output &&
	grep "^ ! \[rejected\] *master -> retsam (non-fast-forward)$" output &&

	grep "To prevent you from losing history, non-fast-forward updates were rejected" \
		output
'

stop_httpd
test_done
