#!/bin/sh

test_description='git rebase --whitespace=fix

This test runs git rebase --whitespace=fix and make sure that it works.
'

. ./test-lib.sh

# prepare initial revision of "file" with a blank line at the end
cat >file <<EOF
a
b
c

EOF

# expected contents in "file" after rebase
cat >expect-first <<EOF
a
b
c
EOF

# prepare second revision of "file"
cat >second <<EOF
a
b
c

d
e
f




EOF

# expected contents in second revision after rebase
cat >expect-second <<EOF
a
b
c

d
e
f
EOF

test_expect_success 'blank line at end of file; extend at end of file' '
	git commit --allow-empty -m "Initial empty commit" &&
	git add file && git commit -m first &&
	mv second file &&
	git add file && git commit -m second &&
	git rebase --whitespace=fix HEAD^^ &&
	git diff --exit-code HEAD^:file expect-first &&
	test_cmp expect-second file
'

# prepare third revision of "file"
sed -e's/Z//' >third <<EOF
a
b
c

d
e
f
    Z
 Z
h
i
j
k
l
EOF

sed -e's/ //g' <third >expect-third

test_expect_success 'two blanks line at end of file; extend at end of file' '
	cp third file && git add file && git commit -m third &&
	git rebase --whitespace=fix HEAD^^ &&
	git diff --exit-code HEAD^:file expect-second &&
	test_cmp expect-third file
'

test_expect_success 'same, but do not remove trailing spaces' '
	git config core.whitespace "-blank-at-eol" &&
	git reset --hard HEAD^ &&
	cp third file && git add file && git commit -m third &&
	git rebase --whitespace=fix HEAD^^ &&
	git diff --exit-code HEAD^:file expect-second &&
	test_cmp file third
'

sed -e's/Z//' >beginning <<EOF
a
		    Z
       Z
EOF

cat >expect-beginning <<EOF
a


1
2
3
4
5
EOF

test_expect_success 'at beginning of file' '
	git config core.whitespace "blank-at-eol" &&
	cp beginning file &&
	git commit -m beginning file &&
	test_write_lines 1 2 3 4 5 >>file &&
	git commit -m more file &&
	git rebase --whitespace=fix HEAD^^ &&
	test_cmp expect-beginning file
'

test_done
