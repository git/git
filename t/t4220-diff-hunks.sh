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

test_expect_success 'log --stat matches with and without the store' '
	no_store log --stat >expect &&
	warm &&
	git log --stat >actual &&
	test_cmp expect actual
'

test_expect_success 'log --numstat and --shortstat match' '
	no_store log --numstat >expect_num &&
	no_store log --shortstat >expect_short &&
	warm &&
	git log --numstat >actual_num &&
	git log --shortstat >actual_short &&
	test_cmp expect_num actual_num &&
	test_cmp expect_short actual_short
'

# A built store must reproduce diffstat output at every context
# length.  Only trim-stable pairs are recorded, so one entry serves
# every context; a trim-divergent pair is never recorded and always
# computed.  Zero context is where trim_common_tail runs, which is
# what makes the two diffs differ.
test_expect_success 'diffstat matches at several context lengths' '
	no_store log --stat >expect_def &&
	no_store log -U0 --stat >expect_u0 &&
	no_store log -U7 --stat >expect_u7 &&
	warm &&
	git log --stat >got_def &&
	git log -U0 --stat >got_u0 &&
	git log -U7 --stat >got_u7 &&
	test_cmp expect_def got_def &&
	test_cmp expect_u0 got_u0 &&
	test_cmp expect_u7 got_u7
'

test_expect_success 'store built at a nonzero context stays correct at that context' '
	no_store -c diff.context=5 log --stat >expect &&
	warm -c diff.context=5 &&
	git -c diff.context=5 log --stat >actual &&
	test_cmp expect actual
'

# This blob pair (a real git test file being modernized) has different
# valid diffs at different contexts: at zero context, where
# trim_common_tail runs, "diff -U0" reports 9/6, while "diff -U3"
# reports 10/7. Such a trim-divergent pair is exactly what the writer
# must never record, since no single entry could serve both readers.
# A compact synthetic pair cannot show this count split: on small
# inputs xdiff produces minimal diffs, minimal diffs of one pair all
# add and delete the same number of lines, and trimming the common
# tail preserves minimality, so the counts agree by construction (a
# search over thousands of synthetic pairs up to 8 lines found no
# split). The split needs the cost-capping heuristics that only larger
# inputs trigger, so the pair is shipped as a fixture under t4220/.
test_expect_success 'a trim-divergent file is correct at each context' '
	cp "$TEST_DIRECTORY/t4220/trim-divergent-old" div.sh &&
	git add div.sh &&
	git commit -m divergent-old &&
	cp "$TEST_DIRECTORY/t4220/trim-divergent-new" div.sh &&
	git add div.sh &&
	git commit -m divergent-new &&
	no_store log -1 --format= --stat -- div.sh >expect_def &&
	no_store log -1 --format= -U0 --stat -- div.sh >expect_u0 &&
	warm &&
	git log -1 --format= --stat -- div.sh >got_def &&
	git log -1 --format= -U0 --stat -- div.sh >got_u0 &&
	test_cmp expect_def got_def &&
	test_cmp expect_u0 got_u0 &&
	# The fixture must actually diverge, or the test would pass without
	# exercising the split; fail loudly if a diff change ever levels it.
	! test_cmp expect_def expect_u0
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

test_expect_success 'diff --stat matches with and without the store, both directions' '
	no_store diff --stat second fourth >expect_fwd &&
	no_store diff --stat fourth second >expect_rev &&
	warm &&
	git diff --stat second fourth >got_fwd &&
	git diff --stat fourth second >got_rev &&
	test_cmp expect_fwd got_fwd &&
	test_cmp expect_rev got_rev
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

test_expect_success 'log -R --stat matches (reversed pairs keyed apart)' '
	no_store log -R --stat >expect &&
	warm &&
	git log -R --stat >actual &&
	test_cmp expect actual
'

