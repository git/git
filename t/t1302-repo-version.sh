#!/bin/sh
#
# Copyright (c) 2007 Nguyễn Thái Ngọc Duy
#

test_description='Test repository version check'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >test.patch <<-\EOF &&
	diff --git a/test.txt b/test.txt
	new file mode 100644
	--- /dev/null
	+++ b/test.txt
	@@ -0,0 +1 @@
	+123
	EOF

	test_create_repo "test" &&
	test_create_repo "test2" &&
	git config --file=test2/.git/config core.repositoryformatversion 99
'

test_expect_success 'gitdir selection on normal repos' '
	if test_have_prereq DEFAULT_REPO_FORMAT
	then
		echo 0
	else
		echo 1
	fi >expect &&
	git config core.repositoryformatversion >actual &&
	git -C test config core.repositoryformatversion >actual2 &&
	test_cmp expect actual &&
	test_cmp expect actual2
'

test_expect_success 'gitdir selection on unsupported repo' '
	# Make sure it would stop at test2, not trash
	test_expect_code 1 git -C test2 config core.repositoryformatversion
'

test_expect_success 'gitdir not required mode' '
	git apply --stat test.patch &&
	git -C test apply --stat ../test.patch &&
	git -C test2 apply --stat ../test.patch
'

test_expect_success 'gitdir required mode' '
	git apply --check --index test.patch &&
	git -C test apply --check --index ../test.patch &&
	test_must_fail git -C test2 apply --check --index ../test.patch
'

check_allow () {
	git rev-parse --git-dir >actual &&
	echo .git >expect &&
	test_cmp expect actual
}

check_abort () {
	test_must_fail git rev-parse --git-dir
}

# avoid git-config, since it cannot be trusted to run
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
		test_when_finished 'rm -rf extensions' &&
		git init extensions &&
		(
			cd extensions &&
			mkconfig $version $extensions >.git/config &&
			check_${outcome}
		)
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
	git config core.repositoryFormatVersion 1 &&
	git config extensions.preciousObjects 1 &&
	check_allow
'

test_expect_success 'precious-objects blocks destructive repack' '
	test_must_fail git repack -ad
'

test_expect_success 'other repacks are OK' '
	test_commit foo &&
	git repack
'

test_expect_success 'precious-objects blocks prune' '
	test_must_fail git prune
'

test_expect_success 'gc runs without complaint' '
	git gc
'

test_done
