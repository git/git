#!/bin/sh

test_description='RCS merge replacement: merge-file'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >orig.txt <<-\EOF &&
	Dominus regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	EOF

	cat >new1.txt <<-\EOF &&
	Dominus regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	cat >new2.txt <<-\EOF &&
	Dominus regit me, et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	EOF

	cat >new3.txt <<-\EOF &&
	DOMINUS regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	EOF

	cat >new4.txt <<-\EOF &&
	Dominus regit me, et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	EOF

	printf "propter nomen suum." >>new4.txt &&

	cat >base.c <<-\EOF &&
	int f(int x, int y)
	{
		if (x == 0)
		{
			return y;
		}
		return x;
	}

	int g(size_t u)
	{
		while (u < 30)
		{
			u++;
		}
		return u;
	}
	EOF

	cat >ours.c <<-\EOF &&
	int g(size_t u)
	{
		while (u < 30)
		{
			u++;
		}
		return u;
	}

	int h(int x, int y, int z)
	{
		if (z == 0)
		{
			return x;
		}
		return y;
	}
	EOF

	cat >theirs.c <<-\EOF
	int f(int x, int y)
	{
		if (x == 0)
		{
			return y;
		}
		return x;
	}

	int g(size_t u)
	{
		while (u > 34)
		{
			u--;
		}
		return u;
	}
	EOF
'

test_expect_success 'merge with no changes' '
	cp orig.txt test.txt &&
	git merge-file test.txt orig.txt orig.txt &&
	test_cmp test.txt orig.txt
'

test_expect_success 'merge with no changes with --object-id' '
	git add orig.txt &&
	git merge-file -p --object-id :orig.txt :orig.txt :orig.txt >actual &&
	test_cmp actual orig.txt
'

test_expect_success "merge without conflict" '
	cp new1.txt test.txt &&
	git merge-file test.txt orig.txt new2.txt
'

test_expect_success 'merge without conflict with --object-id' '
	git add orig.txt new2.txt &&
	git merge-file --object-id :orig.txt :orig.txt :new2.txt >actual &&
	git rev-parse :new2.txt >expected &&
	test_cmp actual expected
'

test_expect_success 'can accept object ID with --object-id' '
	git merge-file --object-id $(test_oid empty_blob) $(test_oid empty_blob) :new2.txt >actual &&
	git rev-parse :new2.txt >expected &&
	test_cmp actual expected
'

test_expect_success 'works in subdirectory' '
	mkdir dir &&
	cp new1.txt dir/a.txt &&
	cp orig.txt dir/o.txt &&
	cp new2.txt dir/b.txt &&
	( cd dir && git merge-file a.txt o.txt b.txt ) &&
	test_path_is_missing a.txt
'

test_expect_success "merge without conflict (--quiet)" '
	cp new1.txt test.txt &&
	git merge-file --quiet test.txt orig.txt new2.txt
'

test_expect_failure "merge without conflict (missing LF at EOF)" '
	cp new1.txt test2.txt &&
	git merge-file test2.txt orig.txt new4.txt
'

test_expect_failure "merge result added missing LF" '
	test_cmp test.txt test2.txt
'

test_expect_success "merge without conflict (missing LF at EOF, away from change in the other file)" '
	cp new4.txt test3.txt &&
	git merge-file --quiet test3.txt new2.txt new3.txt
'

test_expect_success "merge does not add LF away of change" '
	cat >expect.txt <<-\EOF &&
	DOMINUS regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	EOF
	printf "propter nomen suum." >>expect.txt &&

	test_cmp expect.txt test3.txt
'

test_expect_success "merge with conflicts" '
	cp test.txt backup.txt &&
	test_must_fail git merge-file test.txt orig.txt new3.txt
'

test_expect_success "expected conflict markers" '
	cat >expect.txt <<-\EOF &&
	<<<<<<< test.txt
	Dominus regit me, et nihil mihi deerit.
	=======
	DOMINUS regit me,
	et nihil mihi deerit.
	>>>>>>> new3.txt
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	test_cmp expect.txt test.txt
'

test_expect_success "merge with conflicts with --object-id" '
	git add backup.txt orig.txt new3.txt &&
	test_must_fail git merge-file -p --object-id :backup.txt :orig.txt :new3.txt >actual &&
	sed -e "s/<< test.txt/<< :backup.txt/" \
	    -e "s/>> new3.txt/>> :new3.txt/" \
	    expect.txt >expect &&
	test_cmp expect actual &&
	test_must_fail git merge-file --object-id :backup.txt :orig.txt :new3.txt >oid &&
	git cat-file blob "$(cat oid)" >actual &&
	test_cmp expect actual
'

