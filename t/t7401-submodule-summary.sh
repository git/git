#!/bin/sh
#
# Copyright (c) 2008 Ping Yin
#

test_description='Summary support for submodules

This test tries to verify the sanity of summary subcommand of git-submodule.
'

. ./test-lib.sh

add_file () {
	sm=$1
	shift
	owd=$(pwd)
	cd "$sm"
	for name; do
		echo "$name" > "$name" &&
		git add "$name" &&
		test_tick &&
		git commit -m "Add $name"
	done >/dev/null
	git rev-parse --verify HEAD | cut -c1-7
	cd "$owd"
}
commit_file () {
	test_tick &&
	git commit "$@" -m "Commit $*" >/dev/null
}

test_create_repo sm1 &&
add_file . foo >/dev/null

head1=$(add_file sm1 foo1 foo2)

test_expect_success 'added submodule' "
	git add sm1 &&
	git submodule summary >actual &&
	diff actual - <<-EOF
* sm1 0000000...$head1 (2):
  > Add foo2

EOF
"

commit_file sm1 &&
head2=$(add_file sm1 foo3)

test_expect_success 'modified submodule(forward)' "
	git submodule summary >actual &&
	diff actual - <<-EOF
* sm1 $head1...$head2 (1):
  > Add foo3

EOF
"

commit_file sm1 &&
cd sm1 &&
git reset --hard HEAD~2 >/dev/null &&
head3=$(git rev-parse --verify HEAD | cut -c1-7) &&
cd ..

test_expect_success 'modified submodule(backward)' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head2...$head3 (2):
  < Add foo3
  < Add foo2

EOF
"

head4=$(add_file sm1 foo4 foo5) &&
head4_full=$(GIT_DIR=sm1/.git git rev-parse --verify HEAD)
test_expect_success 'modified submodule(backward and forward)' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head2...$head4 (4):
  > Add foo5
  > Add foo4
  < Add foo3
  < Add foo2

EOF
"

test_expect_success '--summary-limit' "
    git submodule summary -n 3 >actual &&
    diff actual - <<-EOF
* sm1 $head2...$head4 (4):
  > Add foo5
  > Add foo4
  < Add foo3

EOF
"

commit_file sm1 &&
mv sm1 sm1-bak &&
echo sm1 >sm1 &&
head5=$(git hash-object sm1 | cut -c1-7) &&
git add sm1 &&
rm -f sm1 &&
mv sm1-bak sm1

test_expect_success 'typechanged submodule(submodule->blob), --cached' "
    git submodule summary --cached >actual &&
    diff actual - <<-EOF
* sm1 $head4(submodule)->$head5(blob) (3):
  < Add foo5

EOF
"

rm -rf sm1 &&
git checkout-index sm1
test_expect_success 'typechanged submodule(submodule->blob)' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head4(submodule)->$head5(blob):

EOF
"

rm -f sm1 &&
test_create_repo sm1 &&
head6=$(add_file sm1 foo6 foo7)
test_expect_success 'nonexistent commit' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head4...$head6:
  Warn: sm1 doesn't contain commit $head4_full

EOF
"

commit_file
test_expect_success 'typechanged submodule(blob->submodule)' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head5(blob)->$head6(submodule) (2):
  > Add foo7

EOF
"

commit_file sm1 &&
rm -rf sm1
test_expect_success 'deleted submodule' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head6...0000000:

EOF
"

test_create_repo sm2 &&
head7=$(add_file sm2 foo8 foo9) &&
git add sm2

test_expect_success 'multiple submodules' "
    git submodule summary >actual &&
    diff actual - <<-EOF
* sm1 $head6...0000000:

* sm2 0000000...$head7 (2):
  > Add foo9

EOF
"

test_expect_success 'path filter' "
    git submodule summary sm2 >actual &&
    diff actual - <<-EOF
* sm2 0000000...$head7 (2):
  > Add foo9

EOF
"

commit_file sm2
test_expect_success 'given commit' "
    git submodule summary HEAD^ >actual &&
    diff actual - <<-EOF
* sm1 $head6...0000000:

* sm2 0000000...$head7 (2):
  > Add foo9

EOF
"

test_expect_success '--for-status' "
    git submodule summary --for-status HEAD^ >actual &&
    test_cmp actual - <<EOF
# Modified submodules:
#
# * sm1 $head6...0000000:
#
# * sm2 0000000...$head7 (2):
#   > Add foo9
#
EOF
"

test_done
