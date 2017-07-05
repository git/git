#!/bin/sh

test_description='Tests the performance of various pretty format placeholders'

. ./perf-lib.sh

test_perf_default_repo

for format in %H %h %T %t %P %p %h-%h-%h
do
	test_perf "log with $format" "
		git log --format=\"$format\" >/dev/null
	"
done

test_done
