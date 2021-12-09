#!/bin/sh

test_description='common tail optimization'

. ./test-lib.sh

z=zzzzzzzz ;# 8
z="$z$z$z$z$z$z$z$z" ;# 64
z="$z$z$z$z$z$z$z$z" ;# 512
z="$z$z$z$z" ;# 2048
z2047=$(expr "$z" : '.\(.*\)') ; #2047

x=zzzzzzzzzz			;# 10
y="$x$x$x$x$x$x$x$x$x$x"	;# 100
z="$y$y$y$y$y$y$y$y$y$y"	;# 1000
z1000=$z
z100=$y
z10=$x

zs() {
	count="$1"
	while test "$count" -ge 1000
	do
		count=$(($count - 1000))
		printf "%s" $z1000
	done
	while test "$count" -ge 100
	do
		count=$(($count - 100))
		printf "%s" $z100
	done
	while test "$count" -ge 10
	do
		count=$(($count - 10))
		printf "%s" $z10
	done
	while test "$count" -ge 1
	do
		count=$(($count - 1))
		printf "z"
	done
}

zc () {
	sed -e "/^index/d" \
		-e "s/$z1000/Q/g" \
		-e "s/QQQQQQQQQ/Z9000/g" \
		-e "s/QQQQQQQQ/Z8000/g" \
		-e "s/QQQQQQQ/Z7000/g" \
		-e "s/QQQQQQ/Z6000/g" \
		-e "s/QQQQQ/Z5000/g" \
		-e "s/QQQQ/Z4000/g" \
		-e "s/QQQ/Z3000/g" \
		-e "s/QQ/Z2000/g" \
		-e "s/Q/Z1000/g" \
		-e "s/$z100/Q/g" \
		-e "s/QQQQQQQQQ/Z900/g" \
		-e "s/QQQQQQQQ/Z800/g" \
		-e "s/QQQQQQQ/Z700/g" \
		-e "s/QQQQQQ/Z600/g" \
		-e "s/QQQQQ/Z500/g" \
		-e "s/QQQQ/Z400/g" \
		-e "s/QQQ/Z300/g" \
		-e "s/QQ/Z200/g" \
		-e "s/Q/Z100/g" \
		-e "s/000Z//g" \
		-e "s/$z10/Q/g" \
		-e "s/QQQQQQQQQ/Z90/g" \
		-e "s/QQQQQQQQ/Z80/g" \
		-e "s/QQQQQQQ/Z70/g" \
		-e "s/QQQQQQ/Z60/g" \
		-e "s/QQQQQ/Z50/g" \
		-e "s/QQQQ/Z40/g" \
		-e "s/QQQ/Z30/g" \
		-e "s/QQ/Z20/g" \
		-e "s/Q/Z10/g" \
		-e "s/00Z//g" \
		-e "s/z/Q/g" \
		-e "s/QQQQQQQQQ/Z9/g" \
		-e "s/QQQQQQQQ/Z8/g" \
		-e "s/QQQQQQQ/Z7/g" \
		-e "s/QQQQQQ/Z6/g" \
		-e "s/QQQQQ/Z5/g" \
		-e "s/QQQQ/Z4/g" \
		-e "s/QQQ/Z3/g" \
		-e "s/QQ/Z2/g" \
		-e "s/Q/Z1/g" \
		-e "s/0Z//g" \
	;
}

expect_pattern () {
	cnt="$1"
	cat <<EOF
diff --git a/file-a$cnt b/file-a$cnt
--- a/file-a$cnt
+++ b/file-a$cnt
@@ -1 +1 @@
-Z${cnt}a
+Z${cnt}A
diff --git a/file-b$cnt b/file-b$cnt
--- a/file-b$cnt
+++ b/file-b$cnt
@@ -1 +1 @@
-b
+B
diff --git a/file-c$cnt b/file-c$cnt
--- a/file-c$cnt
+++ b/file-c$cnt
@@ -1 +1 @@
-cZ$cnt
\ No newline at end of file
+CZ$cnt
\ No newline at end of file
diff --git a/file-d$cnt b/file-d$cnt
--- a/file-d$cnt
+++ b/file-d$cnt
@@ -1 +1 @@
-d
+D
EOF
}

sample='1023 1024 1025 2047 4095'

test_expect_success setup '

	for n in $sample
	do
		( zs $n && echo a ) >file-a$n &&
		( echo b && zs $n && echo ) >file-b$n &&
		( printf c && zs $n ) >file-c$n &&
		( echo d && zs $n ) >file-d$n &&

		git add file-a$n file-b$n file-c$n file-d$n &&

		( zs $n && echo A ) >file-a$n &&
		( echo B && zs $n && echo ) >file-b$n &&
		( printf C && zs $n ) >file-c$n &&
		( echo D && zs $n ) >file-d$n &&

		expect_pattern $n || return 1

	done >expect
'

test_expect_success 'diff -U0' '

	for n in $sample
	do
		git diff -U0 file-?$n || return 1
	done | zc >actual &&
	test_cmp expect actual

'

test_done
