#!/bin/sh

test_description='diff process via long-running process'

. ./test-lib.sh

# See t/helper/test-diff-process-backend.c for the backend implementation
# and available --mode= options.

BACKEND="test-tool diff-process-backend"

test_expect_success 'setup' '
	echo "*.c diff=cdiff" >.gitattributes &&
	git add .gitattributes &&

	# boundary.c: 10 lines, changes at 5-6 and 9-10.
	# Used by: hunk boundaries, error fallback, crash, bad hunks, overlap.
	cat >boundary.c <<-\EOF &&
	line1
	line2
	line3
	line4
	OLD5
	OLD6
	line7
	line8
	OLD9
	OLD10
	EOF
	git add boundary.c &&

	# worddiff.c: single-line function, value changes 1 -> 999.
	# Used by: word-diff, --diff-algorithm, --no-ext-diff, --stat.
	cat >worddiff.c <<-\EOF &&
	int value(void) { return 1; }
	EOF
	git add worddiff.c &&

	# newfile.c: single-line function, value changes 42 -> 99.
	# Used by: modified file, --exit-code, multiple drivers.
	cat >newfile.c <<-\EOF &&
	int new_func(void) { return 42; }
	EOF
	git add newfile.c &&

	# logtest.c: single-line function for log/format-patch tests.
	# Needs two commits so log -1 has a diff.
	cat >logtest.c <<-\EOF &&
	int logfunc(void) { return 1; }
	EOF
	git add logtest.c &&

	# two.c/one.c: two-file pair for error/abort/startup-failure tests.
	cat >one.c <<-\EOF &&
	int first(void) { return 1; }
	EOF
	cat >two.c <<-\EOF &&
	int second(void) { return 2; }
	EOF
	git add one.c two.c &&

	git commit -m "initial" &&

	# Second commit for logtest.c (so log -1 has something to show).
	cat >logtest.c <<-\EOF &&
	int logfunc(void) { return 2; }
	EOF
	git add logtest.c &&
	git commit -m "change logtest.c" &&

	# Working tree modifications (not committed).
	cat >boundary.c <<-\EOF &&
	line1
	line2
	line3
	line4
	NEW5
	NEW6
	line7
	line8
	NEW9
	NEW10
	EOF

	cat >worddiff.c <<-\EOF &&
	int value(void) { return 999; }
	EOF

	cat >newfile.c <<-\EOF &&
	int new_func(void) { return 99; }
	EOF

	cat >one.c <<-\EOF &&
	int first(void) { return 10; }
	EOF

	cat >two.c <<-\EOF
	int second(void) { return 20; }
	EOF
'

#
# Core behavior: the tool controls which lines are marked as changed.
#

test_expect_success 'diff process hunk boundaries affect output' '
	# The file has changes at lines 5-6 and 9-10, but fixed-hunk
	# only reports lines 5-6 as changed.  Lines 9-10 should not
	# appear as changed in the output.
	git -c diff.cdiff.process="$BACKEND --mode=fixed-hunk" \
		diff boundary.c >actual &&
	test_grep "^-OLD5" actual &&
	test_grep "^-OLD6" actual &&
	test_grep "^+NEW5" actual &&
	test_grep "^+NEW6" actual &&
	test_grep ! "^-OLD9" actual &&
	test_grep ! "^-OLD10" actual &&
	test_grep ! "^+NEW9" actual &&
	test_grep ! "^+NEW10" actual
'

