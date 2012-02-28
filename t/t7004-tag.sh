#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git tag

Tests for operations with tags.'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

# creating and listing lightweight tags:

tag_exists () {
	git show-ref --quiet --verify refs/tags/"$1"
}

# todo: git tag -l now returns always zero, when fixed, change this test
test_expect_success 'listing all tags in an empty tree should succeed' '
	git tag -l &&
	git tag
'

test_expect_success 'listing all tags in an empty tree should output nothing' '
	test `git tag -l | wc -l` -eq 0 &&
	test `git tag | wc -l` -eq 0
'

test_expect_success 'looking for a tag in an empty tree should fail' \
	'! (tag_exists mytag)'

test_expect_success 'creating a tag in an empty tree should fail' '
	test_must_fail git tag mynotag &&
	! tag_exists mynotag
'

test_expect_success 'creating a tag for HEAD in an empty tree should fail' '
	test_must_fail git tag mytaghead HEAD &&
	! tag_exists mytaghead
'

test_expect_success 'creating a tag for an unknown revision should fail' '
	test_must_fail git tag mytagnorev aaaaaaaaaaa &&
	! tag_exists mytagnorev
'

# commit used in the tests, test_tick is also called here to freeze the date:
test_expect_success 'creating a tag using default HEAD should succeed' '
	test_tick &&
	echo foo >foo &&
	git add foo &&
	git commit -m Foo &&
	git tag mytag
'

test_expect_success 'listing all tags if one exists should succeed' '
	git tag -l &&
	git tag
'

test_expect_success 'listing all tags if one exists should output that tag' '
	test `git tag -l` = mytag &&
	test `git tag` = mytag
'

# pattern matching:

test_expect_success 'listing a tag using a matching pattern should succeed' \
	'git tag -l mytag'

test_expect_success \
	'listing a tag using a matching pattern should output that tag' \
	'test `git tag -l mytag` = mytag'

# todo: git tag -l now returns always zero, when fixed, change this test
test_expect_success \
	'listing tags using a non-matching pattern should suceed' \
	'git tag -l xxx'

test_expect_success \
	'listing tags using a non-matching pattern should output nothing' \
	'test `git tag -l xxx | wc -l` -eq 0'

# special cases for creating tags:

test_expect_success \
	'trying to create a tag with the name of one existing should fail' \
	'test_must_fail git tag mytag'

test_expect_success \
	'trying to create a tag with a non-valid name should fail' '
	test `git tag -l | wc -l` -eq 1 &&
	test_must_fail git tag "" &&
	test_must_fail git tag .othertag &&
	test_must_fail git tag "other tag" &&
	test_must_fail git tag "othertag^" &&
	test_must_fail git tag "other~tag" &&
	test `git tag -l | wc -l` -eq 1
'

test_expect_success 'creating a tag using HEAD directly should succeed' '
	git tag myhead HEAD &&
	tag_exists myhead
'

# deleting tags:

test_expect_success 'trying to delete an unknown tag should fail' '
	! tag_exists unknown-tag &&
	test_must_fail git tag -d unknown-tag
'

cat >expect <<EOF
myhead
mytag
EOF
test_expect_success \
	'trying to delete tags without params should succeed and do nothing' '
	git tag -l > actual && test_cmp expect actual &&
	git tag -d &&
	git tag -l > actual && test_cmp expect actual
'

test_expect_success \
	'deleting two existing tags in one command should succeed' '
	tag_exists mytag &&
	tag_exists myhead &&
	git tag -d mytag myhead &&
	! tag_exists mytag &&
	! tag_exists myhead
'

test_expect_success \
	'creating a tag with the name of another deleted one should succeed' '
	! tag_exists mytag &&
	git tag mytag &&
	tag_exists mytag
'

test_expect_success \
	'trying to delete two tags, existing and not, should fail in the 2nd' '
	tag_exists mytag &&
	! tag_exists myhead &&
	test_must_fail git tag -d mytag anothertag &&
	! tag_exists mytag &&
	! tag_exists myhead
'

test_expect_success 'trying to delete an already deleted tag should fail' \
	'test_must_fail git tag -d mytag'

# listing various tags with pattern matching:

cat >expect <<EOF
a1
aa1
cba
t210
t211
v0.2.1
v1.0
v1.0.1
v1.1.3
EOF
test_expect_success 'listing all tags should print them ordered' '
	git tag v1.0.1 &&
	git tag t211 &&
	git tag aa1 &&
	git tag v0.2.1 &&
	git tag v1.1.3 &&
	git tag cba &&
	git tag a1 &&
	git tag v1.0 &&
	git tag t210 &&
	git tag -l > actual &&
	test_cmp expect actual &&
	git tag > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
