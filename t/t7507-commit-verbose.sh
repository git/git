#!/bin/sh

test_description='verbose commit template'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

write_script "check-for-diff" <<\EOF &&
grep '^diff --git' "$1" >out
exit 0
EOF
test_set_editor "$PWD/check-for-diff"

cat >message <<'EOF'
subject

body
EOF

test_expect_success 'setup' '
	echo content >file &&
	git add file &&
	git commit -F message
'

test_expect_success 'initial commit shows verbose diff' '
	git commit --amend -v &&
	test_line_count = 1 out
'

test_expect_success 'second commit' '
	echo content modified >file &&
	git add file &&
	git commit -F message
'

check_message() {
	git log -1 --pretty=format:%s%n%n%b >actual &&
	test_cmp "$1" actual
}

test_expect_success 'verbose diff is stripped out' '
	git commit --amend -v &&
	check_message message &&
	test_line_count = 1 out
'

test_expect_success 'verbose diff is stripped out (mnemonicprefix)' '
	git config diff.mnemonicprefix true &&
	git commit --amend -v &&
	check_message message &&
	test_line_count = 1 out
'

cat >diff <<'EOF'
This is an example commit message that contains a diff.

diff --git c/file i/file
new file mode 100644
index 0000000..f95c11d
--- /dev/null
+++ i/file
@@ -0,0 +1 @@
+this is some content
EOF

test_expect_success 'diff in message is retained without -v' '
	git commit --amend -F diff &&
	check_message diff
'

test_expect_success 'diff in message is retained with -v' '
	git commit --amend -F diff -v &&
	check_message diff
'

test_expect_success 'submodule log is stripped out too with -v' '
	git config diff.submodule log &&
	test_config_global protocol.file.allow always &&
	git submodule add ./. sub &&
	git commit -m "sub added" &&
	(
		cd sub &&
		echo "more" >>file &&
		git commit -a -m "submodule commit"
	) &&
	(
		GIT_EDITOR=cat &&
		export GIT_EDITOR &&
		test_must_fail git commit -a -v 2>err
	) &&
	test_i18ngrep "Aborting commit due to empty commit message." err
'

test_expect_success 'verbose diff is stripped out with set core.commentChar' '
	(
		GIT_EDITOR=cat &&
		export GIT_EDITOR &&
		test_must_fail git -c core.commentchar=";" commit -a -v 2>err
	) &&
	test_i18ngrep "Aborting commit due to empty commit message." err
'

test_expect_success 'status does not verbose without --verbose' '
	git status >actual &&
	! grep "^diff --git" actual
'

test_expect_success 'setup -v -v' '
	echo dirty >file
'

for i in true 1
do
	test_expect_success "commit.verbose=$i and --verbose omitted" "
		git -c commit.verbose=$i commit --amend &&
		test_line_count = 1 out
	"
done

for i in false -2 -1 0
do
	test_expect_success "commit.verbose=$i and --verbose omitted" "
		git -c commit.verbose=$i commit --amend &&
		test_line_count = 0 out
	"
done

for i in 2 3
do
	test_expect_success "commit.verbose=$i and --verbose omitted" "
		git -c commit.verbose=$i commit --amend &&
		test_line_count = 2 out
	"
done

for i in true false -2 -1 0 1 2 3
do
	test_expect_success "commit.verbose=$i and --verbose" "
		git -c commit.verbose=$i commit --amend --verbose &&
		test_line_count = 1 out
	"

	test_expect_success "commit.verbose=$i and --no-verbose" "
		git -c commit.verbose=$i commit --amend --no-verbose &&
		test_line_count = 0 out
	"

	test_expect_success "commit.verbose=$i and -v -v" "
		git -c commit.verbose=$i commit --amend -v -v &&
		test_line_count = 2 out
	"
done

test_expect_success "status ignores commit.verbose=true" '
	git -c commit.verbose=true status >actual &&
	! grep "^diff --git actual"
'

test_done