test_expect_success 'diff process works with modified file' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff -- newfile.c >actual 2>stderr &&
	test_grep "return 99" actual &&
	test_grep "pathname=newfile.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process works with added file (empty old side)' '
	cat >added.c <<-\EOF &&
	int added(void) { return 1; }
	EOF
	git add added.c &&

	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --cached -- added.c >actual 2>stderr &&
	test_grep "added" actual &&
	test_grep "pathname=added.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process works with deleted file (empty new side)' '
	git add added.c &&
	git commit -m "commit added.c" &&
	git rm added.c &&

	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --cached -- added.c >actual 2>stderr &&
	test_grep "deleted file" actual &&
	test_grep "pathname=added.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process skipped for binary files' '
	printf "\\0binary" >binary.c &&
	git add binary.c &&
	git commit -m "add binary" &&
	printf "\\0changed" >binary.c &&

	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff -- binary.c >actual &&
	test_grep "Binary files" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process not consulted for unmatched driver' '
	echo "not tracked by cdiff" >unmatched.txt &&
	git add unmatched.txt &&
	git commit -m "add unmatched.txt" &&

	echo "modified" >unmatched.txt &&

	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff -- unmatched.txt >actual &&
	test_grep "modified" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'multiple drivers use separate processes' '
	echo "*.h diff=hdiff" >>.gitattributes &&
	git add .gitattributes &&

	cat >multi.h <<-\EOF &&
	int header(void) { return 1; }
	EOF
	git add multi.h &&
	git commit -m "add multi.h" &&

	cat >multi.h <<-\EOF &&
	int header(void) { return 2; }
	EOF

	test_when_finished "rm -f backend-c.log backend-h.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend-c.log" \
	    -c diff.hdiff.process="$BACKEND --log=backend-h.log" \
		diff -- newfile.c multi.h >actual 2>stderr &&
	test_grep "pathname=newfile.c" backend-c.log &&
	test_grep "pathname=multi.h" backend-h.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process works alongside textconv' '
	write_script uppercase-filter <<-\EOF &&
	tr "a-z" "A-Z" <"$1"
	EOF

	cat >textconv.c <<-\EOF &&
	hello world
	EOF
	git add textconv.c &&
	git commit -m "add textconv.c" &&

	cat >textconv.c <<-\EOF &&
	goodbye world
	EOF

	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.textconv="./uppercase-filter" \
	    -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff -- textconv.c >actual 2>stderr &&
	# The diff process receives textconv-transformed (uppercase) content.
	test_grep "pathname=textconv.c" backend.log &&
	test_grep "old=HELLO WORLD" backend.log &&
	test_grep "new=GOODBYE WORLD" backend.log &&
	test_must_be_empty stderr
'

#
# Downstream features: word diff, log, equivalent files, exit code.
#

test_expect_success 'diff process with --word-diff' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --word-diff worddiff.c >actual 2>stderr &&
	test_grep "\[-1;-\]" actual &&
	test_grep "{+999;+}" actual &&
	test_grep "pathname=worddiff.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process works with git log -p' '
	# With no-hunks mode, the tool says the files are equivalent,
	# so log -p should show the commit but no diff content.
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=no-hunks --log=backend.log" \
		log -1 -p -- logtest.c >actual 2>stderr &&
	test_grep "change logtest.c" actual &&
	test_grep ! "return 2" actual &&
	test_grep "command=hunks pathname=logtest.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process no hunks suppresses diff output' '
	cat >nohunks.c <<-\EOF &&
	int zero(void) { return 0; }
	EOF
	git add nohunks.c &&
	git commit -m "add nohunks.c" &&

	cat >nohunks.c <<-\EOF &&
	int zero(void) { return 999; }
	EOF

	git -c diff.cdiff.process="$BACKEND --mode=no-hunks" \
		diff nohunks.c >actual &&
	test_must_be_empty actual
'

test_expect_success 'diff process no hunks with --exit-code returns success' '
	git -c diff.cdiff.process="$BACKEND --mode=no-hunks" \
		diff --exit-code nohunks.c
'

test_expect_success 'diff process with --exit-code and hunks returns failure' '
	test_expect_code 1 git -c diff.cdiff.process="$BACKEND" \
		diff --exit-code newfile.c
'

#
# Bypass mechanisms: flags and commands that skip the diff process.
#

test_expect_success 'diff process bypassed by --diff-algorithm' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --diff-algorithm=patience worddiff.c >actual &&
	test_grep "return 999" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process bypassed by --no-ext-diff' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --no-ext-diff worddiff.c >actual &&
	test_grep "return 999" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process not used by format-patch' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		format-patch -1 --stdout -- logtest.c >actual &&
	test_grep "return 2" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'diff process not used by --stat' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --log=backend.log" \
		diff --stat worddiff.c >actual &&
	test_grep "worddiff.c" actual &&
	test_path_is_missing backend.log
