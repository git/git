#!/bin/sh
#
# Copyright (c) 2010 Bo Yang
#

test_description='Test git log -L with merge commit'

. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

cat >path0 <<\EOF
void func()
{
	printf("hello");
}
EOF

test_expect_success 'Add path0 and commit.' '
	git add path0 &&
	git commit -m "Base commit"
'

cat >path0 <<\EOF
void func()
{
	printf("hello earth");
}
EOF

test_expect_success 'Change path0 in master.' '
	git add path0 &&
	git commit -m "Change path0 in master"
'

test_expect_success 'Make a new branch from the base commit' '
	git checkout -b feature master^
'

cat >path0 <<\EOF
void func()
{
	print("hello moon");
}
EOF

test_expect_success 'Change path0 in feature.' '
	git add path0 &&
	git commit -m "Change path0 in feature"
'

test_expect_success 'Merge the master to feature' '
	! git merge master
'

cat >path0 <<\EOF
void func()
{
	printf("hello earth and moon");
}
EOF

test_expect_success 'Resolve the conflict' '
	git add path0 &&
	git commit -m "Merge two branches"
'

test_expect_success 'Show the line level log of path0' '
	git log --pretty=format:%s%n%b -L /func/,/^}/ path0 > current
'

cat >expected <<\EOF
Merge two branches

nontrivial merge found
path0
@@ 3,1 @@
 	printf("hello earth and moon");


Change path0 in master

diff --git a/path0 b/path0
index 56aeee5..11e66c5 100644
--- a/path0
+++ b/path0
@@ -1,4 +1,4 @@
 void func()
 {
-	printf("hello");
+	printf("hello earth");
 }

Change path0 in feature

diff --git a/path0 b/path0
index 56aeee5..258fced 100644
--- a/path0
+++ b/path0
@@ -1,4 +1,4 @@
 void func()
 {
-	printf("hello");
+	print("hello moon");
 }

Base commit

diff --git a/path0 b/path0
new file mode 100644
index 0000000..56aeee5
--- /dev/null
+++ b/path0
@@ -0,0 +1,4 @@
+void func()
+{
+	printf("hello");
+}
EOF

cat > expected-graph <<\EOF
*   Merge two branches
|\  
| | 
| | nontrivial merge found
| | path0
| | @@ 3,1 @@
| |  	printf("hello earth and moon");
| | 
| |   
| * Change path0 in master
| | 
| | diff --git a/path0 b/path0
| | index 56aeee5..11e66c5 100644
| | --- a/path0
| | +++ b/path0
| | @@ -3,1 +3,1 @@
| | -	printf("hello");
| | +	printf("hello earth");
| |   
* | Change path0 in feature
|/  
|   
|   diff --git a/path0 b/path0
|   index 56aeee5..258fced 100644
|   --- a/path0
|   +++ b/path0
|   @@ -3,1 +3,1 @@
|   -	printf("hello");
|   +	print("hello moon");
|  
* Base commit
  
  diff --git a/path0 b/path0
  new file mode 100644
  index 0000000..56aeee5
  --- /dev/null
  +++ b/path0
  @@ -0,0 +3,1 @@
  +	printf("hello");
EOF

test_expect_success 'Show the line log of the 2 line of path0 with graph' '
	git log --pretty=format:%s%n%b --graph -L 3,+1 path0 > current-graph
'

test_expect_success 'validate the output.' '
	test_cmp current expected
'

test_expect_success 'validate the graph output.' '
	test_cmp current-graph expected-graph
'

test_done
