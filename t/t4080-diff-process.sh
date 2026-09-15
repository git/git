#!/bin/sh

test_description='diff.<driver>.process: oid-only hunk requests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# See t/helper/test-diff-process-backend.c for the process implementation
# and available --mode= options.

BACKEND="test-tool diff-process-backend"

test_expect_success 'setup' '
	echo "*.c diff=cdiff" >.gitattributes &&
	git add .gitattributes &&

	# 10 lines, changes at 5-6 and 9-10 between the two commits.
	cat >pair.c <<-\EOF &&
	line1
	line2
	line3
	line4
	original5
	original6
	line7
	line8
	line9
	line10
	EOF
	git add pair.c &&
	git commit -m "add pair.c" &&

	cat >pair.c <<-\EOF &&
	line1
	line2
	line3
	line4
	changed5
	changed6
	line7
	line8
	changed9
	changed10
	EOF
	git add pair.c &&
	git commit -m "change pair.c"
'

test_expect_success 'an oid-capable process answers blame by object names alone' '
	test_when_finished "rm -f backend.log" &&
	ORIG=$(git rev-parse --short HEAD~1) &&
	CHANGE=$(git rev-parse --short HEAD) &&
	# The process reports only lines 5-6 as changed, so blame attributes
	# lines 9-10 to the original commit even though the builtin diff
	# would show them as changed.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		blame pair.c >actual &&
	sed -n "9p" actual >line9 &&
	sed -n "10p" actual >line10 &&
	test_grep "$ORIG" line9 &&
	test_grep "$ORIG" line10 &&
	sed -n "5p" actual >line5 &&
	test_grep "$CHANGE" line5 &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'an oid-capable process answers --numstat by object names alone' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		log -1 --format= --numstat -- pair.c >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'need-content falls through to the builtin diff' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-need-content --log=backend.log" \
		log -1 --format= --numstat -- pair.c >actual &&
	printf "4\t4\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'a warmed hunk store does not override process hunks' '
	test_when_finished "git diff-hunks clear" &&
	ORIG=$(git rev-parse --short HEAD~1) &&
	GIT_DIFF_HUNKS_WRITE=1 git log -2 --stat -- pair.c >/dev/null &&

	# Control: without a process, blame is served from the store.
	git blame --show-stats pair.c >stats &&
	test_grep "num precomputed hits: 1" stats &&

	# The store holds the builtin hunks, but a process-capable driver
	# makes the process authoritative, so blame must reflect the
	# process hunks (only lines 5-6), not a store hit.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed" \
		blame pair.c >actual &&
	sed -n "9p" actual >line9 &&
	test_grep "$ORIG" line9
'

test_expect_success 'a worktree side is not asked by object names' '
	test_when_finished "rm -f backend.log && git checkout -- pair.c" &&
	echo "worktree edit" >>pair.c &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff --numstat -- pair.c >actual &&
	printf "1\t0\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process bypassed by --no-ext-diff' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		log -1 --format= --numstat --no-ext-diff -- pair.c >actual &&
	printf "4\t4\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success 'format-patch keeps its diffstat off the process' '
	test_when_finished "rm -f backend.log" &&
	# format-patch emits a diffstat, and a diffstat consults the
	# process, but the gate keeps it builtin so a generated patch
	# applies for recipients without the process.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		format-patch -1 --stdout --stat -- pair.c >actual &&
	test_grep "^+changed9" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'format-patch --ext-diff keeps its diffstat off the process' '
	test_when_finished "rm -f backend.log" &&
	# The gate holds even when --ext-diff enables the external command.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		format-patch -1 --stdout --ext-diff --stat -- pair.c >actual &&
	test_grep "^+changed9" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process not consulted by plumbing diff commands' '
	test_when_finished "rm -f backend.log && git checkout -f HEAD -- pair.c" &&
	# diff-tree diffs the two commits, a real pair a defeated gate would
	# consult the process for.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff-tree --numstat HEAD >actual &&
	test_grep "pair.c" actual &&
	# diff-index needs a change to diff, or there is no pair and a
	# missing log proves nothing; stage one and diff it against HEAD.
	echo "staged change" >>pair.c &&
	git add pair.c &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff-index --cached --numstat HEAD -- pair.c >actual &&
	test_grep "pair.c" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'add -p stages from the builtin diff with a process configured' '
	test_when_finished "rm -f backend.log" &&
	cat >gate.c <<-\EOF &&
	int gate(void) { return 1; }
	EOF
	git add gate.c &&
	git commit -m "add gate.c" &&
	cat >gate.c <<-\EOF &&
	int gate(void) { return 2; }
	EOF
	# add -p builds its hunks from patch text, which is not a provider
	# consumer today, so a configured process cannot shape what it
	# offers. This pins that interactive patch stays builtin for the
	# current consumers, rather than exercising the plumbing gate.
	test_write_lines y |
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		add -p gate.c &&
	git diff --cached -- gate.c >staged &&
	test_grep "return 2" staged &&
	test_path_is_missing backend.log &&
	git commit -m "gate.c v2"
