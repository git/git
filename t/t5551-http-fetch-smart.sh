#!/bin/sh

test_description='test smart fetching over http via http-backend'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	git config push.default matching &&
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
	git push public main:main
'

setup_askpass_helper

test_expect_success 'clone http repository' '
	cat >exp <<-\EOF &&
	> GET /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1
	> Accept: */*
	> Accept-Encoding: ENCODINGS
	> Pragma: no-cache
	< HTTP/1.1 200 OK
	< Pragma: no-cache
	< Cache-Control: no-cache, max-age=0, must-revalidate
	< Content-Type: application/x-git-upload-pack-advertisement
	> POST /smart/repo.git/git-upload-pack HTTP/1.1
	> Accept-Encoding: ENCODINGS
	> Content-Type: application/x-git-upload-pack-request
	> Accept: application/x-git-upload-pack-result
	> Content-Length: xxx
	< HTTP/1.1 200 OK
	< Pragma: no-cache
	< Cache-Control: no-cache, max-age=0, must-revalidate
	< Content-Type: application/x-git-upload-pack-result
	EOF
	GIT_TRACE_CURL=true GIT_TEST_PROTOCOL_VERSION=0 \
		git clone --quiet $HTTPD_URL/smart/repo.git clone 2>err &&
	test_cmp file clone/file &&
	tr '\''\015'\'' Q <err |
	sed -e "
		s/Q\$//
		/^[*] /d
		/^== Info:/d
		/^=> Send header, /d
		/^=> Send header:$/d
		/^<= Recv header, /d
		/^<= Recv header:$/d
		s/=> Send header: //
		s/= Recv header://
		/^<= Recv data/d
		/^=> Send data/d
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
	" >actual &&

	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$GIT_TEST_PROTOCOL_VERSION" = 0
	then
		sed -e "s/^> Accept-Encoding: .*/> Accept-Encoding: ENCODINGS/" \
				actual >actual.smudged &&
		test_cmp exp actual.smudged &&

		grep "Accept-Encoding:.*gzip" actual >actual.gzip &&
		test_line_count = 2 actual.gzip
	fi
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	git commit -a -m two &&
	git push public &&
	(cd clone && git pull) &&
	test_cmp file clone/file
'

test_expect_success 'used upload-pack service' '
	cat >exp <<-\EOF &&
	GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
	POST /smart/repo.git/git-upload-pack HTTP/1.1 200
	GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
	POST /smart/repo.git/git-upload-pack HTTP/1.1 200
	EOF

	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$GIT_TEST_PROTOCOL_VERSION" = 0
	then
		check_access_log exp
	fi
'

test_expect_success 'follow redirects (301)' '
	git clone $HTTPD_URL/smart-redir-perm/repo.git --quiet repo-p
'

test_expect_success 'follow redirects (302)' '
	git clone $HTTPD_URL/smart-redir-temp/repo.git --quiet repo-t
'

test_expect_success 'redirects re-root further requests' '
	git clone $HTTPD_URL/smart-redir-limited/repo.git repo-redir-limited
'

test_expect_success 're-rooting dies on insane schemes' '
	test_must_fail git clone $HTTPD_URL/insane-redir/repo.git insane
'

test_expect_success 'clone from password-protected repository' '
	echo two >expect &&
	set_askpass user@host pass@host &&
	git clone --bare "$HTTPD_URL/auth/smart/repo.git" smart-auth &&
	expect_askpass both user@host &&
	git --git-dir=smart-auth log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'clone from auth-only-for-push repository' '
	echo two >expect &&
	set_askpass wrong &&
	git clone --bare "$HTTPD_URL/auth-push/smart/repo.git" smart-noauth &&
	expect_askpass none &&
	git --git-dir=smart-noauth log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'clone from auth-only-for-objects repository' '
	echo two >expect &&
	set_askpass user@host pass@host &&
	git clone --bare "$HTTPD_URL/auth-fetch/smart/repo.git" half-auth &&
	expect_askpass both user@host &&
	git --git-dir=half-auth log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'no-op half-auth fetch does not require a password' '
	set_askpass wrong &&

	# NEEDSWORK: When using HTTP(S), protocol v0 supports a "half-auth"
	# configuration with authentication required only when downloading
	# objects and not refs, by having the HTTP server only require
	# authentication for the "git-upload-pack" path and not "info/refs".
	# This is not possible with protocol v2, since both objects and refs
	# are obtained from the "git-upload-pack" path. A solution to this is
	# to teach the server and client to be able to inline ls-refs requests
	# as an Extra Parameter (see pack-protocol.txt), so that "info/refs"
	# can serve refs, just like it does in protocol v0.
	GIT_TEST_PROTOCOL_VERSION=0 git --git-dir=half-auth fetch &&
	expect_askpass none
