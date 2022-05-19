#!/bin/sh
#
# Copyright (c) 2007 Jakub Narebski
#

test_description='butweb as standalone script (basic tests).

This test runs butweb (but web interface) as CGI script from
commandline, and checks that it would not write any errors
or warnings to log.'


GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-butweb.sh

# ----------------------------------------------------------------------
# no cummits (empty, just initialized repository)

test_expect_success \
	'no cummits: projects_list (implicit)' \
	'butweb_run'

test_expect_success \
	'no cummits: projects_index' \
	'butweb_run "a=project_index"'

test_expect_success \
	'no cummits: .but summary (implicit)' \
	'butweb_run "p=.but"'

test_expect_success \
	'no cummits: .but cummit (implicit HEAD)' \
	'butweb_run "p=.but;a=cummit"'

test_expect_success \
	'no cummits: .but cummitdiff (implicit HEAD)' \
	'butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'no cummits: .but tree (implicit HEAD)' \
	'butweb_run "p=.but;a=tree"'

test_expect_success \
	'no cummits: .but heads' \
	'butweb_run "p=.but;a=heads"'

test_expect_success \
	'no cummits: .but tags' \
	'butweb_run "p=.but;a=tags"'


# ----------------------------------------------------------------------
# initial cummit

test_expect_success \
	'Make initial cummit' \
	'echo "Not an empty file." >file &&
	 but add file &&
	 but cummit -a -m "Initial cummit." &&
	 but branch b'

test_expect_success \
	'projects_list (implicit)' \
	'butweb_run'

test_expect_success \
	'projects_index' \
	'butweb_run "a=project_index"'

test_expect_success \
	'.but summary (implicit)' \
	'butweb_run "p=.but"'

test_expect_success \
	'.but cummit (implicit HEAD)' \
	'butweb_run "p=.but;a=cummit"'

test_expect_success \
	'.but cummitdiff (implicit HEAD, root cummit)' \
	'butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'.but cummitdiff_plain (implicit HEAD, root cummit)' \
	'butweb_run "p=.but;a=cummitdiff_plain"'

test_expect_success \
	'.but cummit (HEAD)' \
	'butweb_run "p=.but;a=commit;h=HEAD"'

test_expect_success \
	'.but tree (implicit HEAD)' \
	'butweb_run "p=.but;a=tree"'

test_expect_success \
	'.but blob (file)' \
	'butweb_run "p=.but;a=blob;f=file"'

test_expect_success \
	'.but blob_plain (file)' \
	'butweb_run "p=.but;a=blob_plain;f=file"'

# ----------------------------------------------------------------------
# nonexistent objects

test_expect_success \
	'.but cummit (non-existent)' \
	'butweb_run "p=.but;a=commit;h=non-existent"'

test_expect_success \
	'.but cummitdiff (non-existent)' \
	'butweb_run "p=.but;a=cummitdiff;h=non-existent"'

test_expect_success \
	'.but cummitdiff (non-existent vs HEAD)' \
	'butweb_run "p=.but;a=cummitdiff;hp=non-existent;h=HEAD"'

test_expect_success \
	'.but tree (0000000000000000000000000000000000000000)' \
	'butweb_run "p=.but;a=tree;h=0000000000000000000000000000000000000000"'

test_expect_success \
	'.but tag (0000000000000000000000000000000000000000)' \
	'butweb_run "p=.but;a=tag;h=0000000000000000000000000000000000000000"'

test_expect_success \
	'.but blob (non-existent)' \
	'butweb_run "p=.but;a=blob;f=non-existent"'

test_expect_success \
	'.but blob_plain (non-existent)' \
	'butweb_run "p=.but;a=blob_plain;f=non-existent"'


# ----------------------------------------------------------------------
# cummitdiff testing (implicit, one implicit tree-ish)

test_expect_success \
	'cummitdiff(0): root' \
	'butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): file added' \
	'echo "New file" >new_file &&
	 but add new_file &&
	 but cummit -a -m "File added." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): mode change' \
	'test_chmod +x new_file &&
	 but cummit -a -m "Mode changed." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): file renamed' \
	'but mv new_file renamed_file &&
	 but cummit -a -m "File renamed." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): file to symlink' \
	'rm renamed_file &&
	 test_ln_s_add file renamed_file &&
	 but cummit -a -m "File to symlink." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): file deleted' \
	'but rm renamed_file &&
	 rm -f renamed_file &&
	 but cummit -a -m "File removed." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): file copied / new file' \
	'cp file file2 &&
	 but add file2 &&
	 but cummit -a -m "File copied." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): mode change and modified' \
	'echo "New line" >>file2 &&
	 test_chmod +x file2 &&
	 but cummit -a -m "Mode change and modification." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): renamed and modified' \
	'cat >file2<<EOF &&