test_expect_success "merge with conflicts with --object-id with labels" '
	git add backup.txt orig.txt new3.txt &&
	test_must_fail git merge-file -p --object-id \
		-L test.txt -L orig.txt -L new3.txt \
		:backup.txt :orig.txt :new3.txt >actual &&
	test_cmp expect.txt actual &&
	test_must_fail git merge-file --object-id \
		-L test.txt -L orig.txt -L new3.txt \
		:backup.txt :orig.txt :new3.txt >oid &&
	git cat-file blob "$(cat oid)" >actual &&
	test_cmp expect.txt actual
'

test_expect_success "merge conflicting with --ours" '
	cp backup.txt test.txt &&

	cat >expect.txt <<-\EOF &&
	Dominus regit me, et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	git merge-file --ours test.txt orig.txt new3.txt &&
	test_cmp expect.txt test.txt
'

test_expect_success "merge conflicting with --theirs" '
	cp backup.txt test.txt &&

	cat >expect.txt <<-\EOF &&
	DOMINUS regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	git merge-file --theirs test.txt orig.txt new3.txt &&
	test_cmp expect.txt test.txt
'

test_expect_success "merge conflicting with --union" '
	cp backup.txt test.txt &&

	cat >expect.txt <<-\EOF &&
	Dominus regit me, et nihil mihi deerit.
	DOMINUS regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	git merge-file --union test.txt orig.txt new3.txt &&
	test_cmp expect.txt test.txt
'

test_expect_success "merge with conflicts, using -L" '
	cp backup.txt test.txt &&

	test_must_fail git merge-file -L 1 -L 2 test.txt orig.txt new3.txt
'

test_expect_success "expected conflict markers, with -L" '
	cat >expect.txt <<-\EOF &&
	<<<<<<< 1
	Dominus regit me, et nihil mihi deerit.
	=======
	DOMINUS regit me,
	et nihil mihi deerit.
	>>>>>>> new3.txt
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam tu mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	test_cmp expect.txt test.txt
'

test_expect_success "conflict in removed tail" '
	sed "s/ tu / TU /" <new1.txt >new5.txt &&
	test_must_fail git merge-file -p orig.txt new1.txt new5.txt >out
'

test_expect_success "expected conflict markers" '
	cat >expect <<-\EOF &&
	Dominus regit me,
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	<<<<<<< orig.txt
	=======
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam TU mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	>>>>>>> new5.txt
	EOF

	test_cmp expect out
'

test_expect_success 'binary files cannot be merged' '
	test_must_fail git merge-file -p \
		orig.txt "$TEST_DIRECTORY"/test-binary-1.png new1.txt 2> merge.err &&
	grep "Cannot merge binary files" merge.err
'

test_expect_success 'binary files cannot be merged with --object-id' '
	cp "$TEST_DIRECTORY"/test-binary-1.png . &&
	git add orig.txt new1.txt test-binary-1.png &&
	test_must_fail git merge-file --object-id \
		:orig.txt :test-binary-1.png :new1.txt 2> merge.err &&
	grep "Cannot merge binary files" merge.err
'

test_expect_success 'MERGE_ZEALOUS simplifies non-conflicts' '
	sed -e "s/deerit.\$/deerit;/" -e "s/me;\$/me./" <new5.txt >new6.txt &&
	sed -e "s/deerit.\$/deerit,/" -e "s/me;\$/me,/" <new5.txt >new7.txt &&

	test_must_fail git merge-file -p new6.txt new5.txt new7.txt > output &&
	test 1 = $(grep ======= <output | wc -l)
'

test_expect_success 'ZEALOUS_ALNUM' '
	sed -e "s/deerit./&%%%%/" -e "s/locavit,/locavit;/" <new6.txt | tr % "\012" >new8.txt &&
	sed -e "s/deerit./&%%%%/" -e "s/locavit,/locavit --/" <new7.txt | tr % "\012" >new9.txt &&

	test_must_fail git merge-file -p \
		new8.txt new5.txt new9.txt >merge.out &&
	test 1 = $(grep ======= <merge.out | wc -l)
'

test_expect_success '"diff3 -m" style output (1)' '
	cat >expect <<-\EOF &&
	Dominus regit me,
	<<<<<<< new8.txt
	et nihil mihi deerit;




	In loco pascuae ibi me collocavit;
	super aquam refectionis educavit me.
	||||||| new5.txt
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	=======
	et nihil mihi deerit,




	In loco pascuae ibi me collocavit --
	super aquam refectionis educavit me,
	>>>>>>> new9.txt
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam TU mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	test_must_fail git merge-file -p --diff3 \
		new8.txt new5.txt new9.txt >actual &&
	test_cmp expect actual
'

