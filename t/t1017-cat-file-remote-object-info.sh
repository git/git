#!/bin/sh

test_description='git cat-file --batch-command with remote-object-info command'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-cat-file.sh

hello_content="Hello World"
hello_size=$(strlen "$hello_content")
hello_oid=$(echo_without_newline "$hello_content" | git hash-object --stdin)

# This is how we get 13:
# 13 = <file mode> + <a_space> + <file name> + <a_null>, where
# file mode is 100644, which is 6 characters;
# file name is hello, which is 5 characters
# a space is 1 character and a null is 1 character
tree_size=$(($(test_oid rawsz) + 13))

commit_message="Initial commit"

# This is how we get 137:
# 137 = <tree header> + <a_space> + <a newline> +
# <Author line> + <a newline> +
# <Committer line> + <a newline> +
# <a newline> +
# <commit message length>
# An easier way to calculate is: 1. use `git cat-file commit <commit hash> | wc -c`,
# to get 177, 2. then deduct 40 hex characters to get 137
commit_size=$(($(test_oid hexsz) + 137))

tag_header_without_oid="type blob
tag hellotag
tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
tag_header_without_timestamp="object $hello_oid
$tag_header_without_oid"
tag_description="This is a tag"
tag_content="$tag_header_without_timestamp 0 +0000

$tag_description"

tag_oid=$(echo_without_newline "$tag_content" | git hash-object -t tag --stdin -w)
tag_size=$(strlen "$tag_content")

set_transport_variables () {
	hello_oid=$(echo_without_newline "$hello_content" | git hash-object --stdin)
	tree_oid=$(git -C "$1" write-tree)
	commit_oid=$(echo_without_newline "$commit_message" | git -C "$1" commit-tree $tree_oid)
	tag_oid=$(echo_without_newline "$tag_content" | git -C "$1" hash-object -t tag --stdin -w)
	tag_size=$(strlen "$tag_content")
}

# This section tests --batch-command with remote-object-info command
# Since "%(objecttype)" is currently not supported by the command remote-object-info ,
# the filters are set to "%(objectname) %(objectsize)" in some test cases.

# Test --batch-command remote-object-info with 'git://' transport with
# transfer.advertiseobjectinfo set to true, i.e. server has object-info capability
. "$TEST_DIRECTORY"/lib-git-daemon.sh
start_git_daemon --export-all --enable=receive-pack
daemon_parent=$GIT_DAEMON_DOCUMENT_ROOT_PATH/parent

test_expect_success 'create repo to be served by git-daemon' '
	git init "$daemon_parent" &&
	echo_without_newline "$hello_content" > $daemon_parent/hello &&
	git -C "$daemon_parent" update-index --add hello &&
	git -C "$daemon_parent" config transfer.advertiseobjectinfo true &&
	git clone "$GIT_DAEMON_URL/parent" -n "$daemon_parent/daemon_client_empty"
'

