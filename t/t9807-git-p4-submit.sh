#!/bin/sh

test_description='but p4 submit'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1"
	)
'

test_expect_success 'is_cli_file_writeable function' '
	(
		cd "$cli" &&
		echo a >a &&
		is_cli_file_writeable a &&
		! is_cli_file_writeable file1 &&
		rm a
	)
'

test_expect_success 'submit with no client dir' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo file2 >file2 &&
		but add file2 &&
		but cummit -m "but cummit 2" &&
		rm -rf "$cli" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file1 &&
		test_path_is_file file2
	)
'

# make two cummits, but tell it to apply only from HEAD^
test_expect_success 'submit --origin' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file3" &&
		test_cummit "file4" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --origin=HEAD^
	) &&
	(
		cd "$cli" &&
		test_path_is_missing "file3.t" &&
		test_path_is_file "file4.t"
	)
'

test_expect_success 'submit --dry-run' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "dry-run1" &&
		test_cummit "dry-run2" &&
		but p4 submit --dry-run >out &&
		test_i18ngrep "Would apply" out
	) &&
	(
		cd "$cli" &&
		test_path_is_missing "dry-run1.t" &&
		test_path_is_missing "dry-run2.t"
	)
'

test_expect_success 'submit --dry-run --export-labels' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo dry-run1 >dry-run1 &&
		but add dry-run1 &&
		but cummit -m "dry-run1" dry-run1 &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit &&
		echo dry-run2 >dry-run2 &&
		but add dry-run2 &&
		but cummit -m "dry-run2" dry-run2 &&
		but tag -m "dry-run-tag1" dry-run-tag1 HEAD^ &&
		but p4 submit --dry-run --export-labels >out &&
		test_i18ngrep "Would create p4 label" out
	) &&
	(
		cd "$cli" &&
		test_path_is_file "dry-run1" &&
		test_path_is_missing "dry-run2"
	)
'

test_expect_success 'submit with allowSubmit' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file5" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.allowSubmit "nobranch" &&
		test_must_fail but p4 submit &&
		but config but-p4.allowSubmit "nobranch,main" &&
		but p4 submit
	)
'

test_expect_success 'submit with master branch name from argv' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file6" &&
		but config but-p4.skipSubmitEdit true &&
		test_must_fail but p4 submit nobranch &&
		but branch otherbranch &&
		but reset --hard HEAD^ &&
		test_cummit "file7" &&
		but p4 submit otherbranch
	) &&
	(
		cd "$cli" &&
		test_path_is_file "file6.t" &&
		test_path_is_missing "file7.t"
	)
'

test_expect_success 'allow submit from branch with same revision but different name' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file8" &&
		but checkout -b branch1 &&
		but checkout -b branch2 &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.allowSubmit "branch1" &&
		test_must_fail but p4 submit &&
		but checkout branch1 &&
		but p4 submit
	)
'

# make two cummits, but tell it to apply only one

test_expect_success 'submit --cummit one' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file9" &&
		test_cummit "file10" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --commit HEAD
	) &&
	(
		cd "$cli" &&
		test_path_is_missing "file9.t" &&
		test_path_is_file "file10.t"
	)
'

# make three cummits, but tell it to apply only range

test_expect_success 'submit --cummit range' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_cummit "file11" &&
		test_cummit "file12" &&
		test_cummit "file13" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit --commit HEAD~2..HEAD
	) &&
	(
		cd "$cli" &&
		test_path_is_missing "file11.t" &&
		test_path_is_file "file12.t" &&
		test_path_is_file "file13.t"
	)
'

#
# Basic submit tests, the five handled cases
#

test_expect_success 'submit modify' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		echo line >>file1 &&
		but add file1 &&
		but cummit -m file1 &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file1 &&
		test_line_count = 2 file1
	)
'

test_expect_success 'submit add' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		echo file13 >file13 &&
		but add file13 &&
		but cummit -m file13 &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file13
	)
'

test_expect_success 'submit delete' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		but rm file4.t &&
		but cummit -m "delete file4.t" &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing file4.t
	)
'

test_expect_success 'submit copy' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.detectCopies true &&
		but config but-p4.detectCopiesHarder true &&
		cp file5.t file5.ta &&
		but add file5.ta &&
		but cummit -m "copy to file5.ta" &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file5.ta &&
		! is_cli_file_writeable file5.ta
	)
'

test_expect_success 'submit rename' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.detectRenames true &&
		but mv file6.t file6.ta &&
		but cummit -m "rename file6.t to file6.ta" &&
		but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing file6.t &&
		test_path_is_file file6.ta &&
		! is_cli_file_writeable file6.ta
	)