Dominus rebut me,
et nihil mihi deerit.
In loco pascuae ibi me collocavit,
super aquam refectionis educavit me;
animam meam convertit,
deduxit me super semitas jusitiae,
propter nomen suum.
EOF
	 but cummit -a -m "File added." &&
	 but mv file2 file3 &&
	 echo "Propter nomen suum." >>file3 &&
	 but cummit -a -m "File rename and modification." &&
	 butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'cummitdiff(0): renamed, mode change and modified' \
	'but mv file3 file2 &&
	 echo "Propter nomen suum." >>file2 &&
	 test_chmod +x file2 &&
	 but cummit -a -m "File rename, mode change and modification." &&
	 butweb_run "p=.but;a=cummitdiff"'

# ----------------------------------------------------------------------
# cummitdiff testing (taken from t4114-apply-typechange.sh)

test_expect_success 'setup typechange cummits' '
	echo "hello world" >foo &&
	echo "hi planet" >bar &&
	but update-index --add foo bar &&
	but cummit -m initial &&
	but branch initial &&
	rm -f foo &&
	test_ln_s_add bar foo &&
	but cummit -m "foo symlinked to bar" &&
	but branch foo-symlinked-to-bar &&
	rm -f foo &&
	echo "how far is the sun?" >foo &&
	but update-index foo &&
	but cummit -m "foo back to file" &&
	but branch foo-back-to-file &&
	rm -f foo &&
	but update-index --remove foo &&
	mkdir foo &&
	echo "if only I knew" >foo/baz &&
	but update-index --add foo/baz &&
	but cummit -m "foo becomes a directory" &&
	but branch "foo-becomes-a-directory" &&
	echo "hello world" >foo/baz &&
	but update-index foo/baz &&
	but cummit -m "foo/baz is the original foo" &&
	but branch foo-baz-renamed-from-foo
	'

test_expect_success \
	'cummitdiff(2): file renamed from foo to foo/baz' \
	'butweb_run "p=.but;a=cummitdiff;hp=initial;h=foo-baz-renamed-from-foo"'

test_expect_success \
	'cummitdiff(2): file renamed from foo/baz to foo' \
	'butweb_run "p=.but;a=cummitdiff;hp=foo-baz-renamed-from-foo;h=initial"'

test_expect_success \
	'cummitdiff(2): directory becomes file' \
	'butweb_run "p=.but;a=cummitdiff;hp=foo-becomes-a-directory;h=initial"'

test_expect_success \
	'cummitdiff(2): file becomes directory' \
	'butweb_run "p=.but;a=cummitdiff;hp=initial;h=foo-becomes-a-directory"'

test_expect_success \
	'cummitdiff(2): file becomes symlink' \
	'butweb_run "p=.but;a=cummitdiff;hp=initial;h=foo-symlinked-to-bar"'

test_expect_success \
	'cummitdiff(2): symlink becomes file' \
	'butweb_run "p=.but;a=cummitdiff;hp=foo-symlinked-to-bar;h=foo-back-to-file"'

test_expect_success \
	'cummitdiff(2): symlink becomes directory' \
	'butweb_run "p=.but;a=cummitdiff;hp=foo-symlinked-to-bar;h=foo-becomes-a-directory"'

test_expect_success \
	'cummitdiff(2): directory becomes symlink' \
	'butweb_run "p=.but;a=cummitdiff;hp=foo-becomes-a-directory;h=foo-symlinked-to-bar"'

# ----------------------------------------------------------------------
# cummitdiff testing (incomplete lines)

