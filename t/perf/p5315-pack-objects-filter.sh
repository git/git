#!/bin/sh

test_description='Tests pack-objects performance with filters and --path-walk'
. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'setup filter inputs' '
	# Sample a few depth-2 directories from the test repo to build
	# a cone-mode sparse-checkout definition.  The sampling picks
	# directories at evenly-spaced positions so the choice is stable
	# and scales to repos of any shape.

	git ls-tree -d HEAD >top-entries &&
	grep "^040000" top-entries |
		awk "{print \$4;}" >top-dirs &&
	top_nr=$(wc -l <top-dirs) &&

	while read tdir
	do
		git ls-tree -d --format="$tdir/%(path)" "HEAD:$tdir" || return 1
	done <top-dirs >depth2-dirs &&

	d2_nr=$(wc -l <depth2-dirs) &&

	if test "$d2_nr" -ge 2
	then
		# Pick two directories from evenly-spaced positions.
		first=$(sed -n "1p" depth2-dirs) &&
		mid=$(sed -n "$((d2_nr / 2 + 1))p" depth2-dirs) &&

		p1=$(dirname "$first") &&
		p2=$(dirname "$mid") &&

		# Build cone-mode sparse-checkout patterns.
		{
			echo "/*" &&
			echo "!/*/" &&
			echo "/$p1/" &&
			echo "!/$p1/*/" &&
			if test "$p1" != "$p2"
			then
				echo "/$p2/" &&
				echo "!/$p2/*/"
			fi &&
			echo "/$first/" &&
			if test "$first" != "$mid"
			then
				echo "/$mid/"
			fi
		} >sparse-patterns &&

		git hash-object -w sparse-patterns >sparse-oid &&
		echo "Sparse cone: $first $mid" &&
		cat sparse-patterns &&
		test_set_prereq SPARSE_OID
	elif test "$top_nr" -ge 1
	then
		# Fallback: use a single top-level directory.
		first=$(sed -n "1p" top-dirs) &&
		{
			echo "/*" &&
			echo "!/*/" &&
			echo "/$first/"
		} >sparse-patterns &&

		git hash-object -w sparse-patterns >sparse-oid &&
		echo "Sparse cone: $first" &&
		cat sparse-patterns &&
		test_set_prereq SPARSE_OID
	fi
'

test_perf 'repack (no filter)' '
	git pack-objects --stdout --no-reuse-delta --revs --all </dev/null >pk
'

test_size 'repack size (no filter)' '
	test_file_size pk
'

test_perf 'repack (no filter, --path-walk)' '
	git pack-objects --stdout --no-reuse-delta --revs --all --path-walk </dev/null >pk
'

test_size 'repack size (no filter, --path-walk)' '
	test_file_size pk
'

test_perf 'repack (blob:none)' '
	git pack-objects --stdout --no-reuse-delta --revs --all --filter=blob:none </dev/null >pk
'

test_size 'repack size (blob:none)' '
	test_file_size pk
'

test_perf 'repack (blob:none, --path-walk)' '
	git pack-objects --stdout --no-reuse-delta --revs --all --path-walk \
		--filter=blob:none </dev/null >pk
'

test_size 'repack size (blob:none, --path-walk)' '
	test_file_size pk
'

test_perf 'repack (sparse:oid)' \
	--prereq SPARSE_OID '
	git pack-objects --stdout --no-reuse-delta --revs --all \
		--filter=sparse:oid=$(cat sparse-oid) </dev/null >pk
'

test_size 'repack size (sparse:oid)' \
	--prereq SPARSE_OID '
	test_file_size pk
'

test_perf 'repack (sparse:oid, --path-walk)' \
	--prereq SPARSE_OID '
	git pack-objects --stdout --no-reuse-delta --revs --all --path-walk \
		--filter=sparse:oid=$(cat sparse-oid) </dev/null >pk
'

test_size 'repack size (sparse:oid, --path-walk)' \
	--prereq SPARSE_OID '
	test_file_size pk
'

test_done
