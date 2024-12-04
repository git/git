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
		echo ":$i $blob" || return 1
	done >>marks
'

test_expect_success 'import with large marks file' '
	git fast-import --import-marks=marks <dump
'

test_expect_success 'setup dump with submodule' '
	test_config_global protocol.file.allow always &&
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

test_expect_success 'paths adjusted for relative subdir' '
	git init deep-dst &&
	mkdir deep-dst/subdir &&
	>deep-dst/subdir/empty-marks &&
	git -C deep-dst/subdir fast-import \
		--rewrite-submodules-from=sub:../../from \
		--rewrite-submodules-to=sub:../../to \
		--import-marks=empty-marks \
		--export-marks=exported-marks \
		--export-pack-edges=exported-edges \
		<dump &&
	# we do not bother checking resulting repo; we just care that nothing
	# complained about failing to open files for reading, and that files
	# for writing were created in the expected spot
	test_path_is_file deep-dst/subdir/exported-marks &&
	test_path_is_file deep-dst/subdir/exported-edges
'

test_expect_success 'relative marks are not affected by subdir' '
	git init deep-relative &&
	mkdir deep-relative/subdir &&
	git -C deep-relative/subdir fast-import \
		--relative-marks \
		--export-marks=exported-marks \
		<dump &&
	test_path_is_missing deep-relative/subdir/exported-marks &&
	test_path_is_file deep-relative/.git/info/fast-import/exported-marks
'

test_done
