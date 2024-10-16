#!/bin/sh
#
# Copyright (c) 2009 Jens Lehmann, based on t7401 by Ping Yin
# Copyright (c) 2011 Alexey Shumkin (+ non-UTF-8 commit encoding tests)
# Copyright (c) 2016 Jacob Keller (copy + convert to --submodule=diff)
#

test_description='Support for diff format verbose submodule difference in git diff

This test tries to verify the sanity of --submodule=diff option of git diff.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Test non-UTF-8 encoding in case iconv is available.
if test_have_prereq ICONV
then
	test_encoding="ISO8859-1"
	# String "added" in German (translated with Google Translate), encoded in UTF-8,
	# used in sample commit log messages in add_file() function below.
	added=$(printf "hinzugef\303\274gt")
else
	test_encoding="UTF-8"
	added="added"
fi

add_file () {
	(
		cd "$1" &&
		shift &&
		for name
		do
			echo "$name" >"$name" &&
			git add "$name" &&
			test_tick &&
			# "git commit -m" would break MinGW, as Windows refuse to pass
			# $test_encoding encoded parameter to git.
			echo "Add $name ($added $name)" | iconv -f utf-8 -t $test_encoding |
			git -c "i18n.commitEncoding=$test_encoding" commit -F -
		done >/dev/null &&
		git rev-parse --short --verify HEAD
	)
}

commit_file () {
	test_tick &&
	git commit "$@" -m "Commit $*" >/dev/null
}

diff_cmp () {
       for i in "$1" "$2"
       do
		sed -e 's/^index 0000000\.\.[0-9a-f]*/index 0000000..1234567/' \
		-e 's/^index [0-9a-f]*\.\.[0-9a-f]*/index 1234567..89abcde/' \
		"$i" >"$i.compare" || return 1
       done &&
       test_cmp "$1.compare" "$2.compare" &&
       rm -f "$1.compare" "$2.compare"
}

test_expect_success 'setup repository' '
	test_create_repo sm1 &&
	add_file . foo &&
	head1=$(add_file sm1 foo1 foo2) &&
	fullhead1=$(git -C sm1 rev-parse --verify HEAD)
'

test_expect_success 'added submodule' '
	git add sm1 &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	diff --git a/sm1/foo1 b/sm1/foo1
	new file mode 100644
	index 0000000..1715acd
	--- /dev/null
	+++ b/sm1/foo1
	@@ -0,0 +1 @@
	+foo1
	diff --git a/sm1/foo2 b/sm1/foo2
	new file mode 100644
	index 0000000..54b060e
	--- /dev/null
	+++ b/sm1/foo2
	@@ -0,0 +1 @@
	+foo2
	EOF
	diff_cmp expected actual
'

test_expect_success 'added submodule, set diff.submodule' '
	test_config diff.submodule log &&
	git add sm1 &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	diff --git a/sm1/foo1 b/sm1/foo1
	new file mode 100644
	index 0000000..1715acd
	--- /dev/null
	+++ b/sm1/foo1
	@@ -0,0 +1 @@
	+foo1
	diff --git a/sm1/foo2 b/sm1/foo2
	new file mode 100644
	index 0000000..54b060e
	--- /dev/null
	+++ b/sm1/foo2
	@@ -0,0 +1 @@
	+foo2
	EOF
	diff_cmp expected actual
'

test_expect_success '--submodule=short overrides diff.submodule' '
	test_config diff.submodule log &&
	git add sm1 &&
	git diff --submodule=short --cached >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject commit $fullhead1
	EOF
	diff_cmp expected actual
'

test_expect_success 'diff.submodule does not affect plumbing' '
	test_config diff.submodule log &&
	git diff-index -p HEAD >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject commit $fullhead1
	EOF
	diff_cmp expected actual
'

commit_file sm1 &&
head2=$(add_file sm1 foo3)

test_expect_success 'modified submodule(forward)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	diff --git a/sm1/foo3 b/sm1/foo3
	new file mode 100644
	index 0000000..c1ec6c6
	--- /dev/null
	+++ b/sm1/foo3
	@@ -0,0 +1 @@
	+foo3
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule(forward)' '
	git diff --submodule=diff >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	diff --git a/sm1/foo3 b/sm1/foo3
	new file mode 100644
	index 0000000..c1ec6c6
	--- /dev/null
	+++ b/sm1/foo3
	@@ -0,0 +1 @@
	+foo3
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule(forward) --submodule' '
	git diff --submodule >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3 ($added foo3)
	EOF
	diff_cmp expected actual