test_expect_success 'batch-command remote-object-info git://' '
	(
		set_transport_variables "$daemon_parent" &&
		cd "$daemon_parent/daemon_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "$GIT_DAEMON_URL/parent" $hello_oid
		remote-object-info "$GIT_DAEMON_URL/parent" $tree_oid
		remote-object-info "$GIT_DAEMON_URL/parent" $commit_oid
		remote-object-info "$GIT_DAEMON_URL/parent" $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info git:// multiple sha1 per line' '
	(
		set_transport_variables "$daemon_parent" &&
		cd "$daemon_parent/daemon_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "$GIT_DAEMON_URL/parent" $hello_oid $tree_oid $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info git:// default filter' '
	(
		set_transport_variables "$daemon_parent" &&
		cd "$daemon_parent/daemon_client_empty" &&

		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&
		GIT_TRACE_PACKET=1 git cat-file --batch-command >actual <<-EOF &&
		remote-object-info "$GIT_DAEMON_URL/parent" $hello_oid $tree_oid
		remote-object-info "$GIT_DAEMON_URL/parent" $commit_oid $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command --buffer remote-object-info git://' '
	(
		set_transport_variables "$daemon_parent" &&
		cd "$daemon_parent/daemon_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" --buffer >actual <<-EOF &&
		remote-object-info "$GIT_DAEMON_URL/parent" $hello_oid $tree_oid
		remote-object-info "$GIT_DAEMON_URL/parent" $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		flush
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command -Z remote-object-info git:// default filter' '
	(
		set_transport_variables "$daemon_parent" &&
		cd "$daemon_parent/daemon_client_empty" &&

		printf "%s\0" "$hello_oid $hello_size" >expect &&
		printf "%s\0" "$tree_oid $tree_size" >>expect &&
		printf "%s\0" "$commit_oid $commit_size" >>expect &&
		printf "%s\0" "$tag_oid $tag_size" >>expect &&

		printf "%s\0" "$hello_oid missing" >>expect &&
		printf "%s\0" "$tree_oid missing" >>expect &&
		printf "%s\0" "$commit_oid missing" >>expect &&
		printf "%s\0" "$tag_oid missing" >>expect &&

		batch_input="remote-object-info $GIT_DAEMON_URL/parent $hello_oid $tree_oid
remote-object-info $GIT_DAEMON_URL/parent $commit_oid $tag_oid
info $hello_oid
info $tree_oid
info $commit_oid
info $tag_oid
" &&
		echo_without_newline_nul "$batch_input" >commands_null_delimited &&

		git cat-file --batch-command -Z < commands_null_delimited >actual &&
		test_cmp expect actual
	)
'

# Test --batch-command remote-object-info with 'git://' and
# transfer.advertiseobjectinfo set to false, i.e. server does not have object-info capability
test_expect_success 'batch-command remote-object-info git:// fails when transfer.advertiseobjectinfo=false' '
	(
		git -C "$daemon_parent" config transfer.advertiseobjectinfo false &&
		set_transport_variables "$daemon_parent" &&

		test_must_fail git cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info $GIT_DAEMON_URL/parent $hello_oid $tree_oid $commit_oid $tag_oid
		EOF
		test_grep "object-info capability is not enabled on the server" err &&

		# revert server state back
		git -C "$daemon_parent" config transfer.advertiseobjectinfo true

	)
'

stop_git_daemon

# Test --batch-command remote-object-info with 'file://' transport with
# transfer.advertiseobjectinfo set to true, i.e. server has object-info capability
# shellcheck disable=SC2016
test_expect_success 'create repo to be served by file:// transport' '
	git init server &&
	git -C server config protocol.version 2 &&
	git -C server config transfer.advertiseobjectinfo true &&
	echo_without_newline "$hello_content" > server/hello &&
	git -C server update-index --add hello &&
	git clone -n "file://$(pwd)/server" file_client_empty
'

test_expect_success 'batch-command remote-object-info file://' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		cd file_client_empty &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "file://${server_path}" $hello_oid
		remote-object-info "file://${server_path}" $tree_oid
		remote-object-info "file://${server_path}" $commit_oid
		remote-object-info "file://${server_path}" $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info file:// multiple sha1 per line' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		cd file_client_empty &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&


		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "file://${server_path}" $hello_oid $tree_oid $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command --buffer remote-object-info file://' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		cd file_client_empty &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" --buffer >actual <<-EOF &&
		remote-object-info "file://${server_path}" $hello_oid $tree_oid
		remote-object-info "file://${server_path}" $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		flush
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info file:// default filter' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		cd file_client_empty &&

		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		git cat-file --batch-command >actual <<-EOF &&
		remote-object-info "file://${server_path}" $hello_oid $tree_oid
		remote-object-info "file://${server_path}" $commit_oid $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command -Z remote-object-info file:// default filter' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		cd file_client_empty &&

		printf "%s\0" "$hello_oid $hello_size" >expect &&
		printf "%s\0" "$tree_oid $tree_size" >>expect &&
		printf "%s\0" "$commit_oid $commit_size" >>expect &&
		printf "%s\0" "$tag_oid $tag_size" >>expect &&

		printf "%s\0" "$hello_oid missing" >>expect &&
		printf "%s\0" "$tree_oid missing" >>expect &&
		printf "%s\0" "$commit_oid missing" >>expect &&
		printf "%s\0" "$tag_oid missing" >>expect &&

		batch_input="remote-object-info \"file://${server_path}\" $hello_oid $tree_oid
remote-object-info \"file://${server_path}\" $commit_oid $tag_oid
info $hello_oid
info $tree_oid
info $commit_oid
info $tag_oid
" &&
		echo_without_newline_nul "$batch_input" >commands_null_delimited &&

		git cat-file --batch-command -Z < commands_null_delimited >actual &&
		test_cmp expect actual
	)
'

# Test --batch-command remote-object-info with 'file://' and
# transfer.advertiseobjectinfo set to false, i.e. server does not have object-info capability
test_expect_success 'batch-command remote-object-info file:// fails when transfer.advertiseobjectinfo=false' '
	(
		set_transport_variables "server" &&
		server_path="$(pwd)/server" &&
		git -C "${server_path}" config transfer.advertiseobjectinfo false &&

		test_must_fail git cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info "file://${server_path}" $hello_oid $tree_oid $commit_oid $tag_oid
		EOF
		test_grep "object-info capability is not enabled on the server" err &&

		# revert server state back
		git -C "${server_path}" config transfer.advertiseobjectinfo true
	)
'

# Test --batch-command remote-object-info with 'http://' transport with
# transfer.advertiseobjectinfo set to true, i.e. server has object-info capability

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create repo to be served by http:// transport' '
	git init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config http.receivepack true &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config transfer.advertiseobjectinfo true &&
	echo_without_newline "$hello_content" > $HTTPD_DOCUMENT_ROOT_PATH/http_parent/hello &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" update-index --add hello &&
	git clone "$HTTPD_URL/smart/http_parent" -n "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty"
'

test_expect_success 'batch-command remote-object-info http://' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid
		remote-object-info "$HTTPD_URL/smart/http_parent" $tree_oid
		remote-object-info "$HTTPD_URL/smart/http_parent" $commit_oid
		remote-object-info "$HTTPD_URL/smart/http_parent" $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info http:// one line' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" >actual <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid $tree_oid $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command --buffer remote-object-info http://' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty" &&

		# These results prove remote-object-info can get object info from the remote
		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		# These results prove remote-object-info did not download objects from the remote
		echo "$hello_oid missing" >>expect &&
		echo "$tree_oid missing" >>expect &&
		echo "$commit_oid missing" >>expect &&
		echo "$tag_oid missing" >>expect &&

		git cat-file --batch-command="%(objectname) %(objectsize)" --buffer >actual <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid $tree_oid
		remote-object-info "$HTTPD_URL/smart/http_parent" $commit_oid $tag_oid
		info $hello_oid
		info $tree_oid
		info $commit_oid
		info $tag_oid
		flush
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command remote-object-info http:// default filter' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty" &&

		echo "$hello_oid $hello_size" >expect &&
		echo "$tree_oid $tree_size" >>expect &&
		echo "$commit_oid $commit_size" >>expect &&
		echo "$tag_oid $tag_size" >>expect &&

		git cat-file --batch-command >actual <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid $tree_oid
		remote-object-info "$HTTPD_URL/smart/http_parent" $commit_oid $tag_oid
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'batch-command -Z remote-object-info http:// default filter' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_client_empty" &&

		printf "%s\0" "$hello_oid $hello_size" >expect &&
		printf "%s\0" "$tree_oid $tree_size" >>expect &&
		printf "%s\0" "$commit_oid $commit_size" >>expect &&
		printf "%s\0" "$tag_oid $tag_size" >>expect &&

		batch_input="remote-object-info $HTTPD_URL/smart/http_parent $hello_oid $tree_oid
remote-object-info $HTTPD_URL/smart/http_parent $commit_oid $tag_oid
" &&
		echo_without_newline_nul "$batch_input" >commands_null_delimited &&

		git cat-file --batch-command -Z < commands_null_delimited >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'remote-object-info fails on unspported filter option (objectsize:disk)' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&

		test_must_fail git cat-file --batch-command="%(objectsize:disk)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid
		EOF
		test_grep "%(objectsize:disk) is currently not supported with remote-object-info" err
	)
'

test_expect_success 'remote-object-info fails on unspported filter option (deltabase)' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&

		test_must_fail git cat-file --batch-command="%(deltabase)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid
		EOF
		test_grep "%(deltabase) is currently not supported with remote-object-info" err
	)
'

test_expect_success 'remote-object-info fails on server with legacy protocol' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&

		test_must_fail git -c protocol.version=0 cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid
		EOF
		test_grep "remote-object-info requires protocol v2" err
	)
