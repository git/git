# Set up and run tests of the 'bundle-uri' command in protocol v2
#
# The test that includes this script should set BUNDLE_URI_PROTOCOL
# to one of "file", "git", or "http".

BUNDLE_URI_TEST_PARENT=
BUNDLE_URI_TEST_URI=
BUNDLE_URI_TEST_BUNDLE_URI=
case "$BUNDLE_URI_PROTOCOL" in
file)
	BUNDLE_URI_PARENT=file_parent
	BUNDLE_URI_REPO_URI="file://$PWD/file_parent"
	BUNDLE_URI_BUNDLE_URI="$BUNDLE_URI_REPO_URI/fake.bdl"
	test_set_prereq BUNDLE_URI_FILE
	;;
git)
	. "$TEST_DIRECTORY"/lib-git-daemon.sh
	start_git_daemon --export-all --enable=receive-pack
	BUNDLE_URI_PARENT="$GIT_DAEMON_DOCUMENT_ROOT_PATH/parent"
	BUNDLE_URI_REPO_URI="$GIT_DAEMON_URL/parent"
	BUNDLE_URI_BUNDLE_URI="https://example.com/fake.bdl"
	test_set_prereq BUNDLE_URI_GIT
	;;
http)
	. "$TEST_DIRECTORY"/lib-httpd.sh
	start_httpd
	BUNDLE_URI_PARENT="$HTTPD_DOCUMENT_ROOT_PATH/http_parent"
	BUNDLE_URI_REPO_URI="$HTTPD_URL/smart/http_parent"
	BUNDLE_URI_BUNDLE_URI="https://example.com/fake.bdl"
	test_set_prereq BUNDLE_URI_HTTP
	;;
*)
	BUG "Need to pass valid BUNDLE_URI_PROTOCOL (was \"$BUNDLE_URI_PROTOCOL\")"
	;;
esac

test_expect_success "setup protocol v2 $BUNDLE_URI_PROTOCOL:// tests" '
	git init "$BUNDLE_URI_PARENT" &&
	test_commit -C "$BUNDLE_URI_PARENT" one &&
	git -C "$BUNDLE_URI_PARENT" config uploadpack.advertiseBundleURIs true
'

case "$BUNDLE_URI_PROTOCOL" in
http)
	test_expect_success "setup config for $BUNDLE_URI_PROTOCOL:// tests" '
		git -C "$BUNDLE_URI_PARENT" config http.receivepack true
	'
	;;
*)
	;;
esac
BUNDLE_URI_BUNDLE_URI_ESCAPED=$(echo "$BUNDLE_URI_BUNDLE_URI" | test_uri_escape)

test_expect_success "connect with $BUNDLE_URI_PROTOCOL:// using protocol v2: no bundle-uri" '
	test_when_finished "rm -f log" &&
	test_when_finished "git -C \"$BUNDLE_URI_PARENT\" config uploadpack.advertiseBundleURIs true" &&
	git -C "$BUNDLE_URI_PARENT" config uploadpack.advertiseBundleURIs false &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c protocol.version=2 \
		ls-remote --symref "$BUNDLE_URI_REPO_URI" \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	! grep bundle-uri log
'

test_expect_success "connect with $BUNDLE_URI_PROTOCOL:// using protocol v2: have bundle-uri" '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c protocol.version=2 \
		ls-remote --symref "$BUNDLE_URI_REPO_URI" \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	# Server advertised bundle-uri capability
	grep "< bundle-uri" log
'