a1
aa1
cba
EOF
test_expect_success \
	'listing tags with substring as pattern must print those matching' '
	rm *a* &&
	git tag -l "*a*" > current &&
	test_cmp expect current
'

cat >expect <<EOF
v0.2.1
v1.0.1
EOF
test_expect_success \
	'listing tags with a suffix as pattern must print those matching' '
	git tag -l "*.1" > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
t210
t211
EOF
test_expect_success \
	'listing tags with a prefix as pattern must print those matching' '
	git tag -l "t21*" > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
a1
EOF
test_expect_success \
	'listing tags using a name as pattern must print that one matching' '
	git tag -l a1 > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
v1.0
EOF
test_expect_success \
	'listing tags using a name as pattern must print that one matching' '
	git tag -l v1.0 > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
v1.0.1
v1.1.3
EOF
test_expect_success \
	'listing tags with ? in the pattern should print those matching' '
	git tag -l "v1.?.?" > actual &&
	test_cmp expect actual
'

>expect
test_expect_success \
	'listing tags using v.* should print nothing because none have v.' '
	git tag -l "v.*" > actual &&
	test_cmp expect actual
'

cat >expect <<EOF
v0.2.1
v1.0
v1.0.1
v1.1.3
EOF
test_expect_success \
	'listing tags using v* should print only those having v' '
	git tag -l "v*" > actual &&
	test_cmp expect actual
'

test_expect_success 'tag -l can accept multiple patterns' '
	git tag -l "v1*" "v0*" >actual &&
	test_cmp expect actual
'

# creating and verifying lightweight tags:

test_expect_success \
	'a non-annotated tag created without parameters should point to HEAD' '
	git tag non-annotated-tag &&
	test $(git cat-file -t non-annotated-tag) = commit &&
	test $(git rev-parse non-annotated-tag) = $(git rev-parse HEAD)
'

test_expect_success 'trying to verify an unknown tag should fail' \
	'test_must_fail git tag -v unknown-tag'

test_expect_success \
	'trying to verify a non-annotated and non-signed tag should fail' \
	'test_must_fail git tag -v non-annotated-tag'

test_expect_success \
	'trying to verify many non-annotated or unknown tags, should fail' \
	'test_must_fail git tag -v unknown-tag1 non-annotated-tag unknown-tag2'

# creating annotated tags:

get_tag_msg () {
	git cat-file tag "$1" | sed -e "/BEGIN PGP/q"
}

# run test_tick before committing always gives the time in that timezone
get_tag_header () {
cat <<EOF
object $2
type $3
tag $1
tagger C O Mitter <committer@example.com> $4 -0700

EOF
}

commit=$(git rev-parse HEAD)
time=$test_tick

get_tag_header annotated-tag $commit commit $time >expect
echo "A message" >>expect
test_expect_success \
	'creating an annotated tag with -m message should succeed' '
	git tag -m "A message" annotated-tag &&
	get_tag_msg annotated-tag >actual &&
	test_cmp expect actual
'

cat >msgfile <<EOF
Another message
in a file.
EOF
get_tag_header file-annotated-tag $commit commit $time >expect
cat msgfile >>expect
test_expect_success \
	'creating an annotated tag with -F messagefile should succeed' '
	git tag -F msgfile file-annotated-tag &&
	get_tag_msg file-annotated-tag >actual &&
	test_cmp expect actual
'

cat >inputmsg <<EOF
A message from the
standard input
EOF
get_tag_header stdin-annotated-tag $commit commit $time >expect
cat inputmsg >>expect
test_expect_success 'creating an annotated tag with -F - should succeed' '
	git tag -F - stdin-annotated-tag <inputmsg &&
	get_tag_msg stdin-annotated-tag >actual &&
	test_cmp expect actual
'

test_expect_success \
	'trying to create a tag with a non-existing -F file should fail' '
	! test -f nonexistingfile &&
	! tag_exists notag &&
	test_must_fail git tag -F nonexistingfile notag &&
	! tag_exists notag
'

test_expect_success \
	'trying to create tags giving both -m or -F options should fail' '
	echo "message file 1" >msgfile1 &&
	echo "message file 2" >msgfile2 &&
	! tag_exists msgtag &&
	test_must_fail git tag -m "message 1" -F msgfile1 msgtag &&
	! tag_exists msgtag &&
	test_must_fail git tag -F msgfile1 -m "message 1" msgtag &&
	! tag_exists msgtag &&
	test_must_fail git tag -m "message 1" -F msgfile1 \
		-m "message 2" msgtag &&
	! tag_exists msgtag
