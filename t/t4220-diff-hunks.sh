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

test_expect_success 'core.diffhunks=false disables lookups' '
	warm &&
	git -c core.diffhunks=false blame --show-stats file.txt >out 2>&1 &&
	test_grep "num precomputed hits: 0" out
'

# Writing seeds from the current store and merges into it, so a later
# warming run keeps the entries an earlier one recorded rather than
# rebuilding. Warm one pair, then a different pair, and confirm the first
# is still served.
test_expect_success 'a later warming run preserves earlier entries' '
	git init incr &&
	(
		cd incr &&
		test_commit a1 f.txt "1" &&
		test_commit a2 f.txt "1
2" &&
		test_commit a3 f.txt "1
2
3" &&
		GIT_DIFF_HUNKS_WRITE=1 git diff --stat a1 a2 >/dev/null &&
		git diff-hunks verify &&
		GIT_DIFF_HUNKS_WRITE=1 git diff --stat a2 a3 >/dev/null &&
		git diff-hunks verify &&

		# Blaming as of a2 diffs the a1..a2 pair. If seeding had
		# dropped it when the a2..a3 pair was warmed, this would
		# report zero precomputed hits.
		git blame --show-stats a2 -- f.txt >out 2>&1 &&
		test_grep "num precomputed hits: [1-9]" out &&

		# The second warm ADDED the a2..a3 pair; blaming a3 diffs
		# both a2..a3 and a1..a2, so a hit on each shows the store
		# gained the new pair while keeping the earlier one.
		git blame --show-stats a3 -- f.txt >out3 2>&1 &&
		test_grep "num precomputed hits: 2" out3 &&

		no_store log --stat >expect &&
		git log --stat >actual &&
		test_cmp expect actual
	)
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

# One warm serves both diffstat and blame: the blob pairs a blame
# walks are the same parent-child pairs the diffstat warm recorded.
test_expect_success 'a single warming run serves both blame and diffstat' '
	warm &&
	git blame --show-stats file.txt >out 2>&1 &&
	test_grep "num precomputed hits: [1-9][0-9]*" out
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

test_expect_success 'blame matches with and without the store' '
	no_store blame file.txt >expect &&
	warm &&
	git blame file.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'blame --porcelain and --incremental match' '
	no_store blame --porcelain file.txt >expect_p &&
	no_store blame --incremental file.txt >expect_i &&
	warm &&
	git blame --porcelain file.txt >got_p &&
	git blame --incremental file.txt >got_i &&
	test_cmp expect_p got_p &&
	test_cmp expect_i got_i
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

