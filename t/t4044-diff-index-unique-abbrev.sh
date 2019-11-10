#!/bin/sh

test_description='test unique sha1 abbreviation on "index from..to" line'
. ./test-lib.sh

test_expect_success 'setup' '
	test_oid_cache <<-EOF &&
	val1 sha1:4827
	val1 sha256:5664

	val2 sha1:11742
	val2 sha256:10625

	hash1 sha1:51d2738463ea4ca66f8691c91e33ce64b7d41bb1
	hash1 sha256:ae31dfff0af93b2c62b0098a039b38569c43b0a7e97b873000ca42d128f27350

	hasht1 sha1:51d27384
	hasht1 sha256:ae31dfff

	hash2 sha1:51d2738efb4ad8a1e40bed839ab8e116f0a15e47
	hash2 sha256:ae31dffada88a46fd5f53c7ed5aa25a7a8951f1d5e88456c317c8d5484d263e5

	hasht2 sha1:51d2738e
	hasht2 sha256:ae31dffa
	EOF

	cat >expect_initial <<-EOF &&
	100644 blob $(test_oid hash1)	foo
	EOF

	cat >expect_update <<-EOF &&
	100644 blob $(test_oid hash2)	foo
	EOF

	echo "$(test_oid val1)" > foo &&
	git add foo &&
	git commit -m "initial" &&
	git cat-file -p HEAD: > actual &&
	test_cmp expect_initial actual &&
	echo "$(test_oid val2)" > foo &&
	git commit -a -m "update" &&
	git cat-file -p HEAD: > actual &&
	test_cmp expect_update actual
'

cat >expect <<EOF
index $(test_oid hasht1)..$(test_oid hasht2) 100644
EOF

test_expect_success 'diff does not produce ambiguous index line' '
	git diff HEAD^..HEAD | grep index > actual &&
	test_cmp expect actual
'

test_done