'

# blank and empty messages:

get_tag_header empty-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with an empty -m message should succeed' '
	git tag -m "" empty-annotated-tag &&
	get_tag_msg empty-annotated-tag >actual &&
	test_cmp expect actual
'

>emptyfile
get_tag_header emptyfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with an empty -F messagefile should succeed' '
	git tag -F emptyfile emptyfile-annotated-tag &&
	get_tag_msg emptyfile-annotated-tag >actual &&
	test_cmp expect actual
'

printf '\n\n  \n\t\nLeading blank lines\n' >blanksfile
printf '\n\t \t  \nRepeated blank lines\n' >>blanksfile
printf '\n\n\nTrailing spaces      \t  \n' >>blanksfile
printf '\nTrailing blank lines\n\n\t \n\n' >>blanksfile
get_tag_header blanks-annotated-tag $commit commit $time >expect
cat >>expect <<EOF
Leading blank lines

Repeated blank lines

Trailing spaces

Trailing blank lines
EOF
test_expect_success \
	'extra blanks in the message for an annotated tag should be removed' '
	git tag -F blanksfile blanks-annotated-tag &&
	get_tag_msg blanks-annotated-tag >actual &&
	test_cmp expect actual
'

get_tag_header blank-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with blank -m message with spaces should succeed' '
	git tag -m "     " blank-annotated-tag &&
	get_tag_msg blank-annotated-tag >actual &&
	test_cmp expect actual
'

echo '     ' >blankfile
echo ''      >>blankfile
echo '  '    >>blankfile
get_tag_header blankfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with blank -F messagefile with spaces should succeed' '
	git tag -F blankfile blankfile-annotated-tag &&
	get_tag_msg blankfile-annotated-tag >actual &&
	test_cmp expect actual
'

printf '      ' >blanknonlfile
get_tag_header blanknonlfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with -F file of spaces and no newline should succeed' '
	git tag -F blanknonlfile blanknonlfile-annotated-tag &&
	get_tag_msg blanknonlfile-annotated-tag >actual &&
	test_cmp expect actual
'

# messages with commented lines:

cat >commentsfile <<EOF
# A comment

############
The message.
############
One line.


# commented lines
# commented lines

Another line.
# comments

Last line.
EOF
get_tag_header comments-annotated-tag $commit commit $time >expect
cat >>expect <<EOF
The message.
One line.

Another line.

Last line.
EOF
test_expect_success \
	'creating a tag using a -F messagefile with #comments should succeed' '
	git tag -F commentsfile comments-annotated-tag &&
	get_tag_msg comments-annotated-tag >actual &&
	test_cmp expect actual
'

get_tag_header comment-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with a #comment in the -m message should succeed' '
	git tag -m "#comment" comment-annotated-tag &&
	get_tag_msg comment-annotated-tag >actual &&
	test_cmp expect actual
'

echo '#comment' >commentfile
echo ''         >>commentfile
echo '####'     >>commentfile
get_tag_header commentfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with #comments in the -F messagefile should succeed' '
	git tag -F commentfile commentfile-annotated-tag &&
	get_tag_msg commentfile-annotated-tag >actual &&
	test_cmp expect actual
'

printf '#comment' >commentnonlfile
get_tag_header commentnonlfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with a file of #comment and no newline should succeed' '
	git tag -F commentnonlfile commentnonlfile-annotated-tag &&
	get_tag_msg commentnonlfile-annotated-tag >actual &&
	test_cmp expect actual
'

# listing messages for annotated non-signed tags:

test_expect_success \
	'listing the one-line message of a non-signed tag should succeed' '
	git tag -m "A msg" tag-one-line &&

	echo "tag-one-line" >expect &&
	git tag -l | grep "^tag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^tag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l tag-one-line >actual &&
	test_cmp expect actual &&

	echo "tag-one-line    A msg" >expect &&
	git tag -n1 -l | grep "^tag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^tag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l tag-one-line >actual &&
	test_cmp expect actual &&
	git tag -n2 -l tag-one-line >actual &&
	test_cmp expect actual &&
	git tag -n999 -l tag-one-line >actual &&
	test_cmp expect actual
'