test_expect_success 'setup incomplete lines' '
	cat >file<<-\EOF &&
	Dominus rebut me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	CHANGE_ME
	EOF
	but cummit -a -m "Preparing for incomplete lines" &&
	echo "incomplete" | tr -d "\\012" >>file &&
	but cummit -a -m "Add incomplete line" &&
	but tag incomplete_lines_add &&
	sed -e s/CHANGE_ME/change_me/ <file >file+ &&
	mv -f file+ file &&
	but cummit -a -m "Incomplete context line" &&
	but tag incomplete_lines_ctx &&
	echo "Dominus rebut me," >file &&
	echo "incomplete line" | tr -d "\\012" >>file &&
	but cummit -a -m "Change incomplete line" &&
	but tag incomplete_lines_chg &&
	echo "Dominus rebut me," >file &&
	but cummit -a -m "Remove incomplete line" &&
	but tag incomplete_lines_rem
'

test_expect_success 'cummitdiff(1): addition of incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_add"
'

test_expect_success 'cummitdiff(1): incomplete line as context line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_ctx"
'

test_expect_success 'cummitdiff(1): change incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_chg"
'

test_expect_success 'cummitdiff(1): removal of incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_rem"
'

# ----------------------------------------------------------------------
# cummit, cummitdiff: merge, large
test_expect_success \
	'Create a merge' \
	'but checkout b &&
	 echo "Branch" >>b &&
	 but add b &&
	 but cummit -a -m "On branch" &&
	 but checkout main &&
	 but merge b &&
	 but tag merge_cummit'

test_expect_success \
	'cummit(0): merge cummit' \
	'butweb_run "p=.but;a=cummit"'

test_expect_success \
	'cummitdiff(0): merge cummit' \
	'butweb_run "p=.but;a=cummitdiff"'

test_expect_success \
	'Prepare large cummit' \
	'but checkout b &&
	 echo "To be changed" >01-change &&
	 echo "To be renamed" >02-pure-rename-from &&
	 echo "To be deleted" >03-delete &&
	 echo "To be renamed and changed" >04-rename-from &&
	 echo "To have mode changed" >05-mode-change &&
	 echo "File to symlink" >06-file-or-symlink &&
	 echo "To be changed and have mode changed" >07-change-mode-change &&
	 but add 0* &&
	 but cummit -a -m "Prepare large cummit" &&
	 echo "Changed" >01-change &&
	 but mv 02-pure-rename-from 02-pure-rename-to &&
	 but rm 03-delete && rm -f 03-delete &&
	 echo "A new file" >03-new &&
	 but add 03-new &&
	 but mv 04-rename-from 04-rename-to &&
	 echo "Changed" >>04-rename-to &&
	 test_chmod +x 05-mode-change &&
	 rm -f 06-file-or-symlink &&
	 test_ln_s_add 01-change 06-file-or-symlink &&
	 echo "Changed and have mode changed" >07-change-mode-change &&
	 test_chmod +x 07-change-mode-change &&
	 but cummit -a -m "Large cummit" &&
	 but checkout main'

test_expect_success \
	'cummit(1): large cummit' \
	'butweb_run "p=.but;a=commit;h=b"'

test_expect_success \
	'cummitdiff(1): large cummit' \
	'butweb_run "p=.but;a=cummitdiff;h=b"'

# ----------------------------------------------------------------------
# side-by-side diff

test_expect_success 'side-by-side: addition of incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_add;ds=sidebyside"
'

test_expect_success 'side-by-side: incomplete line as context line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_ctx;ds=sidebyside"
'

test_expect_success 'side-by-side: changed incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_chg;ds=sidebyside"
'

test_expect_success 'side-by-side: removal of incomplete line' '
	butweb_run "p=.but;a=cummitdiff;h=incomplete_lines_rem;ds=sidebyside"
'

test_expect_success 'side-by-side: merge cummit' '
	butweb_run "p=.but;a=cummitdiff;h=merge_cummit;ds=sidebyside"
'

# ----------------------------------------------------------------------
# tags testing

test_expect_success \
	'tags: list of different types of tags' \
	'but checkout main &&
	 but tag -a -m "Tag cummit object" tag-commit HEAD &&
	 but tag -a -m "" tag-cummit-nomessage HEAD &&
	 but tag -a -m "Tag tag object" tag-tag tag-cummit &&
	 but tag -a -m "Tag tree object" tag-tree HEAD^{tree} &&
	 but tag -a -m "Tag blob object" tag-blob HEAD:file &&
	 but tag lightweight/tag-commit HEAD &&
	 but tag lightweight/tag-tag tag-cummit &&
	 but tag lightweight/tag-tree HEAD^{tree} &&
	 but tag lightweight/tag-blob HEAD:file &&
	 butweb_run "p=.but;a=tags"'

