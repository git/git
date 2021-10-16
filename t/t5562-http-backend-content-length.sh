#!/bin/sh

test_description='test git-http-backend respects CONTENT_LENGTH'
. ./test-lib.sh

test_lazy_prereq GZIP 'gzip --version'

verify_http_result() {
	# some fatal errors still produce status 200
	# so check if there is the error message
	if grep 'fatal:' act.err.$test_count
	then
		return 1
	fi

	if ! grep "Status" act.out.$test_count >act
	then
		printf "Status: 200 OK\r\n" >act
	fi
	printf "Status: $1\r\n" >exp &&
	test_cmp exp act
}

test_http_env() {
	handler_type="$1"
	request_body="$2"
	shift
	env \
		CONTENT_TYPE="application/x-git-$handler_type-pack-request" \
		QUERY_STRING="/repo.git/git-$handler_type-pack" \
		PATH_TRANSLATED="$PWD/.git/git-$handler_type-pack" \
		GIT_HTTP_EXPORT_ALL=TRUE \
		REQUEST_METHOD=POST \
		"$PERL_PATH" \
		"$TEST_DIRECTORY"/t5562/invoke-with-content-length.pl \
		    "$request_body" git http-backend >act.out.$test_count 2>act.err.$test_count
}

ssize_b100dots() {
	# hardcoded ((size_t) SSIZE_MAX) + 1
	case "$(build_option sizeof-size_t)" in
	8) echo 9223372036854775808;;
	4) echo 2147483648;;
	*) die "Unexpected ssize_t size: $(build_option sizeof-size_t)";;
	esac
}

test_expect_success 'setup' '
	HTTP_CONTENT_ENCODING="identity" &&
	export HTTP_CONTENT_ENCODING &&
	git config http.receivepack true &&
	test_commit c0 &&
	test_commit c1 &&
	hash_head=$(git rev-parse HEAD) &&
	hash_prev=$(git rev-parse HEAD~1) &&
	{
		packetize "want $hash_head" &&
		printf 0000 &&
		packetize "have $hash_prev" &&
		packetize "done"
	} >fetch_body &&
	test_copy_bytes 10 <fetch_body >fetch_body.trunc &&
	hash_next=$(git commit-tree -p HEAD -m next HEAD^{tree}) &&
	{
		printf "%s %s refs/heads/newbranch\\0report-status object-format=%s\\n" \
			"$ZERO_OID" "$hash_next" "$(test_oid algo)" | packetize_raw
		printf 0000 &&
		echo "$hash_next" | git pack-objects --stdout
	} >push_body &&
	test_copy_bytes 10 <push_body >push_body.trunc &&
	: >empty_body
'

test_expect_success GZIP 'setup, compression related' '
	gzip -c fetch_body >fetch_body.gz &&
	test_copy_bytes 10 <fetch_body.gz >fetch_body.gz.trunc &&
	gzip -c push_body >push_body.gz &&
	test_copy_bytes 10 <push_body.gz >push_body.gz.trunc
'

test_expect_success 'fetch plain' '
	test_http_env upload fetch_body &&
	verify_http_result "200 OK"
'

test_expect_success 'fetch plain truncated' '
	test_http_env upload fetch_body.trunc &&
	! verify_http_result "200 OK"
'

test_expect_success 'fetch plain empty' '
	test_http_env upload empty_body &&
	! verify_http_result "200 OK"
'

test_expect_success GZIP 'fetch gzipped' '
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env upload fetch_body.gz &&
	verify_http_result "200 OK"
'

test_expect_success GZIP 'fetch gzipped truncated' '
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env upload fetch_body.gz.trunc &&
	! verify_http_result "200 OK"
'

test_expect_success GZIP 'fetch gzipped empty' '
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env upload empty_body &&
	! verify_http_result "200 OK"
'

test_expect_success GZIP 'push plain' '
	test_when_finished "git branch -D newbranch" &&
	test_http_env receive push_body &&
	verify_http_result "200 OK" &&
	git rev-parse newbranch >act.head &&
	echo "$hash_next" >exp.head &&
	test_cmp act.head exp.head
'

test_expect_success 'push plain truncated' '
	test_http_env receive push_body.trunc &&
	! verify_http_result "200 OK"
'

test_expect_success 'push plain empty' '
	test_http_env receive empty_body &&
	! verify_http_result "200 OK"
'

test_expect_success GZIP 'push gzipped' '
	test_when_finished "git branch -D newbranch" &&
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env receive push_body.gz &&
	verify_http_result "200 OK" &&
	git rev-parse newbranch >act.head &&
	echo "$hash_next" >exp.head &&
	test_cmp act.head exp.head
'

test_expect_success GZIP 'push gzipped truncated' '
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env receive push_body.gz.trunc &&
	! verify_http_result "200 OK"
'

test_expect_success GZIP 'push gzipped empty' '
	test_env HTTP_CONTENT_ENCODING="gzip" test_http_env receive empty_body &&
	! verify_http_result "200 OK"
'

test_expect_success 'CONTENT_LENGTH overflow ssite_t' '
	NOT_FIT_IN_SSIZE=$(ssize_b100dots) &&
	env \
		CONTENT_TYPE=application/x-git-upload-pack-request \
		QUERY_STRING=/repo.git/git-upload-pack \
		PATH_TRANSLATED="$PWD"/.git/git-upload-pack \
		GIT_HTTP_EXPORT_ALL=TRUE \
		REQUEST_METHOD=POST \
		CONTENT_LENGTH="$NOT_FIT_IN_SSIZE" \
		git http-backend </dev/null >/dev/null 2>err &&
	grep "fatal:.*CONTENT_LENGTH" err
'

test_expect_success 'empty CONTENT_LENGTH' '
	env \
		QUERY_STRING="service=git-receive-pack" \
		PATH_TRANSLATED="$PWD"/.git/info/refs \
		GIT_HTTP_EXPORT_ALL=TRUE \
		REQUEST_METHOD=GET \
		CONTENT_LENGTH="" \
		git http-backend <empty_body >act.out.$test_count 2>act.err.$test_count &&
	verify_http_result "200 OK"
'

test_done
