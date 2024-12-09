#!/bin/sh

test_description='basic symbolic-ref tests'

. ./test-lib.sh

# If the tests munging HEAD fail, they can break detection of
# the git repo, meaning that further tests will operate on
# the surrounding git repo instead of the trash directory.
reset_to_sane() {
	rm -rf .git &&
	"$TAR" xf .git.tar
}

test_expect_success 'setup' '
	git symbolic-ref HEAD refs/heads/foo &&
	test_commit file &&
	"$TAR" cf .git.tar .git
'

test_expect_success 'symbolic-ref read/write roundtrip' '
	git symbolic-ref HEAD refs/heads/read-write-roundtrip &&
	echo refs/heads/read-write-roundtrip >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref refuses non-ref for HEAD' '
	test_must_fail git symbolic-ref HEAD foo
'

reset_to_sane

test_expect_success 'symbolic-ref refuses bare sha1' '
	rev=$(git rev-parse HEAD) &&
	test_must_fail git symbolic-ref HEAD "$rev"
'

reset_to_sane

test_expect_success 'HEAD cannot be removed' '
	test_must_fail git symbolic-ref -d HEAD
'

reset_to_sane

test_expect_success 'symbolic-ref can be deleted' '
	git symbolic-ref NOTHEAD refs/heads/foo &&
	git symbolic-ref -d NOTHEAD &&
	git rev-parse refs/heads/foo &&
	test_must_fail git symbolic-ref NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref can delete dangling symref' '
	git symbolic-ref NOTHEAD refs/heads/missing &&
	git symbolic-ref -d NOTHEAD &&
	test_must_fail git rev-parse refs/heads/missing &&
	test_must_fail git symbolic-ref NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete missing FOO' '
	echo "fatal: Cannot delete FOO, not a symbolic ref" >expect &&
	test_must_fail git symbolic-ref -d FOO >actual 2>&1 &&
	test_cmp expect actual
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete real ref' '
	echo "fatal: Cannot delete refs/heads/foo, not a symbolic ref" >expect &&
	test_must_fail git symbolic-ref -d refs/heads/foo >actual 2>&1 &&
	git rev-parse --verify refs/heads/foo &&
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
	tree=$(git write-tree) &&
	commit=$(echo foo | git commit-tree $tree) &&
	if git update-ref $long_ref $commit; then
		test_set_prereq LONG_REF
	else
		echo >&2 "long refs not supported"
	fi
'

test_expect_success LONG_REF 'symbolic-ref can point to large ref name' '
	git symbolic-ref HEAD $long_ref &&
	echo $long_ref >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success LONG_REF 'we can parse long symbolic ref' '
	echo $commit >expect &&
	git rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref reports failure in exit code' '
	# Create d/f conflict to simulate failure.
	test_must_fail git symbolic-ref refs/heads refs/heads/foo
'

test_expect_success 'symbolic-ref writes reflog entry' '
	git checkout -b log1 &&
	test_commit one &&
	git checkout -b log2  &&
	test_commit two &&
	git checkout --orphan orphan &&
	git symbolic-ref -m create HEAD refs/heads/log1 &&
	git symbolic-ref -m update HEAD refs/heads/log2 &&
	cat >expect <<-\EOF &&
	update
	create
	EOF
	git log --format=%gs -g -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref does not create ref d/f conflicts' '
	git checkout -b df &&
	test_commit df &&
	test_must_fail git symbolic-ref refs/heads/df/conflict refs/heads/df &&
	git pack-refs --all --prune &&
	test_must_fail git symbolic-ref refs/heads/df/conflict refs/heads/df
'

test_expect_success 'symbolic-ref can overwrite pointer to invalid name' '
	test_when_finished reset_to_sane &&
	head=$(git rev-parse HEAD) &&
	git symbolic-ref HEAD refs/heads/outer &&
	test_when_finished "git update-ref -d refs/heads/outer/inner" &&
	git update-ref refs/heads/outer/inner $head &&
	git symbolic-ref HEAD refs/heads/unrelated
'

test_expect_success 'symbolic-ref can resolve d/f name (EISDIR)' '
	test_when_finished reset_to_sane &&
	head=$(git rev-parse HEAD) &&
	git symbolic-ref HEAD refs/heads/outer/inner &&
	test_when_finished "git update-ref -d refs/heads/outer" &&
	git update-ref refs/heads/outer $head &&
	echo refs/heads/outer/inner >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref can resolve d/f name (ENOTDIR)' '
	test_when_finished reset_to_sane &&
	head=$(git rev-parse HEAD) &&
	git symbolic-ref HEAD refs/heads/outer &&
	test_when_finished "git update-ref -d refs/heads/outer/inner" &&
	git update-ref refs/heads/outer/inner $head &&
	echo refs/heads/outer >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref refuses invalid target for non-HEAD' '
	test_must_fail git symbolic-ref refs/heads/invalid foo..bar
'

test_expect_success 'symbolic-ref allows top-level target for non-HEAD' '
	git symbolic-ref refs/heads/top-level ORIG_HEAD &&
	git update-ref ORIG_HEAD HEAD &&
	test_cmp_rev top-level HEAD
'

test_expect_success 'symbolic-ref pointing at another' '
	git update-ref refs/heads/maint-2.37 HEAD &&
	git symbolic-ref refs/heads/maint refs/heads/maint-2.37 &&
	git checkout maint &&

	git symbolic-ref HEAD >actual &&
	echo refs/heads/maint-2.37 >expect &&
	test_cmp expect actual &&

	git symbolic-ref --no-recurse HEAD >actual &&
	echo refs/heads/maint >expect &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref --short handles complex utf8 case' '
	name="测试-加-增加-加-增加" &&
	git symbolic-ref TEST_SYMREF "refs/heads/$name" &&
	# In the real world, we saw problems with this case only
	# when the locale includes UTF-8. Set it here to try to make things as
	# hard as possible for us to pass, but in practice we should do the
	# right thing regardless (and of course some platforms may not even
	# have this locale).
	LC_ALL=en_US.UTF-8 git symbolic-ref --short TEST_SYMREF >actual &&
	echo "$name" >expect &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref --short handles name with suffix' '
	git symbolic-ref TEST_SYMREF "refs/remotes/origin/HEAD" &&
	git symbolic-ref --short TEST_SYMREF >actual &&
	echo "origin" >expect &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref --short handles almost-matching name' '
	git symbolic-ref TEST_SYMREF "refs/headsXfoo" &&
	git symbolic-ref --short TEST_SYMREF >actual &&
	echo "headsXfoo" >expect &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref --short handles name with percent' '
	git symbolic-ref TEST_SYMREF "refs/heads/%foo" &&
	git symbolic-ref --short TEST_SYMREF >actual &&
	echo "%foo" >expect &&
	test_cmp expect actual
'

test_done
