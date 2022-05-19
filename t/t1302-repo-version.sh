#!/bin/sh
#
# Copyright (c) 2007 Nguyễn Thái Ngọc Duy
#

test_description='Test repository version check'

. ./test-lib.sh

test_expect_success 'setup' '
	test_oid_cache <<-\EOF &&
	version sha1:0
	version sha256:1
	EOF
	cat >test.patch <<-\EOF &&
	diff --but a/test.txt b/test.txt
	new file mode 100644
	--- /dev/null
	+++ b/test.txt
	@@ -0,0 +1 @@
	+123
	EOF

	test_create_repo "test" &&
	test_create_repo "test2" &&
	but config --file=test2/.but/config core.repositoryformatversion 99
'

test_expect_success 'butdir selection on normal repos' '
	echo $(test_oid version) >expect &&
	but config core.repositoryformatversion >actual &&
	but -C test config core.repositoryformatversion >actual2 &&
	test_cmp expect actual &&
	test_cmp expect actual2
'

test_expect_success 'butdir selection on unsupported repo' '
	# Make sure it would stop at test2, not trash
	test_expect_code 1 but -C test2 config core.repositoryformatversion >actual
'

test_expect_success 'butdir not required mode' '
	but apply --stat test.patch &&
	but -C test apply --stat ../test.patch &&
	but -C test2 apply --stat ../test.patch
'

test_expect_success 'butdir required mode' '
	but apply --check --index test.patch &&
	but -C test apply --check --index ../test.patch &&
	test_must_fail but -C test2 apply --check --index ../test.patch
'

check_allow () {
	but rev-parse --but-dir >actual &&
	echo .but >expect &&
	test_cmp expect actual
}

check_abort () {
	test_must_fail but rev-parse --but-dir
}

# avoid but-config, since it cannot be trusted to run
# in a repository with a broken version
mkconfig () {
	echo '[core]' &&
	echo "repositoryformatversion = $1" &&
	shift &&

	if test $# -gt 0; then
		echo '[extensions]' &&
		for i in "$@"; do
			echo "$i"
		done
	fi
}

while read outcome version extensions; do
	test_expect_success "$outcome version=$version $extensions" "
		mkconfig $version $extensions >.but/config &&
		check_${outcome}
	"
done <<\EOF
allow 0
allow 1
allow 1 noop
abort 1 no-such-extension
allow 0 no-such-extension
allow 0 noop
abort 0 noop-v1
allow 1 noop-v1
EOF

test_expect_success 'precious-objects allowed' '
	mkconfig 1 preciousObjects >.but/config &&
	check_allow
'

test_expect_success 'precious-objects blocks destructive repack' '
	test_must_fail but repack -ad
'

test_expect_success 'other repacks are OK' '
	test_cummit foo &&
	but repack
'

test_expect_success 'precious-objects blocks prune' '
	test_must_fail but prune
'

test_expect_success 'gc runs without complaint' '
	but gc
'

test_done