'

fullhead2=$(cd sm1; git rev-parse --verify HEAD)
test_expect_success 'modified submodule(forward) --submodule=short' '
	git diff --submodule=short >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	index $head1..$head2 160000
	--- a/sm1
	+++ b/sm1
	@@ -1 +1 @@
	-Subproject commit $fullhead1
	+Subproject commit $fullhead2
	EOF
	diff_cmp expected actual
'

commit_file sm1 &&
head3=$(
	cd sm1 &&
	git reset --hard HEAD~2 >/dev/null &&
	git rev-parse --short --verify HEAD
)

test_expect_success 'modified submodule(backward)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2..$head3 (rewind):
	diff --git a/sm1/foo2 b/sm1/foo2
	deleted file mode 100644
	index 54b060e..0000000
	--- a/sm1/foo2
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo2
	diff --git a/sm1/foo3 b/sm1/foo3
	deleted file mode 100644
	index c1ec6c6..0000000
	--- a/sm1/foo3
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo3
	EOF
	diff_cmp expected actual
'

head4=$(add_file sm1 foo4 foo5)
test_expect_success 'modified submodule(backward and forward)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2...$head4:
	diff --git a/sm1/foo2 b/sm1/foo2
	deleted file mode 100644
	index 54b060e..0000000
	--- a/sm1/foo2
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo2
	diff --git a/sm1/foo3 b/sm1/foo3
	deleted file mode 100644
	index c1ec6c6..0000000
	--- a/sm1/foo3
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo3
	diff --git a/sm1/foo4 b/sm1/foo4
	new file mode 100644
	index 0000000..a0016db
	--- /dev/null
	+++ b/sm1/foo4
	@@ -0,0 +1 @@
	+foo4
	diff --git a/sm1/foo5 b/sm1/foo5
	new file mode 100644
	index 0000000..d6f2413
	--- /dev/null
	+++ b/sm1/foo5
	@@ -0,0 +1 @@
	+foo5
	EOF
	diff_cmp expected actual
'

commit_file sm1 &&
mv sm1 sm1-bak &&
echo sm1 >sm1 &&
head5=$(git hash-object sm1 | cut -c1-7) &&
git add sm1 &&
rm -f sm1 &&
mv sm1-bak sm1

test_expect_success 'typechanged submodule(submodule->blob), --cached' '
	git diff --submodule=diff --cached >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --git a/sm1/foo1 b/sm1/foo1
	deleted file mode 100644
	index 1715acd..0000000
	--- a/sm1/foo1
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo1
	diff --git a/sm1/foo4 b/sm1/foo4
	deleted file mode 100644
	index a0016db..0000000
	--- a/sm1/foo4
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo4
	diff --git a/sm1/foo5 b/sm1/foo5
	deleted file mode 100644
	index d6f2413..0000000
	--- a/sm1/foo5
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo5
	diff --git a/sm1 b/sm1
	new file mode 100644
	index 0000000..9da5fb8
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+sm1
	EOF
	diff_cmp expected actual
'

test_expect_success 'typechanged submodule(submodule->blob)' '
	git diff --submodule=diff >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	deleted file mode 100644
	index 9da5fb8..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-sm1
	Submodule sm1 0000000...$head4 (new submodule)
	diff --git a/sm1/foo1 b/sm1/foo1
	new file mode 100644
	index 0000000..1715acd
	--- /dev/null
	+++ b/sm1/foo1
	@@ -0,0 +1 @@
	+foo1
	diff --git a/sm1/foo4 b/sm1/foo4
	new file mode 100644
	index 0000000..a0016db
	--- /dev/null
	+++ b/sm1/foo4
	@@ -0,0 +1 @@
	+foo4
	diff --git a/sm1/foo5 b/sm1/foo5
	new file mode 100644
	index 0000000..d6f2413
	--- /dev/null
	+++ b/sm1/foo5
	@@ -0,0 +1 @@
	+foo5
	EOF
	diff_cmp expected actual
