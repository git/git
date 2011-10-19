#!/bin/sh

test_description='git am with corrupt input'
. ./test-lib.sh

# Note the missing "+++" line:
cat > bad-patch.diff <<'EOF'
From: A U Thor <au.thor@example.com>
diff --git a/f b/f
index 7898192..6178079 100644
--- a/f
@@ -1 +1 @@
-a
+b
EOF

test_expect_success setup '
	test $? = 0 &&
	echo a > f &&
	git add f &&
	test_tick &&
	git commit -m initial
'

# This used to fail before, too, but with a different diagnostic.
#   fatal: unable to write file '(null)' mode 100644: Bad address
# Also, it had the unwanted side-effect of deleting f.
test_expect_success 'try to apply corrupted patch' '
	git am bad-patch.diff 2> actual
	test $? = 1
'

cat > expected <<EOF
fatal: git diff header lacks filename information (line 4)
EOF

test_expect_success 'compare diagnostic; ensure file is still here' '
	test $? = 0 &&
	test -f f &&
	test_cmp expected actual
'

test_done
