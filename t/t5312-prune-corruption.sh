#!/bin/sh

test_description='
Test pruning of repositories with minor corruptions. The goal
here is that we should always be erring on the side of safety. So
if we see, for example, a ref with a bogus name, it is OK either to
bail out or to proceed using it as a reachable tip, but it is _not_
OK to proceed as if it did not exist. Otherwise we might silently
delete objects that cannot be recovered.

Note that we do assert command failure in these cases, because that is
what currently happens. If that changes, these tests should be revisited.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'disable reflogs' '
	but config core.logallrefupdates false &&
	but reflog expire --expire=all --all
'

create_bogus_ref () {
	test-tool ref-store main update-ref msg "refs/heads/bogus..name" $bogus $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	test_when_finished "test-tool ref-store main delete-refs REF_NO_DEREF msg refs/heads/bogus..name"
}

test_expect_success 'create history reachable only from a bogus-named ref' '
	test_tick && but cummit --allow-empty -m main &&
	base=$(but rev-parse HEAD) &&
	test_tick && but cummit --allow-empty -m bogus &&
	bogus=$(but rev-parse HEAD) &&
	but cat-file cummit $bogus >saved &&
	but reset --hard HEAD^
'

test_expect_success 'pruning does not drop bogus object' '
	test_when_finished "but hash-object -w -t cummit saved" &&
	create_bogus_ref &&
	test_must_fail but prune --expire=now &&
	but cat-file -e $bogus
'

test_expect_success 'put bogus object into pack' '
	but tag reachable $bogus &&
	but repack -ad &&
	but tag -d reachable &&
	but cat-file -e $bogus
'

test_expect_success 'non-destructive repack bails on bogus ref' '
	create_bogus_ref &&
	test_must_fail but repack -adk
'

test_expect_success 'GIT_REF_PARANOIA=0 overrides safety' '
	create_bogus_ref &&
	GIT_REF_PARANOIA=0 but repack -adk
'


test_expect_success 'destructive repack keeps packed object' '
	create_bogus_ref &&
	test_must_fail but repack -Ad --unpack-unreachable=now &&
	but cat-file -e $bogus &&
	test_must_fail but repack -ad &&
	but cat-file -e $bogus
'

test_expect_success 'destructive repack not confused by dangling symref' '
	test_when_finished "but symbolic-ref -d refs/heads/dangling" &&
	but symbolic-ref refs/heads/dangling refs/heads/does-not-exist &&
	but repack -ad &&
	test_must_fail but cat-file -e $bogus
'

# We create two new objects here, "one" and "two". Our
# main branch points to "two", which is deleted,
# corrupting the repository. But we'd like to make sure
# that the otherwise unreachable "one" is not pruned
# (since it is the user's best bet for recovering
# from the corruption).
#
# Note that we also point HEAD somewhere besides "two",
# as we want to make sure we test the case where we
# pick up the reference to "two" by iterating the refs,
# not by resolving HEAD.
test_expect_success 'create history with missing tip cummit' '
	test_tick && but cummit --allow-empty -m one &&
	recoverable=$(but rev-parse HEAD) &&
	but cat-file cummit $recoverable >saved &&
	test_tick && but cummit --allow-empty -m two &&
	missing=$(but rev-parse HEAD) &&
	but checkout --detach $base &&
	rm .but/objects/$(echo $missing | sed "s,..,&/,") &&
	test_must_fail but cat-file -e $missing
'

test_expect_success 'pruning with a corrupted tip does not drop history' '
	test_when_finished "but hash-object -w -t cummit saved" &&
	test_must_fail but prune --expire=now &&
	but cat-file -e $recoverable
'

test_expect_success 'pack-refs does not silently delete broken loose ref' '
	but pack-refs --all --prune &&
	echo $missing >expect &&
	but rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

# we do not want to count on running pack-refs to
# actually pack it, as it is perfectly reasonable to
# skip processing a broken ref
test_expect_success REFFILES 'create packed-refs file with broken ref' '
	rm -f .but/refs/heads/main &&
	cat >.but/packed-refs <<-EOF &&
	$missing refs/heads/main
	$recoverable refs/heads/other
	EOF
	echo $missing >expect &&
	but rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success REFFILES 'pack-refs does not silently delete broken packed ref' '
	but pack-refs --all --prune &&
	but rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success REFFILES  'pack-refs does not drop broken refs during deletion' '
	but update-ref -d refs/heads/other &&
	but rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_done
