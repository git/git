#!/bin/sh

test_description='basic symbolic-ref tests'
. ./test-lib.sh

# If the tests munging HEAD fail, they can break detection of
# the but repo, meaning that further tests will operate on
# the surrounding but repo instead of the trash directory.
reset_to_sane() {
	rm -rf .but &&
	"$TAR" xf .but.tar
}

test_expect_success 'setup' '
	but symbolic-ref HEAD refs/heads/foo &&
	test_cummit file &&
	"$TAR" cf .but.tar .but/
'

test_expect_success 'symbolic-ref read/write roundtrip' '
	but symbolic-ref HEAD refs/heads/read-write-roundtrip &&
	echo refs/heads/read-write-roundtrip >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref refuses non-ref for HEAD' '
	test_must_fail but symbolic-ref HEAD foo
'

reset_to_sane

test_expect_success 'symbolic-ref refuses bare sha1' '
	test_must_fail but symbolic-ref HEAD $(but rev-parse HEAD)
'

reset_to_sane

test_expect_success 'HEAD cannot be removed' '
	test_must_fail but symbolic-ref -d HEAD
'

reset_to_sane

test_expect_success 'symbolic-ref can be deleted' '
	but symbolic-ref NOTHEAD refs/heads/foo &&
	but symbolic-ref -d NOTHEAD &&
	but rev-parse refs/heads/foo &&
	test_must_fail but symbolic-ref NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref can delete dangling symref' '
	but symbolic-ref NOTHEAD refs/heads/missing &&
	but symbolic-ref -d NOTHEAD &&
	test_must_fail but rev-parse refs/heads/missing &&
	test_must_fail but symbolic-ref NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete missing FOO' '
	echo "fatal: Cannot delete FOO, not a symbolic ref" >expect &&
	test_must_fail but symbolic-ref -d FOO >actual 2>&1 &&
	test_cmp expect actual
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete real ref' '
	echo "fatal: Cannot delete refs/heads/foo, not a symbolic ref" >expect &&
	test_must_fail but symbolic-ref -d refs/heads/foo >actual 2>&1 &&
	but rev-parse --verify refs/heads/foo &&
	test_cmp expect actual
'
reset_to_sane

test_expect_success 'create large ref name' '
	# make 256+ character ref; some systems may not handle that,
	# so be gentle
	long=0123456789abcdef &&
	long=$long/$long/$long/$long &&
	long=$long/$long/$long/$long &&
	long_ref=refs/heads/$long &&
	tree=$(but write-tree) &&
	cummit=$(echo foo | but cummit-tree $tree) &&
	if but update-ref $long_ref $cummit; then
		test_set_prereq LONG_REF
	else
		echo >&2 "long refs not supported"
	fi
'

test_expect_success LONG_REF 'symbolic-ref can point to large ref name' '
	but symbolic-ref HEAD $long_ref &&
	echo $long_ref >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success LONG_REF 'we can parse long symbolic ref' '
	echo $cummit >expect &&
	but rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref reports failure in exit code' '
	test_when_finished "rm -f .but/HEAD.lock" &&
	>.but/HEAD.lock &&
	test_must_fail but symbolic-ref HEAD refs/heads/whatever
'

test_expect_success 'symbolic-ref writes reflog entry' '
	but checkout -b log1 &&
	test_cummit one &&
	but checkout -b log2  &&
	test_cummit two &&
	but checkout --orphan orphan &&
	but symbolic-ref -m create HEAD refs/heads/log1 &&
	but symbolic-ref -m update HEAD refs/heads/log2 &&
	cat >expect <<-\EOF &&
	update
	create
	EOF
	but log --format=%gs -g -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref does not create ref d/f conflicts' '
	but checkout -b df &&
	test_cummit df &&
	test_must_fail but symbolic-ref refs/heads/df/conflict refs/heads/df &&
	but pack-refs --all --prune &&
	test_must_fail but symbolic-ref refs/heads/df/conflict refs/heads/df
'

test_expect_success 'symbolic-ref can overwrite pointer to invalid name' '
	test_when_finished reset_to_sane &&
	head=$(but rev-parse HEAD) &&
	but symbolic-ref HEAD refs/heads/outer &&
	test_when_finished "but update-ref -d refs/heads/outer/inner" &&
	but update-ref refs/heads/outer/inner $head &&
	but symbolic-ref HEAD refs/heads/unrelated
'

test_expect_success 'symbolic-ref can resolve d/f name (EISDIR)' '
	test_when_finished reset_to_sane &&
	head=$(but rev-parse HEAD) &&
	but symbolic-ref HEAD refs/heads/outer/inner &&
	test_when_finished "but update-ref -d refs/heads/outer" &&
	but update-ref refs/heads/outer $head &&
	echo refs/heads/outer/inner >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref can resolve d/f name (ENOTDIR)' '
	test_when_finished reset_to_sane &&
	head=$(but rev-parse HEAD) &&
	but symbolic-ref HEAD refs/heads/outer &&
	test_when_finished "but update-ref -d refs/heads/outer/inner" &&
	but update-ref refs/heads/outer/inner $head &&
	echo refs/heads/outer >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_done
