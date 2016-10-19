#!/bin/sh

test_description='git am with corrupt input'
. ./test-lib.sh

test_expect_success setup '
	# Note the missing "+++" line:
	cat >bad-patch.diff <<-\EOF &&
	From: A U Thor <au.thor@example.com>
	diff --git a/f b/f
	index 7898192..6178079 100644
	--- a/f
	@@ -1 +1 @@
	-a
	+b
	EOF

	echo a >f &&
	git add f &&
	test_tick &&
	git commit -m initial
'

# This used to fail before, too, but with a different diagnostic.
#   fatal: unable to write file '(null)' mode 100644: Bad address
# Also, it had the unwanted side-effect of deleting f.
test_expect_success 'try to apply corrupted patch' '
	test_must_fail git am bad-patch.diff 2>actual
'

test_expect_success 'compare diagnostic; ensure file is still here' '
	echo "error: git diff header lacks filename information (line 4)" >expected &&
	test_path_is_file f &&
	test_i18ncmp expected actual
'

test_done
