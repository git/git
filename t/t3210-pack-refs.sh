#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
# Copyright (c) 2006 Christian Couder
#

test_description='but pack-refs should not change the branch semantic

This test runs but pack-refs and but show-ref and checks that the branch
semantic is still the same.
'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'enable reflogs' '
	but config core.logallrefupdates true
'

test_expect_success \
    'prepare a trivial repository' \
    'echo Hello > A &&
     but update-index --add A &&
     but cummit -m "Initial cummit." &&
     HEAD=$(but rev-parse --verify HEAD)'

SHA1=

test_expect_success \
    'see if but show-ref works as expected' \
    'but branch a &&
     SHA1=$(cat .but/refs/heads/a) &&
     echo "$SHA1 refs/heads/a" >expect &&
     but show-ref a >result &&
     test_cmp expect result'

test_expect_success \
    'see if a branch still exists when packed' \
    'but branch b &&
     but pack-refs --all &&
     rm -f .but/refs/heads/b &&
     echo "$SHA1 refs/heads/b" >expect &&
     but show-ref b >result &&
     test_cmp expect result'

test_expect_success 'but branch c/d should barf if branch c exists' '
     but branch c &&
     but pack-refs --all &&
     rm -f .but/refs/heads/c &&
     test_must_fail but branch c/d
'

test_expect_success \
    'see if a branch still exists after but pack-refs --prune' \
    'but branch e &&
     but pack-refs --all --prune &&
     echo "$SHA1 refs/heads/e" >expect &&
     but show-ref e >result &&
     test_cmp expect result'

test_expect_success 'see if but pack-refs --prune remove ref files' '
     but branch f &&
     but pack-refs --all --prune &&
     ! test -f .but/refs/heads/f
'

test_expect_success 'see if but pack-refs --prune removes empty dirs' '
     but branch r/s/t &&
     but pack-refs --all --prune &&
     ! test -e .but/refs/heads/r
'

test_expect_success \
    'but branch g should work when but branch g/h has been deleted' \
    'but branch g/h &&
     but pack-refs --all --prune &&
     but branch -d g/h &&
     but branch g &&
     but pack-refs --all &&
     but branch -d g'

test_expect_success 'but branch i/j/k should barf if branch i exists' '
     but branch i &&
     but pack-refs --all --prune &&
     test_must_fail but branch i/j/k
'

test_expect_success \
    'test but branch k after branch k/l/m and k/lm have been deleted' \
    'but branch k/l &&
     but branch k/lm &&
     but branch -d k/l &&
     but branch k/l/m &&
     but branch -d k/l/m &&
     but branch -d k/lm &&
     but branch k'

test_expect_success \
    'test but branch n after some branch deletion and pruning' \
    'but branch n/o &&
     but branch n/op &&
     but branch -d n/o &&
     but branch n/o/p &&
     but branch -d n/op &&
     but pack-refs --all --prune &&
     but branch -d n/o/p &&
     but branch n'

test_expect_success \
	'see if up-to-date packed refs are preserved' \
	'but branch q &&
	 but pack-refs --all --prune &&
	 but update-ref refs/heads/q refs/heads/q &&
	 ! test -f .but/refs/heads/q'

test_expect_success 'pack, prune and repack' '
	but tag foo &&
	but pack-refs --all --prune &&
	but show-ref >all-of-them &&
	but pack-refs &&
	but show-ref >again &&
	test_cmp all-of-them again
'

test_expect_success 'explicit pack-refs with dangling packed reference' '
	but cummit --allow-empty -m "soon to be garbage-collected" &&
	but pack-refs --all &&
	but reset --hard HEAD^ &&
	but reflog expire --expire=all --all &&
	but prune --expire=all &&
	but pack-refs --all 2>result &&
	test_must_be_empty result
'

test_expect_success 'delete ref with dangling packed version' '
	but checkout -b lamb &&
	but cummit --allow-empty -m "future garbage" &&
	but pack-refs --all &&
	but reset --hard HEAD^ &&
	but checkout main &&
	but reflog expire --expire=all --all &&
	but prune --expire=all &&
	but branch -d lamb 2>result &&
	test_must_be_empty result
'