'

#
# Error handling and fallback.
#

test_expect_success 'diff process fallback on tool error status' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=error --log=backend.log" \
		diff boundary.c >actual 2>stderr &&
	# Fallback produces the full builtin diff (both change regions).
	test_grep "^-OLD5" actual &&
	test_grep "^+NEW5" actual &&
	test_grep "^-OLD9" actual &&
	test_grep "^+NEW9" actual &&
	# Tool was contacted (it replied with error, not crash).
	test_grep "command=hunks pathname=boundary.c" backend.log &&
	test_grep "diff process.*failed" stderr
'

test_expect_success 'diff process error keeps tool available for next file' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=error --log=backend.log" \
		diff -- one.c two.c >actual 2>stderr &&
	# Unlike abort, error keeps the tool available: both files
	# are sent to the tool (and both fall back).
	test_grep "pathname=one.c" backend.log &&
	test_grep "pathname=two.c" backend.log &&
	test_grep "return 10" actual &&
	test_grep "return 20" actual &&
	test_grep "diff process.*failed" stderr
'

test_expect_success 'diff process abort disables for session' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=abort --log=backend.log" \
		diff -- one.c two.c >actual 2>stderr &&
	# Both files should still produce diff output via fallback.
	test_grep "return 10" actual &&
	test_grep "return 20" actual &&
	# The tool aborts on the first file and git clears its
	# capability.  The second file never contacts the tool.
	test_grep "pathname=one.c" backend.log &&
	test_grep ! "pathname=two.c" backend.log &&
	test_must_be_empty stderr
'

test_expect_success 'diff process fallback on tool crash' '
	git -c diff.cdiff.process="$BACKEND --mode=crash" \
		diff boundary.c >actual 2>stderr &&
	test_grep "^-OLD5" actual &&
	test_grep "^+NEW5" actual &&
	test_grep "^-OLD9" actual &&
	test_grep "^+NEW9" actual &&
	# Crash is a communication failure, so a warning is emitted.
	test_grep "diff process.*failed" stderr
'

test_expect_success 'diff process startup failure only warns once' '
	git -c diff.cdiff.process="/nonexistent/tool" \
		diff -- one.c two.c >actual 2>stderr &&
	# Both files produce diff output via fallback.
	test_grep "return 10" actual &&
	test_grep "return 20" actual &&
	# Sentinel prevents repeated warnings: only one, not one per file.
	test_grep "diff process.*failed" stderr >warnings &&
	test_line_count = 1 warnings
'


test_expect_success 'diff process fallback on bad hunks' '
	git -c diff.cdiff.process="$BACKEND --mode=bad-hunk" \
		diff boundary.c >actual 2>stderr &&
	test_grep "^-OLD5" actual &&
	test_grep "^+NEW5" actual &&
	test_grep "^-OLD9" actual &&
	test_grep "^+NEW9" actual &&
	test_grep "exceeds.*lines" stderr
'

test_expect_success 'diff process fallback on mismatched unchanged totals' '
	cat >synctest.c <<-\EOF &&
	line1
	line2
	line3
	EOF
	git add synctest.c &&
	git commit -m "add synctest.c" &&

	cat >synctest.c <<-\EOF &&
	line1
	changed
	line3
	EOF

	# bad-sync reports hunk 1 2 1 1: marks 2 old lines and 1 new
	# line as changed, leaving 1 unchanged old vs 2 unchanged new.
	# The synchronization invariant fails and git falls back.
	git -c diff.cdiff.process="$BACKEND --mode=bad-sync" \
		diff synctest.c >actual 2>stderr &&
	test_grep "changed" actual &&
	test_grep "unchanged line count mismatch" stderr
'

