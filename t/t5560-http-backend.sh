#!/bin/sh

test_description='test git-http-backend'
. ./test-lib.sh

if test -n "$NO_CURL"; then
	say 'skipping test, git built without http support'
	test_done
fi

LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'5560'}
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

find_file() {
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	find $1 -type f |
	sed -e 1q
}

config() {
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" config $1 $2
}

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

test_expect_success 'setup repository' '
	echo content >file &&
	git add file &&
	git commit -m one &&

	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git --bare init &&
	 : >objects/info/alternates &&
	 : >objects/info/http-alternates
	) &&
	git remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public master:master &&

	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git repack -a -d
	) &&

	echo other >file &&
	git add file &&
	git commit -m two &&
	git push public master:master &&

	LOOSE_URL=$(find_file objects/??) &&
	PACK_URL=$(find_file objects/pack/*.pack) &&
	IDX_URL=$(find_file objects/pack/*.idx)
'

get_static_files() {
	GET HEAD "$1" &&
	GET info/refs "$1" &&
	GET objects/info/packs "$1" &&
	GET objects/info/alternates "$1" &&
	GET objects/info/http-alternates "$1" &&
	GET $LOOSE_URL "$1" &&
	GET $PACK_URL "$1" &&
	GET $IDX_URL "$1"
}

SMART=smart
test_expect_success 'direct refs/heads/master not found' '
	log_div "refs/heads/master"
	GET refs/heads/master "404 Not Found"
'
test_expect_success 'static file is ok' '
	log_div "getanyfile default"
	get_static_files "200 OK"
'
SMART=smart_noexport
test_expect_success 'no export by default' '
	log_div "no git-daemon-export-ok"
	get_static_files "404 Not Found"
'
test_expect_success 'export if git-daemon-export-ok' '
	log_div "git-daemon-export-ok"
	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 touch git-daemon-export-ok
	) &&
	get_static_files "200 OK"
'
SMART=smart
test_expect_success 'static file if http.getanyfile true is ok' '
	log_div "getanyfile true"
	config http.getanyfile true &&
	get_static_files "200 OK"
'
test_expect_success 'static file if http.getanyfile false fails' '
	log_div "getanyfile false"
	config http.getanyfile false &&
	get_static_files "403 Forbidden"
'

test_expect_success 'http.uploadpack default enabled' '
	log_div "uploadpack default"
	GET info/refs?service=git-upload-pack "200 OK"  &&
	POST git-upload-pack 0000 "200 OK"
'
test_expect_success 'http.uploadpack true' '
	log_div "uploadpack true"
	config http.uploadpack true &&
	GET info/refs?service=git-upload-pack "200 OK" &&
	POST git-upload-pack 0000 "200 OK"
'
test_expect_success 'http.uploadpack false' '
	log_div "uploadpack false"
	config http.uploadpack false &&
	GET info/refs?service=git-upload-pack "403 Forbidden" &&
	POST git-upload-pack 0000 "403 Forbidden"
'

test_expect_success 'http.receivepack default disabled' '
	log_div "receivepack default"
	GET info/refs?service=git-receive-pack "403 Forbidden"  &&
	POST git-receive-pack 0000 "403 Forbidden"
'
test_expect_success 'http.receivepack true' '
	log_div "receivepack true"
	config http.receivepack true &&
	GET info/refs?service=git-receive-pack "200 OK" &&
	POST git-receive-pack 0000 "200 OK"
'
test_expect_success 'http.receivepack false' '
	log_div "receivepack false"
	config http.receivepack false &&
	GET info/refs?service=git-receive-pack "403 Forbidden" &&
	POST git-receive-pack 0000 "403 Forbidden"
'
run_backend() {
	REQUEST_METHOD=GET \
	GIT_PROJECT_ROOT="$HTTPD_DOCUMENT_ROOT_PATH" \
	PATH_INFO="$2" \
	git http-backend >act.out 2>act.err
}

path_info() {
	if test $1 = 0; then
		run_backend "$2"
	else
		test_must_fail run_backend "$2" &&
		echo "fatal: '$2': aliased" >exp.err &&
		test_cmp exp.err act.err
	fi
}

test_expect_success 'http-backend blocks bad PATH_INFO' '
	config http.getanyfile true &&

	run_backend 0 /repo.git/HEAD &&

	run_backend 1 /repo.git/../HEAD &&
	run_backend 1 /../etc/passwd &&
	run_backend 1 ../etc/passwd &&
	run_backend 1 /etc//passwd &&
	run_backend 1 /etc/./passwd &&
	run_backend 1 /etc/.../passwd &&
	run_backend 1 //domain/data.txt
'

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