'

test_expect_success 'blame withholds identity for the working-tree pair' '
	test_when_finished "rm -f backend.log && git checkout -- pair.c" &&
	echo "uncommitted" >>pair.c &&
	wt=$(git hash-object pair.c) &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		blame pair.c >actual &&
	# The dirty working-tree side is not a stored blob: no request
	# may name its bytes by object id.
	test_grep ! "new-oid=$wt" backend.log
'

test_expect_success 'a replaced blob makes the process step aside' '
	new_blob=$(git rev-parse HEAD:pair.c) &&
	test_when_finished "rm -f backend.log && git replace -d $new_blob" &&
	# Replacing the new-side blob redirects the content the diff reads
	# under the id the process would be sent, so a raw-id request would
	# name bytes other than the ones diffed. Identity is withheld: the
	# process is not consulted and the builtin computes the pair from
	# the replaced content.
	repl=$(printf "just one line\n" | git hash-object -w --stdin) &&
	git replace "$new_blob" "$repl" &&
	git log -1 --format= --numstat -- pair.c >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		log -1 --format= --numstat -- pair.c >actual &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success 'an equivalence answer omits the pair from the stat' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-equal --log=backend.log" \
		log -1 --format= --numstat -- pair.c >actual &&
	# An equivalent pair sums to a zero-count entry, and the stat
	# code omits zero-count modified entries, the same way a
	# whitespace-only pair prints nothing under -w. The builtin
	# diff would print nonzero counts here, and the log proves the
	# process was consulted.
	test_must_be_empty actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'blame passes equivalent pairs through to the boundary' '
	ORIG=$(git rev-parse --short ":/add pair.c") &&
	# The process asserts every consulted pair equal, so no line is
	# ever treated as changed: every line passes through to the
	# commit that added the file, marked as the blame boundary.
	git -c diff.cdiff.process="$BACKEND --mode=oid-equal" \
		blame pair.c >actual &&
	sed -n "5p" actual >line5 &&
	test_grep "^\^$ORIG" line5 &&
	sed -n "10p" actual >line10 &&
	test_grep "^\^$ORIG" line10
'

test_expect_success 'a warming run records a pair the process defers' '
	test_when_finished "git diff-hunks clear" &&
	git diff-hunks clear &&
	# The process owns the path but defers this pair with
	# need-content, so the pair gets the builtin diff; that is the
	# result the store holds, so the warming run records it and a
	# later read is served from the store.
	GIT_DIFF_HUNKS_WRITE=1 git -c core.diffHunks=true \
		-c diff.cdiff.process="$BACKEND --mode=oid-need-content" \
		log -1 --format= --stat -- pair.c >/dev/null &&
	git -c core.diffHunks=true blame --show-stats pair.c >stats 2>&1 &&
	test_grep "num precomputed hits: 1" stats
'

# The protocol error paths: each adversarial response shape must warn,
# fall back to the builtin output, and either keep the process alive
# (a per-pair rejection) or disable it for the rest of the command (a
# protocol error).  The request log tells the two apart: the log walk
# below consults two pairs (gate.c first, then pair.c), so a disabled
# process shows one logged request and a live one shows two.

test_expect_success 'a malformed hunk line disables the process for the command' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-malformed --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "disabling it for the remainder" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'coordinates past the blob size skip the pair, process stays alive' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-huge --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "past the end" err &&
	test_line_count = 2 backend.log
'

test_expect_success 'a count that overflows long skips the pair, process stays alive' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-erange --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "out-of-range coordinates" err &&
	test_line_count = 2 backend.log
'

test_expect_success 'overlapping hunks are rejected per pair' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-overlap --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "overlapping hunks" err &&
	test_line_count = 2 backend.log
'

test_expect_success 'misaligned hunks are rejected per pair' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-misaligned --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "misaligned" err &&
	test_line_count = 2 backend.log