'

test_expect_success 'redirects send auth to new location' '
	set_askpass user@host pass@host &&
	git -c credential.useHttpPath=true \
	  clone $HTTPD_URL/smart-redir-auth/repo.git repo-redir-auth &&
	expect_askpass both user@host auth/smart/repo.git
'

test_expect_success 'GIT_TRACE_CURL redacts auth details' '
	rm -rf redact-auth trace &&
	set_askpass user@host pass@host &&
	GIT_TRACE_CURL="$(pwd)/trace" git clone --bare "$HTTPD_URL/auth/smart/repo.git" redact-auth &&
	expect_askpass both user@host &&

	# Ensure that there is no "Basic" followed by a base64 string, but that
	# the auth details are redacted
	! grep "Authorization: Basic [0-9a-zA-Z+/]" trace &&
	grep "Authorization: Basic <redacted>" trace
'

test_expect_success 'GIT_CURL_VERBOSE redacts auth details' '
	rm -rf redact-auth trace &&
	set_askpass user@host pass@host &&
	GIT_CURL_VERBOSE=1 git clone --bare "$HTTPD_URL/auth/smart/repo.git" redact-auth 2>trace &&
	expect_askpass both user@host &&

	# Ensure that there is no "Basic" followed by a base64 string, but that
	# the auth details are redacted
	! grep "Authorization: Basic [0-9a-zA-Z+/]" trace &&
	grep "Authorization: Basic <redacted>" trace
'

test_expect_success 'GIT_TRACE_CURL does not redact auth details if GIT_TRACE_REDACT=0' '
	rm -rf redact-auth trace &&
	set_askpass user@host pass@host &&
	GIT_TRACE_REDACT=0 GIT_TRACE_CURL="$(pwd)/trace" \
		git clone --bare "$HTTPD_URL/auth/smart/repo.git" redact-auth &&
	expect_askpass both user@host &&

	grep "Authorization: Basic [0-9a-zA-Z+/]" trace
'

test_expect_success 'disable dumb http on server' '
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
		config http.getanyfile false
'

test_expect_success 'GIT_SMART_HTTP can disable smart http' '
	(GIT_SMART_HTTP=0 &&
	 export GIT_SMART_HTTP &&
	 cd clone &&
	 test_must_fail git fetch)
'

test_expect_success 'invalid Content-Type rejected' '
	test_must_fail git clone $HTTPD_URL/broken_smart/repo.git 2>actual &&
	test_i18ngrep "not valid:" actual
'

test_expect_success 'create namespaced refs' '
	test_commit namespaced &&
	git push public HEAD:refs/namespaces/ns/refs/heads/main &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
		symbolic-ref refs/namespaces/ns/HEAD refs/namespaces/ns/refs/heads/main
'

