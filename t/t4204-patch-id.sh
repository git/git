#!/bin/sh

test_description='git patch-id'

. ./test-lib.sh

test_expect_success 'setup' '
	as="a a a a a a a a" && # eight a
	test_write_lines $as >foo &&
	test_write_lines $as >bar &&
	git add foo bar &&
	git commit -a -m initial &&
	test_write_lines $as b >foo &&
	test_write_lines $as b >bar &&
	git commit -a -m first &&
	git checkout -b same master &&
	git commit --amend -m same-msg &&
	git checkout -b notsame master &&
	echo c >foo &&
	echo c >bar &&
	git commit --amend -a -m notsame-msg &&
	test_write_lines bar foo >bar-then-foo &&
	test_write_lines foo bar >foo-then-bar
'

test_expect_success 'patch-id output is well-formed' '
	git log -p -1 | git patch-id >output &&
	grep "^[a-f0-9]\{40\} $(git rev-parse HEAD)$" output
'

#calculate patch id. Make sure output is not empty.
calc_patch_id () {
	patch_name="$1"
	shift
	git patch-id "$@" |
	sed "s/ .*//" >patch-id_"$patch_name" &&
	test_line_count -gt 0 patch-id_"$patch_name"
}

get_top_diff () {
	git log -p -1 "$@" -O bar-then-foo --
}

get_patch_id () {
	get_top_diff "$1" | calc_patch_id "$@"
}

test_expect_success 'patch-id detects equality' '
	get_patch_id master &&
	get_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

test_expect_success 'patch-id detects inequality' '
	get_patch_id master &&
	get_patch_id notsame &&
	! test_cmp patch-id_master patch-id_notsame
'

test_expect_success 'patch-id supports git-format-patch output' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --stdout | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same &&
	set $(git format-patch -1 --stdout | git patch-id) &&
	test "$2" = $(git rev-parse HEAD)
'

test_expect_success 'whitespace is irrelevant in footer' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --stdout | sed "s/ \$//" | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

cmp_patch_id () {
	if
		test "$1" = "relevant"
	then
		! test_cmp patch-id_"$2" patch-id_"$3"
	else
		test_cmp patch-id_"$2" patch-id_"$3"
	fi
}

test_patch_id_file_order () {
	relevant="$1"
	shift
	name="order-${1}-$relevant"
	shift
	get_top_diff "master" | calc_patch_id "$name" "$@" &&
	git checkout same &&
	git format-patch -1 --stdout -O foo-then-bar |
		calc_patch_id "ordered-$name" "$@" &&
	cmp_patch_id $relevant "$name" "ordered-$name"

}

# combined test for options: add more tests here to make them
# run with all options
test_patch_id () {
	test_patch_id_file_order "$@"
}

# small tests with detailed diagnostic for basic options.
test_expect_success 'file order is irrelevant with --stable' '
	test_patch_id_file_order irrelevant --stable --stable
'

test_expect_success 'file order is relevant with --unstable' '
	test_patch_id_file_order relevant --unstable --unstable
'

#Now test various option combinations.
test_expect_success 'default is unstable' '
	test_patch_id relevant default
'

test_expect_success 'patchid.stable = true is stable' '
	test_config patchid.stable true &&
	test_patch_id irrelevant patchid.stable=true
'

test_expect_success 'patchid.stable = false is unstable' '
	test_config patchid.stable false &&
	test_patch_id relevant patchid.stable=false
'

test_expect_success '--unstable overrides patchid.stable = true' '
	test_config patchid.stable true &&
	test_patch_id relevant patchid.stable=true--unstable --unstable
'

test_expect_success '--stable overrides patchid.stable = false' '
	test_config patchid.stable false &&
	test_patch_id irrelevant patchid.stable=false--stable --stable
'

test_expect_success 'patch-id supports git-format-patch MIME output' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --attach --stdout | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

test_expect_success 'patch-id respects config from subdir' '
	test_config patchid.stable true &&
	mkdir subdir &&

	# copy these because test_patch_id() looks for them in
	# the current directory
	cp bar-then-foo foo-then-bar subdir &&

	(
		cd subdir &&
		test_patch_id irrelevant patchid.stable=true
	)
'

cat >nonl <<\EOF
diff --git i/a w/a
index e69de29..2e65efe 100644
--- i/a
+++ w/a
@@ -0,0 +1 @@
+a
\ No newline at end of file
diff --git i/b w/b
index e69de29..6178079 100644
--- i/b
+++ w/b
@@ -0,0 +1 @@
+b
EOF

cat >withnl <<\EOF
diff --git i/a w/a
index e69de29..7898192 100644
--- i/a
+++ w/a
@@ -0,0 +1 @@
+a
diff --git i/b w/b
index e69de29..6178079 100644
--- i/b
+++ w/b
@@ -0,0 +1 @@
+b
EOF

test_expect_success 'patch-id handles no-nl-at-eof markers' '
	cat nonl | calc_patch_id nonl &&
	cat withnl | calc_patch_id withnl &&
	test_cmp patch-id_nonl patch-id_withnl
'
test_done
