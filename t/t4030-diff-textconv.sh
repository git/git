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
	test_cummit --printf one file "\\0\\n" &&
	test_cummit --printf --append two file "\\01\\n"
'

test_expect_success 'file is considered binary by porcelain' '
	but diff HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'file is considered binary by plumbing' '
	but diff-tree -p HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'setup textconv filters' '
	echo file diff=foo >.butattributes &&
	but config diff.foo.textconv "\"$(pwd)\""/hexdump &&
	but config diff.fail.textconv false
'

test_expect_success 'diff produces text' '
	but diff HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.text actual
'

test_expect_success 'show cummit produces text' '
	but show HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.text actual
'

test_expect_success 'diff-tree produces binary' '
	but diff-tree -p HEAD^ HEAD >diff &&
	find_diff <diff >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'log produces text' '
	but log -1 -p >log &&
	find_diff <log >actual &&
	test_cmp expect.text actual
'

test_expect_success 'format-patch produces binary' '
	but format-patch --no-binary --stdout HEAD^ >patch &&
	find_diff <patch >actual &&
	test_cmp expect.binary actual
'

test_expect_success 'status -v produces text' '
	but reset --soft HEAD^ &&
	but status -v >diff &&
	find_diff <diff >actual &&
	test_cmp expect.text actual &&
	but reset --soft HEAD@{1}
'

test_expect_success 'show blob produces binary' '
	but show HEAD:file >actual &&
	printf "\\0\\n\\01\\n" >expect &&
	test_cmp expect actual
'

test_expect_success 'show --textconv blob produces text' '
	but show --textconv HEAD:file >actual &&
	printf "0\\n1\\n" >expect &&
	test_cmp expect actual
'

test_expect_success 'show --no-textconv blob produces binary' '
	but show --no-textconv HEAD:file >actual &&
	printf "\\0\\n\\01\\n" >expect &&
	test_cmp expect actual
'

test_expect_success 'grep-diff (-G) operates on textconv data (add)' '
	echo one >expect &&
	but log --root --format=%s -G0 >actual &&
	test_cmp expect actual
'

test_expect_success 'grep-diff (-G) operates on textconv data (modification)' '
	echo two >expect &&
	but log --root --format=%s -G1 >actual &&
	test_cmp expect actual
'

test_expect_success 'pickaxe (-S) operates on textconv data (add)' '
	echo one >expect &&
	but log --root --format=%s -S0 >actual &&
	test_cmp expect actual
'

test_expect_success 'pickaxe (-S) operates on textconv data (modification)' '
	echo two >expect &&
	but log --root --format=%s -S1 >actual &&
	test_cmp expect actual
'

cat >expect.stat <<'EOF'
 file | Bin 2 -> 4 bytes
 1 file changed, 0 insertions(+), 0 deletions(-)
EOF
test_expect_success 'diffstat does not run textconv' '
	echo file diff=fail >.butattributes &&
	but diff --stat HEAD^ HEAD >actual &&
	test_cmp expect.stat actual &&

	head -n1 <expect.stat >expect.line1 &&
	head -n1 <actual >actual.line1 &&
	test_cmp expect.line1 actual.line1
'
# restore working setup
echo file diff=foo >.butattributes

symlink=$(but rev-parse --short $(printf frotz | but hash-object --stdin))
cat >expect.typechange <<EOF
--- a/file
+++ /dev/null
@@ -1,2 +0,0 @@
-0
-1
diff --but a/file b/file
new file mode 120000
index 0000000..$symlink
--- /dev/null
+++ b/file
@@ -0,0 +1 @@
+frotz
\ No newline at end of file
EOF

test_expect_success 'textconv does not act on symlinks' '
	rm -f file &&
	test_ln_s_add frotz file &&
	but cummit -m typechange &&
	but show >diff &&
	find_diff <diff >actual &&
	test_cmp expect.typechange actual
'

test_done