test_expect_success \
	'listing the zero-lines message of a non-signed tag should succeed' '
	git tag -m "" tag-zero-lines &&

	echo "tag-zero-lines" >expect &&
	git tag -l | grep "^tag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^tag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l tag-zero-lines >actual &&
	test_cmp expect actual &&

	echo "tag-zero-lines  " >expect &&
	git tag -n1 -l | grep "^tag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^tag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l tag-zero-lines >actual &&
	test_cmp expect actual &&
	git tag -n2 -l tag-zero-lines >actual &&
	test_cmp expect actual &&
	git tag -n999 -l tag-zero-lines >actual &&
	test_cmp expect actual
'

echo 'tag line one' >annotagmsg
echo 'tag line two' >>annotagmsg
echo 'tag line three' >>annotagmsg
test_expect_success \
	'listing many message lines of a non-signed tag should succeed' '
	git tag -F annotagmsg tag-lines &&

	echo "tag-lines" >expect &&
	git tag -l | grep "^tag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^tag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l tag-lines >actual &&
	test_cmp expect actual &&

	echo "tag-lines       tag line one" >expect &&
	git tag -n1 -l | grep "^tag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^tag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l tag-lines >actual &&
	test_cmp expect actual &&

	echo "    tag line two" >>expect &&
	git tag -n2 -l | grep "^ *tag.line" >actual &&
	test_cmp expect actual &&
	git tag -n2 -l tag-lines >actual &&
	test_cmp expect actual &&

	echo "    tag line three" >>expect &&
	git tag -n3 -l | grep "^ *tag.line" >actual &&
	test_cmp expect actual &&
	git tag -n3 -l tag-lines >actual &&
	test_cmp expect actual &&
	git tag -n4 -l | grep "^ *tag.line" >actual &&
	test_cmp expect actual &&
	git tag -n4 -l tag-lines >actual &&
	test_cmp expect actual &&
	git tag -n99 -l | grep "^ *tag.line" >actual &&
	test_cmp expect actual &&
	git tag -n99 -l tag-lines >actual &&
	test_cmp expect actual
'

test_expect_success 'annotations for blobs are empty' '
	blob=$(git hash-object -w --stdin <<-\EOF
	Blob paragraph 1.

	Blob paragraph 2.
	EOF
	) &&
	git tag tag-blob $blob &&
	echo "tag-blob        " >expect &&
	git tag -n1 -l tag-blob >actual &&
	test_cmp expect actual
'

# trying to verify annotated non-signed tags:

test_expect_success GPG \
	'trying to verify an annotated non-signed tag should fail' '
	tag_exists annotated-tag &&
	test_must_fail git tag -v annotated-tag
'

test_expect_success GPG \
	'trying to verify a file-annotated non-signed tag should fail' '
	tag_exists file-annotated-tag &&
	test_must_fail git tag -v file-annotated-tag
'

test_expect_success GPG \
	'trying to verify two annotated non-signed tags should fail' '
	tag_exists annotated-tag file-annotated-tag &&
	test_must_fail git tag -v annotated-tag file-annotated-tag
'

# creating and verifying signed tags:

get_tag_header signed-tag $commit commit $time >expect
echo 'A signed tag message' >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG 'creating a signed tag with -m message should succeed' '
	git tag -s -m "A signed tag message" signed-tag &&
	get_tag_msg signed-tag >actual &&
	test_cmp expect actual
'

get_tag_header u-signed-tag $commit commit $time >expect
echo 'Another message' >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG 'sign with a given key id' '

	git tag -u committer@example.com -m "Another message" u-signed-tag &&
	get_tag_msg u-signed-tag >actual &&
	test_cmp expect actual

'

test_expect_success GPG 'sign with an unknown id (1)' '

	test_must_fail git tag -u author@example.com \
		-m "Another message" o-signed-tag

'

test_expect_success GPG 'sign with an unknown id (2)' '

	test_must_fail git tag -u DEADBEEF -m "Another message" o-signed-tag

'

cat >fakeeditor <<'EOF'
#!/bin/sh
test -n "$1" && exec >"$1"
echo A signed tag message
echo from a fake editor.
EOF
chmod +x fakeeditor

get_tag_header implied-sign $commit commit $time >expect
./fakeeditor >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG '-u implies signed tag' '
	GIT_EDITOR=./fakeeditor git tag -u CDDE430D implied-sign &&
	get_tag_msg implied-sign >actual &&
	test_cmp expect actual
'

