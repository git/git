#!/bin/sh

test_description='check that local clone does not fetch from promisor remotes'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create evil repo' '
	git init tmp &&
	test_commit -C tmp a &&
	git -C tmp config uploadpack.allowfilter 1 &&
	git clone --filter=blob:none --no-local --no-checkout tmp evil &&
	rm -rf tmp &&

	git -C evil config remote.origin.uploadpack \"\$TRASH_DIRECTORY/fake-upload-pack\" &&
	write_script fake-upload-pack <<-\EOF &&
		echo >&2 "fake-upload-pack running"
		>"$TRASH_DIRECTORY/script-executed"
		exit 1
	EOF
	export TRASH_DIRECTORY &&

	# empty shallow file disables local clone optimization
	>evil/.git/shallow
'

test_expect_success 'local clone must not fetch from promisor remote and execute script' '
	rm -f script-executed &&
	test_must_fail git clone \
		--upload-pack="GIT_TEST_ASSUME_DIFFERENT_OWNER=true git-upload-pack" \
		evil clone1 2>err &&
	test_grep "detected dubious ownership" err &&
	test_grep ! "fake-upload-pack running" err &&
	test_path_is_missing script-executed
'

test_expect_success 'clone from file://... must not fetch from promisor remote and execute script' '
	rm -f script-executed &&
	test_must_fail git clone \
		--upload-pack="GIT_TEST_ASSUME_DIFFERENT_OWNER=true git-upload-pack" \
		"file://$(pwd)/evil" clone2 2>err &&
	test_grep "detected dubious ownership" err &&
	test_grep ! "fake-upload-pack running" err &&
	test_path_is_missing script-executed
'

test_expect_success 'fetch from file://... must not fetch from promisor remote and execute script' '
	rm -f script-executed &&
	test_must_fail git fetch \
		--upload-pack="GIT_TEST_ASSUME_DIFFERENT_OWNER=true git-upload-pack" \
		"file://$(pwd)/evil" 2>err &&
	test_grep "detected dubious ownership" err &&
	test_grep ! "fake-upload-pack running" err &&
	test_path_is_missing script-executed
'

test_expect_success 'pack-objects should fetch from promisor remote and execute script' '
	rm -f script-executed &&
	echo "HEAD" | test_must_fail git -C evil pack-objects --revs --stdout >/dev/null 2>err &&
	test_grep "fake-upload-pack running" err &&
	test_path_is_file script-executed
'

test_expect_success 'clone from promisor remote does not lazy-fetch by default' '
	rm -f script-executed &&

	# The --path-walk feature of "git pack-objects" is not
	# compatible with this kind of fetch from an incomplete repo.
	GIT_TEST_PACK_PATH_WALK=0 &&
	export GIT_TEST_PACK_PATH_WALK &&

	test_must_fail git clone evil no-lazy 2>err &&
	test_grep "lazy fetching disabled" err &&
	test_path_is_missing script-executed
'

test_expect_success 'promisor lazy-fetching can be re-enabled' '
	rm -f script-executed &&
	test_must_fail env GIT_NO_LAZY_FETCH=0 \
		git clone evil lazy-ok 2>err &&
	test_grep "fake-upload-pack running" err &&
	test_path_is_file script-executed
'

test_done
