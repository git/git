#!/bin/sh

test_description='test trace2 cmd_ancestry event'

. ./test-lib.sh

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_BRIEF
sane_unset GIT_TRACE2_CONFIG_PARAMS

# Add t/helper directory to PATH so that we can use a relative
# path to run nested instances of test-tool.exe (see 004child).
# This helps with HEREDOC comparisons later.
TTDIR="$GIT_BUILD_DIR/t/helper/" && export TTDIR
PATH="$TTDIR:$PATH" && export PATH

# The 400ancestry helper spawns a child process so that the child
# sees "test-tool" in its process ancestry.  We capture only the
# child's trace2 output to a file.
#
# The tests use git commands that spawn child git processes (e.g.,
# alias resolution) to create a controlled multi-level process tree.
# Because cmd_ancestry walks the real process tree, processes will
# also report ancestors above "test-tool" that depend on the test
# runner environment (e.g., bash, make, tmux).  The filter functions
# below truncate the ancestry at "test-tool", discarding anything
# above it, so only the controlled portion is verified.
#
# On platforms without a real procinfo implementation (the stub),
# no cmd_ancestry event is emitted.  We detect this at runtime and
# skip the format-specific tests accordingly.

# Determine if cmd_ancestry is supported on this platform.
test_expect_success 'detect cmd_ancestry support' '
	test_when_finished "rm -f trace.detect" &&
	GIT_TRACE2_BRIEF=1 GIT_TRACE2="$(pwd)/trace.detect" \
		test-tool trace2 001return 0 &&
	if grep -q "^cmd_ancestry" trace.detect
	then
		test_set_prereq TRACE2_ANCESTRY
	fi
'

# Filter functions for each trace2 target format.
#
# Each extracts cmd_ancestry events, strips format-specific syntax,
# and truncates the ancestor list at the outermost "test-tool"
# (or "test-tool.exe" on Windows), discarding any higher-level
# (uncontrolled) ancestors.
#
# Output is a space-separated list of ancestor names, one line per
# cmd_ancestry event, with the immediate parent listed first:
#
#   test-tool                          (or: test-tool.exe)
#   git test-tool                      (or: git.exe test-tool.exe)
#   git test-tool test-tool            (or: git.exe test-tool.exe test-tool.exe)

if test_have_prereq MINGW
then
	TT=test-tool$X
else
	TT=test-tool
fi

filter_ancestry_normal () {
	sed -n '/^cmd_ancestry/{
		s/^cmd_ancestry //
		s/ <- / /g
		s/\(.*'"$TT"'\) .*/\1/
		p
	}'
}

filter_ancestry_perf () {
	sed -n '/cmd_ancestry/{
		s/.*ancestry:\[//
		s/\]//
		s/\(.*'"$TT"'\) .*/\1/
		p
	}'
}

filter_ancestry_event () {
	sed -n '/"cmd_ancestry"/{
		s/.*"ancestry":\[//
		s/\].*//
		s/"//g
		s/,/ /g
		s/\(.*'"$TT"'\) .*/\1/
		p
	}'
}

# On Windows (MINGW) when running with the bin-wrappers, we also see "sh.exe" in
# the ancestry. We must therefore account for this expected ancestry element in
# the expected output of the tests.
if test_have_prereq MINGW && test -z "$no_bin_wrappers"; then
	SH_TT="sh$X $TT"
else
	SH_TT="$TT"
fi

# Git alias resolution spawns the target command as a child process.
# Using "git -c alias.xyz=version xyz" creates a two-level chain:
#
#   test-tool (400ancestry)
#     -> git (resolves alias xyz -> version)
#          -> git (version)
#
# Both git processes are instrumented and emit cmd_ancestry.  After
# filtering out ancestors above test-tool, we get:
#
#   test-tool                 (from git alias resolver)
#   git test-tool             (from git version)

test_expect_success TRACE2_ANCESTRY 'normal: git alias chain, 2 levels' '
	test_when_finished "rm -f trace.normal actual expect" &&
	test-tool trace2 400ancestry normal "$(pwd)/trace.normal" \
		git -c alias.xyz=version xyz &&
	filter_ancestry_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
	$SH_TT
	git$X $SH_TT
	EOF
	test_cmp expect actual
'

test_expect_success TRACE2_ANCESTRY 'perf: git alias chain, 2 levels' '
	test_when_finished "rm -f trace.perf actual expect" &&
	test-tool trace2 400ancestry perf "$(pwd)/trace.perf" \
		git -c alias.xyz=version xyz &&
	filter_ancestry_perf <trace.perf >actual &&
	cat >expect <<-EOF &&
	$SH_TT
	git$X $SH_TT
	EOF
	test_cmp expect actual
'

test_expect_success TRACE2_ANCESTRY 'event: git alias chain, 2 levels' '
	test_when_finished "rm -f trace.event actual expect" &&
	test-tool trace2 400ancestry event "$(pwd)/trace.event" \
		git -c alias.xyz=version xyz &&
	filter_ancestry_event <trace.event >actual &&
	cat >expect <<-EOF &&
	$SH_TT
	git$X $SH_TT
	EOF
	test_cmp expect actual
'

# Use 004child to add a test-tool layer, creating a three-level chain:
#
#   test-tool (400ancestry)
#     -> test-tool (004child)
#          -> git (resolves alias xyz -> version)
#               -> git (version)
#
# Three instrumented processes emit cmd_ancestry.  After filtering:
#
#   test-tool                  (from test-tool 004child)
#   test-tool test-tool        (from git alias resolver)
#   git test-tool test-tool    (from git version)

test_expect_success TRACE2_ANCESTRY 'normal: deeper chain, 3 levels' '
	test_when_finished "rm -f trace.normal actual expect" &&
	test-tool trace2 400ancestry normal "$(pwd)/trace.normal" \
		test-tool trace2 004child \
			git -c alias.xyz=version xyz &&
	filter_ancestry_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
	$TT
	$SH_TT $TT
	git$X $SH_TT $TT
	EOF
	test_cmp expect actual
'

test_done
