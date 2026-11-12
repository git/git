#!/bin/sh

test_description='test fast-import stream handling and output'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'empty stream succeeds' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		git fast-import </dev/null
	)
'

test_expect_success 'truncated stream complains' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "tag foo" | test_must_fail git fast-import
	)
'

test_expect_success !MINGW 'print old blob' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		blob=$(echo "yes it can" | git hash-object -w --stdin) &&
		cat >expect <<-EOF &&
		${blob} blob 11
		yes it can

		EOF
		echo "cat-blob $blob" |
		git fast-import --cat-blob-fd=6 6>actual &&
		test_cmp expect actual
	)
'

test_expect_success !MINGW 'in-stream cat-blob-fd not respected' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo hello >greeting &&
		blob=$(git hash-object -w greeting) &&
		cat >expect <<-EOF &&
		${blob} blob 6
		hello

		EOF
		git fast-import --cat-blob-fd=3 3>actual.3 >actual.1 <<-EOF &&
		cat-blob $blob
		EOF
		test_cmp expect actual.3 &&
		test_must_be_empty actual.1 &&
		git fast-import 3>actual.3 >actual.1 <<-EOF &&
		option cat-blob-fd=3
		cat-blob $blob
		EOF
		test_must_be_empty actual.3 &&
		test_cmp expect actual.1
	)
'

test_expect_success !MINGW 'print mark for new blob' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "effluentish" | git hash-object --stdin >expect &&
		git fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
		blob
		mark :1
		data <<BLOB_END
		effluentish
		BLOB_END
		get-mark :1
		EOF
		test_cmp expect actual
	)
'

test_expect_success !MINGW 'print new blob' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		blob=$(echo "yep yep yep" | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		${blob} blob 12
		yep yep yep

		EOF
		git fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
		blob
		mark :1
		data <<BLOB_END
		yep yep yep
		BLOB_END
		cat-blob :1
		EOF
		test_cmp expect actual
	)
'

test_expect_success !MINGW 'print new blob by sha1' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		blob=$(echo "a new blob named by sha1" | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		${blob} blob 25
		a new blob named by sha1

		EOF
		git fast-import --cat-blob-fd=6 6>actual <<-EOF &&
		blob
		data <<BLOB_END
		a new blob named by sha1
		BLOB_END
		cat-blob $blob
		EOF
		test_cmp expect actual
	)
'

setup_big_file () {
	(
		echo "the quick brown fox jumps over the lazy dog" >big &&
		for i in 1 2 3
		do
			cat big big big big >bigger &&
			cat bigger bigger bigger bigger >big ||
			exit
		done
	)
}

test_expect_success 'print two blobs to stdout' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		setup_big_file &&
		blob1=$(git hash-object big) &&
		blob1_len=$(wc -c <big) &&
		blob2=$(echo hello | git hash-object --stdin) &&
		{
			echo ${blob1} blob $blob1_len &&
			cat big &&
			cat <<-EOF

			${blob2} blob 6
			hello

			EOF
		} >expect &&
		{
			cat <<-\END_PART1 &&
				blob
				mark :1
				data <<data_end
			END_PART1
			cat big &&
			cat <<-\EOF
				data_end
				blob
				mark :2
				data <<data_end
				hello
				data_end
				cat-blob :1
				cat-blob :2
			EOF
		} |
		git fast-import >actual &&
		test_cmp expect actual
	)
'

test_expect_success PIPE 'copy using cat-file' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		setup_big_file &&
		expect_id=$(git hash-object big) &&
		expect_len=$(wc -c <big) &&
		echo $expect_id blob $expect_len >expect.response &&

		rm -f blobs &&

		mkfifo blobs &&
		(
			export GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE &&
			cat <<-\EOF &&
			feature cat-blob
			blob
			mark :1
			data <<BLOB
			EOF
			cat big &&
			cat <<-\EOF &&
			BLOB
			cat-blob :1
			EOF

			read blob_id type size <&3 &&
			echo "$blob_id $type $size" >response &&
			test_copy_bytes $size >blob <&3 &&
			read newline <&3 &&

			cat <<-EOF &&
			commit refs/heads/copied
			committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
			data <<COMMIT
			copy big file as file3
			COMMIT
			M 644 inline file3
			data <<BLOB
			EOF
			cat blob &&
			echo BLOB
		) 3<blobs |
		git fast-import --cat-blob-fd=3 3>blobs &&
		git show copied:file3 >actual &&
		test_cmp expect.response response &&
		test_cmp big actual
	)
'

