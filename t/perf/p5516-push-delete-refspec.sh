#!/bin/sh

test_description='explicit delete refspec matching on push

Measure client-side matching of explicit delete refspecs with "git push
--dry-run" against a server that advertises lots of refs.  An empty client
(no local refs) and a mirror client (full local copy of the server refs)
are tested pushing 1, 10, and 100 refspecs each.

A second set of rows measures one --force-with-lease per refspec for
timing those paths.
'
. ./perf-lib.sh

test_perf_fresh_repo

ref_count=100000

test_expect_success 'create server with many refs and two clients' '
	test_commit base &&
	git clone --bare --ref-format=reftable . server &&
	test_seq -f "create refs/heads/b%d HEAD" $ref_count |
	git -C server update-ref --stdin &&
	git init --bare client_empty &&
	git -C client_empty remote add origin "$PWD/server" &&
	git clone --mirror --ref-format=reftable "$PWD/server" client_mirror &&
	git -C client_mirror config --unset remote.origin.mirror
'

oid=$(git -C server rev-parse HEAD)

for mode in empty mirror
do
	client=client_$mode
	for nr_refspecs in 1 10 100
	do
		test_expect_success "create $mode refspecs: $nr_refspecs" '
			test_seq -f ":refs/heads/b%d" $nr_refspecs >refspecs
		'

		test_perf "$mode:refspecs:$nr_refspecs" '
			git -C '"$client"' push --dry-run origin $(cat refspecs)
		'
	done
done

for mode in empty mirror
do
	client=client_$mode
	for nr_refspecs in 1 10 100
	do
		test_expect_success "create $mode lease refspecs: $nr_refspecs" '
			test_seq -f ":refs/heads/b%d" $nr_refspecs >refspecs
		'

		test_expect_success "create $mode leases: $nr_refspecs" '
			test_seq -f "refs/heads/b%d:'"$oid"'" $nr_refspecs |
			sed "s/^/--force-with-lease=/" >leases
		'

		test_perf "$mode:lease:$nr_refspecs" '
			git -C '"$client"' push --dry-run origin $(cat refspecs) $(cat leases)
		'
	done
done

test_done
