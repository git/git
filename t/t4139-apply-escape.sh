#!/bin/sh

test_description='paths written by git-apply cannot escape the working tree'

. ./test-lib.sh

# tests will try to write to ../foo, and we do not
# want them to escape the trash directory when they
# fail
test_expect_success 'bump git repo one level down' '
	mkdir inside &&
	mv .git inside/ &&
	cd inside
'

# $1 = name of file
# $2 = current path to file (if different)
mkpatch_add () {
	rm -f "${2:-$1}" &&
	cat <<-EOF
	diff --git a/$1 b/$1
	new file mode 100644
	index 0000000..53c74cd
	--- /dev/null
	+++ b/$1
	@@ -0,0 +1 @@
	+evil
	EOF
}

mkpatch_del () {
	echo evil >"${2:-$1}" &&
	cat <<-EOF
	diff --git a/$1 b/$1
	deleted file mode 100644
	index 53c74cd..0000000
	--- a/$1
	+++ /dev/null
	@@ -1 +0,0 @@
	-evil
	EOF
}

# $1 = name of file
# $2 = content of symlink
mkpatch_symlink () {
	rm -f "$1" &&
	cat <<-EOF
	diff --git a/$1 b/$1
	new file mode 120000
	index 0000000..$(printf "%s" "$2" | git hash-object --stdin)
	--- /dev/null
	+++ b/$1
	@@ -0,0 +1 @@
	+$2
	\ No newline at end of file
	EOF
}

test_expect_success 'cannot create file containing ..' '
	mkpatch_add ../foo >patch &&
	test_must_fail git apply patch &&
	test_path_is_missing ../foo
'

test_expect_success 'can create file containing .. with --unsafe-paths' '
	mkpatch_add ../foo >patch &&
	git apply --unsafe-paths patch &&
	test_path_is_file ../foo
'

test_expect_success  'cannot create file containing .. (index)' '
	mkpatch_add ../foo >patch &&
	test_must_fail git apply --index patch &&
	test_path_is_missing ../foo
'

test_expect_success  'cannot create file containing .. with --unsafe-paths (index)' '
	mkpatch_add ../foo >patch &&
	test_must_fail git apply --index --unsafe-paths patch &&
	test_path_is_missing ../foo
'

test_expect_success 'cannot delete file containing ..' '
	mkpatch_del ../foo >patch &&
	test_must_fail git apply patch &&
	test_path_is_file ../foo
'

test_expect_success 'can delete file containing .. with --unsafe-paths' '
	mkpatch_del ../foo >patch &&
	git apply --unsafe-paths patch &&
	test_path_is_missing ../foo
'

test_expect_success 'cannot delete file containing .. (index)' '
	mkpatch_del ../foo >patch &&
	test_must_fail git apply --index patch &&
	test_path_is_file ../foo
'

test_expect_success SYMLINKS 'symlink escape via ..' '
	{
		mkpatch_symlink tmp .. &&
		mkpatch_add tmp/foo ../foo
	} >patch &&
	test_must_fail git apply patch &&
	test_path_is_missing tmp &&
	test_path_is_missing ../foo
'

test_expect_success SYMLINKS 'symlink escape via .. (index)' '
	{
		mkpatch_symlink tmp .. &&
		mkpatch_add tmp/foo ../foo
	} >patch &&
	test_must_fail git apply --index patch &&
	test_path_is_missing tmp &&
	test_path_is_missing ../foo
'

test_expect_success SYMLINKS 'symlink escape via absolute path' '
	{
		mkpatch_symlink tmp "$(pwd)" &&
		mkpatch_add tmp/foo ../foo
	} >patch &&
	test_must_fail git apply patch &&
	test_path_is_missing tmp &&
	test_path_is_missing ../foo
'

test_expect_success SYMLINKS 'symlink escape via absolute path (index)' '
	{
		mkpatch_symlink tmp "$(pwd)" &&
		mkpatch_add tmp/foo ../foo
	} >patch &&
	test_must_fail git apply --index patch &&
	test_path_is_missing tmp &&
	test_path_is_missing ../foo
'

test_done