'

rm -rf sm1 &&
git checkout-index sm1
test_expect_success 'typechanged submodule(submodule->blob)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --git a/sm1 b/sm1
	new file mode 100644
	index 0000000..9da5fb8
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+sm1
	EOF
	diff_cmp expected actual
'

rm -f sm1 &&
test_create_repo sm1 &&
head6=$(add_file sm1 foo6 foo7)
test_expect_success 'nonexistent commit' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...$head6 (commits not present)
	EOF
	diff_cmp expected actual
'

commit_file
test_expect_success 'typechanged submodule(blob->submodule)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	deleted file mode 100644
	index 9da5fb8..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-sm1
	Submodule sm1 0000000...$head6 (new submodule)
	diff --git a/sm1/foo6 b/sm1/foo6
	new file mode 100644
	index 0000000..462398b
	--- /dev/null
	+++ b/sm1/foo6
	@@ -0,0 +1 @@
	+foo6
	diff --git a/sm1/foo7 b/sm1/foo7
	new file mode 100644
	index 0000000..6e9262c
	--- /dev/null
	+++ b/sm1/foo7
	@@ -0,0 +1 @@
	+foo7
	EOF
	diff_cmp expected actual
'

commit_file sm1 &&
test_expect_success 'submodule is up to date' '
	head7=$(git -C sm1 rev-parse --short --verify HEAD) &&
	git diff-index -p --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content' '
	echo new > sm1/new-file &&
	git diff-index -p --ignore-submodules=none --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	EOF
	diff_cmp expected actual
'

test_expect_success 'submodule contains untracked content (untracked ignored)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content (dirty ignored)' '
	git diff-index -p --ignore-submodules=dirty --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content (all ignored)' '
	git diff-index -p --ignore-submodules=all --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked and modified content' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules=none --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

