#!/bin/sh

test_description='test dumb fetching over http via static file'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	say 'skipping test, git built without http support'
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'5550'}
start_httpd

test_expect_success 'setup repository' '
	echo content >file &&
	git add file &&
	git commit -m one
'

test_expect_success 'create http-accessible bare repository' '
	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git --bare init &&
	 echo "exec git update-server-info" >hooks/post-update &&
	 chmod +x hooks/post-update
	) &&
	git remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public master:master
'

test_expect_success 'clone http repository' '
	git clone $HTTPD_URL/dumb/repo.git clone &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	git commit -a -m two &&
	git push public
	(cd clone && git pull) &&
	test_cmp file clone/file
'

test_expect_success 'http remote detects correct HEAD' '
	git push public master:other &&
	(cd clone &&
	 git remote set-head origin -d &&
	 git remote set-head origin -a &&
	 git symbolic-ref refs/remotes/origin/HEAD > output &&
	 echo refs/remotes/origin/master > expect &&
	 test_cmp expect output
	)
'

test_expect_success 'fetch packed objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	git --bare repack &&
	git --bare prune-packed &&
	git clone $HTTPD_URL/dumb/repo_pack.git
'

test_expect_success 'did not use upload-pack service' '
	grep '/git-upload-pack' <"$HTTPD_ROOT_PATH"/access.log >act
	: >exp
	test_cmp exp act
'

stop_httpd
test_done
