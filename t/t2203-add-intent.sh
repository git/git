#!/bin/sh

test_description='Intent to add'

. ./test-lib.sh

test_expect_success 'intent to add' '
	test_cummit 1 &&
	but rm 1.t &&
	echo hello >1.t &&
	echo hello >file &&
	echo hello >elif &&
	but add -N file &&
	but add elif &&
	but add -N 1.t
'

test_expect_success 'but status' '
	but status --porcelain | grep -v actual >actual &&
	cat >expect <<-\EOF &&
	DA 1.t
	A  elif
	 A file
	EOF
	test_cmp expect actual
'

test_expect_success 'but status with porcelain v2' '
	but status --porcelain=v2 | grep -v "^?" >actual &&
	nam1=$(echo 1 | but hash-object --stdin) &&
	nam2=$(but hash-object elif) &&
	cat >expect <<-EOF &&
	1 DA N... 100644 000000 100644 $nam1 $ZERO_OID 1.t
	1 A. N... 000000 100644 100644 $ZERO_OID $nam2 elif
	1 .A N... 000000 000000 100644 $ZERO_OID $ZERO_OID file
	EOF
	test_cmp expect actual
'

test_expect_success 'check result of "add -N"' '
	but ls-files -s file >actual &&
	empty=$(but hash-object --stdin </dev/null) &&
	echo "100644 $empty 0	file" >expect &&
	test_cmp expect actual
'

test_expect_success 'intent to add is just an ordinary empty blob' '
	but add -u &&
	but ls-files -s file >actual &&
	but ls-files -s elif | sed -e "s/elif/file/" >expect &&
	test_cmp expect actual
'

test_expect_success 'intent to add does not clobber existing paths' '
	but add -N file elif &&
	empty=$(but hash-object --stdin </dev/null) &&
	but ls-files -s >actual &&
	! grep "$empty" actual
'

test_expect_success 'i-t-a entry is simply ignored' '
	test_tick &&
	but cummit -a -m initial &&
	but reset --hard &&

	echo xyzzy >rezrov &&
	echo frotz >nitfol &&
	but add rezrov &&
	but add -N nitfol &&
	but cummit -m second &&
	test $(but ls-tree HEAD -- nitfol | wc -l) = 0 &&
	test $(but diff --name-only HEAD -- nitfol | wc -l) = 1 &&
	test $(but diff --name-only -- nitfol | wc -l) = 1
'

test_expect_success 'can cummit with an unrelated i-t-a entry in index' '
	but reset --hard &&
	echo bozbar >rezrov &&
	echo frotz >nitfol &&
	but add rezrov &&
	but add -N nitfol &&
	but cummit -m partial rezrov
'

test_expect_success 'can "cummit -a" with an i-t-a entry' '
	but reset --hard &&
	: >nitfol &&
	but add -N nitfol &&
	but cummit -a -m all
'

test_expect_success 'cache-tree invalidates i-t-a paths' '
	but reset --hard &&
	mkdir dir &&
	: >dir/foo &&
	but add dir/foo &&
	but cummit -m foo &&

	: >dir/bar &&
	but add -N dir/bar &&
	but diff --name-only >actual &&
	echo dir/bar >expect &&
	test_cmp expect actual &&

	but write-tree >/dev/null &&

	but diff --name-only >actual &&
	echo dir/bar >expect &&
	test_cmp expect actual
'

test_expect_success 'cache-tree does not ignore dir that has i-t-a entries' '
	but init ita-in-dir &&
	(
		cd ita-in-dir &&
		mkdir 2 &&
		for f in 1 2/1 2/2 3
		do
			echo "$f" >"$f" || return 1
		done &&
		but add 1 2/2 3 &&
		but add -N 2/1 &&
		but cummit -m cummitted &&
		but ls-tree -r HEAD >actual &&
		grep 2/2 actual
	)
'