'

#
# Converting but cummit message to p4 change description, including
# parsing out the optional Jobs: line.
#
test_expect_success 'simple one-line description' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo desc2 >desc2 &&
		but add desc2 &&
		cat >msg <<-EOF &&
		One-line description line for desc2.
		EOF
		but cummit -F - <msg &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit &&
		change=$(p4 -G changes -m 1 //depot/... | \
			 marshal_dump change) &&
		# marshal_dump always adds a newline
		p4 -G describe $change | marshal_dump desc | sed \$d >pmsg &&
		test_cmp msg pmsg
	)
'

test_expect_success 'description with odd formatting' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo desc3 >desc3 &&
		but add desc3 &&
		(
			printf "subject line\n\n\tExtra tab\nline.\n\n" &&
			printf "Description:\n\tBogus description marker\n\n" &&
			# but cummit eats trailing newlines; only use one
			printf "Files:\n\tBogus descs marker\n"
		) >msg &&
		but cummit -F - <msg &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit &&
		change=$(p4 -G changes -m 1 //depot/... | \
			 marshal_dump change) &&
		# marshal_dump always adds a newline
		p4 -G describe $change | marshal_dump desc | sed \$d >pmsg &&
		test_cmp msg pmsg
	)
'

make_job() {
	name="$1" &&
	tab="$(printf \\t)" &&
	p4 job -o | \
	sed -e "/^Job:/s/.*/Job: $name/" \
	    -e "/^Description/{ n; s/.*/$tab job text/; }" | \
	p4 job -i
}

test_expect_success 'description with Jobs section at end' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo desc4 >desc4 &&
		but add desc4 &&
		echo 6060842 >jobname &&
		(
			printf "subject line\n\n\tExtra tab\nline.\n\n" &&
			printf "Files:\n\tBogus files marker\n" &&
			printf "Junk: 3164175\n" &&
			printf "Jobs: $(cat jobname)\n"
		) >msg &&
		but cummit -F - <msg &&
		but config but-p4.skipSubmitEdit true &&
		# build a job
		make_job $(cat jobname) &&
		but p4 submit &&
		change=$(p4 -G changes -m 1 //depot/... | \
			 marshal_dump change) &&
		# marshal_dump always adds a newline
		p4 -G describe $change | marshal_dump desc | sed \$d >pmsg &&
		# make sure Jobs line and all following is gone
		sed "/^Jobs:/,\$d" msg >jmsg &&
		test_cmp jmsg pmsg &&
		# make sure p4 knows about job
		p4 -G describe $change | marshal_dump job0 >job0 &&
		test_cmp jobname job0
	)
'

test_expect_success 'description with Jobs and values on separate lines' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo desc5 >desc5 &&
		but add desc5 &&
		echo PROJ-6060842 >jobname1 &&
		echo PROJ-6060847 >jobname2 &&
		(
			printf "subject line\n\n\tExtra tab\nline.\n\n" &&
			printf "Files:\n\tBogus files marker\n" &&
			printf "Junk: 3164175\n" &&
			printf "Jobs:\n" &&
			printf "\t$(cat jobname1)\n" &&
			printf "\t$(cat jobname2)\n"
		) >msg &&
		but cummit -F - <msg &&
		but config but-p4.skipSubmitEdit true &&
		# build two jobs
		make_job $(cat jobname1) &&
		make_job $(cat jobname2) &&
		but p4 submit &&
		change=$(p4 -G changes -m 1 //depot/... | \
			 marshal_dump change) &&
		# marshal_dump always adds a newline
		p4 -G describe $change | marshal_dump desc | sed \$d >pmsg &&
		# make sure Jobs line and all following is gone
		sed "/^Jobs:/,\$d" msg >jmsg &&
		test_cmp jmsg pmsg &&
		# make sure p4 knows about the two jobs
		p4 -G describe $change >change &&
		(
			marshal_dump job0 <change &&
			marshal_dump job1 <change
		) | sort >jobs &&
		cat jobname1 jobname2 | sort >expected &&
		test_cmp expected jobs
	)
'

test_expect_success 'description with Jobs section and bogus following text' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo desc6 >desc6 &&
		but add desc6 &&
		echo 6060843 >jobname &&
		(
			printf "subject line\n\n\tExtra tab\nline.\n\n" &&
			printf "Files:\n\tBogus files marker\n" &&
			printf "Junk: 3164175\n" &&
			printf "Jobs: $(cat jobname)\n" &&
			printf "MoreJunk: 3711\n"
		) >msg &&
		but cummit -F - <msg &&
		but config but-p4.skipSubmitEdit true &&
		# build a job
		make_job $(cat jobname) &&
		test_must_fail but p4 submit 2>err &&
		test_i18ngrep "Unknown field name" err
	) &&
	(
		cd "$cli" &&
		p4 revert desc6 &&
		rm -f desc6
	)
