#!/bin/sh

test_description='git p4 rcs keywords'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

#
# Make one file with keyword lines at the top, and
# enough plain text to be able to test modifications
# far away from the keywords.
#
test_expect_success 'init depot' '
	(
		cd "$cli" &&
		cat <<-\EOF >filek &&
		$Id$
		/* $Revision$ */
		# $Change$
		line4
		line5
		line6
		line7
		line8
		EOF
		sed "s/Revision/Revision: do not scrub me/" <filek >fileko &&
		sed "s/Id/Id: do not scrub me/" <fileko >file_text &&
		p4 add -t text+k filek &&
		p4 submit -d "filek" &&
		p4 add -t text+ko fileko &&
		p4 submit -d "fileko" &&
		p4 add -t text file_text &&
		p4 submit -d "file_text"
	)
'

#
# Generate these in a function to make it easy to use single quote marks.
#
write_scrub_scripts () {
	cat >"$TRASH_DIRECTORY/scrub_k.py" <<-\EOF &&
	import re, sys
	sys.stdout.write(re.sub(r'(?i)\$(Id|Header|Author|Date|DateTime|Change|File|Revision):[^$]*\$', r'$\1$', sys.stdin.read()))
	EOF
	cat >"$TRASH_DIRECTORY/scrub_ko.py" <<-\EOF
	import re, sys
	sys.stdout.write(re.sub(r'(?i)\$(Id|Header):[^$]*\$', r'$\1$', sys.stdin.read()))
	EOF
}

test_expect_success 'scrub scripts' '
	write_scrub_scripts
'

#
# Compare $cli/file to its scrubbed version, should be different.
# Compare scrubbed $cli/file to $git/file, should be same.
#
scrub_k_check () {
	file="$1" &&
	scrub="$TRASH_DIRECTORY/$file" &&
	"$PYTHON_PATH" "$TRASH_DIRECTORY/scrub_k.py" <"$git/$file" >"$scrub" &&
	! test_cmp "$cli/$file" "$scrub" &&
	test_cmp "$git/$file" "$scrub" &&
	rm "$scrub"
}
scrub_ko_check () {
	file="$1" &&
	scrub="$TRASH_DIRECTORY/$file" &&
	"$PYTHON_PATH" "$TRASH_DIRECTORY/scrub_ko.py" <"$git/$file" >"$scrub" &&
	! test_cmp "$cli/$file" "$scrub" &&
	test_cmp "$git/$file" "$scrub" &&
	rm "$scrub"
}

#
# Modify far away from keywords.  If no RCS lines show up
# in the diff, there is no conflict.
#
test_expect_success 'edit far away from RCS lines' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		sed "s/^line7/line7 edit/" <filek >filek.tmp &&
		mv -f filek.tmp filek &&
		git commit -m "filek line7 edit" filek &&
		git p4 submit &&
		scrub_k_check filek
	)
'

#
# Modify near the keywords.  This will require RCS scrubbing.
#
test_expect_success 'edit near RCS lines' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		sed "s/^line4/line4 edit/" <filek >filek.tmp &&
		mv -f filek.tmp filek &&
		git commit -m "filek line4 edit" filek &&
		git p4 submit &&
		scrub_k_check filek
	)
'

#
# Modify the keywords themselves.  This also will require RCS scrubbing.
#
test_expect_success 'edit keyword lines' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		sed "/Revision/d" <filek >filek.tmp &&
		mv -f filek.tmp filek &&
		git commit -m "filek remove Revision line" filek &&
		git p4 submit &&
		scrub_k_check filek
	)
'

#
# Scrubbing text+ko files should not alter all keywords, just Id, Header.
#
test_expect_success 'scrub ko files differently' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		sed "s/^line4/line4 edit/" <fileko >fileko.tmp &&
		mv -f fileko.tmp fileko &&
		git commit -m "fileko line4 edit" fileko &&
		git p4 submit &&
		scrub_ko_check fileko &&
		! scrub_k_check fileko
	)
'

# hack; git p4 submit should do it on its own
test_expect_success 'cleanup after failure' '
	(
		cd "$cli" &&
		p4 revert ...
	)
'

