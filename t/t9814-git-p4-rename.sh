#!/bin/sh

test_description='but p4 rename'

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

# We rely on this behavior to detect for p4 move availability.
test_expect_success '"p4 help unknown" errors out' '
	(
		cd "$cli" &&
		p4 help client &&
		! p4 help nosuchcommand
	)
'

test_expect_success 'create files' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		cat >file1 <<-EOF &&
		A large block of text
		in file1 that will generate
		enough context so that rename
		and copy detection will find
		something interesting to do.
		EOF
		cat >file2 <<-EOF &&
		/*
		 * This blob looks a bit
		 * different.
		 */
		int main(int argc, char **argv)
		{
			char text[200];

			strcpy(text, "copy/rename this");
			printf("text is %s\n", text);
			return 0;
		}
		EOF
		p4 add file1 file2 &&
		p4 submit -d "add files"
	)
'

# Rename a file and confirm that rename is not detected in P4.
# Rename the new file again with detectRenames option enabled and confirm that
# this is detected in P4.
# Rename the new file again adding an extra line, configure a big threshold in
# detectRenames and confirm that rename is not detected in P4.
# Repeat, this time with a smaller threshold and confirm that the rename is
# detected in P4.
test_expect_success 'detect renames' '
	but p4 clone --dest="$but" //depot@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&

		but mv file1 file4 &&
		but cummit -a -m "Rename file1 to file4" &&
		but diff-tree -r -M HEAD &&
		but p4 submit &&
		p4 filelog //depot/file4 >filelog &&
		! grep " from //depot" filelog &&

		but mv file4 file5 &&
		but cummit -a -m "Rename file4 to file5" &&
		but diff-tree -r -M HEAD &&
		but config but-p4.detectRenames true &&
		but p4 submit &&
		p4 filelog //depot/file5 >filelog &&
		grep " from //depot/file4" filelog &&

		but mv file5 file6 &&
		echo update >>file6 &&
		but add file6 &&
		but cummit -a -m "Rename file5 to file6 with changes" &&
		but diff-tree -r -M HEAD &&
		level=$(but diff-tree -r -M HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/R0*//") &&
		test -n "$level" && test "$level" -gt 0 && test "$level" -lt 98 &&
		but config but-p4.detectRenames $(($level + 2)) &&
		but p4 submit &&
		p4 filelog //depot/file6 >filelog &&
		! grep " from //depot" filelog &&

		but mv file6 file7 &&
		echo update >>file7 &&
		but add file7 &&
		but cummit -a -m "Rename file6 to file7 with changes" &&
		but diff-tree -r -M HEAD &&
		level=$(but diff-tree -r -M HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/R0*//") &&
		test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
		but config but-p4.detectRenames $(($level - 2)) &&
		but p4 submit &&
		p4 filelog //depot/file7 >filelog &&
		grep " from //depot/file6" filelog
	)
'

# Copy a file and confirm that copy is not detected in P4.
# Copy a file with detectCopies option enabled and confirm that copy is not
# detected in P4.
# Modify and copy a file with detectCopies option enabled and confirm that copy
# is detected in P4.
# Copy a file with detectCopies and detectCopiesHarder options enabled and
# confirm that copy is detected in P4.
# Modify and copy a file, configure a bigger threshold in detectCopies and
# confirm that copy is not detected in P4.
# Modify and copy a file, configure a smaller threshold in detectCopies and
# confirm that copy is detected in P4.
test_expect_success 'detect copies' '
	but p4 clone --dest="$but" //depot@all &&
	test_when_finished cleanup_but &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&

		echo "file8" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&
		cp file2 file8 &&
		but add file8 &&
		but cummit -a -m "Copy file2 to file8" &&
		but diff-tree -r -C HEAD &&
		but p4 submit &&
		p4 filelog //depot/file8 &&
		! p4 filelog //depot/file8 | grep -q "branch from" &&

		echo "file9" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&

		cp file2 file9 &&
		but add file9 &&
		but cummit -a -m "Copy file2 to file9" &&
		but diff-tree -r -C HEAD &&
		but config but-p4.detectCopies true &&
		but p4 submit &&
		p4 filelog //depot/file9 &&
		! p4 filelog //depot/file9 | grep -q "branch from" &&

		echo "file10" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&

		echo "file2" >>file2 &&
		cp file2 file10 &&
		but add file2 file10 &&
		but cummit -a -m "Modify and copy file2 to file10" &&
		but diff-tree -r -C HEAD &&
		src=$(but diff-tree -r -C HEAD | sed 1d | sed 2d | cut -f2) &&
		test "$src" = file2 &&
		but p4 submit &&
		p4 filelog //depot/file10 &&
		p4 filelog //depot/file10 | grep -q "branch from //depot/file2" &&

		echo "file11" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&

		cp file2 file11 &&
		but add file11 &&
		but cummit -a -m "Copy file2 to file11" &&
		but diff-tree -r -C --find-copies-harder HEAD &&
		src=$(but diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file2 &&
		but config but-p4.detectCopiesHarder true &&
		but p4 submit &&
		p4 filelog //depot/file11 &&
		p4 filelog //depot/file11 | grep -q "branch from //depot/file2" &&

		echo "file12" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&

		cp file2 file12 &&
		echo "some text" >>file12 &&
		but add file12 &&
		but cummit -a -m "Copy file2 to file12 with changes" &&
		but diff-tree -r -C --find-copies-harder HEAD &&
		level=$(but diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/C0*//") &&
		test -n "$level" && test "$level" -gt 0 && test "$level" -lt 98 &&
		src=$(but diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file2 &&
		but config but-p4.detectCopies $(($level + 2)) &&
		but p4 submit &&
		p4 filelog //depot/file12 &&
		! p4 filelog //depot/file12 | grep -q "branch from" &&

		echo "file13" >>file2 &&
		but cummit -a -m "Differentiate file2" &&
		but p4 submit &&

		cp file2 file13 &&
		echo "different text" >>file13 &&
		but add file13 &&
		but cummit -a -m "Copy file2 to file13 with changes" &&
		but diff-tree -r -C --find-copies-harder HEAD &&
		level=$(but diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/C0*//") &&
		test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
		src=$(but diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file2 &&
		but config but-p4.detectCopies $(($level - 2)) &&
		but p4 submit &&
		p4 filelog //depot/file13 &&
		p4 filelog //depot/file13 | grep -q "branch from //depot/file2"
	)
'

# See if configurables can be set, and in particular if the run.move.allow
# variable exists, which allows admins to disable the "p4 move" command.
test_lazy_prereq P4D_HAVE_CONFIGURABLE_RUN_MOVE_ALLOW '
	p4 configure show run.move.allow >out &&
	egrep ^run.move.allow: out
'

# If move can be disabled, turn it off and test p4 move handling
test_expect_success P4D_HAVE_CONFIGURABLE_RUN_MOVE_ALLOW \
		    'do not use p4 move when administratively disabled' '
	test_when_finished "p4 configure set run.move.allow=1" &&
	p4 configure set run.move.allow=0 &&
	(
		cd "$cli" &&
		echo move-disallow-file >move-disallow-file &&
		p4 add move-disallow-file &&
		p4 submit -d "add move-disallow-file"
	) &&
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		but config but-p4.detectRenames true &&
		but mv move-disallow-file move-disallow-file-moved &&
		but cummit -m "move move-disallow-file" &&
		but p4 submit
	)
'

test_done
