#!/bin/sh
#
# Copyright (c) Jim Meyering
#
test_description='diff honors config option, diff.suppressBlankEmpty'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cat <<\EOF >expected ||
diff --git a/f b/f
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
	before=$(git hash-object f) &&
	before=$(git rev-parse --short $before) &&
	git add f &&
	git commit -q -m. f &&
	printf "\ny\n" > f &&
	after=$(git hash-object f) &&
	after=$(git rev-parse --short $after) &&
	sed -e "s/^index .*/index $before..$after 100644/" expected >exp &&
	git config --bool diff.suppressBlankEmpty true &&
	git diff f > actual &&
	test_cmp exp actual &&
	perl -i.bak -p -e "s/^\$/ /" exp &&
	git config --bool diff.suppressBlankEmpty false &&
	git diff f > actual &&
	test_cmp exp actual &&
	git config --bool --unset diff.suppressBlankEmpty &&
	git diff f > actual &&
	test_cmp exp actual
'

test_done
