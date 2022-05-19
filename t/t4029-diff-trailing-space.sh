#!/bin/sh
#
# Copyright (c) Jim Meyering
#
test_description='diff honors config option, diff.suppressBlankEmpty'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cat <<\EOF >expected ||
diff --but a/f b/f
index 5f6a263..8cb8bae 100644
--- a/f
+++ b/f
@@ -1,2 +1,2 @@

-x
+y
EOF
exit 1

test_expect_success "$test_description" '
	printf "\nx\n" > f &&
	before=$(but hash-object f) &&
	before=$(but rev-parse --short $before) &&
	but add f &&
	but cummit -q -m. f &&
	printf "\ny\n" > f &&
	after=$(but hash-object f) &&
	after=$(but rev-parse --short $after) &&
	sed -e "s/^index .*/index $before..$after 100644/" expected >exp &&
	but config --bool diff.suppressBlankEmpty true &&
	but diff f > actual &&
	test_cmp exp actual &&
	perl -i.bak -p -e "s/^\$/ /" exp &&
	but config --bool diff.suppressBlankEmpty false &&
	but diff f > actual &&
	test_cmp exp actual &&
	but config --bool --unset diff.suppressBlankEmpty &&
	but diff f > actual &&
	test_cmp exp actual
'

test_done
