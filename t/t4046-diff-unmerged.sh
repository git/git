#!/bin/sh

test_description='diff with unmerged index entries'
. ./test-lib.sh

test_expect_success setup '
	for i in 0 1 2 3
	do
		blob=$(echo $i | git hash-object --stdin) &&
		eval "blob$i=$blob" &&
		eval "m$i=\"100644 \$blob$i $i\"" || break
	done &&
	paths= &&
	for b in o x
	do
		for o in o x
		do
			for t in o x
			do
				path="$b$o$t" &&
				case "$path" in ooo) continue ;; esac
				paths="$paths$path " &&
				p="	$path" &&
				case "$b" in x) echo "$m1$p" ;; esac &&
				case "$o" in x) echo "$m2$p" ;; esac &&
				case "$t" in x) echo "$m3$p" ;; esac ||
				break
			done || break
		done || break
	done >ls-files-s.expect &&
	git update-index --index-info <ls-files-s.expect &&
	git ls-files -s >ls-files-s.actual &&
	test_cmp ls-files-s.expect ls-files-s.actual
'

test_expect_success 'diff-files -0' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $_z40 $_z40 U	$path"
	done >diff-files-0.expect &&
	git diff-files -0 >diff-files-0.actual &&
	test_cmp diff-files-0.expect diff-files-0.actual
'

test_expect_success 'diff-files -1' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $_z40 $_z40 U	$path" &&
		case "$path" in
		x??) echo ":100644 100644 $blob1 $_z40 M	$path"
		esac
	done >diff-files-1.expect &&
	git diff-files -1 >diff-files-1.actual &&
	test_cmp diff-files-1.expect diff-files-1.actual
'

test_expect_success 'diff-files -2' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $_z40 $_z40 U	$path" &&
		case "$path" in
		?x?) echo ":100644 100644 $blob2 $_z40 M	$path"
		esac
	done >diff-files-2.expect &&
	git diff-files -2 >diff-files-2.actual &&
	test_cmp diff-files-2.expect diff-files-2.actual &&
	git diff-files >diff-files-default-2.actual &&
	test_cmp diff-files-2.expect diff-files-default-2.actual
'

test_expect_success 'diff-files -3' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $_z40 $_z40 U	$path" &&
		case "$path" in
		??x) echo ":100644 100644 $blob3 $_z40 M	$path"
		esac
	done >diff-files-3.expect &&
	git diff-files -3 >diff-files-3.actual &&
	test_cmp diff-files-3.expect diff-files-3.actual
'

test_done
