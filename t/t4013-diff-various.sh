#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Various diff formatting options'

. ./test-lib.sh

LF='
'

test_expect_success setup '

	GIT_AUTHOR_DATE="2006-06-26 00:00:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:00:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	mkdir dir &&
	mkdir dir2 &&
	for i in 1 2 3; do echo $i; done >file0 &&
	for i in A B; do echo $i; done >dir/sub &&
	cat file0 >file2 &&
	git add file0 file2 dir/sub &&
	git commit -m Initial &&

	git branch initial &&
	git branch side &&

	GIT_AUTHOR_DATE="2006-06-26 00:01:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:01:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	for i in 4 5 6; do echo $i; done >>file0 &&
	for i in C D; do echo $i; done >>dir/sub &&
	rm -f file2 &&
	git update-index --remove file0 file2 dir/sub &&
	git commit -m "Second${LF}${LF}This is the second commit." &&

	GIT_AUTHOR_DATE="2006-06-26 00:02:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:02:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	for i in A B C; do echo $i; done >file1 &&
	git add file1 &&
	for i in E F; do echo $i; done >>dir/sub &&
	git update-index dir/sub &&
	git commit -m Third &&

	GIT_AUTHOR_DATE="2006-06-26 00:03:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:03:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	git checkout side &&
	for i in A B C; do echo $i; done >>file0 &&
	for i in 1 2; do echo $i; done >>dir/sub &&
	cat dir/sub >file3 &&
	git add file3 &&
	git update-index file0 dir/sub &&
	git commit -m Side &&

	GIT_AUTHOR_DATE="2006-06-26 00:04:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:04:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	git checkout master &&
	git pull -s ours . side &&

	GIT_AUTHOR_DATE="2006-06-26 00:05:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:05:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	for i in A B C; do echo $i; done >>file0 &&
	for i in 1 2; do echo $i; done >>dir/sub &&
	git update-index file0 dir/sub &&

	mkdir dir3 &&
	cp dir/sub dir3/sub &&
	test-chmtime +1 dir3/sub &&

	git config log.showroot false &&
	git commit --amend &&

	GIT_AUTHOR_DATE="2006-06-26 00:06:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:06:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&
	git checkout -b rearrange initial &&
	for i in B A; do echo $i; done >dir/sub &&
	git add dir/sub &&
	git commit -m "Rearranged lines in dir/sub" &&
	git checkout master &&

	GIT_AUTHOR_DATE="2006-06-26 00:06:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:06:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&
	git checkout -b mode initial &&
	git update-index --chmod=+x file0 &&
	git commit -m "update mode" &&
	git checkout -f master &&

	git config diff.renames false &&

	git show-branch
'

: <<\EOF
! [initial] Initial
 * [master] Merge branch 'side'
  ! [rearrange] Rearranged lines in dir/sub
   ! [side] Side
----
  +  [rearrange] Rearranged lines in dir/sub
 -   [master] Merge branch 'side'
 * + [side] Side
 *   [master^] Third
 *   [master~2] Second
+*++ [initial] Initial
EOF

V=$(git version | sed -e 's/^git version //' -e 's/\./\\./g')
while read magic cmd
do
	case "$magic" in
	'' | '#'*)
		continue ;;
	:*)
		magic=${magic#:}
		label="$magic-$cmd"
		case "$magic" in
		noellipses) ;;
		*)
			die "bug in t4103: unknown magic $magic" ;;
		esac ;;
	*)
		cmd="$magic $cmd" magic=
		label="$cmd" ;;
	esac
	test=$(echo "$label" | sed -e 's|[/ ][/ ]*|_|g')
	pfx=$(printf "%04d" $test_count)
	expect="$TEST_DIRECTORY/t4013/diff.$test"
	actual="$pfx-diff.$test"

	test_expect_success "git $cmd # magic is ${magic:-"(not used)"}" '
		{
			echo "$ git $cmd"
			case "$magic" in
			"")
				GIT_PRINT_SHA1_ELLIPSIS=yes git $cmd ;;
			noellipses)
				git $cmd ;;
			esac |
			sed -e "s/^\\(-*\\)$V\\(-*\\)\$/\\1g-i-t--v-e-r-s-i-o-n\2/" \
			    -e "s/^\\(.*mixed; boundary=\"-*\\)$V\\(-*\\)\"\$/\\1g-i-t--v-e-r-s-i-o-n\2\"/"
			echo "\$"
		} >"$actual" &&
		if test -f "$expect"
		then
			case $cmd in
			*format-patch* | *-stat*)
				test_i18ncmp "$expect" "$actual";;
			*)
				test_cmp "$expect" "$actual";;
			esac &&
			rm -f "$actual"
		else
			# this is to help developing new tests.
			cp "$actual" "$expect"
			false
		fi
	'
