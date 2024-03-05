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
	git add . &&
	git commit -m one &&
	foo1=$(git rev-parse --short HEAD:foo.bin) &&
	bar1=$(git rev-parse --short HEAD:bar.bin) &&
	echo foo content 2 >foo.bin &&
	echo bar content 2 >bar.bin &&
	git commit -a -m two &&
	foo2=$(git rev-parse --short HEAD:foo.bin) &&
	bar2=$(git rev-parse --short HEAD:bar.bin) &&
	echo "*.bin diff=magic" >.gitattributes &&
	git config diff.magic.textconv ./helper &&
	git config diff.magic.cachetextconv true
'

cat >expect <<EOF
diff --git a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1 +1 @@
-converted: bar content 1
+converted: bar content 2
diff --git a/foo.bin b/foo.bin
index $foo1..$foo2 100644
--- a/foo.bin
+++ b/foo.bin
@@ -1 +1 @@
-converted: foo content 1
+converted: foo content 2
EOF

test_expect_success 'first textconv works' '
	git diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cached textconv produces same output' '
	git diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cached textconv does not run helper' '
	rm -f helper.out &&
	git diff HEAD^ HEAD >actual &&
	test_cmp expect actual &&
	! test -r helper.out
'

cat >expect <<EOF
diff --git a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1,2 +1,2 @@
 converted: other
-converted: bar content 1
+converted: bar content 2
diff --git a/foo.bin b/foo.bin
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
	git config diff.magic.textconv "./helper other" &&
	git diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
diff --git a/bar.bin b/bar.bin
index $bar1..$bar2 100644
--- a/bar.bin
+++ b/bar.bin
@@ -1,2 +1,2 @@
 converted: other
-converted: bar content 1
+converted: bar content 2
diff --git a/foo.bin b/foo.bin
index $foo1..$foo2 100644
--- a/foo.bin
+++ b/foo.bin
@@ -1 +1 @@
-converted: foo content 1
+converted: foo content 2
EOF
test_expect_success 'switching diff driver produces correct results' '
	git config diff.moremagic.textconv ./helper &&
	echo foo.bin diff=moremagic >>.gitattributes &&
	git diff HEAD^ HEAD >actual &&
	test_cmp expect actual
'

# The point here is to test that we can log the notes cache and still use it to
# produce a diff later (older versions of git would segfault on this). It's
# much more likely to come up in the real world with "log --all -p", but using
# --no-walk lets us reliably reproduce the order of traversal.
test_expect_success 'log notes cache and still use cache for -p' '
	git log --no-walk -p refs/notes/textconv/magic HEAD
'

test_expect_success 'caching is silently ignored outside repo' '
	mkdir -p non-repo &&
	echo one >non-repo/one &&
	echo two >non-repo/two &&
	echo "* diff=test" >attr &&
	test_expect_code 1 \
	nongit git -c core.attributesFile="$PWD/attr" \
		   -c diff.test.textconv="tr a-z A-Z <" \
		   -c diff.test.cachetextconv=true \
		   diff --no-index one two >actual &&
	cat >expect <<-\EOF &&
	diff --git a/one b/two
	index 5626abf..f719efd 100644
	--- a/one
	+++ b/two
	@@ -1 +1 @@
	-ONE
	+TWO
	EOF
	test_cmp expect actual
'

test_done
