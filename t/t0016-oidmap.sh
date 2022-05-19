#!/bin/sh

test_description='test oidmap'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# This purposefully is very similar to t0011-hashmap.sh

test_oidmap () {
	echo "$1" | test-tool oidmap $3 >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}


test_expect_success 'setup' '

	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	test_cummit four

'

test_expect_success 'put' '

test_oidmap "put one 1
put two 2
put invalidOid 4
put three 3" "NULL
NULL
Unknown oid: invalidOid
NULL"

'

test_expect_success 'replace' '

test_oidmap "put one 1
put two 2
put three 3
put invalidOid 4
put two deux
put one un" "NULL
NULL
NULL
Unknown oid: invalidOid
2
1"

'

test_expect_success 'get' '

test_oidmap "put one 1
put two 2
put three 3
get two
get four
get invalidOid
get one" "NULL
NULL
NULL
2
NULL
Unknown oid: invalidOid
1"

'

test_expect_success 'remove' '

test_oidmap "put one 1
put two 2
put three 3
remove one
remove two
remove invalidOid
remove four" "NULL
NULL
NULL
1
2
Unknown oid: invalidOid
NULL"

'

test_expect_success 'iterate' '
	test-tool oidmap >actual.raw <<-\EOF &&
	put one 1
	put two 2
	put three 3
	iterate
	EOF

	# sort "expect" too so we do not rely on the order of particular oids
	sort >expect <<-EOF &&
	NULL
	NULL
	NULL
	$(but rev-parse one) 1
	$(but rev-parse two) 2
	$(but rev-parse three) 3
	EOF

	sort <actual.raw >actual &&
	test_cmp expect actual
'

test_done