# NOT OK
test_expect_success 'submodule contains untracked and modified content (untracked ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'submodule contains untracked and modified content (dirty ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules=dirty --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked and modified content (all ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains modified content' '
	rm -f sm1/new-file &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

(cd sm1; git commit -mchange foo6 >/dev/null) &&
head8=$(cd sm1; git rev-parse --short --verify HEAD) &&
test_expect_success 'submodule is modified' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content' '
	echo new > sm1/new-file &&
	git diff-index -p --ignore-submodules=none --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (untracked ignored)' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (dirty ignored)' '
	git diff-index -p --ignore-submodules=dirty --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (all ignored)' '
	git diff-index -p --ignore-submodules=all --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'modified submodule contains untracked and modified content' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules=none --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..dfda541 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1,2 @@
	-foo6
	+new
	+modification
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (untracked ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..e20e2d9 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1,3 @@
	-foo6
	+new
	+modification
	+modification
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (dirty ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules=dirty --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..3e75765 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1 @@
	-foo6
	+new
	EOF
	diff_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (all ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules --submodule=diff HEAD >actual &&
	test_must_be_empty actual
'

# NOT OK
test_expect_success 'modified submodule contains modified content' '
	rm -f sm1/new-file &&
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head7..$head8:
	diff --git a/sm1/foo6 b/sm1/foo6
	index 462398b..ac466ca 100644
	--- a/sm1/foo6
	+++ b/sm1/foo6
	@@ -1 +1,5 @@
	-foo6
	+new
	+modification
	+modification
	+modification
	+modification
	EOF
	diff_cmp expected actual
'

rm -rf sm1
test_expect_success 'deleted submodule' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	EOF
	diff_cmp expected actual
'

test_expect_success 'create second submodule' '
	test_create_repo sm2 &&
	head9=$(add_file sm2 foo8 foo9) &&
	git add sm2
'

test_expect_success 'multiple submodules' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 0000000...$head9 (new submodule)
	diff --git a/sm2/foo8 b/sm2/foo8
	new file mode 100644
	index 0000000..db9916b
	--- /dev/null
	+++ b/sm2/foo8
	@@ -0,0 +1 @@
	+foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	new file mode 100644
	index 0000000..9c3b4f6
	--- /dev/null
	+++ b/sm2/foo9
	@@ -0,0 +1 @@
	+foo9
	EOF
	diff_cmp expected actual
'

test_expect_success 'path filter' '
	git diff-index -p --submodule=diff HEAD sm2 >actual &&
	cat >expected <<-EOF &&
	Submodule sm2 0000000...$head9 (new submodule)
	diff --git a/sm2/foo8 b/sm2/foo8
	new file mode 100644
	index 0000000..db9916b
	--- /dev/null
	+++ b/sm2/foo8
	@@ -0,0 +1 @@
	+foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	new file mode 100644
	index 0000000..9c3b4f6
	--- /dev/null
	+++ b/sm2/foo9
	@@ -0,0 +1 @@
	+foo9
	EOF
	diff_cmp expected actual
'

cat >.gitmodules <<-EOF
[submodule "sm2"]
	path = sm2
	url = bogus_url
EOF
git add .gitmodules
commit_file sm2 .gitmodules

test_expect_success 'given commit' '
	git diff-index -p --submodule=diff HEAD^ >actual &&
	cat >expected <<-EOF &&
	diff --git a/.gitmodules b/.gitmodules
	new file mode 100644
	index 1234567..89abcde
	--- /dev/null
	+++ b/.gitmodules
	@@ -0,0 +1,3 @@
	+[submodule "sm2"]
	+path = sm2
	+url = bogus_url
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 0000000...$head9 (new submodule)
	diff --git a/sm2/foo8 b/sm2/foo8
	new file mode 100644
	index 0000000..db9916b
	--- /dev/null
	+++ b/sm2/foo8
	@@ -0,0 +1 @@
	+foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	new file mode 100644
	index 0000000..9c3b4f6
	--- /dev/null
	+++ b/sm2/foo9
	@@ -0,0 +1 @@
	+foo9
	EOF
	diff_cmp expected actual
'

test_expect_success 'setup .git file for sm2' '
	git submodule absorbgitdirs sm2
'

test_expect_success 'diff --submodule=diff with .git file' '
	git diff --submodule=diff HEAD^ >actual &&
	cat >expected <<-EOF &&
	diff --git a/.gitmodules b/.gitmodules
	new file mode 100644
	index 1234567..89abcde
	--- /dev/null
	+++ b/.gitmodules
	@@ -0,0 +1,3 @@
	+[submodule "sm2"]
	+path = sm2
	+url = bogus_url
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 0000000...$head9 (new submodule)
	diff --git a/sm2/foo8 b/sm2/foo8
	new file mode 100644
	index 0000000..db9916b
	--- /dev/null
	+++ b/sm2/foo8
	@@ -0,0 +1 @@
	+foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	new file mode 100644
	index 0000000..9c3b4f6
	--- /dev/null
	+++ b/sm2/foo9
	@@ -0,0 +1 @@
	+foo9
	EOF
	diff_cmp expected actual
'

mv sm2 sm2-bak

test_expect_success 'deleted submodule with .git file' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 $head9...0000000 (submodule deleted)
	diff --git a/sm2/foo8 b/sm2/foo8
	deleted file mode 100644
	index 1234567..89abcde
	--- a/sm2/foo8
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	deleted file mode 100644
	index 1234567..89abcde
	--- a/sm2/foo9
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo9
	EOF
	diff_cmp expected actual
'

echo submodule-to-blob>sm2

test_expect_success 'typechanged(submodule->blob) submodule with .git file' '
	git diff-index -p --submodule=diff HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 $head9...0000000 (submodule deleted)
	diff --git a/sm2/foo8 b/sm2/foo8
	deleted file mode 100644
	index 1234567..89abcde
	--- a/sm2/foo8
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	deleted file mode 100644
	index 1234567..89abcde
	--- a/sm2/foo9
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo9
	diff --git a/sm2 b/sm2
	new file mode 100644
	index 1234567..89abcde
	--- /dev/null
	+++ b/sm2
	@@ -0,0 +1 @@
	+submodule-to-blob
	EOF
	diff_cmp expected actual
'

rm sm2
mv sm2-bak sm2

test_expect_success 'setup nested submodule' '
	git -c protocol.file.allow=always -C sm2 submodule add ../sm2 nested &&
	git -C sm2 commit -a -m "nested sub" &&
	head10=$(git -C sm2 rev-parse --short --verify HEAD)
'

test_expect_success 'move nested submodule HEAD' '
	echo "nested content" >sm2/nested/file &&
	git -C sm2/nested add file &&
	git -C sm2/nested commit --allow-empty -m "new HEAD" &&
	head11=$(git -C sm2/nested rev-parse --short --verify HEAD)
'

test_expect_success 'diff --submodule=diff with moved nested submodule HEAD' '
	cat >expected <<-EOF &&
	Submodule nested $head9..$head11:
	diff --git a/nested/file b/nested/file
	new file mode 100644
	index 0000000..ca281f5
	--- /dev/null
	+++ b/nested/file
	@@ -0,0 +1 @@
	+nested content
	EOF
	git -C sm2 diff --submodule=diff >actual 2>err &&
	test_must_be_empty err &&
	diff_cmp expected actual
'

test_expect_success 'diff --submodule=diff recurses into nested submodules' '
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 contains modified content
	Submodule sm2 $head9..$head10:
	diff --git a/sm2/.gitmodules b/sm2/.gitmodules
	new file mode 100644
	index 0000000..3a816b8
	--- /dev/null
	+++ b/sm2/.gitmodules
	@@ -0,0 +1,3 @@
	+[submodule "nested"]
	+	path = nested
	+	url = ../sm2
	Submodule nested 0000000...$head11 (new submodule)
	diff --git a/sm2/nested/file b/sm2/nested/file
	new file mode 100644
	index 0000000..ca281f5
	--- /dev/null
	+++ b/sm2/nested/file
	@@ -0,0 +1 @@
	+nested content
	diff --git a/sm2/nested/foo8 b/sm2/nested/foo8
	new file mode 100644
	index 0000000..db9916b
	--- /dev/null
	+++ b/sm2/nested/foo8
	@@ -0,0 +1 @@
	+foo8
	diff --git a/sm2/nested/foo9 b/sm2/nested/foo9
	new file mode 100644
	index 0000000..9c3b4f6
	--- /dev/null
	+++ b/sm2/nested/foo9
	@@ -0,0 +1 @@
	+foo9
	EOF
	git diff --submodule=diff >actual 2>err &&
	test_must_be_empty err &&
	diff_cmp expected actual
'

(cd sm2; commit_file nested)
commit_file sm2
head12=$(cd sm2; git rev-parse --short --verify HEAD)

mv sm2 sm2-bak

test_expect_success 'diff --submodule=diff recurses into deleted nested submodules' '
	cat >expected <<-EOF &&
	Submodule sm1 $head7...0000000 (submodule deleted)
	Submodule sm2 $head12...0000000 (submodule deleted)
	diff --git a/sm2/.gitmodules b/sm2/.gitmodules
	deleted file mode 100644
	index 3a816b8..0000000
	--- a/sm2/.gitmodules
	+++ /dev/null
	@@ -1,3 +0,0 @@
	-[submodule "nested"]
	-	path = nested
	-	url = ../sm2
	diff --git a/sm2/foo8 b/sm2/foo8
	deleted file mode 100644
	index db9916b..0000000
	--- a/sm2/foo8
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo8
	diff --git a/sm2/foo9 b/sm2/foo9
	deleted file mode 100644
	index 9c3b4f6..0000000
	--- a/sm2/foo9
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo9
	Submodule nested $head11...0000000 (submodule deleted)
	diff --git a/sm2/nested/file b/sm2/nested/file
	deleted file mode 100644
	index ca281f5..0000000
	--- a/sm2/nested/file
	+++ /dev/null
	@@ -1 +0,0 @@
	-nested content
	diff --git a/sm2/nested/foo8 b/sm2/nested/foo8
	deleted file mode 100644
	index db9916b..0000000
	--- a/sm2/nested/foo8
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo8
	diff --git a/sm2/nested/foo9 b/sm2/nested/foo9
	deleted file mode 100644
	index 9c3b4f6..0000000
	--- a/sm2/nested/foo9
	+++ /dev/null
	@@ -1 +0,0 @@
	-foo9
	EOF
	git diff --submodule=diff >actual 2>err &&
	test_must_be_empty err &&
	diff_cmp expected actual
'

mv sm2-bak sm2

test_done