done <<\EOF
diff-tree initial
diff-tree -r initial
diff-tree -r --abbrev initial
diff-tree -r --abbrev=4 initial
diff-tree --root initial
diff-tree --root --abbrev initial
:noellipses diff-tree --root --abbrev initial
diff-tree --root -r initial
diff-tree --root -r --abbrev initial
:noellipses diff-tree --root -r --abbrev initial
diff-tree --root -r --abbrev=4 initial
:noellipses diff-tree --root -r --abbrev=4 initial
diff-tree -p initial
diff-tree --root -p initial
diff-tree --patch-with-stat initial
diff-tree --root --patch-with-stat initial
diff-tree --patch-with-raw initial
diff-tree --root --patch-with-raw initial

diff-tree --pretty initial
diff-tree --pretty --root initial
diff-tree --pretty -p initial
diff-tree --pretty --stat initial
diff-tree --pretty --summary initial
diff-tree --pretty --stat --summary initial
diff-tree --pretty --root -p initial
diff-tree --pretty --root --stat initial
# improved by Timo's patch
diff-tree --pretty --root --summary initial
# improved by Timo's patch
diff-tree --pretty --root --summary -r initial
diff-tree --pretty --root --stat --summary initial
diff-tree --pretty --patch-with-stat initial
diff-tree --pretty --root --patch-with-stat initial
diff-tree --pretty --patch-with-raw initial
diff-tree --pretty --root --patch-with-raw initial

diff-tree --pretty=oneline initial
diff-tree --pretty=oneline --root initial
diff-tree --pretty=oneline -p initial
diff-tree --pretty=oneline --root -p initial
diff-tree --pretty=oneline --patch-with-stat initial
# improved by Timo's patch
diff-tree --pretty=oneline --root --patch-with-stat initial
diff-tree --pretty=oneline --patch-with-raw initial
diff-tree --pretty=oneline --root --patch-with-raw initial

diff-tree --pretty side
diff-tree --pretty -p side
diff-tree --pretty --patch-with-stat side

diff-tree initial mode
diff-tree --stat initial mode
diff-tree --summary initial mode

diff-tree master
diff-tree -p master
diff-tree -p -m master
diff-tree -c master
diff-tree -c --abbrev master
:noellipses diff-tree -c --abbrev master
diff-tree --cc master
# stat only should show the diffstat with the first parent
diff-tree -c --stat master
diff-tree --cc --stat master
diff-tree -c --stat --summary master
diff-tree --cc --stat --summary master
# stat summary should show the diffstat and summary with the first parent
diff-tree -c --stat --summary side
diff-tree --cc --stat --summary side
# improved by Timo's patch
diff-tree --cc --patch-with-stat master
# improved by Timo's patch
diff-tree --cc --patch-with-stat --summary master
# this is correct
diff-tree --cc --patch-with-stat --summary side

log master
log -p master
log --root master
log --root -p master
log --patch-with-stat master
log --root --patch-with-stat master
log --root --patch-with-stat --summary master
# improved by Timo's patch
log --root -c --patch-with-stat --summary master
# improved by Timo's patch
log --root --cc --patch-with-stat --summary master
log -p --first-parent master
log -m -p --first-parent master
log -m -p master
log -SF master
log -S F master
log -SF -p master
log -SF master --max-count=0
log -SF master --max-count=1
log -SF master --max-count=2
log -GF master
log -GF -p master
log -GF -p --pickaxe-all master
log --decorate --all
log --decorate=full --all

rev-list --parents HEAD
rev-list --children HEAD

