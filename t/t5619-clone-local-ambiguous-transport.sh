#!/bin/sh

test_description='test local clone with ambiguous transport'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-httpd.sh"

if ! test_have_prereq SYMLINKS
then
	skip_all='skipping test, symlink support unavailable'
	test_done
fi

start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/sub.git"
URI="$HTTPD_URL/dumb/sub.git"

test_expect_success 'setup' '
	mkdir -p sensitive &&
	echo "secret" >sensitive/secret &&

	git init --bare "$REPO" &&
	test_commit_bulk -C "$REPO" --ref=main 1 &&

	git -C "$REPO" update-ref HEAD main &&
	git -C "$REPO" update-server-info &&

	git init malicious &&
	(
		cd malicious &&

		git submodule add "$URI" &&

		mkdir -p repo/refs &&
		touch repo/refs/.gitkeep &&
		printf "ref: refs/heads/a" >repo/HEAD &&
		ln -s "$(cd .. && pwd)/sensitive" repo/objects &&

		mkdir -p "$HTTPD_URL/dumb" &&
		ln -s "../../../.git/modules/sub/../../../repo/" "$URI" &&

		git add . &&
		git commit -m "initial commit"
	) &&

	# Delete all of the references in our malicious submodule to
	# avoid the client attempting to checkout any objects (which
	# will be missing, and thus will cause the clone to fail before
	# we can trigger the exploit).
	git -C "$REPO" for-each-ref --format="delete %(refname)" >in &&
	git -C "$REPO" update-ref --stdin <in &&
	git -C "$REPO" update-server-info
'

test_expect_success 'ambiguous transport does not lead to arbitrary file-inclusion' '
	git clone malicious clone &&
	test_must_fail git -C clone submodule update --init 2>err &&

	test_path_is_missing clone/.git/modules/sub/objects/secret &&
	# We would actually expect "transport .file. not allowed" here,
	# but due to quirks of the URL detection in Git, we mis-parse
	# the absolute path as a bogus URL and die before that step.
	#
	# This works for now, and if we ever fix the URL detection, it
	# is OK to change this to detect the transport error.
	grep "protocol .* is not supported" err
'

test_done