test_expect_success 'diff process fallback on overlapping hunks' '
	# boundary.c has 10 lines, so both hunks are in bounds
	# but they overlap at lines 3-5, triggering the ordering check.
	git -c diff.cdiff.process="$BACKEND --mode=overlap" \
		diff boundary.c >actual 2>stderr &&
	test_grep "NEW5" actual &&
	test_grep "overlaps with previous" stderr
'

test_expect_success 'diff process fallback on malformed hunk line' '
	git -c diff.cdiff.process="$BACKEND --mode=bad-parse" \
		diff boundary.c >actual 2>stderr &&
	test_grep "^-OLD5" actual &&
	test_grep "^+NEW5" actual
'

test_expect_success 'diff process skipped when tool omits capability' '
	git -c diff.cdiff.process="$BACKEND --mode=no-cap" \
		diff boundary.c >actual 2>stderr &&
	test_grep "^-OLD5" actual &&
	test_grep "^+NEW5" actual &&
	test_must_be_empty stderr
'

#
# Blame integration.
#

test_expect_success 'blame uses tool-provided hunks' '
	cat >blame-hunk.c <<-\EOF &&
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
	git add blame-hunk.c &&
	git commit -m "add blame-hunk.c" &&
	ORIG=$(git rev-parse --short HEAD) &&

	cat >blame-hunk.c <<-\EOF &&
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
	git add blame-hunk.c &&
	git commit -m "change blame-hunk.c" &&
	CHANGE=$(git rev-parse --short HEAD) &&

	# With fixed-hunk mode the tool reports only lines 5-6 as changed,
	# so blame should attribute lines 9-10 to the original commit
	# even though the builtin diff would show them as changed.
	git -c diff.cdiff.process="$BACKEND --mode=fixed-hunk" \
		blame blame-hunk.c >actual &&
	sed -n "9p" actual >line9 &&
	sed -n "10p" actual >line10 &&
	test_grep "$ORIG" line9 &&
	test_grep "$ORIG" line10 &&
	sed -n "5p" actual >line5 &&
	sed -n "6p" actual >line6 &&
	test_grep "$CHANGE" line5 &&
	test_grep "$CHANGE" line6
'

test_expect_success 'blame skips commits with no hunks from diff process' '
	cat >blame.c <<-\EOF &&
	int main(void) {
	return 0;
	}
	EOF
	git add blame.c &&
	git commit -m "add blame.c" &&
	ORIG_COMMIT=$(git rev-parse --short HEAD) &&

	cat >blame.c <<-\EOF &&
	int main(void)
	{
	return 0;
	}
	EOF
	git add blame.c &&
	git commit -m "reformat blame.c" &&
	BLAME_COMMIT=$(git rev-parse --short HEAD) &&

	# Without no-hunks mode, blame attributes the change.
	git blame blame.c >without &&
	test_grep "$BLAME_COMMIT" without &&

	# With no-hunks mode, the process considers the files equivalent
	# and blame skips the reformat commit, attributing to the original.
	git -c diff.cdiff.process="$BACKEND --mode=no-hunks" \
		blame blame.c >with &&
	test_grep ! "$BLAME_COMMIT" with &&
	test_grep "$ORIG_COMMIT" with
'

test_expect_success 'blame --no-ext-diff bypasses diff process' '
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=no-hunks --log=backend.log" \
		blame --no-ext-diff blame.c >actual &&
	# Without the process, blame attributes the reformat commit normally.
	test_grep "$BLAME_COMMIT" actual &&
	test_path_is_missing backend.log
'

test_expect_success 'blame --no-ext-diff uses builtin hunks' '
	# fixed-hunk mode would narrow blame to lines 5-6, but
	# --no-ext-diff should bypass it and use the builtin diff.
	test_when_finished "rm -f backend.log" &&
	git -c diff.cdiff.process="$BACKEND --mode=fixed-hunk --log=backend.log" \
		blame --no-ext-diff blame-hunk.c >actual &&
	# Builtin diff attributes lines 9-10 to the change commit.
	sed -n "9p" actual >line9 &&
	test_grep "$CHANGE" line9 &&
	test_path_is_missing backend.log
'

test_done
