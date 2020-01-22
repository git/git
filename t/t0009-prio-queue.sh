#!/bin/sh

test_description='basic tests for priority queue implementation'
. ./test-lib.sh

cat >expect <<'EOF'
1
2
3
4
5
5
6
7
8
9
10
EOF
test_expect_success 'basic ordering' '
	test-tool prio-queue 2 6 3 10 9 5 7 4 5 8 1 dump >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
2
3
4
1
5
6
EOF
test_expect_success 'mixed put and get' '
	test-tool prio-queue 6 2 4 get 5 3 get get 1 dump >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
1
2
NULL
1
2
NULL
EOF
test_expect_success 'notice empty queue' '
	test-tool prio-queue 1 2 get get get 1 2 get get get >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
3
2
6
4
5
1
8
EOF
test_expect_success 'stack order' '
	test-tool prio-queue stack 8 1 5 4 6 2 3 dump >actual &&
	test_cmp expect actual
'

test_done