cat >sigmsgfile <<EOF
Another signed tag
message in a file.
EOF
get_tag_header file-signed-tag $commit commit $time >expect
cat sigmsgfile >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with -F messagefile should succeed' '
	git tag -s -F sigmsgfile file-signed-tag &&
	get_tag_msg file-signed-tag >actual &&
	test_cmp expect actual
'

cat >siginputmsg <<EOF
A signed tag message from
the standard input
EOF
get_tag_header stdin-signed-tag $commit commit $time >expect
cat siginputmsg >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG 'creating a signed tag with -F - should succeed' '
	git tag -s -F - stdin-signed-tag <siginputmsg &&
	get_tag_msg stdin-signed-tag >actual &&
	test_cmp expect actual
'

get_tag_header implied-annotate $commit commit $time >expect
./fakeeditor >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG '-s implies annotated tag' '
	GIT_EDITOR=./fakeeditor git tag -s implied-annotate &&
	get_tag_msg implied-annotate >actual &&
	test_cmp expect actual
'

test_expect_success GPG \
	'trying to create a signed tag with non-existing -F file should fail' '
	! test -f nonexistingfile &&
	! tag_exists nosigtag &&
	test_must_fail git tag -s -F nonexistingfile nosigtag &&
	! tag_exists nosigtag
'

test_expect_success GPG 'verifying a signed tag should succeed' \
	'git tag -v signed-tag'

test_expect_success GPG 'verifying two signed tags in one command should succeed' \
	'git tag -v signed-tag file-signed-tag'

test_expect_success GPG \
	'verifying many signed and non-signed tags should fail' '
	test_must_fail git tag -v signed-tag annotated-tag &&
	test_must_fail git tag -v file-annotated-tag file-signed-tag &&
	test_must_fail git tag -v annotated-tag \
		file-signed-tag file-annotated-tag &&
	test_must_fail git tag -v signed-tag annotated-tag file-signed-tag
'

test_expect_success GPG 'verifying a forged tag should fail' '
	forged=$(git cat-file tag signed-tag |
		sed -e "s/signed-tag/forged-tag/" |
		git mktag) &&
	git tag forged-tag $forged &&
	test_must_fail git tag -v forged-tag
'

# blank and empty messages for signed tags:

get_tag_header empty-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with an empty -m message should succeed' '
	git tag -s -m "" empty-signed-tag &&
	get_tag_msg empty-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v empty-signed-tag
'

>sigemptyfile
get_tag_header emptyfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with an empty -F messagefile should succeed' '
	git tag -s -F sigemptyfile emptyfile-signed-tag &&
	get_tag_msg emptyfile-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v emptyfile-signed-tag
'

printf '\n\n  \n\t\nLeading blank lines\n' > sigblanksfile
printf '\n\t \t  \nRepeated blank lines\n' >>sigblanksfile
printf '\n\n\nTrailing spaces      \t  \n' >>sigblanksfile
printf '\nTrailing blank lines\n\n\t \n\n' >>sigblanksfile
get_tag_header blanks-signed-tag $commit commit $time >expect
cat >>expect <<EOF
Leading blank lines

Repeated blank lines

Trailing spaces

Trailing blank lines
EOF
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'extra blanks in the message for a signed tag should be removed' '
	git tag -s -F sigblanksfile blanks-signed-tag &&
	get_tag_msg blanks-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v blanks-signed-tag
'

get_tag_header blank-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with a blank -m message should succeed' '
	git tag -s -m "     " blank-signed-tag &&
	get_tag_msg blank-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v blank-signed-tag
'

echo '     ' >sigblankfile
echo ''      >>sigblankfile
echo '  '    >>sigblankfile
get_tag_header blankfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with blank -F file with spaces should succeed' '
	git tag -s -F sigblankfile blankfile-signed-tag &&
	get_tag_msg blankfile-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v blankfile-signed-tag
'

printf '      ' >sigblanknonlfile
get_tag_header blanknonlfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with spaces and no newline should succeed' '
	git tag -s -F sigblanknonlfile blanknonlfile-signed-tag &&
	get_tag_msg blanknonlfile-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v signed-tag
'

# messages with commented lines for signed tags:

cat >sigcommentsfile <<EOF
# A comment

############
The message.
############
One line.


# commented lines
# commented lines

Another line.
# comments

Last line.
EOF
get_tag_header comments-signed-tag $commit commit $time >expect
cat >>expect <<EOF
The message.
One line.

Another line.

Last line.
EOF
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with a -F file with #comments should succeed' '
	git tag -s -F sigcommentsfile comments-signed-tag &&
	get_tag_msg comments-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v comments-signed-tag
'

