#!/bin/sh

test_description='diff function context'

. ./test-lib.sh

dir="$TEST_DIRECTORY/t4051"

commit_and_tag () {
	tag=$1 &&
	shift &&
	git add "$@" &&
	test_tick &&
	git commit -m "$tag" &&
	git tag "$tag"
}

first_context_line () {
	awk '
		found {print; exit}
		/^@@/ {found = 1}
	'
}

last_context_line () {
	sed -ne \$p
}

check_diff () {
	name=$1
	desc=$2
	options="-W $3"

	test_expect_success "$desc" '
		git diff $options "$name^" "$name" >"$name.diff"
	'

	test_expect_success ' diff applies' '
		test_when_finished "git reset --hard" &&
		git checkout --detach "$name^" &&
		git apply --index "$name.diff" &&
		git diff --exit-code "$name"
	'
}

test_expect_success 'setup' '
	cat "$dir/includes.c" "$dir/dummy.c" "$dir/dummy.c" "$dir/hello.c" \
		"$dir/dummy.c" "$dir/dummy.c" >file.c &&
	commit_and_tag initial file.c &&

	grep -v "delete me from hello" <file.c >file.c.new &&
	mv file.c.new file.c &&
	commit_and_tag changed_hello file.c &&

	grep -v "delete me from includes" <file.c >file.c.new &&
	mv file.c.new file.c &&
	commit_and_tag changed_includes file.c &&

	cat "$dir/appended1.c" >>file.c &&
	commit_and_tag appended file.c &&

	cat "$dir/appended2.c" >>file.c &&
	commit_and_tag extended file.c &&

	grep -v "Begin of second part" <file.c >file.c.new &&
	mv file.c.new file.c &&
	commit_and_tag long_common_tail file.c
'

check_diff changed_hello 'changed function'

test_expect_success ' context includes begin' '
	grep "^ .*Begin of hello" changed_hello.diff
'

test_expect_success ' context includes end' '
	grep "^ .*End of hello" changed_hello.diff
'

test_expect_success ' context does not include other functions' '
	test $(grep -c "^[ +-].*Begin" changed_hello.diff) -le 1
'

test_expect_success ' context does not include preceding empty lines' '
	test "$(first_context_line <changed_hello.diff)" != " "
'

test_expect_success ' context does not include trailing empty lines' '
	test "$(last_context_line <changed_hello.diff)" != " "
'

check_diff changed_includes 'changed includes'

test_expect_success ' context includes begin' '
	grep "^ .*Begin.h" changed_includes.diff
'

test_expect_success ' context includes end' '
	grep "^ .*End.h" changed_includes.diff
'

test_expect_success ' context does not include other functions' '
	test $(grep -c "^[ +-].*Begin" changed_includes.diff) -le 1
'

test_expect_success ' context does not include trailing empty lines' '
	test "$(last_context_line <changed_includes.diff)" != " "
'

check_diff appended 'appended function'

test_expect_success ' context includes begin' '
	grep "^[+].*Begin of first part" appended.diff
'

test_expect_success ' context includes end' '
	grep "^[+].*End of first part" appended.diff
'

test_expect_success ' context does not include other functions' '
	test $(grep -c "^[ +-].*Begin" appended.diff) -le 1
'

check_diff extended 'appended function part'

test_expect_success ' context includes begin' '
	grep "^ .*Begin of first part" extended.diff
'

test_expect_success ' context includes end' '
	grep "^[+].*End of second part" extended.diff
'

test_expect_success ' context does not include other functions' '
	test $(grep -c "^[ +-].*Begin" extended.diff) -le 2
'

test_expect_success ' context does not include preceding empty lines' '
	test "$(first_context_line <extended.diff)" != " "
'

check_diff long_common_tail 'change with long common tail and no context' -U0

test_expect_success ' context includes begin' '
	grep "^ .*Begin of first part" long_common_tail.diff
'

test_expect_failure ' context includes end' '
	grep "^ .*End of second part" long_common_tail.diff
'

test_expect_success ' context does not include other functions' '
	test $(grep -c "^[ +-].*Begin" long_common_tail.diff) -le 2
'

test_expect_success ' context does not include preceding empty lines' '
	test "$(first_context_line <long_common_tail.diff.diff)" != " "
'

test_done
