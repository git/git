#!/bin/sh

test_description='patience diff algorithm'

. ./test-lib.sh

cat >file1 <<\EOF
#include <stdio.h>

// Frobs foo heartily
int frobnitz(int foo)
{
    int i;
    for(i = 0; i < 10; i++)
    {
        printf("Your answer is: ");
        printf("%d\n", foo);
    }
}

int fact(int n)
{
    if(n > 1)
    {
        return fact(n-1) * n;
    }
    return 1;
}

int main(int argc, char **argv)
{
    frobnitz(fact(10));
}
EOF

cat >file2 <<\EOF
#include <stdio.h>

int fib(int n)
{
    if(n > 2)
    {
        return fib(n-1) + fib(n-2);
    }
    return 1;
}

// Frobs foo heartily
int frobnitz(int foo)
{
    int i;
    for(i = 0; i < 10; i++)
    {
        printf("%d\n", foo);
    }
}

int main(int argc, char **argv)
{
    frobnitz(fib(10));
}
EOF

cat >expect <<\EOF
diff --git a/file1 b/file2
index 6faa5a3..e3af329 100644
--- a/file1
+++ b/file2
@@ -1,26 +1,25 @@
 #include <stdio.h>
 
+int fib(int n)
+{
+    if(n > 2)
+    {
+        return fib(n-1) + fib(n-2);
+    }
+    return 1;
+}
+
 // Frobs foo heartily
 int frobnitz(int foo)
 {
     int i;
     for(i = 0; i < 10; i++)
     {
-        printf("Your answer is: ");
         printf("%d\n", foo);
     }
 }
 
-int fact(int n)
-{
-    if(n > 1)
-    {
-        return fact(n-1) * n;
-    }
-    return 1;
-}
-
 int main(int argc, char **argv)
 {
-    frobnitz(fact(10));
+    frobnitz(fib(10));
 }
EOF

test_expect_success 'patience diff' '

	test_must_fail git diff --no-index --patience file1 file2 > output &&
	test_cmp expect output

'

test_expect_success 'patience diff output is valid' '

	mv file2 expect &&
	git apply < output &&
	test_cmp expect file2

'

cat >uniq1 <<\EOF
1
2
3
4
5
6
EOF

cat >uniq2 <<\EOF
a
b
c
d
e
f
EOF

cat >expect <<\EOF
diff --git a/uniq1 b/uniq2
index b414108..0fdf397 100644
--- a/uniq1
+++ b/uniq2
@@ -1,6 +1,6 @@
-1
-2
-3
-4
-5
-6
+a
+b
+c
+d
+e
+f
EOF

test_expect_success 'completely different files' '

	test_must_fail git diff --no-index --patience uniq1 uniq2 > output &&
	test_cmp expect output

'

test_done
