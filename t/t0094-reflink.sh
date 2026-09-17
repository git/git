#!/bin/sh

test_description='reflink file copying'

. ./test-lib.sh

FAKE_REFLINK=/tmp/git-test-fake-reflink-$$.so
test_atexit 'rm -f "$FAKE_REFLINK"'

test_lazy_prereq FICLONE_PRELOAD '
	test_have_prereq !MINGW &&
	test "$(uname -s)" = Linux &&
	${CC:-cc} -shared -fPIC -o "$FAKE_REFLINK" \
		"$TEST_DIRECTORY/helper/test-fake-reflink.c" -ldl
'

test_expect_success FICLONE_PRELOAD 'generic copy accepts reflink success' '
	printf content >source &&
	GIT_TEST_FICLONE=success \
	GIT_TEST_FICLONE_LOG="$TRASH_DIRECTORY/generic-success.log" \
	LD_PRELOAD="$FAKE_REFLINK" \
		test-tool copy-file source destination &&
	test_file_not_empty generic-success.log &&
	test_cmp source destination &&
	test "$(stat -c %i source)" != "$(stat -c %i destination)"
'

test_expect_success FICLONE_PRELOAD 'generic copy falls back when unsupported' '
	printf fallback >source-fallback &&
	GIT_TEST_FICLONE=unsupported \
	GIT_TEST_FICLONE_LOG="$TRASH_DIRECTORY/generic-unsupported.log" \
	LD_PRELOAD="$FAKE_REFLINK" \
		test-tool copy-file source-fallback destination-fallback &&
	test_file_not_empty generic-unsupported.log &&
	test_cmp source-fallback destination-fallback
'

test_expect_success FICLONE_PRELOAD 'generic copy falls back after reflink error' '
	printf error-fallback >source-error &&
	GIT_TEST_FICLONE=error \
	GIT_TEST_FICLONE_LOG="$TRASH_DIRECTORY/generic-error.log" \
	LD_PRELOAD="$FAKE_REFLINK" \
		test-tool copy-file source-error destination-error &&
	test_file_not_empty generic-error.log &&
	test_cmp source-error destination-error
'

test_expect_success FICLONE_PRELOAD 'local clone prefers hardlinks' '
	git init source-repo &&
	git -C source-repo commit --allow-empty -m base &&
	GIT_TEST_FICLONE=success \
	LD_PRELOAD="$FAKE_REFLINK" \
		git clone --bare source-repo hardlink-clone &&
	find hardlink-clone/objects -type f -links +1 >hardlinks &&
	test_file_not_empty hardlinks &&
	git -C hardlink-clone fsck --no-dangling
'

test_expect_success FICLONE_PRELOAD 'local clone reflinks when hardlinks fail' '
	GIT_TEST_LINK_FAILURE=1 \
	GIT_TEST_FICLONE=success \
	GIT_TEST_FICLONE_LOG="$TRASH_DIRECTORY/clone-fallback.log" \
	LD_PRELOAD="$FAKE_REFLINK" \
		git clone --bare source-repo reflink-clone &&
	test_file_not_empty clone-fallback.log &&
	find reflink-clone/objects -type f -links +1 >hardlinks &&
	test_must_be_empty hardlinks &&
	git -C reflink-clone fsck --no-dangling
'

test_expect_success FICLONE_PRELOAD '--no-hardlinks also prefers successful reflinks' '
	GIT_TEST_FICLONE=success \
	GIT_TEST_FICLONE_LOG="$TRASH_DIRECTORY/no-hardlinks-success.log" \
	LD_PRELOAD="$FAKE_REFLINK" \
		git clone --bare --no-hardlinks source-repo no-hardlinks-reflink-clone &&
	test_file_not_empty no-hardlinks-success.log &&
	find no-hardlinks-reflink-clone/objects -type f -links +1 >hardlinks &&
	test_must_be_empty hardlinks &&
	git -C no-hardlinks-reflink-clone fsck --no-dangling
'

test_expect_success FICLONE_PRELOAD 'local clone copies when links fail' '
	GIT_TEST_LINK_FAILURE=1 \
	GIT_TEST_FICLONE=unsupported \
	LD_PRELOAD="$FAKE_REFLINK" \
		git clone --bare source-repo copied-clone &&
	find copied-clone/objects -type f -links +1 >hardlinks &&
	test_must_be_empty hardlinks &&
	git -C copied-clone fsck --no-dangling
'

test_expect_success FICLONE_PRELOAD '--no-hardlinks preserves byte-copy fallback' '
	GIT_TEST_FICLONE=unsupported \
	LD_PRELOAD="$FAKE_REFLINK" \
		git clone --bare --no-hardlinks source-repo no-hardlinks-copied-clone &&
	find no-hardlinks-copied-clone/objects -type f -links +1 >hardlinks &&
	test_must_be_empty hardlinks &&
	git -C no-hardlinks-copied-clone fsck --no-dangling
'

test_done
