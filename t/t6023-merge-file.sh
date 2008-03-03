#!/bin/sh

test_description='RCS merge replacement: merge-file'
. ./test-lib.sh

cat > orig.txt << EOF
Dominus regit me,
et nihil mihi deerit.
In loco pascuae ibi me collocavit,
super aquam refectionis educavit me;
animam meam convertit,
deduxit me super semitas jusitiae,
propter nomen suum.
EOF

cat > new1.txt << EOF
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

cat > new2.txt << EOF
Dominus regit me, et nihil mihi deerit.
In loco pascuae ibi me collocavit,
super aquam refectionis educavit me;
animam meam convertit,
deduxit me super semitas jusitiae,
propter nomen suum.
EOF

cat > new3.txt << EOF
DOMINUS regit me,
et nihil mihi deerit.
In loco pascuae ibi me collocavit,
super aquam refectionis educavit me;
animam meam convertit,
deduxit me super semitas jusitiae,
propter nomen suum.
EOF

cat > new4.txt << EOF
Dominus regit me, et nihil mihi deerit.
In loco pascuae ibi me collocavit,
super aquam refectionis educavit me;
animam meam convertit,
deduxit me super semitas jusitiae,
EOF
printf "propter nomen suum." >> new4.txt

cp new1.txt test.txt
test_expect_success "merge without conflict" \
	"git merge-file test.txt orig.txt new2.txt"

cp new1.txt test2.txt
test_expect_success "merge without conflict (missing LF at EOF)" \
	"git merge-file test2.txt orig.txt new2.txt"

test_expect_success "merge result added missing LF" \
	"git diff test.txt test2.txt"

cp test.txt backup.txt
test_expect_success "merge with conflicts" \
	"! git merge-file test.txt orig.txt new3.txt"

cat > expect.txt << EOF
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

test_expect_success "expected conflict markers" "git diff test.txt expect.txt"

cp backup.txt test.txt
test_expect_success "merge with conflicts, using -L" \
	"! git merge-file -L 1 -L 2 test.txt orig.txt new3.txt"

cat > expect.txt << EOF
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

test_expect_success "expected conflict markers, with -L" \
	"git diff test.txt expect.txt"

sed "s/ tu / TU /" < new1.txt > new5.txt
test_expect_success "conflict in removed tail" \
	"! git merge-file -p orig.txt new1.txt new5.txt > out"

cat > expect << EOF
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

test_expect_success "expected conflict markers" "git diff expect out"

test_expect_success 'binary files cannot be merged' '
	! git merge-file -p orig.txt ../test4012.png new1.txt 2> merge.err &&
	grep "Cannot merge binary files" merge.err
'

sed -e "s/deerit.$/deerit;/" -e "s/me;$/me./" < new5.txt > new6.txt
sed -e "s/deerit.$/deerit,/" -e "s/me;$/me,/" < new5.txt > new7.txt

test_expect_success 'MERGE_ZEALOUS simplifies non-conflicts' '

	! git merge-file -p new6.txt new5.txt new7.txt > output &&
	test 1 = $(grep ======= < output | wc -l)

'

sed -e 's/deerit./&\n\n\n\n/' -e "s/locavit,/locavit;/" < new6.txt > new8.txt
sed -e 's/deerit./&\n\n\n\n/' -e "s/locavit,/locavit --/" < new7.txt > new9.txt

test_expect_success 'ZEALOUS_ALNUM' '

	! git merge-file -p new8.txt new5.txt new9.txt > merge.out &&
	test 1 = $(grep ======= < merge.out | wc -l)

'

test_done
