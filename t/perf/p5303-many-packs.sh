#!/bin/sh

test_description='performance with large numbers of packs'
. ./perf-lib.sh

test_perf_large_repo

# A real many-pack situation would probably come from having a lot of pushes
# over time. We don't know how big each push would be, but we can fake it by
# just walking the first-parent chain and having every 5 commits be their own
# "push". This isn't _entirely_ accurate, as real pushes would have some
# duplicate objects due to thin-pack fixing, but it's a reasonable
# approximation.
#
# And then all of the rest of the objects can go in a single packfile that
# represents the state before any of those pushes (actually, we'll generate
# that first because in such a setup it would be the oldest pack, and we sort
# the packs by reverse mtime inside git).
repack_into_n () {
	rm -rf staging &&
	mkdir staging &&

	git rev-list --first-parent HEAD |
	sed -n '1~5p' |
	head -n "$1" |
	perl -e 'print reverse <>' \
	>pushes

	# create base packfile
	head -n 1 pushes |
	git pack-objects --delta-base-offset --revs staging/pack

	# and then incrementals between each pair of commits
	last= &&
	while read rev
	do
		if test -n "$last"; then
			{
				echo "$rev" &&
				echo "^$last"
			} |
			git pack-objects --delta-base-offset --revs \
				staging/pack || return 1
		fi
		last=$rev
	done <pushes &&

	# and install the whole thing
	rm -f .git/objects/pack/* &&
	mv staging/* .git/objects/pack/
}

# Pretend we just have a single branch and no reflogs, and that everything is
# in objects/pack; that makes our fake pack-building via repack_into_n()
# much simpler.
test_expect_success 'simplify reachability' '
	tip=$(git rev-parse --verify HEAD) &&
	git for-each-ref --format="option no-deref%0adelete %(refname)" |
	git update-ref --stdin &&
	rm -rf .git/logs &&
	git update-ref refs/heads/master $tip &&
	git symbolic-ref HEAD refs/heads/master &&
	git repack -ad
'

for nr_packs in 1 50 1000
do
	test_expect_success "create $nr_packs-pack scenario" '
		repack_into_n $nr_packs
	'

	test_perf "rev-list ($nr_packs)" '
		git rev-list --objects --all >/dev/null
	'

	# This simulates the interesting part of the repack, which is the
	# actual pack generation, without smudging the on-disk setup
	# between trials.
	test_perf "repack ($nr_packs)" '
		git pack-objects --keep-true-parents \
		  --honor-pack-keep --non-empty --all \
		  --reflog --indexed-objects --delta-base-offset \
		  --stdout </dev/null >/dev/null
	'
done

test_done