# The diffstat read path produces identical output on a hit or a miss, so
# it emits a trace2 "read-hits" count to prove it consulted the store.
test_expect_success 'diffstat consults the store (trace shows read hits)' '
	warm &&
	GIT_TRACE2_EVENT="$PWD/trace_on.json" git log --stat >/dev/null &&
	test_grep read-hits trace_on.json &&
	test_env GIT_TRACE2_EVENT="$PWD/trace_off.json" no_store log --stat >/dev/null &&
	test_grep ! read-hits trace_off.json
'

# Diff settings that change hunks but are not part of the store key must
# bypass it in both directions, so output stays byte-identical to a
# store-less run.
test_expect_success 'setup ignore fixture' '
	git init ignore-repo &&
	(
		cd ignore-repo &&
		test_write_lines code keep "# c" >f &&
		git add f &&
		git commit -m c1 &&
		test_write_lines codeCH keep "# cX" >f &&
		git add f &&
		git commit -m c2 &&
		warm
	)
'

# Output parity alone cannot prove the guard: served counts can
# coincide with computed ones, so each bypass below also asserts the
# consultation itself (no read hit with the option, a hit without it)
# and that a warming run under the option records nothing.
test_expect_success '-I bypasses the store in both directions' '
	(
		cd ignore-repo &&
		no_store diff -I"^#" --numstat HEAD~ HEAD >expect &&
		git diff -I"^#" --numstat HEAD~ HEAD >actual &&
		test_cmp expect actual &&
		# -I does not change the key, so only the ignore_regex
		# guard keeps the warmed entry from serving here.
		GIT_TRACE2_EVENT="$PWD/trace_i.json" \
			git diff -I"^#" --numstat HEAD~ HEAD >/dev/null &&
		test_grep ! read-hits trace_i.json &&
		GIT_TRACE2_EVENT="$PWD/trace_i_ctl.json" \
			git diff --numstat HEAD~ HEAD >/dev/null &&
		test_grep read-hits trace_i_ctl.json &&
		git diff-hunks clear &&
		GIT_DIFF_HUNKS_WRITE=1 git diff -I"^#" --numstat HEAD~ HEAD >/dev/null &&
		test_path_is_missing .git/objects/info/diff-hunks &&
		# Restore the warmed fixture for the tests below.
		warm
	)
'

test_expect_success '-B bypasses the store in both directions' '
	git init break-repo &&
	(
		cd break-repo &&
		test_write_lines a b c d e f g h >f &&
		git add f &&
		git commit -m orig &&
		test_write_lines 1 2 3 4 5 6 7 8 >f &&
		git add f &&
		git commit -m rewrite &&
		warm &&
		no_store diff -B --stat HEAD~ HEAD >expect &&
		git diff -B --stat HEAD~ HEAD >actual &&
		test_cmp expect actual &&
		GIT_TRACE2_EVENT="$PWD/trace_b.json" \
			git diff -B --stat HEAD~ HEAD >/dev/null &&
		test_grep ! read-hits trace_b.json &&
		GIT_TRACE2_EVENT="$PWD/trace_ctl.json" \
			git diff --stat HEAD~ HEAD >/dev/null &&
		test_grep read-hits trace_ctl.json &&
		git diff-hunks clear &&
		GIT_DIFF_HUNKS_WRITE=1 git diff -B --stat HEAD~ HEAD >/dev/null &&
		test_path_is_missing .git/objects/info/diff-hunks
	)
'

test_expect_success '--anchored bypasses the store in both directions' '
	(
		cd ignore-repo &&
		no_store diff --stat --anchored=keep HEAD~ HEAD >expect &&
		git diff --stat --anchored=keep HEAD~ HEAD >actual &&
		test_cmp expect actual &&
		# Anchors do not change the key, so only the anchors guard
		# keeps the warmed entry from serving here.
		GIT_TRACE2_EVENT="$PWD/trace_anchor.json" \
			git diff --stat --anchored=keep HEAD~ HEAD >/dev/null &&
		test_grep ! read-hits trace_anchor.json &&
		GIT_TRACE2_EVENT="$PWD/trace_plain.json" \
			git diff --stat HEAD~ HEAD >/dev/null &&
		test_grep read-hits trace_plain.json &&
		git diff-hunks clear &&
		GIT_DIFF_HUNKS_WRITE=1 \
			git diff --stat --anchored=keep HEAD~ HEAD >/dev/null &&
		test_path_is_missing .git/objects/info/diff-hunks
	)