'

test_expect_success 'submit --prepare-p4-only' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo prep-only-add >prep-only-add &&
		but add prep-only-add &&
		but cummit -m "prep only add" &&
		but p4 submit --prepare-p4-only >out &&
		test_i18ngrep "prepared for submission" out &&
		test_i18ngrep "must be deleted" out &&
		test_i18ngrep ! "everything below this line is just the diff" out
	) &&
	(
		cd "$cli" &&
		test_path_is_file prep-only-add &&
		p4 fstat -T action prep-only-add | grep -w add
	)
'

test_expect_success 'submit --shelve' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 revert ... &&
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		test_cummit "shelveme1" &&
		but p4 submit --origin=HEAD^ &&

		echo 654321 >shelveme2.t &&
		echo 123456 >>shelveme1.t &&
		but add shelveme* &&
		but cummit -m"shelvetest" &&
		but p4 submit --shelve --origin=HEAD^ &&

		test_path_is_file shelveme1.t &&
		test_path_is_file shelveme2.t
	) &&
	(
		cd "$cli" &&
		change=$(p4 -G changes -s shelved -m 1 //depot/... | \
			 marshal_dump change) &&
		p4 describe -S $change | grep shelveme2 &&
		p4 describe -S $change | grep 123456 &&
		test_path_is_file shelveme1.t &&
		test_path_is_missing shelveme2.t
	)
'

last_shelve () {
	p4 -G changes -s shelved -m 1 //depot/... | marshal_dump change
}

make_shelved_cl() {
	test_cummit "$1" >/dev/null &&
	but p4 submit --origin HEAD^ --shelve >/dev/null &&
	p4 -G changes -s shelved -m 1 | marshal_dump change
}

# Update existing shelved changelists

test_expect_success 'submit --update-shelve' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 revert ... &&
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		shelved_cl0=$(make_shelved_cl "shelved-change-0") &&
		echo shelved_cl0=$shelved_cl0 &&
		shelved_cl1=$(make_shelved_cl "shelved-change-1") &&

		echo "updating shelved change lists $shelved_cl0 and $shelved_cl1" &&

		echo "updated-line" >>shelf.t &&
		echo added-file.t >added-file.t &&
		but add shelf.t added-file.t &&
		but rm -f shelved-change-1.t &&
		but cummit --amend -C HEAD &&
		but show --stat HEAD &&
		but p4 submit -v --origin HEAD~2 --update-shelve $shelved_cl0 --update-shelve $shelved_cl1 &&
		echo "done but p4 submit"
	) &&
	(
		cd "$cli" &&
		change=$(last_shelve) &&
		p4 unshelve -c $change -s $change &&
		grep -q updated-line shelf.t &&
		p4 describe -S $change | grep added-file.t &&
		test_path_is_missing shelved-change-1.t &&
		p4 revert ...
	)
'

test_expect_success 'update a shelve involving moved and copied files' '
	test_when_finished cleanup_but &&
	(
		cd "$cli" &&
		: >file_to_move &&
		p4 add file_to_move &&
		p4 submit -d "change1" &&
		p4 edit file_to_move &&
		echo change >>file_to_move &&
		p4 submit -d "change2" &&
		p4 opened
	) &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.detectCopies true &&
		but config but-p4.detectRenames true &&
		but config but-p4.skipSubmitEdit true &&
		mkdir moved &&
		cp file_to_move copy_of_file &&
		but add copy_of_file &&
		but mv file_to_move moved/ &&
		but cummit -m "rename a file" &&
		but p4 submit -M --shelve --origin HEAD^ &&
		: >new_file &&
		but add new_file &&
		but cummit --amend &&
		but show --stat HEAD &&
		change=$(last_shelve) &&
		but p4 submit -M --update-shelve $change --commit HEAD
	) &&
	(
		cd "$cli" &&
		change=$(last_shelve) &&
		echo change=$change &&
		p4 unshelve -s $change &&
		p4 submit -d "Testing update-shelve" &&
		test_path_is_file copy_of_file &&
		test_path_is_file moved/file_to_move &&
		test_path_is_missing file_to_move &&
		test_path_is_file new_file &&
		echo "unshelved and submitted change $change" &&
		p4 changes moved/file_to_move | grep "Testing update-shelve" &&
		p4 changes copy_of_file | grep "Testing update-shelve"
	)
'

test_done
