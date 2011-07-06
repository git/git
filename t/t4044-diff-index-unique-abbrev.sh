#!/bin/sh

test_description='test unique sha1 abbreviation on "index from..to" line'
. ./test-lib.sh

cat >expect_initial <<EOF
100644 blob 51d2738463ea4ca66f8691c91e33ce64b7d41bb1	foo
EOF

cat >expect_update <<EOF
100644 blob 51d2738efb4ad8a1e40bed839ab8e116f0a15e47	foo
EOF

test_expect_success 'setup' '
	echo 4827 > foo &&
	git add foo &&
	git commit -m "initial" &&
	git cat-file -p HEAD: > actual &&
	test_cmp expect_initial actual &&
	echo 11742 > foo &&
	git commit -a -m "update" &&
	git cat-file -p HEAD: > actual &&
	test_cmp expect_update actual
'

cat >expect <<EOF
index 51d27384..51d2738e 100644
EOF

test_expect_success 'diff does not produce ambiguous index line' '
	git diff HEAD^..HEAD | grep index > actual &&
	test_cmp expect actual
'

test_done