test_expect_success \
	'tag: Tag to cummit object' \
	'butweb_run "p=.but;a=tag;h=tag-cummit"'

test_expect_success \
	'tag: on lightweight tag (invalid)' \
	'butweb_run "p=.but;a=tag;h=lightweight/tag-cummit"'

# ----------------------------------------------------------------------
# logs

test_expect_success \
	'logs: log (implicit HEAD)' \
	'butweb_run "p=.but;a=log"'

test_expect_success \
	'logs: shortlog (implicit HEAD)' \
	'butweb_run "p=.but;a=shortlog"'

test_expect_success \
	'logs: history (implicit HEAD, file)' \
	'butweb_run "p=.but;a=history;f=file"'

test_expect_success \
	'logs: history (implicit HEAD, non-existent file)' \
	'butweb_run "p=.but;a=history;f=non-existent"'

test_expect_success \
	'logs: history (implicit HEAD, deleted file)' \
	'but checkout main &&
	 echo "to be deleted" >deleted_file &&
	 but add deleted_file &&
	 but cummit -m "Add file to be deleted" &&
	 but rm deleted_file &&
	 but cummit -m "Delete file" &&
	 butweb_run "p=.but;a=history;f=deleted_file"'

# ----------------------------------------------------------------------
# path_info links
test_expect_success \
	'path_info: project' \
	'butweb_run "" "/.but"'

test_expect_success \
	'path_info: project/branch' \
	'butweb_run "" "/.but/b"'

test_expect_success \
	'path_info: project/branch:file' \
	'butweb_run "" "/.but/main:file"'

test_expect_success \
	'path_info: project/branch:dir/' \
	'butweb_run "" "/.but/main:foo/"'

test_expect_success \
	'path_info: project/branch (non-existent)' \
	'butweb_run "" "/.but/non-existent"'

test_expect_success \
	'path_info: project/branch:filename (non-existent branch)' \
	'butweb_run "" "/.but/non-existent:non-existent"'

test_expect_success \
	'path_info: project/branch:file (non-existent)' \
	'butweb_run "" "/.but/main:non-existent"'

test_expect_success \
	'path_info: project/branch:dir/ (non-existent)' \
	'butweb_run "" "/.but/main:non-existent/"'


test_expect_success \
	'path_info: project/branch:/file' \
	'butweb_run "" "/.but/main:/file"'

test_expect_success \
	'path_info: project/:/file (implicit HEAD)' \
	'butweb_run "" "/.but/:/file"'

test_expect_success \
	'path_info: project/:/ (implicit HEAD, top tree)' \
	'butweb_run "" "/.but/:/"'


# ----------------------------------------------------------------------
# feed generation

test_expect_success \
	'feeds: OPML' \
	'butweb_run "a=opml"'

test_expect_success \
	'feed: RSS' \
	'butweb_run "p=.but;a=rss"'

test_expect_success \
	'feed: Atom' \
	'butweb_run "p=.but;a=atom"'

# ----------------------------------------------------------------------
# encoding/decoding

test_expect_success \
	'encode(cummit): utf8' \
	'. "$TEST_DIRECTORY"/t3901/utf8.txt &&
	 test_when_finished "GIT_AUTHOR_NAME=\"A U Thor\"" &&
	 test_when_finished "GIT_CUMMITTER_NAME=\"C O Mitter\"" &&
	 echo "UTF-8" >>file &&
	 but add file &&
	 but cummit -F "$TEST_DIRECTORY"/t3900/1-UTF-8.txt &&
	 butweb_run "p=.but;a=cummit"'

test_expect_success \
	'encode(cummit): iso-8859-1' \
	'. "$TEST_DIRECTORY"/t3901/8859-1.txt &&
	 test_when_finished "GIT_AUTHOR_NAME=\"A U Thor\"" &&
	 test_when_finished "GIT_CUMMITTER_NAME=\"C O Mitter\"" &&
	 echo "ISO-8859-1" >>file &&
	 but add file &&
	 test_config i18n.cummitencoding ISO-8859-1 &&
	 but cummit -F "$TEST_DIRECTORY"/t3900/ISO8859-1.txt &&
	 butweb_run "p=.but;a=cummit"'