# perl $File:: bug check
test_expect_success 'ktext expansion should not expand multi-line $File::' '
	(
		cd "$cli" &&
		cat >lv.pm <<-\EOF &&
		my $wanted = sub { my $f = $File::Find::name;
				    if ( -f && $f =~ /foo/ ) {
		EOF
		p4 add -t ktext lv.pm &&
		p4 submit -d "lv.pm"
	) &&
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_cmp "$cli/lv.pm" lv.pm
	)
'

#
# Do not scrub anything but +k or +ko files.  Sneak a change into
# the cli file so that submit will get a conflict.  Make sure that
# scrubbing doesn't make a mess of things.
#
# This might happen only if the git repo is behind the p4 repo at
# submit time, and there is a conflict.
#
test_expect_success 'do not scrub plain text' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		sed "s/^line4/line4 edit/" <file_text >file_text.tmp &&
		mv -f file_text.tmp file_text &&
		git commit -m "file_text line4 edit" file_text &&
		(
			cd "$cli" &&
			p4 open file_text &&
			sed "s/^line5/line5 p4 edit/" <file_text >file_text.tmp &&
			mv -f file_text.tmp file_text &&
			p4 submit -d "file5 p4 edit"
		) &&
		echo s | test_expect_code 1 git p4 submit &&
		(
			# make sure the file is not left open
			cd "$cli" &&
			! p4 fstat -T action file_text
		)
	)
'

# hack; git p4 submit should do it on its own
test_expect_success 'cleanup after failure 2' '
	(
		cd "$cli" &&
		p4 revert ...
	)
'

create_kw_file () {
	cat <<\EOF >"$1"
/* A file
	Id: $Id$
	Revision: $Revision$
	File: $File$
 */
int main(int argc, const char **argv) {
	return 0;
}
EOF
}

test_expect_success 'add kwfile' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "file 1" &&
		create_kw_file kwfile1.c &&
		p4 add kwfile1.c &&
		p4 submit -d "Add rcw kw file" kwfile1.c
	)
'

p4_append_to_file () {
	f="$1" &&
	p4 edit -t ktext "$f" &&
	echo "/* $(date) */" >>"$f" &&
	p4 submit -d "appending a line in p4"
}

# Create some files with RCS keywords. If they get modified
# elsewhere then the version number gets bumped which then
# results in a merge conflict if we touch the RCS kw lines,
# even though the change itself would otherwise apply cleanly.
test_expect_success 'cope with rcs keyword expansion damage' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		(cd "$cli" && p4_append_to_file kwfile1.c) &&
		old_lines=$(wc -l <kwfile1.c) &&
		perl -n -i -e "print unless m/Revision:/" kwfile1.c &&
		new_lines=$(wc -l <kwfile1.c) &&
		test $new_lines = $(($old_lines - 1)) &&

		git add kwfile1.c &&
		git commit -m "Zap an RCS kw line" &&
		git p4 submit &&
		git p4 rebase &&
		git diff p4/master &&
		git p4 commit &&
		echo "try modifying in both" &&
		cd "$cli" &&
		p4 edit kwfile1.c &&
		echo "line from p4" >>kwfile1.c &&
		p4 submit -d "add a line in p4" kwfile1.c &&
		cd "$git" &&
		echo "line from git at the top" | cat - kwfile1.c >kwfile1.c.new &&
		mv kwfile1.c.new kwfile1.c &&
		git commit -m "Add line in git at the top" kwfile1.c &&
		git p4 rebase &&
		git p4 submit
	)
'

test_expect_success 'cope with rcs keyword file deletion' '
	test_when_finished cleanup_git &&
	(
		cd "$cli" &&
		echo "\$Revision\$" >kwdelfile.c &&
		p4 add -t ktext kwdelfile.c &&
		p4 submit -d "Add file to be deleted" &&
		cat kwdelfile.c &&
		grep 1 kwdelfile.c
	) &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		grep Revision kwdelfile.c &&
		git rm -f kwdelfile.c &&
		git commit -m "Delete a file containing RCS keywords" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		! test -f kwdelfile.c
	)
'

# If you add keywords in git of the form $Header$ then everything should
# work fine without any special handling.
test_expect_success 'Add keywords in git which match the default p4 values' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo "NewKW: \$Revision\$" >>kwfile1.c &&
		git add kwfile1.c &&
		git commit -m "Adding RCS keywords in git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		test -f kwfile1.c &&
		grep "NewKW.*Revision.*[0-9]" kwfile1.c

	)
'

# If you add keywords in git of the form $Header:#1$ then things will fail
# unless git-p4 takes steps to scrub the *git* commit.
#
test_expect_failure 'Add keywords in git which do not match the default p4 values' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo "NewKW2: \$Revision:1\$" >>kwfile1.c &&
		git add kwfile1.c &&
		git commit -m "Adding RCS keywords in git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.attemptRCSCleanup true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		p4 sync &&
		grep "NewKW2.*Revision.*[0-9]" kwfile1.c

	)
'

test_done
