#!/bin/sh

test_description='
Test pruning of repositories with minor corruptions. The goal
here is that we should always be erring on the side of safety. So
if we see, for example, a ref with a bogus name, it is OK either to
bail out or to proceed using it as a reachable tip, but it is _not_
OK to proceed as if it did not exist. Otherwise we might silently
delete objects that cannot be recovered.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'disable reflogs' '
	git config core.logallrefupdates false &&
	git reflog expire --expire=all --all
'

test_expect_success 'create history reachable only from a bogus-named ref' '
	test_tick && git commit --allow-empty -m main &&
	base=$(git rev-parse HEAD) &&
	test_tick && git commit --allow-empty -m bogus &&
	bogus=$(git rev-parse HEAD) &&
	git cat-file commit $bogus >saved &&
	echo $bogus >.git/refs/heads/bogus..name &&
	git reset --hard HEAD^
'

test_expect_success 'pruning does not drop bogus object' '
	test_when_finished "git hash-object -w -t commit saved" &&
	test_might_fail git prune --expire=now &&
	verbose git cat-file -e $bogus
'

test_expect_success 'put bogus object into pack' '
	git tag reachable $bogus &&
	git repack -ad &&
	git tag -d reachable &&
	verbose git cat-file -e $bogus
'

test_expect_success 'destructive repack keeps packed object' '
	test_might_fail git repack -Ad --unpack-unreachable=now &&
	verbose git cat-file -e $bogus &&
	test_might_fail git repack -ad &&
	verbose git cat-file -e $bogus
'

# subsequent tests will have different corruptions
test_expect_success 'clean up bogus ref' '
	rm .git/refs/heads/bogus..name
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
test_expect_success 'create history with missing tip commit' '
	test_tick && git commit --allow-empty -m one &&
	recoverable=$(git rev-parse HEAD) &&
	git cat-file commit $recoverable >saved &&
	test_tick && git commit --allow-empty -m two &&
	missing=$(git rev-parse HEAD) &&
	git checkout --detach $base &&
	rm .git/objects/$(echo $missing | sed "s,..,&/,") &&
	test_must_fail git cat-file -e $missing
'

test_expect_success 'pruning with a corrupted tip does not drop history' '
	test_when_finished "git hash-object -w -t commit saved" &&
	test_might_fail git prune --expire=now &&
	verbose git cat-file -e $recoverable
'

test_expect_success 'pack-refs does not silently delete broken loose ref' '
	git pack-refs --all --prune &&
	echo $missing >expect &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

# we do not want to count on running pack-refs to
# actually pack it, as it is perfectly reasonable to
# skip processing a broken ref
test_expect_success 'create packed-refs file with broken ref' '
	rm -f .git/refs/heads/main &&
	cat >.git/packed-refs <<-EOF &&
	$missing refs/heads/main
	$recoverable refs/heads/other
	EOF
	echo $missing >expect &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'pack-refs does not silently delete broken packed ref' '
	git pack-refs --all --prune &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'pack-refs does not drop broken refs during deletion' '
	git update-ref -d refs/heads/other &&
	git rev-parse refs/heads/main >actual &&
	test_cmp expect actual
'

test_done