get_tag_header comment-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with #commented -m message should succeed' '
	git tag -s -m "#comment" comment-signed-tag &&
	get_tag_msg comment-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v comment-signed-tag
'

echo '#comment' >sigcommentfile
echo ''         >>sigcommentfile
echo '####'     >>sigcommentfile
get_tag_header commentfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with #commented -F messagefile should succeed' '
	git tag -s -F sigcommentfile commentfile-signed-tag &&
	get_tag_msg commentfile-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v commentfile-signed-tag
'

printf '#comment' >sigcommentnonlfile
get_tag_header commentnonlfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with a #comment and no newline should succeed' '
	git tag -s -F sigcommentnonlfile commentnonlfile-signed-tag &&
	get_tag_msg commentnonlfile-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -v commentnonlfile-signed-tag
'

# listing messages for signed tags:

test_expect_success GPG \
	'listing the one-line message of a signed tag should succeed' '
	git tag -s -m "A message line signed" stag-one-line &&

	echo "stag-one-line" >expect &&
	git tag -l | grep "^stag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^stag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l stag-one-line >actual &&
	test_cmp expect actual &&

	echo "stag-one-line   A message line signed" >expect &&
	git tag -n1 -l | grep "^stag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^stag-one-line" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l stag-one-line >actual &&
	test_cmp expect actual &&
	git tag -n2 -l stag-one-line >actual &&
	test_cmp expect actual &&
	git tag -n999 -l stag-one-line >actual &&
	test_cmp expect actual
'

test_expect_success GPG \
	'listing the zero-lines message of a signed tag should succeed' '
	git tag -s -m "" stag-zero-lines &&

	echo "stag-zero-lines" >expect &&
	git tag -l | grep "^stag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^stag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l stag-zero-lines >actual &&
	test_cmp expect actual &&

	echo "stag-zero-lines " >expect &&
	git tag -n1 -l | grep "^stag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^stag-zero-lines" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l stag-zero-lines >actual &&
	test_cmp expect actual &&
	git tag -n2 -l stag-zero-lines >actual &&
	test_cmp expect actual &&
	git tag -n999 -l stag-zero-lines >actual &&
	test_cmp expect actual
'

echo 'stag line one' >sigtagmsg
echo 'stag line two' >>sigtagmsg
echo 'stag line three' >>sigtagmsg
test_expect_success GPG \
	'listing many message lines of a signed tag should succeed' '
	git tag -s -F sigtagmsg stag-lines &&

	echo "stag-lines" >expect &&
	git tag -l | grep "^stag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l | grep "^stag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n0 -l stag-lines >actual &&
	test_cmp expect actual &&

	echo "stag-lines      stag line one" >expect &&
	git tag -n1 -l | grep "^stag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n -l | grep "^stag-lines" >actual &&
	test_cmp expect actual &&
	git tag -n1 -l stag-lines >actual &&
	test_cmp expect actual &&

	echo "    stag line two" >>expect &&
	git tag -n2 -l | grep "^ *stag.line" >actual &&
	test_cmp expect actual &&
	git tag -n2 -l stag-lines >actual &&
	test_cmp expect actual &&

	echo "    stag line three" >>expect &&
	git tag -n3 -l | grep "^ *stag.line" >actual &&
	test_cmp expect actual &&
	git tag -n3 -l stag-lines >actual &&
	test_cmp expect actual &&
	git tag -n4 -l | grep "^ *stag.line" >actual &&
	test_cmp expect actual &&
	git tag -n4 -l stag-lines >actual &&
	test_cmp expect actual &&
	git tag -n99 -l | grep "^ *stag.line" >actual &&
	test_cmp expect actual &&
	git tag -n99 -l stag-lines >actual &&
	test_cmp expect actual
'

# tags pointing to objects different from commits:

tree=$(git rev-parse HEAD^{tree})
blob=$(git rev-parse HEAD:foo)
tag=$(git rev-parse signed-tag 2>/dev/null)

get_tag_header tree-signed-tag $tree tree $time >expect
echo "A message for a tree" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag pointing to a tree should succeed' '
	git tag -s -m "A message for a tree" tree-signed-tag HEAD^{tree} &&
	get_tag_msg tree-signed-tag >actual &&
	test_cmp expect actual
'

get_tag_header blob-signed-tag $blob blob $time >expect
echo "A message for a blob" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag pointing to a blob should succeed' '
	git tag -s -m "A message for a blob" blob-signed-tag HEAD:foo &&
	get_tag_msg blob-signed-tag >actual &&
	test_cmp expect actual
