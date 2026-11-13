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

test_expect_success 'export-marks feature results in a marks file being created' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		feature export-marks=git.marks
		blob
		mark :1
		data 3
		hi

		EOF

		git fast-import --allow-unsafe-features <input &&
		test_grep :1 git.marks
	)
'

test_expect_success 'export-marks options can be overridden by commandline options' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-\EOF &&
		feature export-marks=feature-sub/git.marks
		blob
		mark :1
		data 3
		hi

		EOF
		git fast-import --allow-unsafe-features \
				--export-marks=cmdline-sub/other.marks <input &&
		test_grep :1 cmdline-sub/other.marks &&
		test_path_is_missing feature-sub
	)
'

test_expect_success 'catch typo in marks file name' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=nonexistent.marks </dev/null &&
		echo "feature import-marks=nonexistent.marks" |
		test_must_fail git fast-import --allow-unsafe-features
	)
'

test_expect_success 'import and output marks can be the same file' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f io.marks &&
		blob=$(echo hi | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		:1 $blob
		:2 $blob
		EOF
		git fast-import --export-marks=io.marks <<-\EOF &&
		blob
		mark :1
		data 3
		hi

		EOF
		git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&
		blob
		mark :2
		data 3
		hi

		EOF
		test_cmp expect io.marks
	)
'

test_expect_success '--import-marks=foo --output-marks=foo to create foo fails' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f io.marks &&
		test_must_fail git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF
		blob
		mark :1
		data 3
		hi

		EOF
	)
'

test_expect_success '--import-marks-if-exists' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f io.marks &&
		blob=$(echo hi | git hash-object --stdin) &&
		echo ":1 $blob" >expect &&
		git fast-import --import-marks-if-exists=io.marks --export-marks=io.marks <<-\EOF &&
		blob
		mark :1
		data 3
		hi

		EOF
		test_cmp expect io.marks
	)
'

test_expect_success 'feature import-marks-if-exists' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f io.marks &&

		git fast-import --export-marks=io.marks \
				--allow-unsafe-features <<-\EOF &&
		feature import-marks-if-exists=not_io.marks
		EOF
		test_must_be_empty io.marks &&

		blob=$(echo hi | git hash-object -w --stdin) &&

		echo ":1 $blob" >io.marks &&
		echo ":1 $blob" >expect &&
		echo ":2 $blob" >>expect &&

		git fast-import --export-marks=io.marks \
				--allow-unsafe-features <<-\EOF &&
		feature import-marks-if-exists=io.marks
		blob
		mark :2
		data 3
		hi

		EOF
		test_cmp expect io.marks &&

		echo ":3 $blob" >>expect &&

		git fast-import --import-marks=io.marks \
				--export-marks=io.marks \
				--allow-unsafe-features <<-\EOF &&
		feature import-marks-if-exists=not_io.marks
		blob
		mark :3
		data 3
		hi

		EOF
		test_cmp expect io.marks &&

		git fast-import --import-marks-if-exists=not_io.marks \
				--export-marks=io.marks \
				--allow-unsafe-features <<-\EOF &&
		feature import-marks-if-exists=io.marks
		EOF
		test_must_be_empty io.marks
	)
'

test_expect_success 'corrupt lines do not mess marks file' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f io.marks &&
		blob=$(echo hi | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		:3 $ZERO_OID
		:1 $blob
		:2 $blob
		EOF
		cp expect io.marks &&
		test_must_fail git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&

		EOF
		test_cmp expect io.marks
	)
'

create_marks_file () {
	git fast-import --export-marks="$1" <<-EOF
	blob
	mark :1
	data 4
	one

	blob
	mark :2
	data 4
	two

	blob
	mark :3
	data 6
	three

	blob
	mark :4
	data 5
	four

	commit refs/heads/marks
	mark :5
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit with a mark
	COMMIT
	M 100644 :1 one

	tag marks-tag
	mark :6
	from :5
	tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<TAG
	tag with a mark
	TAG

	EOF
}

