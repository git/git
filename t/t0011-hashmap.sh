#!/bin/sh

test_description='test hashmap and string hash functions'
. ./test-lib.sh

test_hashmap() {
	echo "$1" | test-hashmap $3 > actual &&
	echo "$2" > expect &&
	test_cmp expect actual
}

test_expect_success 'hash functions' '

test_hashmap "hash key1" "2215982743 2215982743 116372151 116372151" &&
test_hashmap "hash key2" "2215982740 2215982740 116372148 116372148" &&
test_hashmap "hash fooBarFrotz" "1383912807 1383912807 3189766727 3189766727" &&
test_hashmap "hash foobarfrotz" "2862305959 2862305959 3189766727 3189766727"

'

test_expect_success 'put' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
put foobarfrotz value4
size" "NULL
NULL
NULL
NULL
64 4"

'

test_expect_success 'put (case insensitive)' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
size" "NULL
NULL
NULL
64 3" ignorecase

'

test_expect_success 'replace' '

test_hashmap "put key1 value1
put key1 value2
put fooBarFrotz value3
put fooBarFrotz value4
size" "NULL
value1
NULL
value3
64 2"

'

test_expect_success 'replace (case insensitive)' '

test_hashmap "put key1 value1
put Key1 value2
put fooBarFrotz value3
put foobarfrotz value4
size" "NULL
value1
NULL
value3
64 2" ignorecase

'

test_expect_success 'get' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
put foobarfrotz value4
get key1
get key2
get fooBarFrotz
get notInMap" "NULL
NULL
NULL
NULL
value1
value2
value3
NULL"

'

test_expect_success 'get (case insensitive)' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
get Key1
get keY2
get foobarfrotz
get notInMap" "NULL
NULL
NULL
value1
value2
value3
NULL" ignorecase

'

test_expect_success 'add' '

test_hashmap "add key1 value1
add key1 value2
add fooBarFrotz value3
add fooBarFrotz value4
get key1
get fooBarFrotz
get notInMap" "value2
value1
value4
value3
NULL"

'

test_expect_success 'add (case insensitive)' '

test_hashmap "add key1 value1
add Key1 value2
add fooBarFrotz value3
add foobarfrotz value4
get key1
get Foobarfrotz
get notInMap" "value2
value1
value4
value3
NULL" ignorecase

'

test_expect_success 'remove' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
remove key1
remove key2
remove notInMap
size" "NULL
NULL
NULL
value1
value2
NULL
64 1"

'

test_expect_success 'remove (case insensitive)' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
remove Key1
remove keY2
remove notInMap
size" "NULL
NULL
NULL
value1
value2
NULL
64 1" ignorecase

'

test_expect_success 'iterate' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
iterate" "NULL
NULL
NULL
key2 value2
key1 value1
fooBarFrotz value3"

'

test_expect_success 'iterate (case insensitive)' '

test_hashmap "put key1 value1
put key2 value2
put fooBarFrotz value3
iterate" "NULL
NULL
NULL
fooBarFrotz value3
key2 value2
key1 value1" ignorecase

'

test_expect_success 'grow / shrink' '

	rm -f in &&
	rm -f expect &&
	for n in $(test_seq 51)
	do
		echo put key$n value$n >> in &&
		echo NULL >> expect
	done &&
	echo size >> in &&
	echo 64 51 >> expect &&
	echo put key52 value52 >> in &&
	echo NULL >> expect
	echo size >> in &&
	echo 256 52 >> expect &&
	for n in $(test_seq 12)
	do
		echo remove key$n >> in &&
		echo value$n >> expect
	done &&
	echo size >> in &&
	echo 256 40 >> expect &&
	echo remove key40 >> in &&
	echo value40 >> expect &&
	echo size >> in &&
	echo 64 39 >> expect &&
	cat in | test-hashmap > out &&
	test_cmp expect out

'

test_expect_success 'string interning' '

test_hashmap "intern value1
intern Value1
intern value2
intern value2
" "value1
Value1
value2
value2"

'

test_done