test_expect_success 'smart clone respects namespace' '
	git clone "$HTTPD_URL/smart_namespace/repo.git" ns-smart &&
	echo namespaced >expect &&
	git --git-dir=ns-smart/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'dumb clone via http-backend respects namespace' '
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
		config http.getanyfile true &&
	GIT_SMART_HTTP=0 git clone \
		"$HTTPD_URL/smart_namespace/repo.git" ns-dumb &&
	echo namespaced >expect &&
	git --git-dir=ns-dumb/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'cookies stored in http.cookiefile when http.savecookies set' '
	cat >cookies.txt <<-\EOF &&
	127.0.0.1	FALSE	/smart_cookies/	FALSE	0	othername	othervalue
	EOF
	sort >expect_cookies.txt <<-\EOF &&

	127.0.0.1	FALSE	/smart_cookies/	FALSE	0	othername	othervalue
	127.0.0.1	FALSE	/smart_cookies/repo.git/info/	FALSE	0	name	value
	EOF
	git config http.cookiefile cookies.txt &&
	git config http.savecookies true &&
	git ls-remote $HTTPD_URL/smart_cookies/repo.git main &&

	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$GIT_TEST_PROTOCOL_VERSION" = 0
	then
		tail -3 cookies.txt | sort >cookies_tail.txt &&
		test_cmp expect_cookies.txt cookies_tail.txt
	fi
'

test_expect_success 'transfer.hiderefs works over smart-http' '
	test_commit hidden &&
	test_commit visible &&
	git push public HEAD^:refs/heads/a HEAD:refs/heads/b &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
		config transfer.hiderefs refs/heads/a &&
	git clone --bare "$HTTPD_URL/smart/repo.git" hidden.git &&
	test_must_fail git -C hidden.git rev-parse --verify a &&
	git -C hidden.git rev-parse --verify b
'

# create an arbitrary number of tags, numbered from tag-$1 to tag-$2
create_tags () {
	rm -f marks &&
	for i in $(test_seq "$1" "$2")
	do
		# don't use here-doc, because it requires a process
		# per loop iteration
		echo "commit refs/heads/too-many-refs-$1" &&
		echo "mark :$i" &&
		echo "committer git <git@example.com> $i +0000" &&
		echo "data 0" &&
		echo "M 644 inline bla.txt" &&
		echo "data 4" &&
		echo "bla" &&
		# make every commit dangling by always
		# rewinding the branch after each commit
		echo "reset refs/heads/too-many-refs-$1" &&
		echo "from :$1"
	done | git fast-import --export-marks=marks &&

	# now assign tags to all the dangling commits we created above
	tag=$(perl -e "print \"bla\" x 30") &&
	sed -e "s|^:\([^ ]*\) \(.*\)$|\2 refs/tags/$tag-\1|" <marks >>packed-refs
}

test_expect_success 'create 2,000 tags in the repo' '
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
		create_tags 1 2000
	)
'

test_expect_success CMDLINE_LIMIT \
	'clone the 2,000 tag repo to check OS command line overflow' '
	run_with_limited_cmdline git clone $HTTPD_URL/smart/repo.git too-many-refs &&
	(
		cd too-many-refs &&
		git for-each-ref refs/tags >actual &&
		test_line_count = 2000 actual
	)
'

test_expect_success 'large fetch-pack requests can be sent using chunked encoding' '
	GIT_TRACE_CURL=true git -c http.postbuffer=65536 \
		clone --bare "$HTTPD_URL/smart/repo.git" split.git 2>err &&
	grep "^=> Send header: Transfer-Encoding: chunked" err
'

test_expect_success 'test allowreachablesha1inwant' '
	test_when_finished "rm -rf test_reachable.git" &&
	server="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	main_sha=$(git -C "$server" rev-parse refs/heads/main) &&
	git -C "$server" config uploadpack.allowreachablesha1inwant 1 &&

	git init --bare test_reachable.git &&
	git -C test_reachable.git remote add origin "$HTTPD_URL/smart/repo.git" &&
	git -C test_reachable.git fetch origin "$main_sha"
'

