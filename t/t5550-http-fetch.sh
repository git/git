#!/bin/sh

test_description='test dumb fetching over http via static file'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	skip_all='skipping test, git built without http support'
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
	git clone $HTTPD_URL/dumb/repo.git clone-tmpl &&
	cp -R clone-tmpl clone &&
	test_cmp file clone/file
'

test_expect_success 'create password-protected repository' '
	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/auth/" &&
	cp -Rf "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
	       "$HTTPD_DOCUMENT_ROOT_PATH/auth/repo.git"
'

test_expect_success 'setup askpass helpers' '
	cat >askpass <<-EOF &&
	#!/bin/sh
	echo >>"$PWD/askpass-query" "askpass: \$*" &&
	cat "$PWD/askpass-response"
	EOF
	chmod +x askpass &&
	GIT_ASKPASS="$PWD/askpass" &&
	export GIT_ASKPASS &&
	>askpass-expect-none &&
	echo "askpass: Password: " >askpass-expect-pass &&
	{ echo "askpass: Username: " &&
	  cat askpass-expect-pass
	} >askpass-expect-both
'

test_expect_success 'cloning password-protected repository can fail' '
	>askpass-query &&
	echo wrong >askpass-response &&
	test_must_fail git clone "$HTTPD_URL/auth/repo.git" clone-auth-fail &&
	test_cmp askpass-expect-both askpass-query
'

test_expect_success 'http auth can use user/pass in URL' '
	>askpass-query &&
	echo wrong >askpass-response &&
	git clone "$HTTPD_URL_USER_PASS/auth/repo.git" clone-auth-none &&
	test_cmp askpass-expect-none askpass-query
'

test_expect_success 'http auth can use just user in URL' '
	>askpass-query &&
	echo user@host >askpass-response &&
	git clone "$HTTPD_URL_USER/auth/repo.git" clone-auth-pass &&
	test_cmp askpass-expect-pass askpass-query
'

test_expect_success 'http auth can request both user and pass' '
	>askpass-query &&
	echo user@host >askpass-response &&
	git clone "$HTTPD_URL/auth/repo.git" clone-auth-both &&
	test_cmp askpass-expect-both askpass-query
'

test_expect_success 'http auth can pull user from config' '
	>askpass-query &&
	echo user@host >askpass-response &&
	git config --global credential.$HTTPD_PROTO:$HTTPD_DEST.username user@host &&
	git clone "$HTTPD_URL/auth/repo.git" clone-auth-config &&
	test_cmp askpass-expect-pass askpass-query
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	git commit -a -m two &&
	git push public &&
	(cd clone && git pull) &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via manual http-fetch' '
	cp -R clone-tmpl clone2 &&

	HEAD=$(git rev-parse --verify HEAD) &&
	(cd clone2 &&
	 git http-fetch -a -w heads/master-new $HEAD $(git config remote.origin.url) &&
	 git checkout master-new &&
	 test $HEAD = $(git rev-parse --verify HEAD)) &&
	test_cmp file clone2/file
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
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	 git --bare repack &&
	 git --bare prune-packed
	) &&
	git clone $HTTPD_URL/dumb/repo_pack.git
'

test_expect_success 'fetch notices corrupt pack' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	 p=`ls objects/pack/pack-*.pack` &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad1.git &&
	(cd repo_bad1.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch $HTTPD_URL/dumb/repo_bad1.git &&
	 test 0 = `ls objects/pack/pack-*.pack | wc -l`
	)
'

test_expect_success 'fetch notices corrupt idx' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	 p=`ls objects/pack/pack-*.idx` &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad2.git &&
	(cd repo_bad2.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch $HTTPD_URL/dumb/repo_bad2.git &&
	 test 0 = `ls objects/pack | wc -l`
	)
'

test_expect_success 'did not use upload-pack service' '
	grep '/git-upload-pack' <"$HTTPD_ROOT_PATH"/access.log >act
	: >exp
	test_cmp exp act
'

stop_httpd
test_done
