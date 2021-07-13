#!/bin/sh

test_description="Test pickaxe performance"

. ./perf-lib.sh

test_perf_default_repo

# Not --max-count, as that's the number of matching commit, so it's
# unbounded. We want to limit our revision walk here.
from_rev_desc=
from_rev=
max_count=1000
if test_have_prereq EXPENSIVE
then
	max_count=10000
fi
from_rev=" $(git rev-list HEAD | head -n $max_count | tail -n 1).."
from_rev_desc=" <limit-rev>.."

for icase in \
	'' \
	'-i '
do
	# -S (no regex)
	for pattern in \
		'int main' \
		'æ'
	do
		for opts in \
			'-S'
		do
			test_perf "git log $icase$opts'$pattern'$from_rev_desc" "
				git log --pretty=format:%H $icase$opts'$pattern'$from_rev
			"
		done
	done

	# -S (regex)
	for pattern in  \
		'(int|void|null)' \
		'if *\([^ ]+ & ' \
		'[àáâãäåæñøùúûüýþ]'
	do
		for opts in \
			'--pickaxe-regex -S'
		do
			test_perf "git log $icase$opts'$pattern'$from_rev_desc" "
				git log --pretty=format:%H $icase$opts'$pattern'$from_rev
			"
		done
	done

	# -G
	for pattern in  \
		'(int|void|null)' \
		'if *\([^ ]+ & ' \
		'[àáâãäåæñøùúûüýþ]'
	do
		for opts in \
			'-G'
		do
			test_perf "git log $icase$opts'$pattern'$from_rev_desc" "
				git log --pretty=format:%H $icase$opts'$pattern'$from_rev
			"
		done
	done
done

test_done