test_expect_success 'test allowreachablesha1inwant with unreachable' '
	test_when_finished "rm -rf test_reachable.git; git reset --hard $(git rev-parse HEAD)" &&

	#create unreachable sha
	echo content >file2 &&
	git add file2 &&
	git commit -m two &&
	git push public HEAD:refs/heads/doomed &&
	git push public :refs/heads/doomed &&

	server="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	main_sha=$(git -C "$server" rev-parse refs/heads/main) &&
	git -C "$server" config uploadpack.allowreachablesha1inwant 1 &&

	git init --bare test_reachable.git &&
	git -C test_reachable.git remote add origin "$HTTPD_URL/smart/repo.git" &&
	# Some protocol versions (e.g. 2) support fetching
	# unadvertised objects, so restrict this test to v0.
	test_must_fail env GIT_TEST_PROTOCOL_VERSION=0 \
		git -C test_reachable.git fetch origin "$(git rev-parse HEAD)"
'

test_expect_success 'test allowanysha1inwant with unreachable' '
	test_when_finished "rm -rf test_reachable.git; git reset --hard $(git rev-parse HEAD)" &&

	#create unreachable sha
	echo content >file2 &&
	git add file2 &&
	git commit -m two &&
	git push public HEAD:refs/heads/doomed &&
	git push public :refs/heads/doomed &&

	server="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	main_sha=$(git -C "$server" rev-parse refs/heads/main) &&
	git -C "$server" config uploadpack.allowreachablesha1inwant 1 &&

	git init --bare test_reachable.git &&
	git -C test_reachable.git remote add origin "$HTTPD_URL/smart/repo.git" &&
	# Some protocol versions (e.g. 2) support fetching
	# unadvertised objects, so restrict this test to v0.
	test_must_fail env GIT_TEST_PROTOCOL_VERSION=0 \
		git -C test_reachable.git fetch origin "$(git rev-parse HEAD)" &&

	git -C "$server" config uploadpack.allowanysha1inwant 1 &&
	git -C test_reachable.git fetch origin "$(git rev-parse HEAD)"
'

test_expect_success EXPENSIVE 'http can handle enormous ref negotiation' '
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
		create_tags 2001 50000
	) &&
	git -C too-many-refs fetch -q --tags &&
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
		create_tags 50001 100000
	) &&
	git -C too-many-refs fetch -q --tags &&
	git -C too-many-refs for-each-ref refs/tags >tags &&
	test_line_count = 100000 tags
'

test_expect_success 'custom http headers' '
	test_must_fail git -c http.extraheader="x-magic-two: cadabra" \
		fetch "$HTTPD_URL/smart_headers/repo.git" &&
	git -c http.extraheader="x-magic-one: abra" \
	    -c http.extraheader="x-magic-two: cadabra" \
	    fetch "$HTTPD_URL/smart_headers/repo.git" &&
	git update-index --add --cacheinfo 160000,$(git rev-parse HEAD),sub &&
	git config -f .gitmodules submodule.sub.path sub &&
	git config -f .gitmodules submodule.sub.url \
		"$HTTPD_URL/smart_headers/repo.git" &&
	git submodule init sub &&
	test_must_fail git submodule update sub &&
	git -c http.extraheader="x-magic-one: abra" \
	    -c http.extraheader="x-magic-two: cadabra" \
		submodule update sub
'

test_expect_success 'using fetch command in remote-curl updates refs' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/twobranch" &&
	rm -rf "$SERVER" client &&

	git init "$SERVER" &&
	test_commit -C "$SERVER" foo &&
	git -C "$SERVER" update-ref refs/heads/anotherbranch foo &&

	git clone $HTTPD_URL/smart/twobranch client &&

	test_commit -C "$SERVER" bar &&
	git -C client -c protocol.version=0 fetch &&

	git -C "$SERVER" rev-parse main >expect &&
	git -C client rev-parse origin/main >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch by SHA-1 without tag following' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	rm -rf "$SERVER" client &&

	git init "$SERVER" &&
	test_commit -C "$SERVER" foo &&

	git clone $HTTPD_URL/smart/server client &&

	test_commit -C "$SERVER" bar &&
	git -C "$SERVER" rev-parse bar >bar_hash &&
	git -C client -c protocol.version=0 fetch \
		--no-tags origin $(cat bar_hash)
'