'

test_expect_success 'a start of zero with a nonzero count is rejected per pair' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	# A start of 0 names an empty side, so a nonzero count beside it
	# names no line; the coordinate is rejected and the pair falls back
	# to the builtin diff while the process stays alive.
	git -c diff.cdiff.process="$BACKEND --mode=oid-badstart --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "out-of-range coordinates" err &&
	test_line_count = 2 backend.log
'

test_expect_success 'an unrecognized status disables the process for the command' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-unknown-status --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "unrecognized status .frobnicate." err &&
	test_line_count = 1 backend.log
'

test_expect_success 'status=abort withdraws the capability without a warning' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-abort --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep ! "disabling" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'a bare status without the hunk-section flush is a protocol error' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-bare-status --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "disabling it for the remainder" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'an empty packet in the hunk section is a protocol error' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-empty-packet --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "disabling it for the remainder" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'a process that dies mid-response fails the command over to builtin' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-crash --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "disabling it for the remainder" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'garbage bytes on stdout fail the command over to builtin' '
	test_when_finished "rm -f backend.log err" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-garbage --log=backend.log" \
		log --format= --numstat -- "*.c" >actual 2>err &&
	test_cmp expect actual &&
	test_grep "disabling it for the remainder" err &&
	test_line_count = 1 backend.log
'

test_expect_success 'a process announcing no capability is never asked' '
	test_when_finished "rm -f backend.log" &&
	git log --format= --numstat -- "*.c" >expect &&
	git -c diff.cdiff.process="$BACKEND --mode=cap-none --log=backend.log" \
		log --format= --numstat -- "*.c" >actual &&
	test_cmp expect actual &&
	test_must_be_empty backend.log
'

test_expect_success 'a trailing token on a hunk line is ignored' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-trailing --log=backend.log" \
		log -1 --format= --numstat -- pair.c >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'a failed start warns once and the store may serve the path' '
	git init failrepo &&
	(
		cd failrepo &&
		echo "*.c diff=cdiff" >.gitattributes &&
		git add .gitattributes &&
		test_commit f1 f.c "one" &&
		test_commit f2 f.c "one
two" &&
		test_commit f3 f.c "one
two
three" &&
		GIT_DIFF_HUNKS_WRITE=1 git log --format= --stat -- f.c >/dev/null &&
		# The command has no shell metacharacters, so it fails at
		# exec time; a shell-wrapped command would fail at the
		# handshake, which the gentle handshake in the base
		# (061a68e443) likewise degrades to the builtin diff.
		git blame f.c >expect &&
		git -c diff.cdiff.process=/does-not-exist-diff-backend \
			blame f.c >actual 2>err &&
		test_cmp expect actual &&
		# One warning even though the blame consults two pairs.
		test $(grep -c "failed to start" err) = 1 &&
		# A failure is a non-answer like any other: the request
		# that observes it and every later request pass to the
		# store, so the warmed store serves both pairs.
		git -c diff.cdiff.process=/does-not-exist-diff-backend \
			blame --show-stats f.c >stats 2>&1 &&
		test_grep "num precomputed hits: 2" stats
	)
'

test_expect_success 'the store serves a pair the process defers' '
	test_when_finished "git diff-hunks clear" &&
	git diff-hunks clear &&
	GIT_DIFF_HUNKS_WRITE=1 git log --format= --stat -- pair.c >/dev/null &&
	git blame pair.c >expect &&
	# need-content defers the pair to the builtin diff, which is
	# what the store holds, so the walk continues past the process
	# and the store serves the pair.
	git -c diff.cdiff.process="$BACKEND --mode=oid-need-content" \
		blame pair.c >actual &&
	test_cmp expect actual &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-need-content" \
		blame --show-stats pair.c >stats 2>&1 &&
	test_grep "num precomputed hits: 1" stats
'