test_expect_success PIPE 'print blob mid-commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f blobs &&
		echo "A blob from _before_ the commit." >expect &&
		mkfifo blobs &&
		(
			exec 3<blobs &&
			cat <<-EOF &&
			feature cat-blob
			blob
			mark :1
			data <<BLOB
			A blob from _before_ the commit.
			BLOB
			commit refs/heads/temporary
			committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
			data <<COMMIT
			Empty commit
			COMMIT
			cat-blob :1
			EOF

			read blob_id type size <&3 &&
			test_copy_bytes $size >actual <&3 &&
			read newline <&3 &&

			echo
		) |
		git fast-import --cat-blob-fd=3 3>blobs &&
		test_cmp expect actual
	)
'

test_expect_success PIPE 'print staged blob within commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		rm -f blobs &&
		echo "A blob from _within_ the commit." >expect &&
		mkfifo blobs &&
		(
			exec 3<blobs &&
			cat <<-EOF &&
			feature cat-blob
			commit refs/heads/within
			committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
			data <<COMMIT
			Empty commit
			COMMIT
			M 644 inline within
			data <<BLOB
			A blob from _within_ the commit.
			BLOB
			EOF

			to_get=$(
				echo "A blob from _within_ the commit." |
				git hash-object --stdin
			) &&
			echo "cat-blob $to_get" &&

			read blob_id type size <&3 &&
			test_copy_bytes $size >actual <&3 &&
			read newline <&3 &&

			echo deleteall
		) |
		git fast-import --cat-blob-fd=3 3>blobs &&
		test_cmp expect actual
	)
'

test_expect_success 'quiet option results in no stats being output' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		option git quiet
		blob
		data 3
		hi

		EOF

		git fast-import 2>output <input &&
		test_must_be_empty output
	)
'

test_expect_success 'feature done means terminating "done" is mandatory' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo feature done | test_must_fail git fast-import &&
		test_must_fail git fast-import --done </dev/null
	)
'

test_expect_success 'terminating "done" with trailing gibberish is ok' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		git fast-import <<-\EOF &&
		feature done
		done
		trailing gibberish
		EOF
		git fast-import <<-\EOF
		done
		more trailing gibberish
		EOF
	)
'

test_expect_success 'terminating "done" within commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >expect <<-\EOF &&
		OBJID
		:000000 100644 OBJID OBJID A	hello.c
		:000000 100644 OBJID OBJID A	hello2.c
		EOF
		git fast-import <<-EOF &&
		commit refs/heads/done-ends
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<EOT
		Commit terminated by "done" command
		EOT
		M 100644 inline hello.c
		data <<EOT
		Hello, world.
		EOT
		C hello.c hello2.c
		done
		EOF
		git rev-list done-ends |
		git diff-tree -r --stdin --root --always |
		sed -e "s/$OID_REGEX/OBJID/g" >actual &&
		test_cmp expect actual
	)
'

setup_import_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -C repo config fastimport.unpackLimit 0 &&
	mkdir repo/file2 &&
	echo abcd >repo/file2/file4 &&
	git -C repo add file2/file4 &&
	test_tick &&
	git -C repo commit -m base
}

import_reference_commit () {
	git fast-import <<-INPUT_END
	commit refs/heads/$1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	dirty directory copy
	COMMIT

	from refs/heads/main
	M 644 inline file2/file5
	data <<EOF
	an inline file.
	  we should see it later.
	EOF

	C file2 file3
	D file2/file5

	INPUT_END
}

test_expect_success 'comments are all skipped' '
	setup_import_repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		#we will
		commit refs/heads/O1
		# -- ignore all of this text
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		dirty directory copy
		COMMIT

		# do not forget the import blank line!
		#
		# yes, we started from our usual base of main.
		# i like main.
		from refs/heads/main
		# and we need to add file2/file5 to file2.
		M 644 inline file2/file5
		# otherwise the tree will be different
		data <<EOF
		an inline file.
		  we should see it later.
		EOF

		# do not forget to copy file2 to file3
		C file2 file3
		#
		# or to delete file5 from file2.
		D file2/file5
		# are we done yet?

		INPUT_END

		git fast-import <input &&
		blob4=$(echo abcd | git hash-object --stdin) &&
		blob5=$(printf "an inline file.\n  we should see it later.\n" | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		100644 blob $blob4	file2/file4
		100644 blob $blob4	file3/file4
		100644 blob $blob5	file3/file5
		EOF
		git ls-tree -r O1 >actual &&
		test_cmp expect actual &&

		import_reference_commit O1-reference &&
		test_cmp_rev O1-reference O1
	)