test_expect_success \
	'encode(log): utf-8 and iso-8859-1' \
	'butweb_run "p=.but;a=log"'

# ----------------------------------------------------------------------
# extra options

test_expect_success \
	'opt: log --no-merges' \
	'butweb_run "p=.but;a=log;opt=--no-merges"'

test_expect_success \
	'opt: atom --no-merges' \
	'butweb_run "p=.but;a=log;opt=--no-merges"'

test_expect_success \
	'opt: "file" history --no-merges' \
	'butweb_run "p=.but;a=history;f=file;opt=--no-merges"'

test_expect_success \
	'opt: log --no-such-option (invalid option)' \
	'butweb_run "p=.but;a=log;opt=--no-such-option"'

test_expect_success \
	'opt: tree --no-merges (invalid option for action)' \
	'butweb_run "p=.but;a=tree;opt=--no-merges"'

# ----------------------------------------------------------------------
# testing config_to_multi / cloneurl

test_expect_success \
       'URL: no project URLs, no base URL' \
       'butweb_run "p=.but;a=summary"'

test_expect_success \
       'URL: project URLs via butweb.url' \
       'but config --add butweb.url but://example.com/but/trash.but &&
        but config --add butweb.url http://example.com/but/trash.but &&
        butweb_run "p=.but;a=summary"'

cat >.but/cloneurl <<\EOF
but://example.com/but/trash.but
http://example.com/but/trash.but
EOF

test_expect_success \
       'URL: project URLs via cloneurl file' \
       'butweb_run "p=.but;a=summary"'

# ----------------------------------------------------------------------
# butweb config and repo config

cat >>butweb_config.perl <<\EOF

# turn on override for each overridable feature
foreach my $key (keys %feature) {
	if ($feature{$key}{'sub'}) {
		$feature{$key}{'override'} = 1;
	}
}
EOF

test_expect_success \
	'config override: projects list (implicit)' \
	'butweb_run'

test_expect_success \
	'config override: tree view, features not overridden in repo config' \
	'butweb_run "p=.but;a=tree"'

test_expect_success \
	'config override: tree view, features disabled in repo config' \
	'but config butweb.blame no &&
	 but config butweb.snapshot none &&
	 but config butweb.avatar gravatar &&
	 butweb_run "p=.but;a=tree"'

test_expect_success \
	'config override: tree view, features enabled in repo config (1)' \
	'but config butweb.blame yes &&
	 but config butweb.snapshot "zip,tgz, tbz2" &&
	 butweb_run "p=.but;a=tree"'

test_expect_success 'setup' '
	version=$(but config core.repositoryformatversion) &&
	algo=$(test_might_fail but config extensions.objectformat) &&
	cat >.but/config <<-\EOF &&
	# testing noval and alternate separator
	[butweb]
		blame
		snapshot = zip tgz
	EOF
	but config core.repositoryformatversion "$version" &&
	if test -n "$algo"
	then
		but config extensions.objectformat "$algo"
	fi
'

test_expect_success \
	'config override: tree view, features enabled in repo config (2)' \
	'butweb_run "p=.but;a=tree"'

# ----------------------------------------------------------------------
# searching

cat >>butweb_config.perl <<\EOF

# enable search
$feature{'search'}{'default'} = [1];
$feature{'grep'}{'default'} = [1];
$feature{'pickaxe'}{'default'} = [1];
EOF

test_expect_success \
	'search: preparation' \
	'echo "1st MATCH" >>file &&
	 echo "2nd MATCH" >>file &&
	 echo "MATCH" >>bar &&
	 but add file bar &&
	 but cummit -m "Added MATCH word"'

test_expect_success \
	'search: cummit author' \
	'butweb_run "p=.but;a=search;h=HEAD;st=author;s=A+U+Thor"'

test_expect_success \
	'search: cummit message' \
	'butweb_run "p=.but;a=search;h=HEAD;st=cummitr;s=MATCH"'

test_expect_success \
	'search: grep' \
	'butweb_run "p=.but;a=search;h=HEAD;st=grep;s=MATCH"'

test_expect_success \
	'search: pickaxe' \
	'butweb_run "p=.but;a=search;h=HEAD;st=pickaxe;s=MATCH"'

