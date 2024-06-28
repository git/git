#!/bin/sh

test_description='diff with unmerged index entries'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	for i in 0 1 2 3
	do
		blob=$(echo $i | git hash-object --stdin) &&
		eval "blob$i=$blob" &&
		eval "m$i=\"100644 \$blob$i $i\"" || return 1
	done &&
	paths= &&
	for b in o x
	do
		for o in o x
		do
			for t in o x
			do
				path="$b$o$t" &&
				if test "$path" != ooo
				then
					paths="$paths$path " &&
					p="	$path" &&
					case "$b" in x) echo "$m1$p" ;; esac &&
					case "$o" in x) echo "$m2$p" ;; esac &&
					case "$t" in x) echo "$m3$p" ;; esac ||
					return 1
				fi
			done
		done
	done >ls-files-s.expect &&
	git update-index --index-info <ls-files-s.expect &&
	git ls-files -s >ls-files-s.actual &&
	test_cmp ls-files-s.expect ls-files-s.actual
'

test_expect_success 'diff-files -0' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $ZERO_OID $ZERO_OID U	$path" || return 1
	done >diff-files-0.expect &&
	git diff-files -0 >diff-files-0.actual &&
	test_cmp diff-files-0.expect diff-files-0.actual
'

test_expect_success 'diff-files -1' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $ZERO_OID $ZERO_OID U	$path" &&
		case "$path" in
		x??) echo ":100644 100644 $blob1 $ZERO_OID M	$path"
		esac || return 1
	done >diff-files-1.expect &&
	git diff-files -1 >diff-files-1.actual &&
	test_cmp diff-files-1.expect diff-files-1.actual
'

test_expect_success 'diff-files -2' '
	for path in $paths
	do
		>"$path" &&
		echo ":000000 100644 $ZERO_OID $ZERO_OID U	$path" &&
		case "$path" in
		?x?) echo ":100644 100644 $blob2 $ZERO_OID M	$path"
		esac || return 1
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
		echo ":000000 100644 $ZERO_OID $ZERO_OID U	$path" &&
		case "$path" in
		??x) echo ":100644 100644 $blob3 $ZERO_OID M	$path"
		esac || return 1
	done >diff-files-3.expect &&
	git diff-files -3 >diff-files-3.actual &&
	test_cmp diff-files-3.expect diff-files-3.actual
'

test_expect_success 'diff --stat' '
	for path in $paths
	do
		echo " $path | Unmerged" || return 1
	done >diff-stat.expect &&
	echo " 0 files changed" >>diff-stat.expect &&
	git diff --cached --stat >diff-stat.actual &&
	test_cmp diff-stat.expect diff-stat.actual
'

test_done
