#!/bin/sh
#
# Copyright (c) 2009 Giuseppe Bilotta
#

test_description='git-apply --ignore-whitespace.

'
. ./test-lib.sh

# This primes main.c file that indents without using HT at all.
# Various patches with HT and other spaces are attempted in the test.

cat > patch1.patch <<\EOF
diff --git a/main.c b/main.c
new file mode 100644
--- /dev/null
+++ b/main.c
@@ -0,0 +1,22 @@
+#include <stdio.h>
+
+void print_int(int num);
+int func(int num);
+
+int main() {
+       int i;
+
+       for (i = 0; i < 10; i++) {
+               print_int(func(i)); /* stuff */
+       }
+
+       return 0;
+}
+
+int func(int num) {
+       return num * num;
+}
+
+void print_int(int num) {
+       printf("%d", num);
+}
EOF

# Since whitespace is very significant and we want to prevent whitespace
# mangling when creating this test from a patch, we protect 'fixable'
# whitespace by replacing spaces with Z and replacing them at patch
# creation time, hence the sed trick.

# This patch will fail unless whitespace differences are being ignored

sed -e 's/Z/ /g' > patch2.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -10,6 +10,8 @@
Z		print_int(func(i)); /* stuff */
Z	}
Z
+	printf("\n");
+
Z	return 0;
Z}
Z
EOF

# This patch will fail even if whitespace differences are being ignored,
# because of the missing string at EOL. TODO: this testcase should be
# improved by creating a line that has the same hash with and without
# the final string.

sed -e 's/Z/ /g' > patch3.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -10,3 +10,4 @@
Z	for (i = 0; i < 10; i++) {
Z		print_int(func(i));Z
+		/* stuff */
Z	}
EOF

# This patch will fail even if whitespace differences are being ignored,
# because of the missing EOL at EOF.

sed -e 's/Z/ /g' > patch4.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -21,1 +21,1 @@
-	};Z
\ No newline at end of file
+	};
EOF

# This patch will fail unless whitespace differences are being ignored.

sed -e 's/Z/ /g' > patch5.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -2,2 +2,3 @@
Z	void print_int(int num);
+	/* a comment */
Z	int func(int num);
EOF

# And this is how the final output should be.  Patches introduce
# HTs but the original SP indents are mostly kept.

sed -e 's/T/	/g' > main.c.final <<\EOF
#include <stdio.h>

void print_int(int num);
int func(int num);

int main() {
       int i;

       for (i = 0; i < 10; i++) {
               print_int(func(i)); /* stuff */
       }

Tprintf("\n");

       return 0;
}

int func(int num) {
       return num * num;
}

void print_int(int num) {
       printf("%d", num);
}
EOF

test_expect_success 'file creation' '
	git apply patch1.patch
'

test_expect_success 'patch2 fails (retab)' '
	test_must_fail git apply patch2.patch
'

test_expect_success 'patch2 applies with --ignore-whitespace' '
	git apply --ignore-whitespace patch2.patch
'

test_expect_success 'patch2 reverse applies with --ignore-space-change' '
	git apply -R --ignore-space-change patch2.patch
'

git config apply.ignorewhitespace change

test_expect_success 'patch2 applies (apply.ignorewhitespace = change)' '
	git apply patch2.patch &&
	test_cmp main.c.final main.c
'

test_expect_success 'patch3 fails (missing string at EOL)' '
	test_must_fail git apply patch3.patch
'

test_expect_success 'patch4 fails (missing EOL at EOF)' '
	test_must_fail git apply patch4.patch
'

test_expect_success 'patch5 fails (leading whitespace differences matter)' '
	test_must_fail git apply patch5.patch
'

test_expect_success 're-create file (with --ignore-whitespace)' '
	rm -f main.c &&
	git apply patch1.patch
'

test_expect_success 'patch5 fails (--no-ignore-whitespace)' '
	test_must_fail git apply --no-ignore-whitespace patch5.patch
'

test_done