test_expect_success 'git diff between commits consults the process' '
	test_when_finished "rm -f backend.log" &&
	ORIG=$(git rev-parse ":/add pair.c") &&
	CHANGE=$(git rev-parse ":/change pair.c") &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff --numstat $ORIG $CHANGE -- pair.c >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'git show consults the process' '
	test_when_finished "rm -f backend.log" &&
	CHANGE=$(git rev-parse ":/change pair.c") &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		show --format= --numstat $CHANGE -- pair.c >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'diff-tree --ext-diff consults the process' '
	test_when_finished "rm -f backend.log" &&
	CHANGE=$(git rev-parse ":/change pair.c") &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff-tree --ext-diff --no-commit-id --numstat $CHANGE >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success '--no-diff-process forbids consulting alone' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		log -1 --no-diff-process --format= --numstat -- pair.c >actual &&
	printf "4\t4\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success '--diff-process allows plumbing to consult' '
	test_when_finished "rm -f backend.log" &&
	CHANGE=$(git rev-parse ":/change pair.c") &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		diff-tree --diff-process --no-commit-id --numstat $CHANGE >actual &&
	printf "2\t2\tpair.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=pair.c" backend.log
'

test_expect_success 'a forced blame diff algorithm bypasses the process' '
	test_when_finished "rm -f backend.log" &&
	CHANGE=$(git rev-parse --short ":/change pair.c") &&
	# The process would attribute lines 9-10 to the original commit
	# (see the oid-fixed blame test above); a forced builtin
	# algorithm must produce the builtin attribution and never start
	# the process.
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		blame --histogram pair.c >actual &&
	sed -n "9p" actual >line9 &&
	test_grep "$CHANGE" line9 &&
	test_path_is_missing backend.log
'

test_expect_success 'textconv output is never identified to the process' '
	test_when_finished "rm -f backend.log" &&
	echo "*.tcv diff=tcv" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m tcv-attr &&
	test_config diff.tcv.textconv cat &&
	test_commit tcv1 file.tcv "alpha" &&
	test_commit tcv2 file.tcv "alpha
beta" &&
	git blame file.tcv >expect &&
	git -c diff.tcv.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		blame file.tcv >actual &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success 'a gitlink side is never identified to the process' '
	test_when_finished "rm -f backend.log" &&
	echo "sub diff=cdiff" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m sub-attr &&
	C1=$(git rev-parse HEAD) &&
	C2=$(git rev-parse HEAD~1) &&
	git update-index --add --cacheinfo 160000,$C1,sub &&
	git commit -m sub-1 &&
	git update-index --add --cacheinfo 160000,$C2,sub &&
	git commit -m sub-2 &&
	git -c diff.cdiff.process="$BACKEND --mode=oid-fixed --log=backend.log" \
		log -1 --format= --numstat -- sub >actual &&
	printf "1\t1\tsub\n" >expect &&
	test_cmp expect actual &&
	test_path_is_missing backend.log
'

test_expect_success 'a relative diff consults by the repo-relative path' '
	test_when_finished "rm -f backend.log" &&
	echo "reldir/*.rel diff=rdrv" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m rel-attr &&
	mkdir reldir &&
	test_write_lines line1 line2 line3 line4 original5 original6 \
		line7 line8 line9 line10 >reldir/x.rel &&
	git add reldir/x.rel &&
	git commit -m "add x.rel" &&
	test_write_lines line1 line2 line3 line4 changed5 changed6 \
		line7 line8 changed9 changed10 >reldir/x.rel &&
	git add reldir/x.rel &&
	git commit -m "change x.rel" &&
	# diff.relative strips the prefix from the displayed name; the
	# driver lookup and the request pathname must still use the
	# repo-relative path, or the directory-scoped attribute above
	# would not match and the process would never be consulted.
	# The process runs at the repository root, so its log lands there.
	(
		cd reldir &&
		git -c diff.relative=true \
			-c diff.rdrv.process="$BACKEND --mode=oid-fixed --log=backend.log" \
			log -1 --format= --numstat -- x.rel
	) >actual &&
	printf "2\t2\tx.rel\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=reldir/x.rel" backend.log
'

test_expect_success 'an empty file side is answered with a start of zero' '
	test_when_finished "rm -f backend.log" &&
	>empty.c &&
	git add empty.c &&
	git commit -m "add empty.c" &&
	printf "x\ny\nz\n" >empty.c &&
	git add empty.c &&
	git commit -m "fill empty.c" &&
	# The process addresses the empty old side with a start of 0 and a
	# count of 0, and claims two of the three added lines. The answer is
	# used as sent, so the stat shows the two lines it named, not the
	# three the builtin would.
	git -c diff.cdiff.process="$BACKEND --mode=oid-empty --log=backend.log" \
		log -1 --format= --numstat -- empty.c >actual &&
	printf "2\t0\tempty.c\n" >expect &&
	test_cmp expect actual &&
	test_grep "command=hunks-by-oid pathname=empty.c" backend.log
'

test_done