test_expect_success 'delete ref while another dangling packed ref' '
	but branch lamb &&
	but cummit --allow-empty -m "future garbage" &&
	but pack-refs --all &&
	but reset --hard HEAD^ &&
	but reflog expire --expire=all --all &&
	but prune --expire=all &&
	but branch -d lamb 2>result &&
	test_must_be_empty result
'

test_expect_success 'pack ref directly below refs/' '
	but update-ref refs/top HEAD &&
	but pack-refs --all --prune &&
	grep refs/top .but/packed-refs &&
	test_path_is_missing .but/refs/top
'

test_expect_success 'do not pack ref in refs/bisect' '
	but update-ref refs/bisect/local HEAD &&
	but pack-refs --all --prune &&
	! grep refs/bisect/local .but/packed-refs >/dev/null &&
	test_path_is_file .but/refs/bisect/local
'

test_expect_success 'disable reflogs' '
	but config core.logallrefupdates false &&
	rm -rf .but/logs
'

test_expect_success 'create packed foo/bar/baz branch' '
	but branch foo/bar/baz &&
	but pack-refs --all --prune &&
	test_path_is_missing .but/refs/heads/foo/bar/baz &&
	test_must_fail but reflog exists refs/heads/foo/bar/baz
'

test_expect_success 'notice d/f conflict with existing directory' '
	test_must_fail but branch foo &&
	test_must_fail but branch foo/bar
'

test_expect_success 'existing directory reports concrete ref' '
	test_must_fail but branch foo 2>stderr &&
	test_i18ngrep refs/heads/foo/bar/baz stderr
'

test_expect_success 'notice d/f conflict with existing ref' '
	test_must_fail but branch foo/bar/baz/extra &&
	test_must_fail but branch foo/bar/baz/lots/of/extra/components
'

test_expect_success 'reject packed-refs with unterminated line' '
	cp .but/packed-refs .but/packed-refs.bak &&
	test_when_finished "mv .but/packed-refs.bak .but/packed-refs" &&
	printf "%s" "$HEAD refs/zzzzz" >>.but/packed-refs &&
	echo "fatal: unterminated line in .but/packed-refs: $HEAD refs/zzzzz" >expected_err &&
	test_must_fail but for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'reject packed-refs containing junk' '
	cp .but/packed-refs .but/packed-refs.bak &&
	test_when_finished "mv .but/packed-refs.bak .but/packed-refs" &&
	printf "%s\n" "bogus content" >>.but/packed-refs &&
	echo "fatal: unexpected line in .but/packed-refs: bogus content" >expected_err &&
	test_must_fail but for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'reject packed-refs with a short SHA-1' '
	cp .but/packed-refs .but/packed-refs.bak &&
	test_when_finished "mv .but/packed-refs.bak .but/packed-refs" &&
	printf "%.7s %s\n" $HEAD refs/zzzzz >>.but/packed-refs &&
	printf "fatal: unexpected line in .but/packed-refs: %.7s %s\n" $HEAD refs/zzzzz >expected_err &&
	test_must_fail but for-each-ref >out 2>err &&
	test_cmp expected_err err
'

test_expect_success 'timeout if packed-refs.lock exists' '
	LOCK=.but/packed-refs.lock &&
	>"$LOCK" &&
	test_when_finished "rm -f $LOCK" &&
	test_must_fail but pack-refs --all --prune
'

test_expect_success 'retry acquiring packed-refs.lock' '
	LOCK=.but/packed-refs.lock &&
	>"$LOCK" &&
	test_when_finished "wait && rm -f $LOCK" &&
	{
		( sleep 1 && rm -f $LOCK ) &
	} &&
	but -c core.packedrefstimeout=3000 pack-refs --all --prune
'

test_expect_success SYMLINKS 'pack symlinked packed-refs' '
	# First make sure that symlinking works when reading:
	but update-ref refs/heads/lossy refs/heads/main &&
	but for-each-ref >all-refs-before &&
	mv .but/packed-refs .but/my-deviant-packed-refs &&
	ln -s my-deviant-packed-refs .but/packed-refs &&
	but for-each-ref >all-refs-linked &&
	test_cmp all-refs-before all-refs-linked &&
	but pack-refs --all --prune &&
	but for-each-ref >all-refs-packed &&
	test_cmp all-refs-before all-refs-packed &&
	test -h .but/packed-refs &&
	test "$(test_readlink .but/packed-refs)" = "my-deviant-packed-refs"
'

test_done