'

get_tag_header tag-signed-tag $tag tag $time >expect
echo "A message for another tag" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success GPG \
	'creating a signed tag pointing to another tag should succeed' '
	git tag -s -m "A message for another tag" tag-signed-tag signed-tag &&
	get_tag_msg tag-signed-tag >actual &&
	test_cmp expect actual
'

# usage with rfc1991 signatures
echo "rfc1991" > gpghome/gpg.conf
get_tag_header rfc1991-signed-tag $commit commit $time >expect
echo "RFC1991 signed tag" >>expect
echo '-----BEGIN PGP MESSAGE-----' >>expect
test_expect_success GPG \
	'creating a signed tag with rfc1991' '
	git tag -s -m "RFC1991 signed tag" rfc1991-signed-tag $commit &&
	get_tag_msg rfc1991-signed-tag >actual &&
	test_cmp expect actual
'

cat >fakeeditor <<'EOF'
#!/bin/sh
cp "$1" actual
EOF
chmod +x fakeeditor

test_expect_success GPG \
	'reediting a signed tag body omits signature' '
	echo "RFC1991 signed tag" >expect &&
	GIT_EDITOR=./fakeeditor git tag -f -s rfc1991-signed-tag $commit &&
	test_cmp expect actual
'

test_expect_success GPG \
	'verifying rfc1991 signature' '
	git tag -v rfc1991-signed-tag
'

test_expect_success GPG \
	'list tag with rfc1991 signature' '
	echo "rfc1991-signed-tag RFC1991 signed tag" >expect &&
	git tag -l -n1 rfc1991-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -l -n2 rfc1991-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -l -n999 rfc1991-signed-tag >actual &&
	test_cmp expect actual
'

rm -f gpghome/gpg.conf

test_expect_success GPG \
	'verifying rfc1991 signature without --rfc1991' '
	git tag -v rfc1991-signed-tag
'

test_expect_success GPG \
	'list tag with rfc1991 signature without --rfc1991' '
	echo "rfc1991-signed-tag RFC1991 signed tag" >expect &&
	git tag -l -n1 rfc1991-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -l -n2 rfc1991-signed-tag >actual &&
	test_cmp expect actual &&
	git tag -l -n999 rfc1991-signed-tag >actual &&
	test_cmp expect actual
'

test_expect_success GPG \
	'reediting a signed tag body omits signature' '
	echo "RFC1991 signed tag" >expect &&
	GIT_EDITOR=./fakeeditor git tag -f -s rfc1991-signed-tag $commit &&
	test_cmp expect actual
'

# try to sign with bad user.signingkey
git config user.signingkey BobTheMouse
test_expect_success GPG \
	'git tag -s fails if gpg is misconfigured' \
	'test_must_fail git tag -s -m tail tag-gpg-failure'
git config --unset user.signingkey

# try to verify without gpg:

rm -rf gpghome
test_expect_success GPG \
	'verify signed tag fails when public key is not present' \
	'test_must_fail git tag -v signed-tag'

test_expect_success \
	'git tag -a fails if tag annotation is empty' '
	! (GIT_EDITOR=cat git tag -a initial-comment)
'

test_expect_success \
	'message in editor has initial comment' '
	! (GIT_EDITOR=cat git tag -a initial-comment > actual)
'

test_expect_success 'message in editor has initial comment: first line' '
	# check the first line --- should be empty
	echo >first.expect &&
	sed -e 1q <actual >first.actual &&
	test_i18ncmp first.expect first.actual
'

test_expect_success \
	'message in editor has initial comment: remainder' '
	# remove commented lines from the remainder -- should be empty
	>rest.expect
	sed -e 1d -e '/^#/d' <actual >rest.actual &&
	test_cmp rest.expect rest.actual
'

get_tag_header reuse $commit commit $time >expect
echo "An annotation to be reused" >> expect
test_expect_success \
	'overwriting an annoted tag should use its previous body' '
	git tag -a -m "An annotation to be reused" reuse &&
	GIT_EDITOR=true git tag -f -a reuse &&
	get_tag_msg reuse >actual &&
	test_cmp expect actual
'

test_expect_success 'filename for the message is relative to cwd' '
	mkdir subdir &&
	echo "Tag message in top directory" >msgfile-5 &&
	echo "Tag message in sub directory" >subdir/msgfile-5 &&
	(
		cd subdir &&
		git tag -a -F msgfile-5 tag-from-subdir
	) &&
	git cat-file tag tag-from-subdir | grep "in sub directory"
