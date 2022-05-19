#!/bin/sh

test_description='test but-http-backend'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

if ! test_have_prereq CURL; then
	skip_all='skipping raw http-backend tests, curl not available'
	test_done
fi

start_httpd

GET() {
	curl --include "$HTTPD_URL/$SMART/repo.but/$1" >out &&
	tr '\015' Q <out |
	sed '
		s/Q$//
		1q
	' >act &&
	echo "HTTP/1.1 $2" >exp &&
	test_cmp exp act
}

POST() {
	curl --include --data "$2" \
	--header "Content-Type: application/x-$1-request" \
	"$HTTPD_URL/smart/repo.but/$1" >out &&
	tr '\015' Q <out |
	sed '
		s/Q$//
		1q
	' >act &&
	echo "HTTP/1.1 $3" >exp &&
	test_cmp exp act
}

. "$TEST_DIRECTORY"/t556x_common

grep '^[^#]' >exp <<EOF

###  refs/heads/main
###
GET  /smart/repo.but/refs/heads/main HTTP/1.1 404 -

###  getanyfile default
###
GET  /smart/repo.but/HEAD HTTP/1.1 200
GET  /smart/repo.but/info/refs HTTP/1.1 200
GET  /smart/repo.but/objects/info/packs HTTP/1.1 200
GET  /smart/repo.but/objects/info/alternates HTTP/1.1 200 -
GET  /smart/repo.but/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart/repo.but/$LOOSE_URL HTTP/1.1 200
GET  /smart/repo.but/$PACK_URL HTTP/1.1 200
GET  /smart/repo.but/$IDX_URL HTTP/1.1 200

###  no but-daemon-export-ok
###
GET  /smart_noexport/repo.but/HEAD HTTP/1.1 404 -
GET  /smart_noexport/repo.but/info/refs HTTP/1.1 404 -
GET  /smart_noexport/repo.but/objects/info/packs HTTP/1.1 404 -
GET  /smart_noexport/repo.but/objects/info/alternates HTTP/1.1 404 -
GET  /smart_noexport/repo.but/objects/info/http-alternates HTTP/1.1 404 -
GET  /smart_noexport/repo.but/$LOOSE_URL HTTP/1.1 404 -
GET  /smart_noexport/repo.but/$PACK_URL HTTP/1.1 404 -
GET  /smart_noexport/repo.but/$IDX_URL HTTP/1.1 404 -

###  but-daemon-export-ok
###
GET  /smart_noexport/repo.but/HEAD HTTP/1.1 200
GET  /smart_noexport/repo.but/info/refs HTTP/1.1 200
GET  /smart_noexport/repo.but/objects/info/packs HTTP/1.1 200
GET  /smart_noexport/repo.but/objects/info/alternates HTTP/1.1 200 -
GET  /smart_noexport/repo.but/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart_noexport/repo.but/$LOOSE_URL HTTP/1.1 200
GET  /smart_noexport/repo.but/$PACK_URL HTTP/1.1 200
GET  /smart_noexport/repo.but/$IDX_URL HTTP/1.1 200

###  getanyfile true
###
GET  /smart/repo.but/HEAD HTTP/1.1 200
GET  /smart/repo.but/info/refs HTTP/1.1 200
GET  /smart/repo.but/objects/info/packs HTTP/1.1 200
GET  /smart/repo.but/objects/info/alternates HTTP/1.1 200 -
GET  /smart/repo.but/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart/repo.but/$LOOSE_URL HTTP/1.1 200
GET  /smart/repo.but/$PACK_URL HTTP/1.1 200
GET  /smart/repo.but/$IDX_URL HTTP/1.1 200

###  getanyfile false
###
GET  /smart/repo.but/HEAD HTTP/1.1 403 -
GET  /smart/repo.but/info/refs HTTP/1.1 403 -
GET  /smart/repo.but/objects/info/packs HTTP/1.1 403 -
GET  /smart/repo.but/objects/info/alternates HTTP/1.1 403 -
GET  /smart/repo.but/objects/info/http-alternates HTTP/1.1 403 -
GET  /smart/repo.but/$LOOSE_URL HTTP/1.1 403 -
GET  /smart/repo.but/$PACK_URL HTTP/1.1 403 -
GET  /smart/repo.but/$IDX_URL HTTP/1.1 403 -

###  uploadpack default
###
GET  /smart/repo.but/info/refs?service=but-upload-pack HTTP/1.1 200
POST /smart/repo.but/but-upload-pack HTTP/1.1 200 -

###  uploadpack true
###
GET  /smart/repo.but/info/refs?service=but-upload-pack HTTP/1.1 200
POST /smart/repo.but/but-upload-pack HTTP/1.1 200 -

###  uploadpack false
###
GET  /smart/repo.but/info/refs?service=but-upload-pack HTTP/1.1 403 -
POST /smart/repo.but/but-upload-pack HTTP/1.1 403 -

###  receivepack default
###
GET  /smart/repo.but/info/refs?service=but-receive-pack HTTP/1.1 403 -
POST /smart/repo.but/but-receive-pack HTTP/1.1 403 -

###  receivepack true
###
GET  /smart/repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
POST /smart/repo.but/but-receive-pack HTTP/1.1 200 -

###  receivepack false
###
GET  /smart/repo.but/info/refs?service=but-receive-pack HTTP/1.1 403 -
POST /smart/repo.but/but-receive-pack HTTP/1.1 403 -
EOF
test_expect_success 'server request log matches test results' '
	check_access_log exp
'

test_done