'

test_expect_success 'blank lines not necessary after data commands' '
	setup_import_repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/O2
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		dirty directory copy
		COMMIT
		from refs/heads/main
		M 644 inline file2/file5
		data <<EOF
		an inline file.
		  we should see it later.
		EOF
		C file2 file3
		D file2/file5

		INPUT_END

		git fast-import <input &&
		blob4=$(echo abcd | git hash-object --stdin) &&
		blob5=$(printf "an inline file.\n  we should see it later.\n" | git hash-object --stdin) &&
		cat >expect <<-EOF &&
		100644 blob $blob4	file2/file4
		100644 blob $blob4	file3/file4
		100644 blob $blob5	file3/file5
		EOF
		git ls-tree -r O2 >actual &&
		test_cmp expect actual &&

		import_reference_commit O2-reference &&
		test_cmp_rev O2-reference O2
	)
'

test_expect_success 'blank lines not necessary after other commands' '
	setup_import_repo &&
	(
		cd repo &&
		git repack -a -d &&

		cat >input <<-INPUT_END &&
		commit refs/heads/O3
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zstring
		COMMIT
		commit refs/heads/O3
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zof
		COMMIT
		checkpoint
		commit refs/heads/O3
		mark :5
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zempty
		COMMIT
		checkpoint
		commit refs/heads/O3
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zcommits
		COMMIT
		reset refs/tags/O3-2nd
		from :5
		reset refs/tags/O3-3rd
		from :5
		INPUT_END

		cat >expect <<-INPUT_END &&
		string
		of
		empty
		commits
		INPUT_END

		git fast-import <input &&
		ls -la .git/objects/pack/pack-*.pack >packlist &&
		ls -la .git/objects/pack/pack-*.pack >idxlist &&
		test_line_count = 4 idxlist &&
		test_line_count = 4 packlist &&
		test $(git rev-parse refs/tags/O3-2nd) = $(git rev-parse O3^) &&
		git log --reverse --pretty=oneline O3 | sed s/^.*z// >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'progress outputs as requested by input' '
	setup_import_repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/O4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zstring
		COMMIT
		commit refs/heads/O4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zof
		COMMIT
		progress Two commits down, 2 to go!
		commit refs/heads/O4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zempty
		COMMIT
		progress Three commits down, 1 to go!
		commit refs/heads/O4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		zcommits
		COMMIT
		progress done!
		INPUT_END
		git fast-import <input >actual &&
		grep "progress " <input >expect &&
		test_cmp expect actual
	)
'

