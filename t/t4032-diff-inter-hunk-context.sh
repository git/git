#!/bin/sh

test_description='diff hunk fusing'

. ./test-lib.sh

f() {
	echo $1
	i=1
	while test $i -le $2
	do
		echo $i
		i=$(expr $i + 1)
	done
	echo $3
}

t() {
	case $# in
	4) hunks=$4; cmd="diff -U$3";;
	5) hunks=$5; cmd="diff -U$3 --inter-hunk-context=$4";;
	esac
	label="$cmd, $1 common $2"
	file=f$1
	expected=expected.$file.$3.$hunks

	if ! test -f $file
	then
		f A $1 B >$file
		git add $file
		git commit -q -m. $file
		f X $1 Y >$file
	fi

	test_expect_success "$label: count hunks ($hunks)" "
		test $(git $cmd $file | grep '^@@ ' | wc -l) = $hunks
	"

	test -f $expected &&
	test_expect_success "$label: check output" "
		git $cmd $file | grep -v '^index ' >actual &&
		test_cmp $expected actual
	"
}

cat <<EOF >expected.f1.0.1 || exit 1
diff --git a/f1 b/f1
--- a/f1
+++ b/f1
@@ -1,3 +1,3 @@
-A
+X
 1
-B
+Y
EOF

cat <<EOF >expected.f1.0.2 || exit 1
diff --git a/f1 b/f1
--- a/f1
+++ b/f1
@@ -1 +1 @@
-A
+X
@@ -3 +3 @@ A
-B
+Y
EOF

# common lines	ctx	intrctx	hunks
t 1 line	0		2
t 1 line	0	0	2
t 1 line	0	1	1
t 1 line	0	2	1
t 1 line	1		1

t 2 lines	0		2
t 2 lines	0	0	2
t 2 lines	0	1	2
t 2 lines	0	2	1
t 2 lines	1		1

t 3 lines	1		2
t 3 lines	1	0	2
t 3 lines	1	1	1
t 3 lines	1	2	1

t 9 lines	3		2
t 9 lines	3	2	2
t 9 lines	3	3	1

test_done
