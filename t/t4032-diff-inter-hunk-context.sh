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
	use_config=
	git config --unset diff.interHunkContext || :

	case $# in
	4) hunks=$4; cmd="diff -U$3";;
	5) hunks=$5; cmd="diff -U$3 --inter-hunk-context=$4";;
	6) hunks=$5; cmd="diff -U$3"; git config diff.interHunkContext $4; use_config="(diff.interHunkContext=$4) ";;
	esac
	label="$use_config$cmd, $1 common $2"
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

	if test -f $expected
	then
		test_expect_success "$label: check output" "
			git $cmd $file | grep -v '^index ' >actual &&
			test_cmp $expected actual
		"
	fi
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

#					use diff.interHunkContext?
t 1 line	0	0	2	config
t 1 line	0	1	1	config
t 1 line	0	2	1	config
t 9 lines	3	3	1	config
t 2 lines	0	0	2	config
t 2 lines	0	1	2	config
t 2 lines	0	2	1	config
t 3 lines	1	0	2	config
t 3 lines	1	1	1	config
t 3 lines	1	2	1	config
t 9 lines	3	2	2	config
t 9 lines	3	3	1	config

test_expect_success 'diff.interHunkContext invalid' '
	git config diff.interHunkContext asdf &&
	test_must_fail git diff &&
	git config diff.interHunkContext -1 &&
	test_must_fail git diff
'

test_expect_success '--inter-hunk-context rejects negative value' '
	test_unconfig diff.interHunkContext &&
	test_must_fail git diff --inter-hunk-context=-1 2>err &&
	test_grep "expects a non-negative integer" err
'

test_done