##
## R: very large blobs
##
test_expect_success 'blob bigger than threshold' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		blobsize=$((2*1024*1024 + 53)) &&
		test-tool genrandom bar $blobsize >expect &&
		cat >input <<-INPUT_END &&
		commit refs/heads/big-file
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		R - big file
		COMMIT

		M 644 inline big1
		data $blobsize
		INPUT_END
		cat expect >>input &&
		cat >>input <<-INPUT_END &&
		M 644 inline big2
		data $blobsize
		INPUT_END
		cat expect >>input &&
		echo >>input &&

		git -c fastimport.unpackLimit=0 fast-import --big-file-threshold=1 <input &&

		for p in .git/objects/pack/*.pack
		do
			git verify-pack -v "$p" || return 1
		done >verify &&

		git cat-file blob big-file:big1 >actual &&
		test_cmp_bin expect actual &&
		a=$(git rev-parse big-file:big1) &&
		b=$(git rev-parse big-file:big2) &&
		test $a = $b &&
		n=$(grep $a verify | wc -l) &&
		test 1 = $n
	)
'

w_input () {
	cat <<-W_INPUT_END
	commit refs/heads/W-branch
	mark :1
	author Full Name <user@company.tld> 1000000000 +0100
	committer Full Name <user@company.tld> 1000000000 +0100
	data 27
	Intentionally empty commit
	LFsget-mark :1
	W_INPUT_END
}

test_expect_success !MINGW 'get-mark & empty orphan commit with no newlines' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		w_input | sed -e s/LFs// | tr L "\n" | git fast-import
	)
'

test_expect_success !MINGW 'get-mark & empty orphan commit with one newline' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		w_input | sed -e s/LFs/L/ | tr L "\n" | git fast-import
	)
'

test_expect_success !MINGW 'get-mark & empty orphan commit with ugly second newline' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# Technically, this should fail as it has too many linefeeds
		# according to the grammar in fast-import.txt.  But, for whatever
		# reason, it works.  Since using the correct number of newlines
		# does not work with older (pre-2.22) versions of git, allow apps
		# that used this second-newline workaround to keep working by
		# checking it with this test...
		w_input | sed -e s/LFs/LL/ | tr L "\n" | git fast-import
	)
'

test_expect_success !MINGW 'get-mark & empty orphan commit with erroneous third newline' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		# ...but do NOT allow more empty lines than that (see previous test).
		w_input | sed -e s/LFs/LLL/ | tr L "\n" | test_must_fail git fast-import
	)
'

test_expect_success ICONV 'handling encoding' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_tick &&
		cat >input <<-INPUT_END &&
		commit refs/heads/encoding
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		encoding iso-8859-7
		data <<COMMIT
		INPUT_END

		printf "Pi: \360\nCOMMIT\n" >>input &&

		git fast-import <input &&
		git cat-file -p encoding | grep $(printf "\360") &&
		git log -1 --format=%B encoding | grep $(printf "\317\200")
	)
'

test_expect_success 'replace ref that becomes useless is removed' '
	test_when_finished "rm -rf testrepo" &&
	git init -qb main testrepo &&
	(
		cd testrepo &&
		test_commit test &&

		test_commit msg somename content &&

		git mv somename othername &&
		NEW_TREE=$(git write-tree) &&
		MSG="$(git log -1 --format=%B HEAD)" &&
		NEW_COMMIT=$(git commit-tree -p HEAD^1 -m "$MSG" $NEW_TREE) &&
		git replace main $NEW_COMMIT &&

		echo more >>othername &&
		git add othername &&
		git commit -qm more &&

		git fast-export --all >tmp &&
		sed -e s/othername/somename/ tmp >tmp2 &&
		git fast-import --force <tmp2 2>msgs &&

		test_grep "dropping.*since it would point to itself" msgs &&
		git show-ref >refs &&
		test_grep ! refs/replace refs
	)
'

for root in '""' ''
do
	test_expect_success "ls root ($root) tree" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			cd repo &&
			test_commit one &&

			sed -e "s/Z\$//" >expect <<-EOF &&
			040000 tree $(git rev-parse HEAD^{tree})	Z
			EOF
			sha1=$(git rev-parse --verify HEAD) &&
			git fast-import <<-EOF >actual &&
			ls $sha1 $root
			EOF
			test_cmp expect actual
		)
	'
done

test_expect_success 'export-pack-edges' '
	test_when_finished "rm -rf repo" &&
	git init -b main repo &&
	(
		cd repo &&
		test_commit one &&

		cat >input <<-INPUT_END &&
		commit refs/heads/export-boundary
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		we have a border.  its only 40 characters wide.
		COMMIT

		from refs/heads/main

		INPUT_END
		git -c fastimport.unpackLimit=0 fast-import \
			--export-pack-edges=edges.list <input &&

		# verify edge list
		cat >expect <<-EOF &&
		.git/objects/pack/pack-.pack: $(git rev-parse --verify export-boundary)
		EOF
		sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'export-pack-edges option forbidden by default' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo "option git export-pack-edges=edges.list" >input &&
		test_must_fail git fast-import <input 2>err &&
		test_grep "option ${SQ}export-pack-edges${SQ} forbidden in input without --allow-unsafe-features" err
	)
'

test_expect_success 'export-pack-edges via stdin' '
	test_when_finished "rm -rf repo" &&
	git init -b main repo &&
	(
		cd repo &&
		test_commit one &&

		cat >input <<-INPUT_END &&
		option git export-pack-edges=edges.list
		commit refs/heads/export-boundary
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		one more similar commit
		COMMIT

		from refs/heads/main

		INPUT_END

		git -c fastimport.unpackLimit=0 fast-import \
			--allow-unsafe-features <input &&
		cat >expect <<-EOF &&
		.git/objects/pack/pack-.pack: $(git rev-parse --verify export-boundary)
		EOF
		sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--git-completion-helper does not read from stdin' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		find .git/objects/pack -name "tmp_pack_*" >before &&
		echo garbage | git fast-import --git-completion-helper >out &&
		test_grep -e "--date-format=" out &&
		find .git/objects/pack -name "tmp_pack_*" >after &&
		test_cmp before after
	)
'

test_expect_success '--git-completion-helper-all does not read from stdin' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		find .git/objects/pack -name "tmp_pack_*" >before &&
		echo garbage | git fast-import --git-completion-helper-all >all &&
		test_grep -e "--allow-unsafe-features" all &&
		find .git/objects/pack -name "tmp_pack_*" >after &&
		test_cmp before after
	)
'

test_done
