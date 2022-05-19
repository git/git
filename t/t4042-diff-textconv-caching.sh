#!/bin/sh

test_description='test textconv caching'
. ./test-lib.sh

cat >helper <<'EOF'
#!/bin/sh
sed 's/^/converted: /' "$@" >helper.out
cat helper.out
EOF
chmod +x helper

test_expect_success 'setup' '
	echo foo content 1 >foo.bin &&
	echo bar content 1 >bar.bin &&
	but add . &&
	but cummit -m one &&
	foo1=$(but rev-parse --short HEAD:foo.bin) &&
	bar1=$(but rev-parse --short HEAD:bar.bin) &&
	echo foo content 2 >foo.bin &&
	echo bar content 2 >bar.bin &&
	but cummit -a -m two &&
	foo2=$(but rev-parse --short HEAD:foo.bin) &&
	bar2=$(but rev-parse --short HEAD:bar.bin) &&
	echo "*.bin diff=magic" >.butattributes &&
	but config diff.magic.textconv ./helper &&
	but config diff.magic.cachetextconv true
'

cat >expect <<EOF
diff --but a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1 +1 @@
-converted: bar content 1
+converted: bar content 2
diff --but a/foo.bin b/foo.bin
index $foo1..$foo2 100644
--- a/foo.bin
+++ b/foo.bin
@@ -1 +1 @@
-converted: foo content 1
+converted: foo content 2
EOF

test_expect_success 'first textconv works' '
	but diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cached textconv produces same output' '
	but diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cached textconv does not run helper' '
	rm -f helper.out &&
	but diff HEAD^ HEAD >actual &&
	test_cmp expect actual &&
	! test -r helper.out
'

cat >expect <<EOF
diff --but a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1,2 +1,2 @@
 converted: other
-converted: bar content 1
+converted: bar content 2
diff --but a/foo.bin b/foo.bin
index $foo1..$foo2 100644
--- a/foo.bin
+++ b/foo.bin
@@ -1,2 +1,2 @@
 converted: other
-converted: foo content 1
+converted: foo content 2
EOF
test_expect_success 'changing textconv invalidates cache' '
	echo other >other &&
	but config diff.magic.textconv "./helper other" &&
	but diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
diff --but a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1,2 +1,2 @@
 converted: other
-converted: bar content 1
+converted: bar content 2
diff --but a/foo.bin b/foo.bin
index $foo1..$foo2 100644
--- a/foo.bin
+++ b/foo.bin
@@ -1 +1 @@
-converted: foo content 1
+converted: foo content 2
EOF
test_expect_success 'switching diff driver produces correct results' '
	but config diff.moremagic.textconv ./helper &&
	echo foo.bin diff=moremagic >>.butattributes &&
	but diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

# The point here is to test that we can log the notes cache and still use it to
# produce a diff later (older versions of but would segfault on this). It's
# much more likely to come up in the real world with "log --all -p", but using
# --no-walk lets us reliably reproduce the order of traversal.
test_expect_success 'log notes cache and still use cache for -p' '
	but log --no-walk -p refs/notes/textconv/magic HEAD
'

test_done
