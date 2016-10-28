#!/bin/sh

test_description='performance of tag-following with many tags

This tests a fairly pathological case, so rather than rely on a real-world
case, we will construct our own repository. The situation is roughly as
follows.

The parent repository has a large number of tags which are disconnected from
the rest of history. That makes them candidates for tag-following, but we never
actually grab them (and thus they will impact each subsequent fetch).

The child repository is a clone of parent, without the tags, and is at least
one commit behind the parent (meaning that we will fetch one object and then
examine the tags to see if they need followed). Furthermore, it has a large
number of packs.

The exact values of "large" here are somewhat arbitrary; I picked values that
start to show a noticeable performance problem on my machine, but without
taking too long to set up and run the tests.
'
. ./perf-lib.sh

# make a long nonsense history on branch $1, consisting of $2 commits, each
# with a unique file pointing to the blob at $2.
create_history () {
	perl -le '
		my ($branch, $n, $blob) = @ARGV;
		for (1..$n) {
			print "commit refs/heads/$branch";
			print "committer nobody <nobody@example.com> now";
			print "data 4";
			print "foo";
			print "M 100644 $blob $_";
		}
	' "$@" |
	git fast-import --date-format=now
}

# make a series of tags, one per commit in the revision range given by $@
create_tags () {
	git rev-list "$@" |
	perl -lne 'print "create refs/tags/$. $_"' |
	git update-ref --stdin
}

# create $1 nonsense packs, each with a single blob
create_packs () {
	perl -le '
		my ($n) = @ARGV;
		for (1..$n) {
			print "blob";
			print "data <<EOF";
			print "$_";
			print "EOF";
		}
	' "$@" |
	git fast-import &&

	git cat-file --batch-all-objects --batch-check='%(objectname)' |
	while read sha1
	do
		echo $sha1 | git pack-objects .git/objects/pack/pack
	done
}

test_expect_success 'create parent and child' '
	git init parent &&
	git -C parent commit --allow-empty -m base &&
	git clone parent child &&
	git -C parent commit --allow-empty -m trigger-fetch
'

test_expect_success 'populate parent tags' '
	(
		cd parent &&
		blob=$(echo content | git hash-object -w --stdin) &&
		create_history cruft 3000 $blob &&
		create_tags cruft &&
		git branch -D cruft
	)
'

test_expect_success 'create child packs' '
	(
		cd child &&
		git config gc.auto 0 &&
		git config gc.autopacklimit 0 &&
		create_packs 500
	)
'

test_perf 'fetch' '
	# make sure there is something to fetch on each iteration
	git -C child update-ref -d refs/remotes/origin/master &&
	git -C child fetch
'

test_done
