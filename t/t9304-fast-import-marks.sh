#!/bin/sh

test_description='test exotic situations with marks'
. ./test-lib.sh

test_expect_success 'setup dump of basic history' '
	test_commit one &&
	git fast-export --export-marks=marks HEAD >dump
'

test_expect_success 'setup large marks file' '
	# normally a marks file would have a lot of useful, unique
	# marks. But for our purposes, just having a lot of nonsense
	# ones is fine. Start at 1024 to avoid clashing with marks
	# legitimately used in our tiny dump.
	blob=$(git rev-parse HEAD:one.t) &&
	for i in $(test_seq 1024 16384)
	do
		echo ":$i $blob"
	done >>marks
'

test_expect_success 'import with large marks file' '
	git fast-import --import-marks=marks <dump
'

test_expect_success 'setup dump with submodule' '
	git submodule add "$PWD" sub &&
	git commit -m "add submodule" &&
	git fast-export HEAD >dump
'

test_expect_success 'setup submodule mapping with large id' '
	old=$(git rev-parse HEAD:sub) &&
	new=$(echo $old | sed s/./a/g) &&
	echo ":12345 $old" >from &&
	echo ":12345 $new" >to
'

test_expect_success 'import with submodule mapping' '
	git init dst &&
	git -C dst fast-import \
		--rewrite-submodules-from=sub:../from \
		--rewrite-submodules-to=sub:../to \
		<dump &&
	git -C dst rev-parse HEAD:sub >actual &&
	echo "$new" >expect &&
	test_cmp expect actual
'

test_done
