#!/bin/sh

test_description='but merge

Testing pull.* configuration parsing and other things.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 >c0.c &&
	but add c0.c &&
	but cummit -m c0 &&
	but tag c0 &&
	echo c1 >c1.c &&
	but add c1.c &&
	but cummit -m c1 &&
	but tag c1 &&
	but reset --hard c0 &&
	echo c2 >c2.c &&
	but add c2.c &&
	but cummit -m c2 &&
	but tag c2 &&
	but reset --hard c0 &&
	echo c3 >c3.c &&
	but add c3.c &&
	but cummit -m c3 &&
	but tag c3
'

test_expect_success 'pull.rebase not set, ff possible' '
	but reset --hard c0 &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and pull.ff=true' '
	but reset --hard c0 &&
	test_config pull.ff true &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and pull.ff=false' '
	but reset --hard c0 &&
	test_config pull.ff false &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and pull.ff=only' '
	but reset --hard c0 &&
	test_config pull.ff only &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --rebase given' '
	but reset --hard c0 &&
	but pull --rebase . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --no-rebase given' '
	but reset --hard c0 &&
	but pull --no-rebase . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --ff given' '
	but reset --hard c0 &&
	but pull --ff . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --no-ff given' '
	but reset --hard c0 &&
	but pull --no-ff . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --ff-only given' '
	but reset --hard c0 &&
	but pull --ff-only . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set (not-fast-forward)' '
	but reset --hard c2 &&
	test_must_fail but -c color.advice=always pull . c1 2>err &&
	test_decode_color <err >decoded &&
	test_i18ngrep "<YELLOW>hint: " decoded &&
	test_i18ngrep "You have divergent branches" decoded
'

test_expect_success 'pull.rebase not set and pull.ff=true (not-fast-forward)' '
	but reset --hard c2 &&
	test_config pull.ff true &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and pull.ff=false (not-fast-forward)' '
	but reset --hard c2 &&
	test_config pull.ff false &&
	but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and pull.ff=only (not-fast-forward)' '
	but reset --hard c2 &&
	test_config pull.ff only &&
	test_must_fail but pull . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --rebase given (not-fast-forward)' '
	but reset --hard c2 &&
	but pull --rebase . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --no-rebase given (not-fast-forward)' '
	but reset --hard c2 &&
	but pull --no-rebase . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --ff given (not-fast-forward)' '
	but reset --hard c2 &&
	but pull --ff . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --no-ff given (not-fast-forward)' '
	but reset --hard c2 &&
	but pull --no-ff . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_expect_success 'pull.rebase not set and --ff-only given (not-fast-forward)' '
	but reset --hard c2 &&
	test_must_fail but pull --ff-only . c1 2>err &&
	test_i18ngrep ! "You have divergent branches" err
'