test_expect_success 'cookies are redacted by default' '
	rm -rf clone &&
	echo "Set-Cookie: Foo=1" >cookies &&
	echo "Set-Cookie: Bar=2" >>cookies &&
	GIT_TRACE_CURL=true \
		git -c "http.cookieFile=$(pwd)/cookies" clone \
		$HTTPD_URL/smart/repo.git clone 2>err &&
	grep "Cookie:.*Foo=<redacted>" err &&
	grep "Cookie:.*Bar=<redacted>" err &&
	! grep "Cookie:.*Foo=1" err &&
	! grep "Cookie:.*Bar=2" err
'

test_expect_success 'empty values of cookies are also redacted' '
	rm -rf clone &&
	echo "Set-Cookie: Foo=" >cookies &&
	GIT_TRACE_CURL=true \
		git -c "http.cookieFile=$(pwd)/cookies" clone \
		$HTTPD_URL/smart/repo.git clone 2>err &&
	grep "Cookie:.*Foo=<redacted>" err
'

test_expect_success 'GIT_TRACE_REDACT=0 disables cookie redaction' '
	rm -rf clone &&
	echo "Set-Cookie: Foo=1" >cookies &&
	echo "Set-Cookie: Bar=2" >>cookies &&
	GIT_TRACE_REDACT=0 GIT_TRACE_CURL=true \
		git -c "http.cookieFile=$(pwd)/cookies" clone \
		$HTTPD_URL/smart/repo.git clone 2>err &&
	grep "Cookie:.*Foo=1" err &&
	grep "Cookie:.*Bar=2" err
'

test_expect_success 'GIT_TRACE_CURL_NO_DATA prevents data from being traced' '
	rm -rf clone &&
	GIT_TRACE_CURL=true \
		git clone $HTTPD_URL/smart/repo.git clone 2>err &&
	grep "=> Send data" err &&

	rm -rf clone &&
	GIT_TRACE_CURL=true GIT_TRACE_CURL_NO_DATA=1 \
		git clone $HTTPD_URL/smart/repo.git clone 2>err &&
	! grep "=> Send data" err
'

test_expect_success 'server-side error detected' '
	test_must_fail git clone $HTTPD_URL/error_smart/repo.git 2>actual &&
	test_i18ngrep "server-side error" actual
'

test_expect_success 'http auth remembers successful credentials' '
	rm -f .git-credentials &&
	test_config credential.helper store &&

	# the first request prompts the user...
	set_askpass user@host pass@host &&
	git ls-remote "$HTTPD_URL/auth/smart/repo.git" >/dev/null &&
	expect_askpass both user@host &&

	# ...and the second one uses the stored value rather than
	# prompting the user.
	set_askpass bogus-user bogus-pass &&
	git ls-remote "$HTTPD_URL/auth/smart/repo.git" >/dev/null &&
	expect_askpass none
'

test_expect_success 'http auth forgets bogus credentials' '
	# seed credential store with bogus values. In real life,
	# this would probably come from a password which worked
	# for a previous request.
	rm -f .git-credentials &&
	test_config credential.helper store &&
	{
		echo "url=$HTTPD_URL" &&
		echo "username=bogus" &&
		echo "password=bogus"
	} | git credential approve &&

	# we expect this to use the bogus values and fail, never even
	# prompting the user...
	set_askpass user@host pass@host &&
	test_must_fail git ls-remote "$HTTPD_URL/auth/smart/repo.git" >/dev/null &&
	expect_askpass none &&

	# ...but now we should have forgotten the bad value, causing
	# us to prompt the user again.
	set_askpass user@host pass@host &&
	git ls-remote "$HTTPD_URL/auth/smart/repo.git" >/dev/null &&
	expect_askpass both user@host
'

test_expect_success 'client falls back from v2 to v0 to match server' '
	GIT_TRACE_PACKET=$PWD/trace \
	GIT_TEST_PROTOCOL_VERSION=2 \
	git clone $HTTPD_URL/smart_v0/repo.git repo-v0 &&
	# check for v0; there the HEAD symref is communicated in the capability
	# line; v2 uses a different syntax on each ref advertisement line
	grep symref=HEAD:refs/heads/ trace
'

test_done
