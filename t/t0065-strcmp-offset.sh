#!/bin/sh

test_description='Test strcmp_offset functionality'

. ./test-lib.sh

while read s1 s2 expect
do
	test_expect_success "strcmp_offset($s1, $s2)" '
		echo "$expect" >expect &&
		test-tool strcmp-offset "$s1" "$s2" >actual &&
		test_cmp expect actual
	'
done <<-EOF
abc abc 0 3
abc def -1 0
abc abz -1 2
abc abcdef -1 3
EOF

test_done