test_does_rebase () {
	but reset --hard c2 &&
	but "$@" . c1 &&
	# Check that we actually did a rebase
	but rev-list --count HEAD >actual &&
	but rev-list --merges --count HEAD >>actual &&
	test_write_lines 3 0 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

# Prefers merge over fast-forward
test_does_merge_when_ff_possible () {
	but reset --hard c0 &&
	but "$@" . c1 &&
	# Check that we actually did a merge
	but rev-list --count HEAD >actual &&
	but rev-list --merges --count HEAD >>actual &&
	test_write_lines 3 1 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

# Prefers fast-forward over merge or rebase
test_does_fast_forward () {
	but reset --hard c0 &&
	but "$@" . c1 &&

	# Check that we did not get any merges
	but rev-list --count HEAD >actual &&
	but rev-list --merges --count HEAD >>actual &&
	test_write_lines 2 0 >expect &&
	test_cmp expect actual &&

	# Check that we ended up at c1
	but rev-parse HEAD >actual &&
	but rev-parse c1^{cummit} >expect &&
	test_cmp actual expect &&

	# Remove temporary files
	rm actual expect
}

# Doesn't fail when fast-forward not possible; does a merge
test_falls_back_to_full_merge () {
	but reset --hard c2 &&
	but "$@" . c1 &&
	# Check that we actually did a merge
	but rev-list --count HEAD >actual &&
	but rev-list --merges --count HEAD >>actual &&
	test_write_lines 4 1 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

# Attempts fast forward, which is impossible, and bails
test_attempts_fast_forward () {
	but reset --hard c2 &&
	test_must_fail but "$@" . c1 2>err &&
	test_i18ngrep "Not possible to fast-forward, aborting" err
}

#
# Group 1: Interaction of --ff-only with --[no-]rebase
# (And related interaction of pull.ff=only with pull.rebase)
#
test_expect_success '--ff-only overrides --rebase' '
	test_attempts_fast_forward pull --rebase --ff-only
'

test_expect_success '--ff-only overrides --rebase even if first' '
	test_attempts_fast_forward pull --ff-only --rebase
'

test_expect_success '--ff-only overrides --no-rebase' '
	test_attempts_fast_forward pull --ff-only --no-rebase
'

test_expect_success 'pull.ff=only overrides pull.rebase=true' '
	test_attempts_fast_forward -c pull.ff=only -c pull.rebase=true pull
'

test_expect_success 'pull.ff=only overrides pull.rebase=false' '
	test_attempts_fast_forward -c pull.ff=only -c pull.rebase=false pull
'

# Group 2: --rebase=[!false] overrides --no-ff and --ff
# (And related interaction of pull.rebase=!false and pull.ff=!only)
test_expect_success '--rebase overrides --no-ff' '
	test_does_rebase pull --rebase --no-ff
'

test_expect_success '--rebase overrides --ff' '
	test_does_rebase pull --rebase --ff
'

test_expect_success '--rebase fast-forwards when possible' '
	test_does_fast_forward pull --rebase --ff
'

test_expect_success 'pull.rebase=true overrides pull.ff=false' '
	test_does_rebase -c pull.rebase=true -c pull.ff=false pull
'

test_expect_success 'pull.rebase=true overrides pull.ff=true' '
	test_does_rebase -c pull.rebase=true -c pull.ff=true pull
'

# Group 3: command line flags take precedence over config
test_expect_success '--ff-only takes precedence over pull.rebase=true' '
	test_attempts_fast_forward -c pull.rebase=true pull --ff-only
'

test_expect_success '--ff-only takes precedence over pull.rebase=false' '
	test_attempts_fast_forward -c pull.rebase=false pull --ff-only
'

test_expect_success '--no-rebase takes precedence over pull.ff=only' '
	test_falls_back_to_full_merge -c pull.ff=only pull --no-rebase
'

test_expect_success '--rebase takes precedence over pull.ff=only' '
	test_does_rebase -c pull.ff=only pull --rebase
'

test_expect_success '--rebase overrides pull.ff=true' '
	test_does_rebase -c pull.ff=true pull --rebase
'

test_expect_success '--rebase overrides pull.ff=false' '
	test_does_rebase -c pull.ff=false pull --rebase
'

test_expect_success '--rebase overrides pull.ff unset' '
	test_does_rebase pull --rebase
'

# Group 4: --no-rebase heeds pull.ff=!only or explict --ff or --no-ff

test_expect_success '--no-rebase works with --no-ff' '
	test_does_merge_when_ff_possible pull --no-rebase --no-ff
'

test_expect_success '--no-rebase works with --ff' '
	test_does_fast_forward pull --no-rebase --ff
'

test_expect_success '--no-rebase does ff if pull.ff unset' '
	test_does_fast_forward pull --no-rebase
'

test_expect_success '--no-rebase heeds pull.ff=true' '
	test_does_fast_forward -c pull.ff=true pull --no-rebase
'

test_expect_success '--no-rebase heeds pull.ff=false' '
	test_does_merge_when_ff_possible -c pull.ff=false pull --no-rebase
'

# Group 5: pull.rebase=!false in combination with --no-ff or --ff
test_expect_success 'pull.rebase=true and --no-ff' '
	test_does_rebase -c pull.rebase=true pull --no-ff
'

test_expect_success 'pull.rebase=true and --ff' '
	test_does_rebase -c pull.rebase=true pull --ff
'

test_expect_success 'pull.rebase=false and --no-ff' '
	test_does_merge_when_ff_possible -c pull.rebase=false pull --no-ff
'

test_expect_success 'pull.rebase=false and --ff, ff possible' '
	test_does_fast_forward -c pull.rebase=false pull --ff
'

test_expect_success 'pull.rebase=false and --ff, ff not possible' '
	test_falls_back_to_full_merge -c pull.rebase=false pull --ff
'

# End of groupings for conflicting merge vs. rebase flags/options

test_expect_success 'Multiple heads warns about inability to fast forward' '
	but reset --hard c1 &&
	test_must_fail but pull . c2 c3 2>err &&
	test_i18ngrep "You have divergent branches" err
'

test_expect_success 'Multiple can never be fast forwarded' '
	but reset --hard c0 &&
	test_must_fail but -c pull.ff=only pull . c1 c2 c3 2>err &&
	test_i18ngrep ! "You have divergent branches" err &&
	# In addition to calling out "cannot fast-forward", we very much
	# want the "multiple branches" piece to be called out to users.
	test_i18ngrep "Cannot fast-forward to multiple branches" err
'

test_expect_success 'Cannot rebase with multiple heads' '
	but reset --hard c0 &&
	test_must_fail but -c pull.rebase=true pull . c1 c2 c3 2>err &&
	test_i18ngrep ! "You have divergent branches" err &&
	test_i18ngrep "Cannot rebase onto multiple branches." err
'

test_expect_success 'merge c1 with c2' '
	but reset --hard c1 &&
	test -f c0.c &&
	test -f c1.c &&
	test ! -f c2.c &&
	test ! -f c3.c &&
	but merge c2 &&
	test -f c1.c &&
	test -f c2.c
'

test_expect_success 'fast-forward pull succeeds with "true" in pull.ff' '
	but reset --hard c0 &&
	test_config pull.ff true &&
	but pull . c1 &&
	test "$(but rev-parse HEAD)" = "$(but rev-parse c1)"
'

test_expect_success 'pull.ff=true overrides merge.ff=false' '
	but reset --hard c0 &&
	test_config merge.ff false &&
	test_config pull.ff true &&
	but pull . c1 &&
	test "$(but rev-parse HEAD)" = "$(but rev-parse c1)"
'

test_expect_success 'fast-forward pull creates merge with "false" in pull.ff' '
	but reset --hard c0 &&
	test_config pull.ff false &&
	but pull . c1 &&
	test "$(but rev-parse HEAD^1)" = "$(but rev-parse c0)" &&
	test "$(but rev-parse HEAD^2)" = "$(but rev-parse c1)"
'

test_expect_success 'pull prevents non-fast-forward with "only" in pull.ff' '
	but reset --hard c1 &&
	test_config pull.ff only &&
	test_must_fail but pull . c3
'

test_expect_success 'already-up-to-date pull succeeds with unspecified pull.ff' '
	but reset --hard c1 &&
	but pull . c0 &&
	test "$(but rev-parse HEAD)" = "$(but rev-parse c1)"
'

test_expect_success 'already-up-to-date pull succeeds with "only" in pull.ff' '
	but reset --hard c1 &&
	test_config pull.ff only &&
	but pull . c0 &&
	test "$(but rev-parse HEAD)" = "$(but rev-parse c1)"
'

test_expect_success 'already-up-to-date pull/rebase succeeds with "only" in pull.ff' '
	but reset --hard c1 &&
	test_config pull.ff only &&
	but -c pull.rebase=true pull . c0 &&
	test "$(but rev-parse HEAD)" = "$(but rev-parse c1)"
'

test_expect_success 'merge c1 with c2 (ours in pull.twohead)' '
	but reset --hard c1 &&
	but config pull.twohead ours &&
	but merge c2 &&
	test -f c1.c &&
	! test -f c2.c
'

test_expect_success 'merge c1 with c2 and c3 (recursive in pull.octopus)' '
	but reset --hard c1 &&
	but config pull.octopus "recursive" &&
	test_must_fail but merge c2 c3 &&
	test "$(but rev-parse c1)" = "$(but rev-parse HEAD)"
'

test_expect_success 'merge c1 with c2 and c3 (recursive and octopus in pull.octopus)' '
	but reset --hard c1 &&
	but config pull.octopus "recursive octopus" &&
	but merge c2 c3 &&
	test "$(but rev-parse c1)" != "$(but rev-parse HEAD)" &&
	test "$(but rev-parse c1)" = "$(but rev-parse HEAD^1)" &&
	test "$(but rev-parse c2)" = "$(but rev-parse HEAD^2)" &&
	test "$(but rev-parse c3)" = "$(but rev-parse HEAD^3)" &&
	but diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c
'

conflict_count()
{
	{
		but diff-files --name-only
		but ls-files --unmerged
	} | wc -l
}

# c4 - c5
#    \ c6
#
# There are two conflicts here:
#
# 1) Because foo.c is renamed to bar.c, recursive will handle this,
# resolve won't.
#
# 2) One in conflict.c and that will always fail.

test_expect_success 'setup conflicted merge' '
	but reset --hard c0 &&
	echo A >conflict.c &&
	but add conflict.c &&
	echo contents >foo.c &&
	but add foo.c &&
	but cummit -m c4 &&
	but tag c4 &&
	echo B >conflict.c &&
	but add conflict.c &&
	but mv foo.c bar.c &&
	but cummit -m c5 &&
	but tag c5 &&
	but reset --hard c4 &&
	echo C >conflict.c &&
	but add conflict.c &&
	echo secondline >> foo.c &&
	but add foo.c &&
	but cummit -m c6 &&
	but tag c6
'

# First do the merge with resolve and recursive then verify that
# recursive is chosen.

test_expect_success 'merge picks up the best result' '
	but config --unset-all pull.twohead &&
	but reset --hard c5 &&
	test_must_fail but merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	but reset --hard c5 &&
	test_must_fail but merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	but reset --hard c5 &&
	test_must_fail but merge -s recursive -s resolve c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge picks up the best result (from config)' '
	but config pull.twohead "recursive resolve" &&
	but reset --hard c5 &&
	test_must_fail but merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	but reset --hard c5 &&
	test_must_fail but merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	but reset --hard c5 &&
	test_must_fail but merge c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge errors out on invalid strategy' '
	but config pull.twohead "foobar" &&
	but reset --hard c5 &&
	test_must_fail but merge c6
'

test_expect_success 'merge errors out on invalid strategy' '
	but config --unset-all pull.twohead &&
	but reset --hard c5 &&
	test_must_fail but merge -s "resolve recursive" c6
'

test_done
