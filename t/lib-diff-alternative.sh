# Helpers shared by the test scripts for diff algorithms (patience,
# histogram, etc).

test_diff_frobnitz() {
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

	file1=$(git rev-parse --short $(git hash-object file1))
	file2=$(git rev-parse --short $(git hash-object file2))
	cat >expect <<EOF
diff --git a/file1 b/file2
index $file1..$file2 100644
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

	cat >expect_diffstat <<EOF
 file1 => file2 | 21 ++++++++++-----------
 1 file changed, 10 insertions(+), 11 deletions(-)
EOF

	STRATEGY=$1

	test_expect_success "setup attributes files for tests with $STRATEGY" '
		git checkout -b master &&
		echo "file* diff=driver" >.gitattributes &&
		git add file1 file2 .gitattributes &&
		git commit -m "adding files" &&
		git checkout -b branchA &&
		echo "file* diff=driverA" >.gitattributes &&
		git add .gitattributes &&
		git commit -m "adding driverA as diff driver" &&
		git checkout master &&
		git clone --bare --no-local . bare.git
	'

	test_expect_success "$STRATEGY diff from attributes" '
		test_must_fail git -c diff.driver.algorithm=$STRATEGY diff --no-index file1 file2 > output &&
		test_cmp expect output
	'

	test_expect_success "diff from attributes with bare repo with source" '
		git -C bare.git --attr-source=branchA -c diff.driver.algorithm=myers \
			-c diff.driverA.algorithm=$STRATEGY \
			diff HEAD:file1 HEAD:file2 >output &&
		test_cmp expect output
	'

	test_expect_success "diff from attributes with bare repo with invalid source" '
		test_must_fail git -C bare.git --attr-source=invalid-branch diff \
			HEAD:file1 HEAD:file2
	'

	test_expect_success "$STRATEGY diff from attributes has valid diffstat" '
		echo "file* diff=driver" >.gitattributes &&
		git config diff.driver.algorithm "$STRATEGY" &&
		test_must_fail git diff --stat --no-index file1 file2 > output &&
		test_cmp expect_diffstat output
	'

	test_expect_success "$STRATEGY diff" '
		test_must_fail git diff --no-index "--diff-algorithm=$STRATEGY" file1 file2 > output &&
		test_cmp expect output
	'

	test_expect_success "$STRATEGY diff command line precedence before attributes" '
		echo "file* diff=driver" >.gitattributes &&
		git config diff.driver.algorithm myers &&
		test_must_fail git diff --no-index "--diff-algorithm=$STRATEGY" file1 file2 > output &&
		test_cmp expect output
	'

	test_expect_success "$STRATEGY diff attributes precedence before config" '
		git config diff.algorithm default &&
		echo "file* diff=driver" >.gitattributes &&
		git config diff.driver.algorithm "$STRATEGY" &&
		test_must_fail git diff --no-index file1 file2 > output &&
		test_cmp expect output
	'

	test_expect_success "$STRATEGY diff output is valid" '
		mv file2 expect &&
		git apply < output &&
		test_cmp expect file2
	'
}

test_diff_unique() {
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

	uniq1=$(git rev-parse --short $(git hash-object uniq1))
	uniq2=$(git rev-parse --short $(git hash-object uniq2))
	cat >expect <<EOF
diff --git a/uniq1 b/uniq2
index $uniq1..$uniq2 100644
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

	STRATEGY=$1

	test_expect_success 'completely different files' '
		test_must_fail git diff --no-index "--$STRATEGY" uniq1 uniq2 > output &&
		test_cmp expect output
	'
}

