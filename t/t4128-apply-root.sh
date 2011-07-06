#!/bin/sh

test_description='apply same filename'

. ./test-lib.sh

test_expect_success 'setup' '

	mkdir -p some/sub/dir &&
	echo Hello > some/sub/dir/file &&
	git add some/sub/dir/file &&
	git commit -m initial &&
	git tag initial

'

cat > patch << EOF
diff a/bla/blub/dir/file b/bla/blub/dir/file
--- a/bla/blub/dir/file
+++ b/bla/blub/dir/file
@@ -1,1 +1,1 @@
-Hello
+Bello
EOF

test_expect_success 'apply --directory -p (1)' '

	git apply --directory=some/sub -p3 --index patch &&
	test Bello = $(git show :some/sub/dir/file) &&
	test Bello = $(cat some/sub/dir/file)

'

test_expect_success 'apply --directory -p (2) ' '

	git reset --hard initial &&
	git apply --directory=some/sub/ -p3 --index patch &&
	test Bello = $(git show :some/sub/dir/file) &&
	test Bello = $(cat some/sub/dir/file)

'

test_done
