#!/bin/sh

test_description='git am with corrupt input'
. ./test-lib.sh

make_mbox_with_nul () {
	space=' '
	q_nul_in_subject=
	q_nul_in_body=
	while test $# -ne 0
	do
		case "$1" in
		subject) q_nul_in_subject='=00' ;;
		body)    q_nul_in_body='=00' ;;
		esac &&
		shift
	done &&
	cat <<-EOF
	From ec7364544f690c560304f5a5de9428ea3b978b26 Mon Sep 17 00:00:00 2001
	From: A U Thor <author@example.com>
	Date: Sun, 19 Apr 2020 13:42:07 +0700
	Subject: [PATCH] =?ISO-8859-1?q?=C4=CB${q_nul_in_subject}=D1=CF=D6?=
	MIME-Version: 1.0
	Content-Type: text/plain; charset=ISO-8859-1
	Content-Transfer-Encoding: quoted-printable

	abc${q_nul_in_body}def
	---
	diff --git a/afile b/afile
	new file mode 100644
	index 0000000000..e69de29bb2
	--$space
	2.26.1
	EOF
}

test_expect_success setup '
	# Note the missing "+++" line:
	cat >bad-patch.diff <<-\EOF &&
	From: A U Thor <au.thor@example.com>
	diff --git a/f b/f
	index 7898192..6178079 100644
	--- a/f
	@@ -1 +1 @@
	-a
	+b
	EOF

	echo a >f &&
	git add f &&
	test_tick &&
	git commit -m initial
'

# This used to fail before, too, but with a different diagnostic.
#   fatal: unable to write file '(null)' mode 100644: Bad address
# Also, it had the unwanted side-effect of deleting f.
test_expect_success 'try to apply corrupted patch' '
	test_when_finished "git am --abort" &&
	test_must_fail git -c advice.amWorkDir=false am bad-patch.diff 2>actual &&
	echo "error: git diff header lacks filename information (line 4)" >expected &&
	test_path_is_file f &&
	test_cmp expected actual
'

test_expect_success "NUL in commit message's body" '
	test_when_finished "git am --abort" &&
	make_mbox_with_nul body >body.patch &&
	test_must_fail git am body.patch 2>err &&
	grep "a NUL byte in commit log message not allowed" err
'

test_expect_success "NUL in commit message's header" "
	test_when_finished 'git am --abort' &&
	make_mbox_with_nul subject >subject.patch &&
	test_must_fail git mailinfo msg patch <subject.patch 2>err &&
	grep \"a NUL byte in 'Subject' is not allowed\" err &&
	test_must_fail git am subject.patch 2>err &&
	grep \"a NUL byte in 'Subject' is not allowed\" err
"

test_done
