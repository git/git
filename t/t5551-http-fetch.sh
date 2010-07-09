#!/bin/sh

test_description='test smart fetching over http via http-backend'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	skip_all='skipping test, git built without http support'
	test_done
fi

LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'5551'}
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	echo content >file &&
	git add file &&
	git commit -m one
'

test_expect_success 'create http-accessible bare repository' '
	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git --bare init
	) &&
	git remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public master:master
'

cat >exp <<EOF
> GET /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1
> Accept: */*
> Pragma: no-cache
< HTTP/1.1 200 OK
< Pragma: no-cache
< Cache-Control: no-cache, max-age=0, must-revalidate
< Content-Type: application/x-git-upload-pack-advertisement
> POST /smart/repo.git/git-upload-pack HTTP/1.1
> Accept-Encoding: deflate, gzip
> Content-Type: application/x-git-upload-pack-request
> Accept: application/x-git-upload-pack-result
> Content-Length: xxx
< HTTP/1.1 200 OK
< Pragma: no-cache
< Cache-Control: no-cache, max-age=0, must-revalidate
< Content-Type: application/x-git-upload-pack-result
EOF
test_expect_success 'clone http repository' '
	GIT_CURL_VERBOSE=1 git clone --quiet $HTTPD_URL/smart/repo.git clone 2>err &&
	test_cmp file clone/file &&
	tr '\''\015'\'' Q <err |
	sed -e "
		s/Q\$//
		/^[*] /d
		/^$/d
		/^< $/d

		/^[^><]/{
			s/^/> /
		}

		/^> User-Agent: /d
		/^> Host: /d
		/^> POST /,$ {
			/^> Accept: [*]\\/[*]/d
		}
		s/^> Content-Length: .*/> Content-Length: xxx/
		/^> 00..want /d
		/^> 00.*done/d

		/^< Server: /d
		/^< Expires: /d
		/^< Date: /d
		/^< Content-Length: /d
		/^< Transfer-Encoding: /d
	" >act &&
	test_cmp exp act
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	git commit -a -m two &&
	git push public
	(cd clone && git pull) &&
	test_cmp file clone/file
'

cat >exp <<EOF
GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/repo.git/git-upload-pack HTTP/1.1 200
GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/repo.git/git-upload-pack HTTP/1.1 200
EOF
test_expect_success 'used upload-pack service' '
	sed -e "
		s/^.* \"//
		s/\"//
		s/ [1-9][0-9]*\$//
		s/^GET /GET  /
	" >act <"$HTTPD_ROOT_PATH"/access.log &&
	test_cmp exp act
'

stop_httpd
test_done
