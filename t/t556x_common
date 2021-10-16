#!/bin/sh

find_file() {
	cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	find $1 -type f |
	sed -e 1q
}

config() {
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" config $1 $2
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
	git push public main:main &&

	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git repack -a -d
	) &&

	echo other >file &&
	git add file &&
	git commit -m two &&
	git push public main:main &&

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
GIT_HTTP_EXPORT_ALL=1 && export GIT_HTTP_EXPORT_ALL
test_expect_success 'direct refs/heads/main not found' '
	GET refs/heads/main "404 Not Found"
'
test_expect_success 'static file is ok' '
	get_static_files "200 OK"
'
SMART=smart_noexport
unset GIT_HTTP_EXPORT_ALL
test_expect_success 'no export by default' '
	get_static_files "404 Not Found"
'
test_expect_success 'export if git-daemon-export-ok' '
        (cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 touch git-daemon-export-ok
	) &&
        get_static_files "200 OK"
'
SMART=smart
GIT_HTTP_EXPORT_ALL=1 && export GIT_HTTP_EXPORT_ALL
test_expect_success 'static file if http.getanyfile true is ok' '
	config http.getanyfile true &&
	get_static_files "200 OK"
'
test_expect_success 'static file if http.getanyfile false fails' '
	config http.getanyfile false &&
	get_static_files "403 Forbidden"
'

test_expect_success 'http.uploadpack default enabled' '
	GET info/refs?service=git-upload-pack "200 OK"  &&
	POST git-upload-pack 0000 "200 OK"
'
test_expect_success 'http.uploadpack true' '
	config http.uploadpack true &&
	GET info/refs?service=git-upload-pack "200 OK" &&
	POST git-upload-pack 0000 "200 OK"
'
test_expect_success 'http.uploadpack false' '
	config http.uploadpack false &&
	GET info/refs?service=git-upload-pack "403 Forbidden" &&
	POST git-upload-pack 0000 "403 Forbidden"
'

test_expect_success 'http.receivepack default disabled' '
	GET info/refs?service=git-receive-pack "403 Forbidden"  &&
	POST git-receive-pack 0000 "403 Forbidden"
'
test_expect_success 'http.receivepack true' '
	config http.receivepack true &&
	GET info/refs?service=git-receive-pack "200 OK" &&
	POST git-receive-pack 0000 "200 OK"
'
test_expect_success 'http.receivepack false' '
	config http.receivepack false &&
	GET info/refs?service=git-receive-pack "403 Forbidden" &&
	POST git-receive-pack 0000 "403 Forbidden"
'