test_expect_success 'blame -w stays correct and does not hit default entries' '
	(
		cd ws-repo &&
		no_store blame -w f >expect &&
		git blame -w --show-stats f >out 2>&1 &&
		test_grep "num precomputed hits: 0" out &&
		git blame -w f >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'blame with indentHeuristic off stays correct and misses' '
	warm &&
	git -c diff.indentHeuristic=false blame --show-stats file.txt >out 2>&1 &&
	test_grep "num precomputed hits: 0" out &&
	no_store -c diff.indentHeuristic=false blame file.txt >expect &&
	git -c diff.indentHeuristic=false blame file.txt >actual &&
	test_cmp expect actual
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

test_expect_success 'blame --reverse never consults the store' '
	warm &&
	git blame --reverse HEAD~3..HEAD file.txt >actual 2>/dev/null &&
	no_store blame --reverse HEAD~3..HEAD file.txt >expect 2>/dev/null &&
	test_cmp expect actual &&
	# Reverse blame withholds the pair identity. Zero hits alone
	# cannot prove that: reverse pairs are never warmed, so a
	# consulted pair would miss, not hit. Zero misses is what shows
	# the store was never consulted.
	git blame --reverse --show-stats HEAD~3..HEAD file.txt \
		>stats 2>/dev/null &&
	test_grep "num precomputed hits: 0" stats &&
	test_grep "num precomputed misses: 0" stats
'

test_expect_success 'blame with a textconv driver bypasses the store' '
	echo "tc.txt diff=tc" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m tc-attr &&
	git config diff.tc.textconv "sed -e s/1/one/" &&
	test_commit tc1 tc.txt "line 1" &&
	test_commit tc2 tc.txt "line 1
line 2" &&
	warm &&
	git blame --show-stats tc.txt >out 2>&1 &&
	test_grep "num precomputed hits: 0" out &&
	no_store blame tc.txt >expect &&
	git blame tc.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'a replaced blob makes the store step aside' '
	git init replace-repo &&
	(
		cd replace-repo &&
		test_commit r1 f.txt "a" &&
		test_commit r2 f.txt "a
b" &&
		warm &&
		# Control: without a replacement the pair is served.
		GIT_TRACE2_EVENT="$PWD/trace_ctl.json" \
			git log -1 --format= --numstat -- f.txt >/dev/null &&
		test_grep read-hits trace_ctl.json &&
		# Replace r2 blob: the diff now reads different content
		# (through OBJECT_INFO_LOOKUP_REPLACE) under the id the store
		# keyed, so a served answer would be the pre-replacement diff.
		# Identity is withheld, the store steps aside, and the builtin
		# computes from the replaced content.
		new_blob=$(git rev-parse HEAD:f.txt) &&
		repl=$(printf "a\nB\nC\nD\n" | git hash-object -w --stdin) &&
		git replace "$new_blob" "$repl" &&
		no_store log -1 --format= --numstat -- f.txt >expect &&
		git log -1 --format= --numstat -- f.txt >actual &&
		test_cmp expect actual &&
		GIT_TRACE2_EVENT="$PWD/trace_repl.json" \
			git log -1 --format= --numstat -- f.txt >/dev/null &&
		test_grep ! read-hits trace_repl.json
	)
'

test_expect_success 'blame -M and -C stay correct with the store' '
	warm &&
	no_store blame -M file.txt >expect_m &&
	no_store blame -C file.txt >expect_c &&
	git blame -M file.txt >got_m &&
	git blame -C file.txt >got_c &&
	test_cmp expect_m got_m &&
	test_cmp expect_c got_c
'

# Copy-detecting (and reverse) blame still diff blob pairs through
# pass_blame_to_parent, so they must use the real blame xdl_opts. A
# whitespace-only change is invisible under -w; if -w were dropped on
# these paths the -w and non-w results would coincide.
test_expect_success 'blame -C honors -w' '
	git init -q blame-cw &&
	(
		cd blame-cw &&
		printf "one\ntwo\nthree\n" >f &&
		git add f && git commit -q -m base &&
		printf "one\n  two  \nthree\n" >f &&
		git add f && git commit -q -m reindent &&
		git blame -C -w f >with_w &&
		git blame -C f >without_w &&
		! test_cmp with_w without_w
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

test_expect_success 'blame across a rename matches' '
	echo "original content" >rename-src.txt &&
	git add rename-src.txt &&
	git commit -m "add rename-src" &&
	echo "more" >>rename-src.txt &&
	git add rename-src.txt &&
	git commit -m "modify rename-src" &&
	git mv rename-src.txt rename-dst.txt &&
	git commit -m "rename" &&
	echo "post" >>rename-dst.txt &&
	git add rename-dst.txt &&
	git commit -m "modify after rename" &&
	no_store blame rename-dst.txt >expect &&
	warm &&
	git blame rename-dst.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'blame handles merge commits' '
	git checkout -b merge-side main~2 &&
	test_commit merge-change merge-file.txt "side content" &&
	git checkout main &&
	git merge --no-edit merge-side &&
	no_store blame merge-file.txt >expect &&
	warm &&
	git blame merge-file.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'distinct --contents against one revision do not collide' '
	warm &&
	test_write_lines "line 1" "appended line" >c1 &&
	test_write_lines "rewritten line" >c2 &&
	# Ground truth without the store.
	no_store blame -s --contents=c2 file.txt initial >expect &&
	# With the store, an intervening c1 run must not poison the c2 lookup.
	git blame -s --contents=c1 file.txt initial >/dev/null &&
	git blame -s --contents=c2 file.txt initial >actual &&
	test_cmp expect actual &&
	# The --contents side is a working-tree pseudo-commit (a null commit
	# id), so its pairs withhold identity and never consult the store.
	# Output parity alone cannot show that: a consulted unwarmed pair
	# would miss, not hit, so zero misses is what proves the pair was
	# never looked up.
	git blame -s --show-stats --contents=c2 file.txt initial >stats 2>&1 &&
	test_grep "num precomputed hits: 0" stats &&
	test_grep "num precomputed misses: 0" stats
'

test_expect_success 'blame --ignore-rev bypasses the store for ignored pairs' '
	git init ignore-rev-repo &&
	(
		cd ignore-rev-repo &&
		test_commit ir1 f.txt "base" &&
		test_commit ir2 f.txt "base
more" &&
		warm &&
		# Control: the ordinary pass is served, nothing is computed.
		git blame --show-stats f.txt >ctl 2>&1 &&
		test_grep "num precomputed hits: 1" ctl &&
		test_grep "num get patch: 0" ctl &&
		no_store blame --ignore-rev ir2 f.txt >expect &&
		git blame --ignore-rev ir2 f.txt >actual &&
		test_cmp expect actual &&
		# The ignored revision adds a pass that withholds identity:
		# it computes its diff (get patch rises) instead of being
		# served or even counted as a store consultation.
		git blame --ignore-rev ir2 --show-stats f.txt >stats 2>&1 &&
		test_grep "num precomputed hits: 1" stats &&
		test_grep "num precomputed misses: 0" stats &&
		test_grep "num get patch: 1" stats
	)
'

test_expect_success 'blame counts misses for pairs the store does not hold' '
	(
		cd ignore-rev-repo &&
		test_commit ir3 f.txt "base
more
third" &&
		git blame --show-stats f.txt >stats 2>&1 &&
		test_grep "num precomputed hits: 1" stats &&
		test_grep "num precomputed misses: 1" stats
	)
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

# Integrity: a structurally broken header is read as absent (the reader
# falls back to xdiff and stays correct); a checksum mismatch is caught
# by verify, which is when integrity is checked.
test_expect_success 'a truncated store is read as absent' '
	warm &&
	test_copy_bytes 20 <$STORE >truncated &&
	mv truncated $STORE &&
	no_store blame file.txt >expect &&
	git blame file.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'a corrupt signature is read as absent' '
	warm &&
	printf "XXXX" >corrupt &&
	tail -c +5 <$STORE >>corrupt &&
	mv corrupt $STORE &&
	no_store blame file.txt >expect &&
	git blame file.txt >actual &&
	test_cmp expect actual
'

# Byte 6 of the header is the chunk count; a value larger than the file
# can hold must be rejected before the chunk table is walked.
test_expect_success 'an over-claimed chunk count is read as absent' '
	warm &&
	printf "\377" | dd of=$STORE bs=1 seek=6 count=1 conv=notrunc 2>/dev/null &&
	no_store blame file.txt >expect &&
	git blame file.txt >actual &&
	test_cmp expect actual
'

# A record with no hunks would replay as an equivalence claim, which
# the writer never records; the reader must treat such a record as a
# miss and recompute, and verify must flag it.
test_expect_success 'a zero-hunk record is read as a miss and fails verify' '
	git init zero-hunk &&
	(
		cd zero-hunk &&
		test_commit z1 f.txt "base" &&
		test_commit z2 f.txt "base
more" &&
		warm &&
		# The store holds one entry of one hunk: a 4-byte count and
		# one 16-byte hunk record, just before the trailing
		# checksum. Zero the count to craft the record the writer
		# refuses to produce.
		rawsz=$(test_oid rawsz) &&
		fsize=$(test_file_size $STORE) &&
		printf "\\0\\0\\0\\0" | dd of=$STORE bs=1 \
			seek=$((fsize - rawsz - 20)) count=4 conv=notrunc \
			2>/dev/null &&
		no_store blame f.txt >expect &&
		git blame --show-stats f.txt >stats 2>&1 &&
		test_grep "num precomputed hits: 0" stats &&
		git blame f.txt >actual &&
		test_cmp expect actual &&
		test_must_fail git diff-hunks verify
	)
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

# A generated patch must carry the builtin diffstat, not one served from
# the sender's local store, so its counts do not depend on whether the
# sender warmed the store. Poison the store so a served answer diverges
# from the builtin, then confirm format-patch shows the builtin counts.
test_expect_success 'format-patch keeps its diffstat off the store' '
	git init fp-repo &&
	(
		cd fp-repo &&
		test_commit p1 f.txt "a" &&
		test_commit p2 f.txt "a
b" &&
		warm &&
		# Bump the new-side count of the single recorded hunk. The
		# record stays structurally valid, and a read skips the
		# trailing checksum, so the store serves this poisoned count.
		rawsz=$(test_oid rawsz) &&
		fsize=$(test_file_size .git/objects/info/diff-hunks) &&
		printf "\\0\\0\\0\\7" | dd of=.git/objects/info/diff-hunks bs=1 \
			seek=$((fsize - rawsz - 4)) count=4 conv=notrunc 2>/dev/null &&
		# The store now serves a divergent count, proving the poison
		# is live and observable through a store consumer.
		printf "7\t0\tf.txt\n" >poisoned &&
		git log -1 --format= --numstat -- f.txt >served &&
		test_cmp poisoned served &&
		# format-patch does not consult the store, so its output is
		# identical with the store poisoned and with it disabled.
		no_store format-patch -1 --stdout --stat -- f.txt >expect &&
		git format-patch -1 --stdout --stat -- f.txt >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'diff-hunks clear removes the store file' '
	warm &&
	test_path_is_file $STORE &&
	git diff-hunks clear &&
	test_path_is_missing $STORE
'

test_done
