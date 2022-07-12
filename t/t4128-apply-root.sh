#!/bin/sh

test_description='apply same filename'

TEST_PASSES_SANITIZE_LEAK=true
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
	echo Bello >expect &&
	git show :some/sub/dir/file >actual &&
	test_cmp expect actual &&
	test_cmp expect some/sub/dir/file

'

test_expect_success 'apply --directory -p (2) ' '

	git reset --hard initial &&
	git apply --directory=some/sub/ -p3 --index patch &&
	echo Bello >expect &&
	git show :some/sub/dir/file >actual &&
	test_cmp expect actual &&
	test_cmp expect some/sub/dir/file

'

cat > patch << EOF
diff --git a/newfile b/newfile
new file mode 100644
index 0000000..d95f3ad
--- /dev/null
+++ b/newfile
@@ -0,0 +1 @@
+content
EOF

test_expect_success 'apply --directory (new file)' '
	git reset --hard initial &&
	git apply --directory=some/sub/dir/ --index patch &&
	echo content >expect &&
	git show :some/sub/dir/newfile >actual &&
	test_cmp expect actual &&
	test_cmp expect some/sub/dir/newfile
'

cat > patch << EOF
diff --git a/c/newfile2 b/c/newfile2
new file mode 100644
index 0000000..d95f3ad
--- /dev/null
+++ b/c/newfile2
@@ -0,0 +1 @@
+content
EOF

test_expect_success 'apply --directory -p (new file)' '
	git reset --hard initial &&
	git apply -p2 --directory=some/sub/dir/ --index patch &&
	echo content >expect &&
	git show :some/sub/dir/newfile2 >actual &&
	test_cmp expect actual &&
	test_cmp expect some/sub/dir/newfile2
'

cat > patch << EOF
diff --git a/delfile b/delfile
deleted file mode 100644
index d95f3ad..0000000
--- a/delfile
+++ /dev/null
@@ -1 +0,0 @@
-content
EOF

test_expect_success 'apply --directory (delete file)' '
	git reset --hard initial &&
	echo content >some/sub/dir/delfile &&
	git add some/sub/dir/delfile &&
	git apply --directory=some/sub/dir/ --index patch &&
	git ls-files >out &&
	! grep delfile out
'

cat > patch << 'EOF'
diff --git "a/qu\157tefile" "b/qu\157tefile"
new file mode 100644
index 0000000..d95f3ad
--- /dev/null
+++ "b/qu\157tefile"
@@ -0,0 +1 @@
+content
EOF

test_expect_success 'apply --directory (quoted filename)' '
	git reset --hard initial &&
	git apply --directory=some/sub/dir/ --index patch &&
	echo content >expect &&
	git show :some/sub/dir/quotefile >actual &&
	test_cmp expect actual &&
	test_cmp expect some/sub/dir/quotefile
'

test_done
