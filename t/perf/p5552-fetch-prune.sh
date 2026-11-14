#!/bin/sh

test_description='performance of fetch pruning for refspecs without destinations

Pruning uses refspec destinations to decide which local refs may be stale.
A refspec without a destination cannot match a local ref. This test creates
many refspecs without destinations and many local refs to expose code that
compares each refspec with each local ref.
'
. ./perf-lib.sh

test_expect_success 'create parent and child' '
	git init parent &&
	git -C parent commit --allow-empty -m base &&
	oid=$($MODERN_GIT -C parent rev-parse HEAD) &&
	test_seq 10000 |
	sed "s,.*,$oid," >oids &&
	git clone parent child
'

test_expect_success 'create local refs' '
	oid=$($MODERN_GIT -C parent rev-parse HEAD) &&
	test_seq 20000 |
	sed "s,.*,update refs/remotes/origin/stale-& $oid," |
	$MODERN_GIT -C child update-ref --stdin &&
	$MODERN_GIT -C child pack-refs --all
'

test_perf 'fetch --prune with refspecs without destinations' '
	git -C child fetch --prune --no-tags --no-write-fetch-head \
		--no-auto-maintenance --stdin origin <oids
'

test_done
