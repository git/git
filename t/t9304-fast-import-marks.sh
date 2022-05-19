#!/bin/sh

test_description='test exotic situations with marks'
. ./test-lib.sh

test_expect_success 'setup dump of basic history' '
	test_cummit one &&
	but fast-export --export-marks=marks HEAD >dump
'

test_expect_success 'setup large marks file' '
	# normally a marks file would have a lot of useful, unique
	# marks. But for our purposes, just having a lot of nonsense
	# ones is fine. Start at 1024 to avoid clashing with marks
	# lebutimately used in our tiny dump.
	blob=$(but rev-parse HEAD:one.t) &&
	for i in $(test_seq 1024 16384)
	do
		echo ":$i $blob" || return 1
	done >>marks
'

test_expect_success 'import with large marks file' '
	but fast-import --import-marks=marks <dump
'

test_expect_success 'setup dump with submodule' '
	but submodule add "$PWD" sub &&
	but cummit -m "add submodule" &&
	but fast-export HEAD >dump
'

test_expect_success 'setup submodule mapping with large id' '
	old=$(but rev-parse HEAD:sub) &&
	new=$(echo $old | sed s/./a/g) &&
	echo ":12345 $old" >from &&
	echo ":12345 $new" >to
'

test_expect_success 'import with submodule mapping' '
	but init dst &&
	but -C dst fast-import \
		--rewrite-submodules-from=sub:../from \
		--rewrite-submodules-to=sub:../to \
		<dump &&
	but -C dst rev-parse HEAD:sub >actual &&
	echo "$new" >expect &&
	test_cmp expect actual
'

test_done