test_expect_success 'cache-tree does skip dir that becomes empty' '
	rm -fr ita-in-dir &&
	but init ita-in-dir &&
	(
		cd ita-in-dir &&
		mkdir -p 1/2/3 &&
		echo 4 >1/2/3/4 &&
		but add -N 1/2/3/4 &&
		but write-tree >actual &&
		echo $EMPTY_TREE >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'cummit: ita entries ignored in empty initial cummit check' '
	but init empty-initial-cummit &&
	(
		cd empty-initial-cummit &&
		: >one &&
		but add -N one &&
		test_must_fail but cummit -m nothing-new-here
	)
'

test_expect_success 'cummit: ita entries ignored in empty cummit check' '
	but init empty-subsequent-cummit &&
	(
		cd empty-subsequent-cummit &&
		test_cummit one &&
		: >two &&
		but add -N two &&
		test_must_fail but cummit -m nothing-new-here
	)
'

test_expect_success 'rename detection finds the right names' '
	but init rename-detection &&
	(
		cd rename-detection &&
		echo contents >first &&
		but add first &&
		but cummit -m first &&
		mv first third &&
		but add -N third &&

		but status | grep -v "^?" >actual.1 &&
		test_i18ngrep "renamed: *first -> third" actual.1 &&

		but status --porcelain | grep -v "^?" >actual.2 &&
		cat >expected.2 <<-\EOF &&
		 R first -> third
		EOF
		test_cmp expected.2 actual.2 &&

		hash=$(but hash-object third) &&
		but status --porcelain=v2 | grep -v "^?" >actual.3 &&
		cat >expected.3 <<-EOF &&
		2 .R N... 100644 100644 100644 $hash $hash R100 third	first
		EOF
		test_cmp expected.3 actual.3 &&

		but diff --stat >actual.4 &&
		cat >expected.4 <<-EOF &&
		 first => third | 0
		 1 file changed, 0 insertions(+), 0 deletions(-)
		EOF
		test_cmp expected.4 actual.4 &&

		but diff --cached --stat >actual.5 &&
		test_must_be_empty actual.5

	)
'

test_expect_success 'double rename detection in status' '
	but init rename-detection-2 &&
	(
		cd rename-detection-2 &&
		echo contents >first &&
		but add first &&
		but cummit -m first &&
		but mv first second &&
		mv second third &&
		but add -N third &&

		but status | grep -v "^?" >actual.1 &&
		test_i18ngrep "renamed: *first -> second" actual.1 &&
		test_i18ngrep "renamed: *second -> third" actual.1 &&

		but status --porcelain | grep -v "^?" >actual.2 &&
		cat >expected.2 <<-\EOF &&
		R  first -> second
		 R second -> third
		EOF
		test_cmp expected.2 actual.2 &&

		hash=$(but hash-object third) &&
		but status --porcelain=v2 | grep -v "^?" >actual.3 &&
		cat >expected.3 <<-EOF &&
		2 R. N... 100644 100644 100644 $hash $hash R100 second	first
		2 .R N... 100644 100644 100644 $hash $hash R100 third	second
		EOF
		test_cmp expected.3 actual.3
	)
'

test_expect_success 'i-t-a files shown as new for "diff", "diff-files"; not-new for "diff --cached"' '
	but reset --hard &&
	: >empty &&
	content="foo" &&
	echo "$content" >not-empty &&

	hash_e=$(but hash-object empty) &&
	hash_n=$(but hash-object not-empty) &&

	cat >expect.diff_p <<-EOF &&
	diff --but a/empty b/empty
	new file mode 100644
	index 0000000..$(but rev-parse --short $hash_e)
	diff --but a/not-empty b/not-empty
	new file mode 100644
	index 0000000..$(but rev-parse --short $hash_n)
	--- /dev/null
	+++ b/not-empty
	@@ -0,0 +1 @@
	+$content
	EOF
	cat >expect.diff_s <<-EOF &&
	 create mode 100644 empty
	 create mode 100644 not-empty
	EOF
	cat >expect.diff_a <<-EOF &&
	:000000 100644 0000000 0000000 A$(printf "\t")empty
	:000000 100644 0000000 0000000 A$(printf "\t")not-empty
	EOF

	but add -N empty not-empty &&

	but diff >actual &&
	test_cmp expect.diff_p actual &&

	but diff --summary >actual &&
	test_cmp expect.diff_s actual &&

	but diff-files -p >actual &&
	test_cmp expect.diff_p actual &&

	but diff-files --abbrev >actual &&
	test_cmp expect.diff_a actual &&

	but diff --cached >actual &&
	test_must_be_empty actual
'

test_expect_success '"diff HEAD" includes ita as new files' '
	but reset --hard &&
	echo new >new-ita &&
	oid=$(but hash-object new-ita) &&
	oid=$(but rev-parse --short $oid) &&
	but add -N new-ita &&
	but diff HEAD >actual &&
	cat >expected <<-EOF &&
	diff --but a/new-ita b/new-ita
	new file mode 100644
	index 0000000..$oid
	--- /dev/null
	+++ b/new-ita
	@@ -0,0 +1 @@
	+new
	EOF
	test_cmp expected actual
'

test_expect_success 'apply --intent-to-add' '
	but reset --hard &&
	echo new >new-ita &&
	but add -N new-ita &&
	but diff >expected &&
	grep "new file" expected &&
	but reset --hard &&
	but apply --intent-to-add expected &&
	but diff >actual &&
	test_cmp expected actual
'

test_done
