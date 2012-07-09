#!/bin/sh

test_description='diff.*.textconv tests'
. ./test-lib.sh

find_diff() {
	sed '1,/^index /d' | sed '/^-- $/,$d'
}

cat >expect.binary <<'EOF'
Binary files a/file and b/file differ
EOF

cat >expect.text <<'EOF'
--- a/file
+++ b/file
@@ -1 +1,2 @@
 0
+1
EOF

cat >hexdump <<'EOF'
#!/bin/sh
"$PERL_PATH" -e '$/ = undef; $_ = <>; s/./ord($&)/ge; print $_' < "$1"
EOF
chmod +x hexdump

test_expect_success 'setup binary file with history' '
	printf "\\0\\n" >file &&
	git add file &&
	git commit -m one &&
	printf "\\01\\n" >>file &&
	git add file &&
	git commit -m two
'

test_expect_success 'file is considered binary by porcelain' '
	git diff HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'file is considered binary by plumbing' '
	git diff-tree -p HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'setup textconv filters' '
	echo file diff=foo >.gitattributes &&
	git config diff.foo.textconv "\"$(pwd)\""/hexdump &&
	git config diff.fail.textconv false
'

test_expect_success 'diff produces text' '
	git diff HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.text actual
'

test_expect_success 'diff-tree produces binary' '
	git diff-tree -p HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'log produces text' '
	git log -1 -p >log &&
	find_diff <log >actual &&
	test_cmp expect.text actual
'

test_expect_success 'format-patch produces binary' '
	git format-patch --no-binary --stdout HEAD^ >patch &&
	find_diff <patch >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'status -v produces text' '
	git reset --soft HEAD^ &&
	git status -v >diff &&
	find_diff <diff >actual &&
	test_cmp expect.text actual &&
	git reset --soft HEAD@{1}
'

cat >expect.stat <<'EOF'
 file | Bin 2 -> 4 bytes
 1 file changed, 0 insertions(+), 0 deletions(-)
EOF
test_expect_success 'diffstat does not run textconv' '
	echo file diff=fail >.gitattributes &&
	git diff --stat HEAD^ HEAD >actual &&
	test_i18ncmp expect.stat actual &&

	head -n1 <expect.stat >expect.line1 &&
	head -n1 <actual >actual.line1 &&
	test_cmp expect.line1 actual.line1
'
# restore working setup
echo file diff=foo >.gitattributes

cat >expect.typechange <<'EOF'
--- a/file
+++ /dev/null
@@ -1,2 +0,0 @@
-0
-1
diff --git a/file b/file
new file mode 120000
index 0000000..67be421
--- /dev/null
+++ b/file
@@ -0,0 +1 @@
+frotz
\ No newline at end of file
EOF
# make a symlink the hard way that works on symlink-challenged file systems
test_expect_success 'textconv does not act on symlinks' '
	printf frotz > file &&
	git add file &&
	git ls-files -s | sed -e s/100644/120000/ |
		git update-index --index-info &&
	git commit -m typechange &&
	git show >diff &&
	find_diff <diff >actual &&
	test_cmp expect.typechange actual
'

test_done