'

test_expect_success 'filename for the message is relative to cwd' '
	echo "Tag message in sub directory" >subdir/msgfile-6 &&
	(
		cd subdir &&
		git tag -a -F msgfile-6 tag-from-subdir-2
	) &&
	git cat-file tag tag-from-subdir-2 | grep "in sub directory"
'

# create a few more commits to test --contains

hash1=$(git rev-parse HEAD)

test_expect_success 'creating second commit and tag' '
	echo foo-2.0 >foo &&
	git add foo &&
	git commit -m second &&
	git tag v2.0
'

hash2=$(git rev-parse HEAD)

test_expect_success 'creating third commit without tag' '
	echo foo-dev >foo &&
	git add foo &&
	git commit -m third
'

hash3=$(git rev-parse HEAD)

# simple linear checks of --continue

cat > expected <<EOF
v0.2.1
v1.0
v1.0.1
v1.1.3
v2.0
EOF

test_expect_success 'checking that first commit is in all tags (hash)' "
	git tag -l --contains $hash1 v* >actual &&
	test_cmp expected actual
"

# other ways of specifying the commit
test_expect_success 'checking that first commit is in all tags (tag)' "
	git tag -l --contains v1.0 v* >actual &&
	test_cmp expected actual
"

test_expect_success 'checking that first commit is in all tags (relative)' "
	git tag -l --contains HEAD~2 v* >actual &&
	test_cmp expected actual
"

cat > expected <<EOF
v2.0
EOF

test_expect_success 'checking that second commit only has one tag' "
	git tag -l --contains $hash2 v* >actual &&
	test_cmp expected actual
"


cat > expected <<EOF
EOF

test_expect_success 'checking that third commit has no tags' "
	git tag -l --contains $hash3 v* >actual &&
	test_cmp expected actual
"

# how about a simple merge?

test_expect_success 'creating simple branch' '
	git branch stable v2.0 &&
        git checkout stable &&
	echo foo-3.0 > foo &&
	git commit foo -m fourth &&
	git tag v3.0
'

hash4=$(git rev-parse HEAD)

cat > expected <<EOF
v3.0
EOF

test_expect_success 'checking that branch head only has one tag' "
	git tag -l --contains $hash4 v* >actual &&
	test_cmp expected actual
"

test_expect_success 'merging original branch into this branch' '
	git merge --strategy=ours master &&
        git tag v4.0
'

cat > expected <<EOF
v4.0
EOF

test_expect_success 'checking that original branch head has one tag now' "
	git tag -l --contains $hash3 v* >actual &&
	test_cmp expected actual
"

cat > expected <<EOF
v0.2.1
v1.0
v1.0.1
v1.1.3
v2.0
v3.0
v4.0
EOF

test_expect_success 'checking that initial commit is in all tags' "
	git tag -l --contains $hash1 v* >actual &&
	test_cmp expected actual
"

# mixing modes and options:

test_expect_success 'mixing incompatibles modes and options is forbidden' '
	test_must_fail git tag -a &&
	test_must_fail git tag -l -v &&
	test_must_fail git tag -n 100 &&
	test_must_fail git tag -l -m msg &&
	test_must_fail git tag -l -F some file &&
	test_must_fail git tag -v -s
'

# check points-at

test_expect_success '--points-at cannot be used in non-list mode' '
	test_must_fail git tag --points-at=v4.0 foo
'

test_expect_success '--points-at finds lightweight tags' '
	echo v4.0 >expect &&
	git tag --points-at v4.0 >actual &&
	test_cmp expect actual
'

test_expect_success '--points-at finds annotated tags of commits' '
	git tag -m "v4.0, annotated" annotated-v4.0 v4.0 &&
	echo annotated-v4.0 >expect &&
	git tag -l --points-at v4.0 "annotated*" >actual &&
	test_cmp expect actual
'

test_expect_success '--points-at finds annotated tags of tags' '
	git tag -m "describing the v4.0 tag object" \
		annotated-again-v4.0 annotated-v4.0 &&
	cat >expect <<-\EOF &&
	annotated-again-v4.0
	annotated-v4.0
	EOF
	git tag --points-at=annotated-v4.0 >actual &&
	test_cmp expect actual
'

test_expect_success 'multiple --points-at are OR-ed together' '
	cat >expect <<-\EOF &&
	v2.0
	v3.0
	EOF
	git tag --points-at=v2.0 --points-at=v3.0 >actual &&
	test_cmp expect actual
'

test_done
