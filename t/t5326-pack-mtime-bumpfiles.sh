#!/bin/sh

test_description='packfile mtime use bump files'
. ./test-lib.sh

if stat -c %Y . >/dev/null 2>&1; then
    get_modified_time() { stat -c %Y "$1" 2>/dev/null; }
elif stat -f %m . >/dev/null 2>&1; then
    get_modified_time() { stat -f %m "$1" 2>/dev/null; }
elif date -r . +%s >/dev/null 2>&1; then
    get_modified_time() { date -r "$1" +%s 2>/dev/null; }
else
    echo 'get_modified_time() is unsupported' >&2
    get_modified_time() { printf '%s' 0; }
fi

test_expect_success 'freshen existing packfile without core.packMtimeToBumpFiles' '
	obj1=$(echo one | git hash-object -w --stdin) &&
	obj2=$(echo two | git hash-object -w --stdin) &&
	pack1=$(echo $obj1 | git pack-objects .git/objects/pack/pack) &&
	pack2=$(echo $obj2 | git pack-objects .git/objects/pack/pack) &&
	test-tool chmtime =-60 .git/objects/pack/pack-$pack1.* &&
	test-tool chmtime =-60 .git/objects/pack/pack-$pack2.* &&
	pack1_mtime=$(get_modified_time .git/objects/pack/pack-$pack1.pack) &&
	pack2_mtime=$(get_modified_time .git/objects/pack/pack-$pack2.pack) &&
	(echo one | git hash-object -w --stdin) &&
	! test_path_exists .git/objects/pack/pack-$pack1.bump &&
	! test_path_exists .git/objects/pack/pack-$pack2.bump &&
	pack1_mtime_new=$(get_modified_time .git/objects/pack/pack-$pack1.pack) &&
	pack2_mtime_new=$(get_modified_time .git/objects/pack/pack-$pack2.pack) &&
	echo "$pack1_mtime : $pack1_mtime_new" &&
	test ! "$pack1_mtime" = "$pack1_mtime_new" &&
	test "$pack2_mtime" = "$pack2_mtime_new"

'

test_expect_success 'freshen existing packfile with core.packMtimeToBumpFiles' '

	rm -rf .git/objects && git init &&
	obj1=$(echo one | git hash-object -w --stdin) &&
	obj2=$(echo two | git hash-object -w --stdin) &&
	pack1=$(echo $obj1 | git pack-objects .git/objects/pack/pack) &&
	pack2=$(echo $obj2 | git pack-objects .git/objects/pack/pack) &&
	test-tool chmtime =-60 .git/objects/pack/pack-$pack1.* &&
	test-tool chmtime =-60 .git/objects/pack/pack-$pack2.* &&
	pack1_mtime=$(get_modified_time .git/objects/pack/pack-$pack1.pack) &&
	pack2_mtime=$(get_modified_time .git/objects/pack/pack-$pack2.pack) &&
	(echo one | git -c core.packMtimeToBumpFiles=true hash-object -w --stdin) &&
	test_path_exists .git/objects/pack/pack-$pack1.bump &&
	! test_path_exists .git/objects/pack/pack-$pack2.bump &&
	pack1_mtime_new=$(get_modified_time .git/objects/pack/pack-$pack1.pack) &&
	pack2_mtime_new=$(get_modified_time .git/objects/pack/pack-$pack2.pack) &&
	test "$pack1_mtime" = "$pack1_mtime_new" &&
	test "$pack2_mtime" = "$pack2_mtime_new"

'

test_expect_success 'repack prune unreachable objects without core.packMtimeToBumpFiles' '

	rm -rf .git/objects && git init &&
	obj1=$(echo one | git hash-object -w --stdin) &&
	obj2=$(echo two | git hash-object -w --stdin) &&
	pack1=$(echo $obj1 | git pack-objects .git/objects/pack/pack) &&
	pack2=$(echo $obj2 | git pack-objects .git/objects/pack/pack) &&
	echo one | git hash-object -w --stdin &&
	echo two | git hash-object -w --stdin &&
	! test_path_exists .git/objects/pack/pack-$pack1.bump &&
	! test_path_exists .git/objects/pack/pack-$pack2.bump &&
	git prune-packed &&
	git cat-file -p $obj1 &&
	git cat-file -p $obj2 &&
	test-tool chmtime =-86400 .git/objects/pack/pack-$pack2.pack &&
	git repack -A -d --unpack-unreachable=1.hour.ago &&
	git cat-file -p $obj1 &&
	test_must_fail git cat-file -p $obj2

'

test_expect_success 'repack prune unreachable objects with core.packMtimeToBumpFiles and bump files' '

	rm -rf .git/objects && git init &&
	obj1=$(echo one | git hash-object -w --stdin) &&
	obj2=$(echo two | git hash-object -w --stdin) &&
	pack1=$(echo $obj1 | git pack-objects .git/objects/pack/pack) &&
	pack2=$(echo $obj2 | git pack-objects .git/objects/pack/pack) &&
	echo one | git -c core.packMtimeToBumpFiles=true hash-object -w --stdin &&
	echo two | git -c core.packMtimeToBumpFiles=true hash-object -w --stdin &&
	test_path_exists .git/objects/pack/pack-$pack1.bump &&
	test_path_exists .git/objects/pack/pack-$pack2.bump &&
	test-tool chmtime =-86400 .git/objects/pack/pack-$pack2.pack &&
	git -c core.packMtimeToBumpFiles=true repack -A -d --unpack-unreachable=1.hour.ago &&
	git cat-file -p $obj1 &&
	git cat-file -p $obj2

'

test_expect_success 'repack prune unreachable objects with core.packMtimeToBumpFiles and old bump files' '

	rm -rf .git/objects && git init &&
	obj1=$(echo one | git hash-object -w --stdin) &&
	obj2=$(echo two | git hash-object -w --stdin) &&
	pack1=$(echo $obj1 | git pack-objects .git/objects/pack/pack) &&
	pack2=$(echo $obj2 | git pack-objects .git/objects/pack/pack) &&
	echo one | git -c core.packMtimeToBumpFiles=true hash-object -w --stdin &&
	echo two | git -c core.packMtimeToBumpFiles=true hash-object -w --stdin &&
	test_path_exists .git/objects/pack/pack-$pack1.bump &&
	test_path_exists .git/objects/pack/pack-$pack2.bump &&
	git prune-packed &&
	git cat-file -p $obj1 &&
	git cat-file -p $obj2 &&
	test-tool chmtime =-86400 .git/objects/pack/pack-$pack2.bump &&
	git -c core.packMtimeToBumpFiles=true repack -A -d --unpack-unreachable=1.hour.ago &&
	git cat-file -p $obj1 &&
	test_must_fail git cat-file -p $obj2

'

test_done