test_expect_success '"diff3 -m" style output (2)' '
	git config merge.conflictstyle diff3 &&
	test_must_fail git merge-file -p \
		new8.txt new5.txt new9.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'marker size' '
	cat >expect <<-\EOF &&
	Dominus regit me,
	<<<<<<<<<< new8.txt
	et nihil mihi deerit;




	In loco pascuae ibi me collocavit;
	super aquam refectionis educavit me.
	|||||||||| new5.txt
	et nihil mihi deerit.
	In loco pascuae ibi me collocavit,
	super aquam refectionis educavit me;
	==========
	et nihil mihi deerit,




	In loco pascuae ibi me collocavit --
	super aquam refectionis educavit me,
	>>>>>>>>>> new9.txt
	animam meam convertit,
	deduxit me super semitas jusitiae,
	propter nomen suum.
	Nam et si ambulavero in medio umbrae mortis,
	non timebo mala, quoniam TU mecum es:
	virga tua et baculus tuus ipsa me consolata sunt.
	EOF

	test_must_fail git merge-file -p --marker-size=10 \
		new8.txt new5.txt new9.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'conflict at EOF without LF resolved by --ours' '
	printf "line1\nline2\nline3" >nolf-orig.txt &&
	printf "line1\nline2\nline3x" >nolf-diff1.txt &&
	printf "line1\nline2\nline3y" >nolf-diff2.txt &&

	git merge-file -p --ours nolf-diff1.txt nolf-orig.txt nolf-diff2.txt >output.txt &&
	printf "line1\nline2\nline3x" >expect.txt &&
	test_cmp expect.txt output.txt
'

test_expect_success 'conflict at EOF without LF resolved by --theirs' '
	git merge-file -p --theirs nolf-diff1.txt nolf-orig.txt nolf-diff2.txt >output.txt &&
	printf "line1\nline2\nline3y" >expect.txt &&
	test_cmp expect.txt output.txt
'

test_expect_success 'conflict at EOF without LF resolved by --union' '
	git merge-file -p --union nolf-diff1.txt nolf-orig.txt nolf-diff2.txt >output.txt &&
	printf "line1\nline2\nline3x\nline3y" >expect.txt &&
	test_cmp expect.txt output.txt
'

test_expect_success 'conflict sections match existing line endings' '
	printf "1\\r\\n2\\r\\n3" >crlf-orig.txt &&
	printf "1\\r\\n2\\r\\n4" >crlf-diff1.txt &&
	printf "1\\r\\n2\\r\\n5" >crlf-diff2.txt &&
	test_must_fail git -c core.eol=crlf merge-file -p \
		crlf-diff1.txt crlf-orig.txt crlf-diff2.txt >crlf.txt &&
	test $(tr "\015" Q <crlf.txt | grep "^[<=>].*Q$" | wc -l) = 3 &&
	test $(tr "\015" Q <crlf.txt | grep "[345]Q$" | wc -l) = 3 &&
	test_must_fail git -c core.eol=crlf merge-file -p \
		nolf-diff1.txt nolf-orig.txt nolf-diff2.txt >nolf.txt &&
	test $(tr "\015" Q <nolf.txt | grep "^[<=>].*Q$" | wc -l) = 0
'

test_expect_success '--object-id fails without repository' '
	empty="$(test_oid empty_blob)" &&
	nongit test_must_fail git merge-file --object-id $empty $empty $empty 2>err &&
	grep "not a git repository" err
'

test_expect_success 'merging C files with "myers" diff algorithm creates some spurious conflicts' '
	cat >expect.c <<-\EOF &&
	int g(size_t u)
	{
		while (u < 30)
		{
			u++;
		}
		return u;
	}

	int h(int x, int y, int z)
	{
	<<<<<<< ours.c
		if (z == 0)
	||||||| base.c
		while (u < 30)
	=======
		while (u > 34)
	>>>>>>> theirs.c
		{
	<<<<<<< ours.c
			return x;
	||||||| base.c
			u++;
	=======
			u--;
	>>>>>>> theirs.c
		}
		return y;
	}
	EOF

	test_must_fail git merge-file -p --diff3 --diff-algorithm myers ours.c base.c theirs.c >myers_output.c &&
	test_cmp expect.c myers_output.c
'

test_expect_success 'merging C files with "histogram" diff algorithm avoids some spurious conflicts' '
	cat >expect.c <<-\EOF &&
	int g(size_t u)
	{
		while (u > 34)
		{
			u--;
		}
		return u;
	}

	int h(int x, int y, int z)
	{
		if (z == 0)
		{
			return x;
		}
		return y;
	}
	EOF

	git merge-file -p --diff3 --diff-algorithm histogram ours.c base.c theirs.c >histogram_output.c &&
	test_cmp expect.c histogram_output.c
'

test_done