test_expect_success 'import to output marks works without any content' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		create_marks_file marks.out &&

		cat >input <<-EOF &&
		feature import-marks=marks.out
		feature export-marks=marks.new
		EOF

		git fast-import --allow-unsafe-features <input &&
		test_cmp marks.out marks.new
	)
'

test_expect_success 'import marks prefers commandline marks file over the stream' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		create_marks_file marks.out &&

		cat >input <<-EOF &&
		feature import-marks=nonexistent.marks
		feature export-marks=marks.new
		EOF

		git fast-import --import-marks=marks.out --allow-unsafe-features <input &&
		test_cmp marks.out marks.new
	)
'

test_expect_success 'multiple --import-marks= should be honoured' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		create_marks_file marks.out &&

		cat >input <<-EOF &&
		feature import-marks=nonexistent.marks
		feature export-marks=combined.marks
		EOF

		head -n2 marks.out >one.marks &&
		tail -n +3 marks.out >two.marks &&
		git fast-import --import-marks=one.marks --import-marks=two.marks \
			--allow-unsafe-features <input &&
		test_cmp marks.out combined.marks
	)
'

test_expect_success 'feature relative-marks should be honoured' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		create_marks_file marks.out &&

		cat >input <<-EOF &&
		feature relative-marks
		feature import-marks=relative.in
		feature export-marks=relative.out
		EOF

		mkdir -p .git/info/fast-import/ &&
		cp marks.out .git/info/fast-import/relative.in &&
		git fast-import --allow-unsafe-features <input &&
		test_cmp marks.out .git/info/fast-import/relative.out
	)
'

test_expect_success 'feature no-relative-marks should be honoured' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		create_marks_file marks.out &&

		cat >input <<-EOF &&
		feature relative-marks
		feature import-marks=relative.in
		feature no-relative-marks
		feature export-marks=non-relative.out
		EOF

		mkdir -p .git/info/fast-import/ &&
		cp marks.out .git/info/fast-import/relative.in &&
		git fast-import --allow-unsafe-features <input &&
		test_cmp marks.out non-relative.out
	)
'

test_expect_success 'export marks with large values' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_tick &&
		mt=$(git hash-object --stdin < /dev/null) &&
		>input.blob &&
		>marks.exp &&
		>tree.exp &&

		cat >input.commit <<-EOF &&
		commit refs/heads/verify--dump-marks
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		test the sparse array dumping routines with exponentially growing marks
		COMMIT
		EOF

		i=0 l=4 m=6 n=7 &&
		while test "$i" -lt 27
		do
			cat >>input.blob <<-EOF &&
			blob
			mark :$l
			data 0
			blob
			mark :$m
			data 0
			blob
			mark :$n
			data 0
			EOF
			echo "M 100644 :$l l$i" >>input.commit &&
			echo "M 100644 :$m m$i" >>input.commit &&
			echo "M 100644 :$n n$i" >>input.commit &&

			echo ":$l $mt" >>marks.exp &&
			echo ":$m $mt" >>marks.exp &&
			echo ":$n $mt" >>marks.exp &&

			printf "100644 blob $mt\tl$i\n" >>tree.exp &&
			printf "100644 blob $mt\tm$i\n" >>tree.exp &&
			printf "100644 blob $mt\tn$i\n" >>tree.exp &&

			l=$(($l + $l)) &&
			m=$(($m + $m)) &&
			n=$(($l + $n)) &&

			i=$((1 + $i)) || return 1
		done &&

		sort tree.exp > tree.exp_s &&

		cat input.blob input.commit | git fast-import --export-marks=marks.large &&
		git ls-tree refs/heads/verify--dump-marks >tree.out &&
		test_cmp tree.exp_s tree.out &&
		test_cmp marks.exp marks.large
	)
'

test_done
