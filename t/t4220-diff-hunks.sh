#!/bin/sh

test_description='precomputed diff hunks store (git diff-hunks)

The store maps an (old blob, new blob, diff settings) key to the hunks of
diffing the pair. It is a cache: reading is on by default
(core.diffHunks), while writing is
off by default and enabled per run by GIT_DIFF_HUNKS_WRITE (or the
diffHunks.write config), so a diff or log warms the store only when the
owner opts in. These tests check that a warmed store never changes
output, that lookups honor the diff settings, and that a corrupt store is
read as absent while verify reports the corruption.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

STORE=.git/objects/info/diff-hunks

# Warm the store the way a repository owner would: a stat walk with
# writing enabled. A --stat walk records one entry per trim-stable blob
# pair, serving blame and the summary formats alike. Extra arguments
# (e.g. -c options) are passed to git before "log".
warm () {
	GIT_DIFF_HUNKS_WRITE=1 git "$@" log --all --stat >/dev/null
}

# Run a command with the store disabled, for ground truth.
no_store () {
	git -c core.diffhunks=false "$@"
}

test_expect_success 'setup' '
	test_commit initial file.txt "line 1" &&
	test_commit second file.txt "line 1
line 2" &&
	test_commit third file.txt "line 1
line 2
line 3" &&
	test_commit fourth file.txt "changed line 1
line 2
line 3
line 4"
'

test_expect_success 'ordinary commands do not create the store' '
	git log --stat >/dev/null &&
	git blame file.txt >/dev/null &&
	git diff --stat second third >/dev/null &&
	test_path_is_missing $STORE
'

test_expect_success 'writing is gated by env and config, env wins' '
	test_when_finished "git diff-hunks clear" &&
	# The diffHunks.write config enables writing.
	git -c diffHunks.write=true log --all --stat >/dev/null &&
	test_path_is_file $STORE &&
	git diff-hunks clear &&
	# GIT_DIFF_HUNKS_WRITE overrides the config: 0 disables it.
	GIT_DIFF_HUNKS_WRITE=0 git -c diffHunks.write=true log --all --stat >/dev/null &&
	test_path_is_missing $STORE &&
	# and enables it without any config.
	GIT_DIFF_HUNKS_WRITE=1 git log --all --stat >/dev/null &&
	test_path_is_file $STORE
'

test_expect_success 'a warm builds a store that verifies' '
	warm &&
	test_path_is_file $STORE &&
	git diff-hunks verify
'

test_expect_success 'a second warming run refreshes the store in place' '
	warm &&
	test_commit fifth file.txt "brand new line" &&
	warm &&
	git diff-hunks verify &&
	no_store log --stat >expect &&
	git log --stat >actual &&
	test_cmp expect actual
'

# A warming run displays the diffstat it computes. At zero context xdi_diff
# trims, so the displayed counts must be the trimmed ones (what a store-less
# run shows), not the untrimmed ones the writer compares against when it
# decides whether the pair is stable enough to record.
test_expect_success 'warming --stat at zero context matches a store-less run' '
	git init -q warm-u0 &&
	(
		cd warm-u0 &&
		cp "$TEST_DIRECTORY/t4220/trim-divergent-old" div.sh &&
		git add div.sh && git commit -q -m old &&
		cp "$TEST_DIRECTORY/t4220/trim-divergent-new" div.sh &&
		git add div.sh && git commit -q -m new &&
		git -c core.diffhunks=false log -1 --format= -U0 --stat -- div.sh >expect &&
		GIT_DIFF_HUNKS_WRITE=1 git log -1 --format= -U0 --stat -- div.sh >got &&
		test_cmp expect got
	)
'

test_expect_success 'show and diff-tree --stat use the store' '
	test_when_finished "git diff-hunks clear" &&
	# diff_hunks_attach() runs for show and diff-tree: a write-enabled
	# --stat records into the store (without the attach there is no
	# writer, so nothing is written).
	git diff-hunks clear &&
	GIT_DIFF_HUNKS_WRITE=1 git show --stat fourth >/dev/null &&
	test_path_is_file "$STORE" &&
	git diff-hunks clear &&
	GIT_DIFF_HUNKS_WRITE=1 git diff-tree --stat fourth >/dev/null &&
	test_path_is_file "$STORE" &&
	# Reading never changes their output.
	git diff-hunks clear &&
	no_store show --stat fourth >expect_show &&
	no_store diff-tree --stat fourth >expect_dt &&
	warm &&
	git show --stat fourth >got_show &&
	git diff-tree --stat fourth >got_dt &&
	test_cmp expect_show got_show &&
	test_cmp expect_dt got_dt
'

# Cover the pair shapes an object walk encounters: binary and
# mode-only changes produce no text hunks to record.
test_expect_success 'binary and mode-only changes do not break the writer' '
	printf "\\000\\001\\002" >bin.dat &&
	git add bin.dat &&
	git commit -m binary-1 &&
	printf "\\000\\001\\003\\004" >bin.dat &&
	git add bin.dat &&
	git commit -m binary-2 &&
	echo "mode content" >mode.txt &&
	git add mode.txt &&
	git commit -m mode-1 &&
	test_chmod +x mode.txt &&
	git commit -m mode-2 &&
	no_store log --stat >expect &&
	warm &&
	git log --stat >actual &&
	test_cmp expect actual
'

test_expect_success 'verify succeeds on a valid store and on an absent one' '
	warm &&
	git diff-hunks verify &&
	git diff-hunks clear &&
	test_path_is_missing $STORE &&
	git diff-hunks verify
'

test_expect_success 'verify detects a checksum mismatch' '
	test_when_finished "git diff-hunks clear" &&
	warm &&
	fsize=$(test_file_size $STORE) &&
	mid=$((fsize / 2)) &&
	printf "\\377" | dd of=$STORE bs=1 seek=$mid count=1 conv=notrunc 2>/dev/null &&
	test_must_fail git diff-hunks verify
'

test_expect_success 'a warm discards a corrupt store rather than seeding from it' '
	test_when_finished "git diff-hunks clear" &&
	warm &&
	# Corrupt the checksum: the next warm must not carry the corrupt
	# entries forward into a fresh checksum-valid file; it discards
	# them (with a warning) and rewrites a store that verifies.
	fsize=$(test_file_size $STORE) &&
	printf "\\377" | dd of=$STORE bs=1 seek=$((fsize / 2)) count=1 conv=notrunc 2>/dev/null &&
	warm 2>err &&
	test_grep "failed its checksum" err &&
	git diff-hunks verify &&
	no_store log --stat >expect &&
	git log --stat >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-hunks clear removes the store file' '
	warm &&
	test_path_is_file $STORE &&
	git diff-hunks clear &&
	test_path_is_missing $STORE
'

test_done
