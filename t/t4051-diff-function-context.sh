#!/bin/sh

test_description='diff function context'

. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh


cat <<\EOF >hello.c
#include <stdio.h>

static int a(void)
{
	/*
	 * Dummy.
	 */
}

static int hello_world(void)
{
	/* Classic. */
	printf("Hello world.\n");

	/* Success! */
	return 0;
}
static int b(void)
{
	/*
	 * Dummy, too.
	 */
}

int main(int argc, char **argv)
{
	a();
	b();
	return hello_world();
}
EOF

test_expect_success 'setup' '
	git add hello.c &&
	test_tick &&
	git commit -m initial &&

	grep -v Classic <hello.c >hello.c.new &&
	mv hello.c.new hello.c
'

cat <<\EOF >expected
diff --git a/hello.c b/hello.c
--- a/hello.c
+++ b/hello.c
@@ -10,8 +10,7 @@ static int a(void)
 static int hello_world(void)
 {
-	/* Classic. */
 	printf("Hello world.\n");
 
 	/* Success! */
 	return 0;
 }
EOF

test_expect_success 'diff -U0 -W' '
	git diff -U0 -W >actual &&
	compare_diff_patch actual expected
'

cat <<\EOF >expected
diff --git a/hello.c b/hello.c
--- a/hello.c
+++ b/hello.c
@@ -9,9 +9,8 @@ static int a(void)
 
 static int hello_world(void)
 {
-	/* Classic. */
 	printf("Hello world.\n");
 
 	/* Success! */
 	return 0;
 }
EOF

test_expect_success 'diff -W' '
	git diff -W >actual &&
	compare_diff_patch actual expected
'

test_done