test_expect_success \
	'search: projects' \
	'butweb_run "a=project_list;s=.but"'

# ----------------------------------------------------------------------
# non-ASCII in README.html

test_expect_success \
	'README.html with non-ASCII characters (utf-8)' \
	'echo "<b>UTF-8 example:</b><br />" >.but/README.html &&
	 cat "$TEST_DIRECTORY"/t3900/1-UTF-8.txt >>.but/README.html &&
	 butweb_run "p=.but;a=summary"'

# ----------------------------------------------------------------------
# syntax highlighting


highlight_version=$(highlight --version </dev/null 2>/dev/null)
if [ $? -eq 127 ]; then
	say "Skipping syntax highlighting tests: 'highlight' not found"
elif test -z "$highlight_version"; then
	say "Skipping syntax highlighting tests: incorrect 'highlight' found"
else
	test_set_prereq HIGHLIGHT
	cat >>butweb_config.perl <<-\EOF
	our $highlight_bin = "highlight";
	$feature{'highlight'}{'override'} = 1;
	EOF
fi

test_expect_success HIGHLIGHT \
	'syntax highlighting (no highlight, unknown syntax)' \
	'but config butweb.highlight yes &&
	 butweb_run "p=.but;a=blob;f=file"'

test_expect_success HIGHLIGHT \
	'syntax highlighting (highlighted, shell script)' \
	'but config butweb.highlight yes &&
	 echo "#!/usr/bin/sh" >test.sh &&
	 but add test.sh &&
	 but cummit -m "Add test.sh" &&
	 butweb_run "p=.but;a=blob;f=test.sh"'

test_expect_success HIGHLIGHT \
	'syntax highlighting (highlighter language autodetection)' \
	'but config butweb.highlight yes &&
	 echo "#!/usr/bin/perl" >test &&
	 but add test &&
	 but cummit -m "Add test" &&
	 butweb_run "p=.but;a=blob;f=test"'

# ----------------------------------------------------------------------
# forks of projects

cat >>butweb_config.perl <<\EOF &&
$feature{'forks'}{'default'} = [1];
EOF

test_expect_success \
	'forks: prepare' \
	'but init --bare foo.but &&
	 but --but-dir=foo.but --work-tree=. add file &&
	 but --but-dir=foo.but --work-tree=. cummit -m "Initial cummit" &&
	 echo "foo" >foo.but/description &&
	 mkdir -p foo &&
	 (cd foo &&
	  but clone --shared --bare ../foo.but foo-forked.but &&
	  echo "fork of foo" >foo-forked.but/description)'

test_expect_success \
	'forks: projects list' \
	'butweb_run'

test_expect_success \
	'forks: forks action' \
	'butweb_run "p=foo.but;a=forks"'

# ----------------------------------------------------------------------
# content tags (tag cloud)

cat >>butweb_config.perl <<-\EOF &&
# we don't test _setting_ content tags, so any true value is good
$feature{'ctags'}{'default'} = ['ctags_script.cgi'];
EOF

test_expect_success \
	'ctags: tag cloud in projects list' \
	'mkdir .but/ctags &&
	 echo "2" >.but/ctags/foo &&
	 echo "1" >.but/ctags/bar &&
	butweb_run'

test_expect_success \
	'ctags: search projects by existing tag' \
	'butweb_run "by_tag=foo"'

test_expect_success \
	'ctags: search projects by non existent tag' \
	'butweb_run "by_tag=non-existent"'

test_expect_success \
	'ctags: malformed tag weights' \
	'mkdir -p .but/ctags &&
	 echo "not-a-number" >.but/ctags/nan &&
	 echo "not-a-number-2" >.but/ctags/nan2 &&
	 echo "0.1" >.but/ctags/floating-point &&
	 butweb_run'

# ----------------------------------------------------------------------
# categories

test_expect_success \
	'categories: projects list, only default category' \
	'echo "\$projects_list_group_categories = 1;" >>butweb_config.perl &&
	 butweb_run'

# ----------------------------------------------------------------------
# unborn branches

test_expect_success \
	'unborn HEAD: "summary" page (with "heads" subview)' \
	'{
		but checkout orphan_branch ||
		but checkout --orphan orphan_branch
	 } &&
	 test_when_finished "but checkout main" &&
	 butweb_run "p=.but;a=summary"'

test_done
