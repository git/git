#!/bin/sh

test_description='test git-http-backend'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	skip_all='skipping test, git built without http support'
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

GET() {
	curl --include "$HTTPD_URL/$SMART/repo.git/$1" >out 2>/dev/null &&
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
	"$HTTPD_URL/smart/repo.git/$1" >out 2>/dev/null &&
	tr '\015' Q <out |
	sed '
		s/Q$//
		1q
	' >act &&
	echo "HTTP/1.1 $3" >exp &&
	test_cmp exp act
}

log_div() {
	echo >>"$HTTPD_ROOT_PATH"/access.log
	echo "###  $1" >>"$HTTPD_ROOT_PATH"/access.log
	echo "###" >>"$HTTPD_ROOT_PATH"/access.log
}

. "$TEST_DIRECTORY"/t556x_common

cat >exp <<EOF

###  refs/heads/master
###
GET  /smart/repo.git/refs/heads/master HTTP/1.1 404 -

###  getanyfile default
###
GET  /smart/repo.git/HEAD HTTP/1.1 200
GET  /smart/repo.git/info/refs HTTP/1.1 200
GET  /smart/repo.git/objects/info/packs HTTP/1.1 200
GET  /smart/repo.git/objects/info/alternates HTTP/1.1 200 -
GET  /smart/repo.git/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart/repo.git/$LOOSE_URL HTTP/1.1 200
GET  /smart/repo.git/$PACK_URL HTTP/1.1 200
GET  /smart/repo.git/$IDX_URL HTTP/1.1 200

###  no git-daemon-export-ok
###
GET  /smart_noexport/repo.git/HEAD HTTP/1.1 404 -
GET  /smart_noexport/repo.git/info/refs HTTP/1.1 404 -
GET  /smart_noexport/repo.git/objects/info/packs HTTP/1.1 404 -
GET  /smart_noexport/repo.git/objects/info/alternates HTTP/1.1 404 -
GET  /smart_noexport/repo.git/objects/info/http-alternates HTTP/1.1 404 -
GET  /smart_noexport/repo.git/$LOOSE_URL HTTP/1.1 404 -
GET  /smart_noexport/repo.git/$PACK_URL HTTP/1.1 404 -
GET  /smart_noexport/repo.git/$IDX_URL HTTP/1.1 404 -

###  git-daemon-export-ok
###
GET  /smart_noexport/repo.git/HEAD HTTP/1.1 200
GET  /smart_noexport/repo.git/info/refs HTTP/1.1 200
GET  /smart_noexport/repo.git/objects/info/packs HTTP/1.1 200
GET  /smart_noexport/repo.git/objects/info/alternates HTTP/1.1 200 -
GET  /smart_noexport/repo.git/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart_noexport/repo.git/$LOOSE_URL HTTP/1.1 200
GET  /smart_noexport/repo.git/$PACK_URL HTTP/1.1 200
GET  /smart_noexport/repo.git/$IDX_URL HTTP/1.1 200

###  getanyfile true
###
GET  /smart/repo.git/HEAD HTTP/1.1 200
GET  /smart/repo.git/info/refs HTTP/1.1 200
GET  /smart/repo.git/objects/info/packs HTTP/1.1 200
GET  /smart/repo.git/objects/info/alternates HTTP/1.1 200 -
GET  /smart/repo.git/objects/info/http-alternates HTTP/1.1 200 -
GET  /smart/repo.git/$LOOSE_URL HTTP/1.1 200
GET  /smart/repo.git/$PACK_URL HTTP/1.1 200
GET  /smart/repo.git/$IDX_URL HTTP/1.1 200

###  getanyfile false
###
GET  /smart/repo.git/HEAD HTTP/1.1 403 -
GET  /smart/repo.git/info/refs HTTP/1.1 403 -
GET  /smart/repo.git/objects/info/packs HTTP/1.1 403 -
GET  /smart/repo.git/objects/info/alternates HTTP/1.1 403 -
GET  /smart/repo.git/objects/info/http-alternates HTTP/1.1 403 -
GET  /smart/repo.git/$LOOSE_URL HTTP/1.1 403 -
GET  /smart/repo.git/$PACK_URL HTTP/1.1 403 -
GET  /smart/repo.git/$IDX_URL HTTP/1.1 403 -

###  uploadpack default
###
GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/repo.git/git-upload-pack HTTP/1.1 200 -

###  uploadpack true
###
GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/repo.git/git-upload-pack HTTP/1.1 200 -

###  uploadpack false
###
GET  /smart/repo.git/info/refs?service=git-upload-pack HTTP/1.1 403 -
POST /smart/repo.git/git-upload-pack HTTP/1.1 403 -

###  receivepack default
###
GET  /smart/repo.git/info/refs?service=git-receive-pack HTTP/1.1 403 -
POST /smart/repo.git/git-receive-pack HTTP/1.1 403 -

###  receivepack true
###
GET  /smart/repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/repo.git/git-receive-pack HTTP/1.1 200 -

###  receivepack false
###
GET  /smart/repo.git/info/refs?service=git-receive-pack HTTP/1.1 403 -
POST /smart/repo.git/git-receive-pack HTTP/1.1 403 -
EOF
test_expect_success 'server request log matches test results' '
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