'

test_expect_success '--ignore-blank-lines bypasses the store in both directions' '
	git init ibl-repo &&
	(
		cd ibl-repo &&
		printf "a\n\nx\ny\nb\n" >f &&
		git add f &&
		git commit -m v1 &&
		printf "a\nx\ny\nB\n" >f &&
		git add f &&
		git commit -m v2 &&
		warm &&
		no_store diff --stat --ignore-blank-lines HEAD~ HEAD >expect &&
		git diff --stat --ignore-blank-lines HEAD~ HEAD >actual &&
		test_cmp expect actual &&
		# The flag is an xdl_opts bit and thus part of the key; the
		# stat consumer excludes it before consulting at all.
		GIT_TRACE2_EVENT="$PWD/trace_ibl.json" \
			git diff --stat --ignore-blank-lines HEAD~ HEAD >/dev/null &&
		test_grep ! read-hits trace_ibl.json &&
		GIT_TRACE2_EVENT="$PWD/trace_plain.json" \
			git diff --stat HEAD~ HEAD >/dev/null &&
		test_grep read-hits trace_plain.json &&
		git diff-hunks clear &&
		GIT_DIFF_HUNKS_WRITE=1 \
			git diff --stat --ignore-blank-lines HEAD~ HEAD >/dev/null &&
		test_path_is_missing .git/objects/info/diff-hunks
	)
'

test_expect_success 'a whitespace-ignoring diff is not served default entries' '
	git init ws-repo &&
	(
		cd ws-repo &&
		test_write_lines alpha beta gamma >f &&
		git add f &&
		git commit -m c1 &&
		test_write_lines "  alpha" beta gamma delta >f &&
		git add f &&
		git commit -m c2 &&
		warm &&
		no_store diff -w --numstat HEAD~ HEAD >expect &&
		git diff -w --numstat HEAD~ HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'a driver algorithm override keeps output correct and keys apart' '
	git init driver-algo &&
	(
		cd driver-algo &&
		echo "file.foo diff=foo" >.gitattributes &&
		git add .gitattributes &&
		git commit -m attributes &&
		test_write_lines 1 2 3 4 5 >file.foo &&
		git add file.foo &&
		git commit -m one &&
		test_write_lines 1 2 X 4 5 6 >file.foo &&
		git add file.foo &&
		git commit -m two &&
		warm -c diff.foo.algorithm=histogram &&
		no_store -c diff.foo.algorithm=histogram log --stat >expect &&
		git -c diff.foo.algorithm=histogram log --stat >actual &&
		test_cmp expect actual &&
		# The driver algorithm is an xdl_opts key bit: entries
		# warmed at the default settings must not serve a
		# driver-forced histogram read, and output stays correct.
		git diff-hunks clear &&
		warm &&
		no_store -c diff.foo.algorithm=histogram log --stat >expect2 &&
		git -c diff.foo.algorithm=histogram log --stat >actual2 &&
		test_cmp expect2 actual2 &&
		GIT_TRACE2_EVENT="$PWD/trace_algo.json" \
			git -c diff.foo.algorithm=histogram log --stat >/dev/null &&
		test_grep ! read-hits trace_algo.json &&
		GIT_TRACE2_EVENT="$PWD/trace_algo_ctl.json" \
			git log --stat >/dev/null &&
		test_grep read-hits trace_algo_ctl.json
	)
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

test_expect_success 'log -L --stat neither reads nor records' '
	warm &&
	GIT_TRACE2_EVENT="$PWD/trace_linelog.json" \
		git log -L1,1:file.txt --stat >/dev/null &&
	test_grep ! read-hits trace_linelog.json &&
	git diff-hunks clear &&
	GIT_DIFF_HUNKS_WRITE=1 git log -L1,1:file.txt --stat >/dev/null &&
	test_path_is_missing $STORE
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
