#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='but status'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'status -h in broken repository' '
	but config --global advice.statusuoption false &&
	mkdir broken &&
	test_when_finished "rm -fr broken" &&
	(
		cd broken &&
		but init &&
		echo "[status] showuntrackedfiles = CORRUPT" >>.but/config &&
		test_expect_code 129 but status -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'cummit -h in broken repository' '
	mkdir broken &&
	test_when_finished "rm -fr broken" &&
	(
		cd broken &&
		but init &&
		echo "[status] showuntrackedfiles = CORRUPT" >>.but/config &&
		test_expect_code 129 but cummit -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'create upstream branch' '
	but checkout -b upstream &&
	test_cummit upstream1 &&
	test_cummit upstream2 &&
	# leave the first cummit on main as root because several
	# tests depend on this case; for our upstream we only
	# care about cummit counts anyway, so a totally divergent
	# history is OK
	but checkout --orphan main
'

test_expect_success 'setup' '
	: >tracked &&
	: >modified &&
	mkdir dir1 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	mkdir dir2 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	but add . &&

	but status >output &&

	test_tick &&
	but cummit -m initial &&
	: >untracked &&
	: >dir1/untracked &&
	: >dir2/untracked &&
	echo 1 >dir1/modified &&
	echo 2 >dir2/modified &&
	echo 3 >dir2/added &&
	but add dir2/added &&

	but branch --set-upstream-to=upstream
'

test_expect_success 'status (1)' '
	test_i18ngrep "use \"but rm --cached <file>\.\.\.\" to unstage" output
'

strip_comments () {
	tab='	'
	sed "s/^\# //; s/^\#$//; s/^#$tab/$tab/" <"$1" >"$1".tmp &&
	rm "$1" && mv "$1".tmp "$1"
}

cat >.butignore <<\EOF
.butignore
expect*
output*
EOF

test_expect_success 'status --column' '
	cat >expect <<\EOF &&
# On branch main
# Your branch and '\''upstream'\'' have diverged,
# and have 1 and 2 different cummits each, respectively.
#   (use "but pull" to merge the remote branch into yours)
#
# Changes to be cummitted:
#   (use "but restore --staged <file>..." to unstage)
#	new file:   dir2/added
#
# Changes not staged for cummit:
#   (use "but add <file>..." to update what will be cummitted)
#   (use "but restore <file>..." to discard changes in working directory)
#	modified:   dir1/modified
#
# Untracked files:
#   (use "but add <file>..." to include in what will be cummitted)
#	dir1/untracked dir2/untracked
#	dir2/modified  untracked
#
EOF
	COLUMNS=50 but -c status.displayCommentPrefix=true status --column="column dense" >output &&
	test_cmp expect output
'

test_expect_success 'status --column status.displayCommentPrefix=false' '
	strip_comments expect &&
	COLUMNS=49 but -c status.displayCommentPrefix=false status --column="column dense" >output &&
	test_cmp expect output
'

cat >expect <<\EOF
# On branch main
# Your branch and 'upstream' have diverged,
# and have 1 and 2 different cummits each, respectively.
#   (use "but pull" to merge the remote branch into yours)
#
# Changes to be cummitted:
#   (use "but restore --staged <file>..." to unstage)
#	new file:   dir2/added
#
# Changes not staged for cummit:
#   (use "but add <file>..." to update what will be cummitted)
#   (use "but restore <file>..." to discard changes in working directory)
#	modified:   dir1/modified
#
# Untracked files:
#   (use "but add <file>..." to include in what will be cummitted)
#	dir1/untracked
#	dir2/modified
#	dir2/untracked
#	untracked
#
EOF

test_expect_success 'status with status.displayCommentPrefix=true' '
	but -c status.displayCommentPrefix=true status >output &&
	test_cmp expect output
'

test_expect_success 'status with status.displayCommentPrefix=false' '
	strip_comments expect &&
	but -c status.displayCommentPrefix=false status >output &&
	test_cmp expect output
'

test_expect_success 'status -v' '
	(cat expect && but diff --cached) >expect-with-v &&
	but status -v >output &&
	test_cmp expect-with-v output
'

test_expect_success 'status -v -v' '
	(cat expect &&
	 echo "Changes to be cummitted:" &&
	 but -c diff.mnemonicprefix=true diff --cached &&
	 echo "--------------------------------------------------" &&
	 echo "Changes not staged for cummit:" &&
	 but -c diff.mnemonicprefix=true diff) >expect-with-v &&
	but status -v -v >output &&
	test_cmp expect-with-v output
'

test_expect_success 'setup fake editor' '
	cat >.but/editor <<-\EOF &&
	#! /bin/sh
	cp "$1" output
EOF
	chmod 755 .but/editor
'

cummit_template_commented () {
	(
		EDITOR=.but/editor &&
		export EDITOR &&
		# Fails due to empty message
		test_must_fail but cummit
	) &&
	! grep '^[^#]' output
}

test_expect_success 'cummit ignores status.displayCommentPrefix=false in CUMMIT_EDITMSG' '
	cummit_template_commented
'

cat >expect <<\EOF
On branch main
Your branch and 'upstream' have diverged,
and have 1 and 2 different cummits each, respectively.

Changes to be cummitted:
	new file:   dir2/added

Changes not staged for cummit:
	modified:   dir1/modified

Untracked files:
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF

test_expect_success 'status (advice.statusHints false)' '
	test_config advice.statusHints false &&
	but status >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF

test_expect_success 'status -s' '

	but status -s >output &&
	test_cmp expect output

'

test_expect_success 'status with butignore' '
	{
		echo ".butignore" &&
		echo "expect*" &&
		echo "output" &&
		echo "untracked"
	} >.butignore &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	?? dir2/modified
	EOF
	but status -s >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	?? dir2/modified
	!! .butignore
	!! dir1/untracked
	!! dir2/untracked
	!! expect
	!! expect-with-v
	!! output
	!! untracked
	EOF
	but status -s --ignored >output &&
	test_cmp expect output &&

	cat >expect <<\EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir2/modified

Ignored files:
  (use "but add -f <file>..." to include in what will be cummitted)
	.butignore
	dir1/untracked
	dir2/untracked
	expect
	expect-with-v
	output
	untracked

EOF
	but status --ignored >output &&
	test_cmp expect output
'

test_expect_success 'status with butignore (nothing untracked)' '
	{
		echo ".butignore" &&
		echo "expect*" &&
		echo "dir2/modified" &&
		echo "output" &&
		echo "untracked"
	} >.butignore &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	EOF
	but status -s >output &&
	test_cmp expect output &&

	cat >expect <<-\EOF &&
	 M dir1/modified
	A  dir2/added
	!! .butignore
	!! dir1/untracked
	!! dir2/modified
	!! dir2/untracked
	!! expect
	!! expect-with-v
	!! output
	!! untracked
	EOF
	but status -s --ignored >output &&
	test_cmp expect output &&

	cat >expect <<\EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Ignored files:
  (use "but add -f <file>..." to include in what will be cummitted)
	.butignore
	dir1/untracked
	dir2/modified
	dir2/untracked
	expect
	expect-with-v
	output
	untracked

EOF
	but status --ignored >output &&
	test_cmp expect output
'

cat >.butignore <<\EOF
.butignore
expect*
output*
EOF

cat >expect <<\EOF
## main...upstream [ahead 1, behind 2]
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF

test_expect_success 'status -s -b' '

	but status -s -b >output &&
	test_cmp expect output

'

test_expect_success 'status -s -z -b' '
	tr "\\n" Q <expect >expect.q &&
	mv expect.q expect &&
	but status -s -z -b >output &&
	nul_to_q <output >output.q &&
	mv output.q output &&
	test_cmp expect output
'

test_expect_success 'setup dir3' '
	mkdir dir3 &&
	: >dir3/untracked1 &&
	: >dir3/untracked2
'

test_expect_success 'status -uno' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files not listed (use -u option to show untracked files)
EOF
	but status -uno >output &&
	test_cmp expect output
'

test_expect_success 'status (status.showUntrackedFiles no)' '
	test_config status.showuntrackedfiles no &&
	but status >output &&
	test_cmp expect output
'

test_expect_success 'status -uno (advice.statusHints false)' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.

Changes to be cummitted:
	new file:   dir2/added

Changes not staged for cummit:
	modified:   dir1/modified

Untracked files not listed
EOF
	test_config advice.statusHints false &&
	but status -uno >output &&
	test_cmp expect output
'

cat >expect << EOF
 M dir1/modified
A  dir2/added
EOF
test_expect_success 'status -s -uno' '
	but status -s -uno >output &&
	test_cmp expect output
'

test_expect_success 'status -s (status.showUntrackedFiles no)' '
	but config status.showuntrackedfiles no &&
	but status -s >output &&
	test_cmp expect output
'

test_expect_success 'status -unormal' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	dir3/
	untracked

EOF
	but status -unormal >output &&
	test_cmp expect output
'

test_expect_success 'status (status.showUntrackedFiles normal)' '
	test_config status.showuntrackedfiles normal &&
	but status >output &&
	test_cmp expect output
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? dir3/
?? untracked
EOF
test_expect_success 'status -s -unormal' '
	but status -s -unormal >output &&
	test_cmp expect output
'

test_expect_success 'status -s (status.showUntrackedFiles normal)' '
	but config status.showuntrackedfiles normal &&
	but status -s >output &&
	test_cmp expect output
'

test_expect_success 'status -uall' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	dir3/untracked1
	dir3/untracked2
	untracked

EOF
	but status -uall >output &&
	test_cmp expect output
'

test_expect_success 'status (status.showUntrackedFiles all)' '
	test_config status.showuntrackedfiles all &&
	but status >output &&
	test_cmp expect output
'

test_expect_success 'teardown dir3' '
	rm -rf dir3
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF
test_expect_success 'status -s -uall' '
	test_unconfig status.showuntrackedfiles &&
	but status -s -uall >output &&
	test_cmp expect output
'
test_expect_success 'status -s (status.showUntrackedFiles all)' '
	test_config status.showuntrackedfiles all &&
	but status -s >output &&
	rm -rf dir3 &&
	test_cmp expect output
'

test_expect_success 'status with relative paths' '
	cat >expect <<\EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   ../dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	untracked
	../dir2/modified
	../dir2/untracked
	../untracked

EOF
	(cd dir1 && but status) >output &&
	test_cmp expect output
'

cat >expect <<\EOF
 M modified
A  ../dir2/added
?? untracked
?? ../dir2/modified
?? ../dir2/untracked
?? ../untracked
EOF
test_expect_success 'status -s with relative paths' '

	(cd dir1 && but status -s) >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF

test_expect_success 'status --porcelain ignores relative paths setting' '

	(cd dir1 && but status --porcelain) >output &&
	test_cmp expect output

'

test_expect_success 'setup unique colors' '

	but config status.color.untracked blue &&
	but config status.color.branch green &&
	but config status.color.localBranch yellow &&
	but config status.color.remoteBranch cyan

'

test_expect_success TTY 'status with color.ui' '
	cat >expect <<\EOF &&
On branch <GREEN>main<RESET>
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	<GREEN>new file:   dir2/added<RESET>

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	<RED>modified:   dir1/modified<RESET>

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	<BLUE>dir1/untracked<RESET>
	<BLUE>dir2/modified<RESET>
	<BLUE>dir2/untracked<RESET>
	<BLUE>untracked<RESET>

EOF
	test_config color.ui auto &&
	test_terminal but status | test_decode_color >output &&
	test_cmp expect output
'

test_expect_success TTY 'status with color.status' '
	test_config color.status auto &&
	test_terminal but status | test_decode_color >output &&
	test_cmp expect output
'

cat >expect <<\EOF
 <RED>M<RESET> dir1/modified
<GREEN>A<RESET>  dir2/added
<BLUE>??<RESET> dir1/untracked
<BLUE>??<RESET> dir2/modified
<BLUE>??<RESET> dir2/untracked
<BLUE>??<RESET> untracked
EOF

test_expect_success TTY 'status -s with color.ui' '

	but config color.ui auto &&
	test_terminal but status -s | test_decode_color >output &&
	test_cmp expect output

'

test_expect_success TTY 'status -s with color.status' '

	but config --unset color.ui &&
	but config color.status auto &&
	test_terminal but status -s | test_decode_color >output &&
	test_cmp expect output

'

cat >expect <<\EOF
## <YELLOW>main<RESET>...<CYAN>upstream<RESET> [ahead <YELLOW>1<RESET>, behind <CYAN>2<RESET>]
 <RED>M<RESET> dir1/modified
<GREEN>A<RESET>  dir2/added
<BLUE>??<RESET> dir1/untracked
<BLUE>??<RESET> dir2/modified
<BLUE>??<RESET> dir2/untracked
<BLUE>??<RESET> untracked
EOF

test_expect_success TTY 'status -s -b with color.status' '

	test_terminal but status -s -b | test_decode_color >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF

test_expect_success TTY 'status --porcelain ignores color.ui' '

	but config --unset color.status &&
	but config color.ui auto &&
	test_terminal but status --porcelain | test_decode_color >output &&
	test_cmp expect output

'

test_expect_success TTY 'status --porcelain ignores color.status' '

	but config --unset color.ui &&
	but config color.status auto &&
	test_terminal but status --porcelain | test_decode_color >output &&
	test_cmp expect output

'

# recover unconditionally from color tests
but config --unset color.status
but config --unset color.ui

test_expect_success 'status --porcelain respects -b' '

	but status --porcelain -b >output &&
	{
		echo "## main...upstream [ahead 1, behind 2]" &&
		cat expect
	} >tmp &&
	mv tmp expect &&
	test_cmp expect output

'



test_expect_success 'status without relative paths' '
	cat >expect <<\EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	test_config status.relativePaths false &&
	(cd dir1 && but status) >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF

test_expect_success 'status -s without relative paths' '

	test_config status.relativePaths false &&
	(cd dir1 && but status -s) >output &&
	test_cmp expect output

'

cat >expect <<\EOF
 M dir1/modified
A  dir2/added
A  "file with spaces"
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? "file with spaces 2"
?? untracked
EOF

test_expect_success 'status -s without relative paths' '
	test_when_finished "but rm --cached \"file with spaces\"; rm -f file*" &&
	>"file with spaces" &&
	>"file with spaces 2" &&
	>"expect with spaces" &&
	but add "file with spaces" &&

	but status -s >output &&
	test_cmp expect output &&

	but status -s --ignored >output &&
	grep "^!! \"expect with spaces\"$" output &&
	grep -v "^!! " output >output-wo-ignored &&
	test_cmp expect output-wo-ignored
'

test_expect_success 'dry-run of partial cummit excluding new file in index' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/
	untracked

EOF
	but cummit --dry-run dir1/modified >output &&
	test_cmp expect output
'

cat >expect <<EOF
:100644 100644 $EMPTY_BLOB $ZERO_OID M	dir1/modified
EOF
test_expect_success 'status refreshes the index' '
	touch dir2/added &&
	but status &&
	but diff-files >output &&
	test_cmp expect output
'

test_expect_success 'status shows detached HEAD properly after checking out non-local upstream branch' '
	test_when_finished rm -rf upstream downstream actual &&

	test_create_repo upstream &&
	test_cummit -C upstream foo &&

	but clone upstream downstream &&
	but -C downstream checkout @{u} &&
	but -C downstream status >actual &&
	grep -E "HEAD detached at [0-9a-f]+" actual
'

test_expect_success 'setup status submodule summary' '
	test_create_repo sm && (
		cd sm &&
		>foo &&
		but add foo &&
		but cummit -m "Add foo"
	) &&
	but add sm
'

test_expect_success 'status submodule summary is disabled by default' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added
	new file:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but status >output &&
	test_cmp expect output
'

# we expect the same as the previous test
test_expect_success 'status --untracked-files=all does not show submodule' '
	but status --untracked-files=all >output &&
	test_cmp expect output
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
A  sm
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF
test_expect_success 'status -s submodule summary is disabled by default' '
	but status -s >output &&
	test_cmp expect output
'

# we expect the same as the previous test
test_expect_success 'status -s --untracked-files=all does not show submodule' '
	but status -s --untracked-files=all >output &&
	test_cmp expect output
'

head=$(cd sm && but rev-parse --short=7 --verify HEAD)

test_expect_success 'status submodule summary' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 1 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	new file:   dir2/added
	new file:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Submodule changes to be cummitted:

* sm 0000000...$head (1):
  > Add foo

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but config status.submodulesummary 10 &&
	but status >output &&
	test_cmp expect output
'

test_expect_success 'status submodule summary with status.displayCommentPrefix=false' '
	strip_comments expect &&
	but -c status.displayCommentPrefix=false status >output &&
	test_cmp expect output
'

test_expect_success 'cummit with submodule summary ignores status.displayCommentPrefix' '
	cummit_template_commented
'

cat >expect <<EOF
 M dir1/modified
A  dir2/added
A  sm
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF
test_expect_success 'status -s submodule summary' '
	but status -s >output &&
	test_cmp expect output
'

test_expect_success 'status submodule summary (clean submodule): cummit' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but cummit -m "cummit submodule" &&
	but config status.submodulesummary 10 &&
	test_must_fail but cummit --dry-run >output &&
	test_cmp expect output &&
	but status >output &&
	test_cmp expect output
'

cat >expect <<EOF
 M dir1/modified
?? dir1/untracked
?? dir2/modified
?? dir2/untracked
?? untracked
EOF
test_expect_success 'status -s submodule summary (clean submodule)' '
	but status -s >output &&
	test_cmp expect output
'

test_expect_success 'status -z implies porcelain' '
	but status --porcelain |
	perl -pe "s/\012/\000/g" >expect &&
	but status -z >output &&
	test_cmp expect output
'

test_expect_success 'cummit --dry-run submodule summary (--amend)' '
	cat >expect <<EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --source=HEAD^1 --staged <file>..." to unstage)
	new file:   dir2/added
	new file:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Submodule changes to be cummitted:

* sm 0000000...$head (1):
  > Add foo

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but config status.submodulesummary 10 &&
	but cummit --dry-run --amend >output &&
	test_cmp expect output
'

test_expect_success POSIXPERM,SANITY 'status succeeds in a read-only repository' '
	test_when_finished "chmod 775 .but" &&
	(
		chmod a-w .but &&
		# make dir1/tracked stat-dirty
		>dir1/tracked1 && mv -f dir1/tracked1 dir1/tracked &&
		but status -s >output &&
		! grep dir1/tracked output &&
		# make sure "status" succeeded without writing index out
		but diff-files | grep dir1/tracked
	)
'

(cd sm && echo > bar && but add bar && but cummit -q -m 'Add bar') && but add sm
new_head=$(cd sm && but rev-parse --short=7 --verify HEAD)
touch .butmodules

test_expect_success '--ignore-submodules=untracked suppresses submodules with untracked content' '
	cat > expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Submodule changes to be cummitted:

* sm $head...$new_head (1):
  > Add bar

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	.butmodules
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	echo modified  sm/untracked &&
	but status --ignore-submodules=untracked >output &&
	test_cmp expect output
'

test_expect_success '.butmodules ignore=untracked suppresses submodules with untracked content' '
	test_config diff.ignoreSubmodules dirty &&
	but status >output &&
	test_cmp expect output &&
	but config --add -f .butmodules submodule.subname.ignore untracked &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success '.but/config ignore=untracked suppresses submodules with untracked content' '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore untracked &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config --remove-section -f .butmodules submodule.subname
'

test_expect_success '--ignore-submodules=dirty suppresses submodules with untracked content' '
	but status --ignore-submodules=dirty >output &&
	test_cmp expect output
'

test_expect_success '.butmodules ignore=dirty suppresses submodules with untracked content' '
	test_config diff.ignoreSubmodules dirty &&
	but status >output &&
	! test -s actual &&
	but config --add -f .butmodules submodule.subname.ignore dirty &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success '.but/config ignore=dirty suppresses submodules with untracked content' '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore dirty &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success '--ignore-submodules=dirty suppresses submodules with modified content' '
	echo modified >sm/foo &&
	but status --ignore-submodules=dirty >output &&
	test_cmp expect output
'

test_expect_success '.butmodules ignore=dirty suppresses submodules with modified content' '
	but config --add -f .butmodules submodule.subname.ignore dirty &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success '.but/config ignore=dirty suppresses submodules with modified content' '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore dirty &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success "--ignore-submodules=untracked doesn't suppress submodules with modified content" '
	cat > expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
  (cummit or discard the untracked or modified content in submodules)
	modified:   dir1/modified
	modified:   sm (modified content)

Submodule changes to be cummitted:

* sm $head...$new_head (1):
  > Add bar

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	.butmodules
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but status --ignore-submodules=untracked > output &&
	test_cmp expect output
'

test_expect_success ".butmodules ignore=untracked doesn't suppress submodules with modified content" '
	but config --add -f .butmodules submodule.subname.ignore untracked &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success ".but/config ignore=untracked doesn't suppress submodules with modified content" '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore untracked &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

head2=$(cd sm && but cummit -q -m "2nd cummit" foo && but rev-parse --short=7 --verify HEAD)

test_expect_success "--ignore-submodules=untracked doesn't suppress submodule summary" '
	cat > expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified
	modified:   sm (new cummits)

Submodule changes to be cummitted:

* sm $head...$new_head (1):
  > Add bar

Submodules changed but not updated:

* sm $new_head...$head2 (1):
  > 2nd cummit

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	.butmodules
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but status --ignore-submodules=untracked > output &&
	test_cmp expect output
'

test_expect_success ".butmodules ignore=untracked doesn't suppress submodule summary" '
	but config --add -f .butmodules submodule.subname.ignore untracked &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success ".but/config ignore=untracked doesn't suppress submodule summary" '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore untracked &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success "--ignore-submodules=dirty doesn't suppress submodule summary" '
	but status --ignore-submodules=dirty > output &&
	test_cmp expect output
'
test_expect_success ".butmodules ignore=dirty doesn't suppress submodule summary" '
	but config --add -f .butmodules submodule.subname.ignore dirty &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success ".but/config ignore=dirty doesn't suppress submodule summary" '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore dirty &&
	but config --add submodule.subname.path sm &&
	but status >output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

cat > expect << EOF
; On branch main
; Your branch and 'upstream' have diverged,
; and have 2 and 2 different cummits each, respectively.
;   (use "but pull" to merge the remote branch into yours)
;
; Changes to be cummitted:
;   (use "but restore --staged <file>..." to unstage)
;	modified:   sm
;
; Changes not staged for cummit:
;   (use "but add <file>..." to update what will be cummitted)
;   (use "but restore <file>..." to discard changes in working directory)
;	modified:   dir1/modified
;	modified:   sm (new cummits)
;
; Submodule changes to be cummitted:
;
; * sm $head...$new_head (1):
;   > Add bar
;
; Submodules changed but not updated:
;
; * sm $new_head...$head2 (1):
;   > 2nd cummit
;
; Untracked files:
;   (use "but add <file>..." to include in what will be cummitted)
;	.butmodules
;	dir1/untracked
;	dir2/modified
;	dir2/untracked
;	untracked
;
EOF

test_expect_success "status (core.commentchar with submodule summary)" '
	test_config core.commentchar ";" &&
	but -c status.displayCommentPrefix=true status >output &&
	test_cmp expect output
'

test_expect_success "status (core.commentchar with two chars with submodule summary)" '
	test_config core.commentchar ";;" &&
	test_must_fail but -c status.displayCommentPrefix=true status
'

test_expect_success "--ignore-submodules=all suppresses submodule summary" '
	cat > expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	.butmodules
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --ignore-submodules=all > output &&
	test_cmp expect output
'

test_expect_success '.butmodules ignore=all suppresses unstaged submodule summary' '
	cat > expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files:
  (use "but add <file>..." to include in what will be cummitted)
	.butmodules
	dir1/untracked
	dir2/modified
	dir2/untracked
	untracked

EOF
	but config --add -f .butmodules submodule.subname.ignore all &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but status > output &&
	test_cmp expect output &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success '.but/config ignore=all suppresses unstaged submodule summary' '
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore all &&
	but config --add submodule.subname.path sm &&
	but status > output &&
	test_cmp expect output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success 'setup of test environment' '
	but config status.showUntrackedFiles no &&
	but status -s >expected_short &&
	but status --no-short >expected_noshort
'

test_expect_success '"status.short=true" same as "-s"' '
	but -c status.short=true status >actual &&
	test_cmp expected_short actual
'

test_expect_success '"status.short=true" weaker than "--no-short"' '
	but -c status.short=true status --no-short >actual &&
	test_cmp expected_noshort actual
'

test_expect_success '"status.short=false" same as "--no-short"' '
	but -c status.short=false status >actual &&
	test_cmp expected_noshort actual
'

test_expect_success '"status.short=false" weaker than "-s"' '
	but -c status.short=false status -s >actual &&
	test_cmp expected_short actual
'

test_expect_success '"status.branch=true" same as "-b"' '
	but status -sb >expected_branch &&
	but -c status.branch=true status -s >actual &&
	test_cmp expected_branch actual
'

test_expect_success '"status.branch=true" different from "--no-branch"' '
	but status -s --no-branch  >expected_nobranch &&
	but -c status.branch=true status -s >actual &&
	! test_cmp expected_nobranch actual
'

test_expect_success '"status.branch=true" weaker than "--no-branch"' '
	but -c status.branch=true status -s --no-branch >actual &&
	test_cmp expected_nobranch actual
'

test_expect_success '"status.branch=true" weaker than "--porcelain"' '
       but -c status.branch=true status --porcelain >actual &&
       test_cmp expected_nobranch actual
'

test_expect_success '"status.branch=false" same as "--no-branch"' '
	but -c status.branch=false status -s >actual &&
	test_cmp expected_nobranch actual
'

test_expect_success '"status.branch=false" weaker than "-b"' '
	but -c status.branch=false status -sb >actual &&
	test_cmp expected_branch actual
'

test_expect_success 'Restore default test environment' '
	but config --unset status.showUntrackedFiles
'

test_expect_success 'but cummit will cummit a staged but ignored submodule' '
	but config --add -f .butmodules submodule.subname.ignore all &&
	but config --add -f .butmodules submodule.subname.path sm &&
	but config --add submodule.subname.ignore all &&
	but status -s --ignore-submodules=dirty >output &&
	test_i18ngrep "^M. sm" output &&
	GIT_EDITOR="echo hello >>\"\$1\"" &&
	export GIT_EDITOR &&
	but cummit -uno &&
	but status -s --ignore-submodules=dirty >output &&
	test_i18ngrep ! "^M. sm" output
'

test_expect_success 'but cummit --dry-run will show a staged but ignored submodule' '
	but reset HEAD^ &&
	but add sm &&
	cat >expect << EOF &&
On branch main
Your branch and '\''upstream'\'' have diverged,
and have 2 and 2 different cummits each, respectively.
  (use "but pull" to merge the remote branch into yours)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   sm

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   dir1/modified

Untracked files not listed (use -u option to show untracked files)
EOF
	but cummit -uno --dry-run >output &&
	test_cmp expect output &&
	but status -s --ignore-submodules=dirty >output &&
	test_i18ngrep "^M. sm" output
'

test_expect_success 'but cummit -m will cummit a staged but ignored submodule' '
	but cummit -uno -m message &&
	but status -s --ignore-submodules=dirty >output &&
	test_i18ngrep ! "^M. sm" output &&
	but config --remove-section submodule.subname &&
	but config -f .butmodules  --remove-section submodule.subname
'

test_expect_success 'show stash info with "--show-stash"' '
	but reset --hard &&
	but stash clear &&
	echo 1 >file &&
	but add file &&
	but stash &&
	but status >expected_default &&
	but status --show-stash >expected_with_stash &&
	test_i18ngrep "^Your stash currently has 1 entry$" expected_with_stash
'

test_expect_success 'no stash info with "--show-stash --no-show-stash"' '
	but status --show-stash --no-show-stash >expected_without_stash &&
	test_cmp expected_default expected_without_stash
'

test_expect_success '"status.showStash=false" weaker than "--show-stash"' '
	but -c status.showStash=false status --show-stash >actual &&
	test_cmp expected_with_stash actual
'

test_expect_success '"status.showStash=true" weaker than "--no-show-stash"' '
	but -c status.showStash=true status --no-show-stash >actual &&
	test_cmp expected_without_stash actual
'

test_expect_success 'no additional info if no stash entries' '
	but stash clear &&
	but -c status.showStash=true status >actual &&
	test_cmp expected_without_stash actual
'

test_expect_success '"No cummits yet" should be noted in status output' '
	but checkout --orphan empty-branch-1 &&
	but status >output &&
	test_i18ngrep "No cummits yet" output
'

test_expect_success '"No cummits yet" should not be noted in status output' '
	but checkout --orphan empty-branch-2 &&
	test_cummit test-cummit-1 &&
	but status >output &&
	test_i18ngrep ! "No cummits yet" output
'

test_expect_success '"Initial cummit" should be noted in cummit template' '
	but checkout --orphan empty-branch-3 &&
	touch to_be_cummitted_1 &&
	but add to_be_cummitted_1 &&
	but cummit --dry-run >output &&
	test_i18ngrep "Initial cummit" output
'

test_expect_success '"Initial cummit" should not be noted in cummit template' '
	but checkout --orphan empty-branch-4 &&
	test_cummit test-cummit-2 &&
	touch to_be_cummitted_2 &&
	but add to_be_cummitted_2 &&
	but cummit --dry-run >output &&
	test_i18ngrep ! "Initial cummit" output
'

test_expect_success '--no-optional-locks prevents index update' '
	test_set_magic_mtime .but/index &&
	but --no-optional-locks status &&
	test_is_magic_mtime .but/index &&
	but status &&
	! test_is_magic_mtime .but/index
'

test_expect_success 'racy timestamps will be fixed for clean worktree' '
	echo content >racy-dirty &&
	echo content >racy-racy &&
	but add racy* &&
	but cummit -m "racy test files" &&
	# let status rewrite the index, if necessary; after that we expect
	# no more index writes unless caused by racy timestamps; note that
	# timestamps may already be racy now (depending on previous tests)
	but status &&
	test_set_magic_mtime .but/index &&
	but status &&
	! test_is_magic_mtime .but/index
'

test_expect_success 'racy timestamps will be fixed for dirty worktree' '
	echo content2 >racy-dirty &&
	but status &&
	test_set_magic_mtime .but/index &&
	but status &&
	! test_is_magic_mtime .but/index
'

test_done
