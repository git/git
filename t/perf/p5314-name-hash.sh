#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

test_perf_large_repo

test_size 'paths at head' '
	git ls-tree -r --name-only HEAD >path-list &&
	wc -l <path-list &&
	test-tool name-hash <path-list >name-hashes
'

for version in 1 2
do
	test_size "distinct hash value: v$version" '
		awk "{ print \$$version; }" <name-hashes | sort | \
			uniq -c >name-hash-count &&
		wc -l <name-hash-count
	'

	test_size "maximum multiplicity: v$version" '
		sort -nr <name-hash-count | head -n 1 |	\
			awk "{ print \$1; }"
	'
done

test_done