'

test_expect_success 'remote-object-info fails on server with legacy protocol' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&

		test_must_fail git -c protocol.version=0 cat-file --batch-command 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid
		EOF
		test_grep "remote-object-info requires protocol v2" err
	)
'

test_expect_success 'remote-object-info fails on malformed OID' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		malformed_object_id="this_id_is_not_valid" &&

		test_must_fail git cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $malformed_object_id
		EOF
		test_grep "Not a valid object name '$malformed_object_id'" err
	)
'

test_expect_success 'remote-object-info fails on malformed OID with default filter' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		cd "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		malformed_object_id="this_id_is_not_valid" &&

		test_must_fail git cat-file --batch-command 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $malformed_object_id
		EOF
		test_grep "Not a valid object name '$malformed_object_id'" err
	)
'

test_expect_success 'remote-object-info fails on missing OID' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		git clone "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" missing_oid_repo &&
		test_commit -C missing_oid_repo message1 c.txt &&
		cd missing_oid_repo &&

		object_id=$(git rev-parse message1:c.txt) &&
		test_must_fail git cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $object_id
		EOF
		test_grep "object-info: not our ref $object_id" err
	)
'


# Test --batch-command remote-object-info with 'http://' transport and
# transfer.advertiseobjectinfo set to false, i.e. server does not have object-info capability
test_expect_success 'batch-command remote-object-info http:// fails when transfer.advertiseobjectinfo=false ' '
	(
		set_transport_variables "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
		git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config transfer.advertiseobjectinfo false &&

		test_must_fail git cat-file --batch-command="%(objectname) %(objectsize)" 2>err <<-EOF &&
		remote-object-info "$HTTPD_URL/smart/http_parent" $hello_oid $tree_oid $commit_oid $tag_oid
		EOF
		test_grep "object-info capability is not enabled on the server" err &&

		# revert server state back
		git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config transfer.advertiseobjectinfo true
	)
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