whatchanged master
:noellipses whatchanged master
whatchanged -p master
whatchanged --root master
:noellipses whatchanged --root master
whatchanged --root -p master
whatchanged --patch-with-stat master
whatchanged --root --patch-with-stat master
whatchanged --root --patch-with-stat --summary master
# improved by Timo's patch
whatchanged --root -c --patch-with-stat --summary master
# improved by Timo's patch
whatchanged --root --cc --patch-with-stat --summary master
whatchanged -SF master
:noellipses whatchanged -SF master
whatchanged -SF -p master

log --patch-with-stat master -- dir/
whatchanged --patch-with-stat master -- dir/
log --patch-with-stat --summary master -- dir/
whatchanged --patch-with-stat --summary master -- dir/

show initial
show --root initial
show side
show master
show -c master
show -m master
show --first-parent master
show --stat side
show --stat --summary side
show --patch-with-stat side
show --patch-with-raw side
:noellipses show --patch-with-raw side
show --patch-with-stat --summary side

format-patch --stdout initial..side
format-patch --stdout initial..master^
format-patch --stdout initial..master
format-patch --stdout --no-numbered initial..master
format-patch --stdout --numbered initial..master
format-patch --attach --stdout initial..side
format-patch --attach --stdout --suffix=.diff initial..side
format-patch --attach --stdout initial..master^
format-patch --attach --stdout initial..master
format-patch --inline --stdout initial..side
format-patch --inline --stdout initial..master^
format-patch --inline --stdout --numbered-files initial..master
format-patch --inline --stdout initial..master
format-patch --inline --stdout --subject-prefix=TESTCASE initial..master
config format.subjectprefix DIFFERENT_PREFIX
format-patch --inline --stdout initial..master^^
format-patch --stdout --cover-letter -n initial..master^

diff --abbrev initial..side
diff -r initial..side
diff --stat initial..side
diff -r --stat initial..side
diff initial..side
diff --patch-with-stat initial..side
diff --patch-with-raw initial..side
:noellipses diff --patch-with-raw initial..side
diff --patch-with-stat -r initial..side
diff --patch-with-raw -r initial..side
:noellipses diff --patch-with-raw -r initial..side
diff --name-status dir2 dir
diff --no-index --name-status dir2 dir
diff --no-index --name-status -- dir2 dir
diff --no-index dir dir3
diff master master^ side
# Can't use spaces...
diff --line-prefix=abc master master^ side
diff --dirstat master~1 master~2
diff --dirstat initial rearrange
diff --dirstat-by-file initial rearrange
# No-index --abbrev and --no-abbrev
diff --raw initial
:noellipses diff --raw initial
diff --raw --abbrev=4 initial
:noellipses diff --raw --abbrev=4 initial
diff --raw --no-abbrev initial
diff --no-index --raw dir2 dir
:noellipses diff --no-index --raw dir2 dir
diff --no-index --raw --abbrev=4 dir2 dir
:noellipses diff --no-index --raw --abbrev=4 dir2 dir
diff --no-index --raw --no-abbrev dir2 dir
EOF

test_expect_success 'log -S requires an argument' '
	test_must_fail git log -S
'

test_expect_success 'diff --cached on unborn branch' '
	echo ref: refs/heads/unborn >.git/HEAD &&
	git diff --cached >result &&
	test_cmp "$TEST_DIRECTORY/t4013/diff.diff_--cached" result
'

test_expect_success 'diff --cached -- file on unborn branch' '
	git diff --cached -- file0 >result &&
	test_cmp "$TEST_DIRECTORY/t4013/diff.diff_--cached_--_file0" result
'
test_expect_success 'diff --line-prefix with spaces' '
	git diff --line-prefix="| | | " --cached -- file0 >result &&
	test_cmp "$TEST_DIRECTORY/t4013/diff.diff_--line-prefix_--cached_--_file0" result
'

test_expect_success 'diff-tree --stdin with log formatting' '
	cat >expect <<-\EOF &&
	Side
	Third
	Second
	EOF
	git rev-list master | git diff-tree --stdin --format=%s -s >actual &&
	test_cmp expect actual
'

test_done
