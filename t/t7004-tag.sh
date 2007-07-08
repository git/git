#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git-tag

Basic tests for operations with tags.'

. ./test-lib.sh

# creating and listing lightweight tags:

tag_exists () {
	git show-ref --quiet --verify refs/tags/"$1"
}

# todo: git tag -l now returns always zero, when fixed, change this test
test_expect_success 'listing all tags in an empty tree should succeed' \
	'git tag -l'

test_expect_success 'listing all tags in an empty tree should output nothing' \
	'test `git-tag -l | wc -l` -eq 0'

test_expect_failure 'looking for a tag in an empty tree should fail' \
	'tag_exists mytag'

test_expect_success 'creating a tag in an empty tree should fail' '
	! git-tag mynotag &&
	! tag_exists mynotag
'

test_expect_success 'creating a tag for HEAD in an empty tree should fail' '
	! git-tag mytaghead HEAD &&
	! tag_exists mytaghead
'

test_expect_success 'creating a tag for an unknown revision should fail' '
	! git-tag mytagnorev aaaaaaaaaaa &&
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

test_expect_success 'listing all tags if one exists should succeed' \
	'git-tag -l'

test_expect_success 'listing all tags if one exists should output that tag' \
	'test `git-tag -l` = mytag'

# pattern matching:

test_expect_success 'listing a tag using a matching pattern should succeed' \
	'git-tag -l mytag'

test_expect_success \
	'listing a tag using a matching pattern should output that tag' \
	'test `git-tag -l mytag` = mytag'

# todo: git tag -l now returns always zero, when fixed, change this test
test_expect_success \
	'listing tags using a non-matching pattern should suceed' \
	'git-tag -l xxx'

test_expect_success \
	'listing tags using a non-matching pattern should output nothing' \
	'test `git-tag -l xxx | wc -l` -eq 0'

# special cases for creating tags:

test_expect_failure \
	'trying to create a tag with the name of one existing should fail' \
	'git tag mytag'

test_expect_success \
	'trying to create a tag with a non-valid name should fail' '
	test `git-tag -l | wc -l` -eq 1 &&
	! git tag "" &&
	! git tag .othertag &&
	! git tag "other tag" &&
	! git tag "othertag^" &&
	! git tag "other~tag" &&
	test `git-tag -l | wc -l` -eq 1
'

test_expect_success 'creating a tag using HEAD directly should succeed' '
	git tag myhead HEAD &&
	tag_exists myhead
'

# deleting tags:

test_expect_success 'trying to delete an unknown tag should fail' '
	! tag_exists unknown-tag &&
	! git-tag -d unknown-tag
'

cat >expect <<EOF
myhead
mytag
EOF
test_expect_success \
	'trying to delete tags without params should succeed and do nothing' '
	git tag -l > actual && git diff expect actual &&
	git-tag -d &&
	git tag -l > actual && git diff expect actual
'

test_expect_success \
	'deleting two existing tags in one command should succeed' '
	tag_exists mytag &&
	tag_exists myhead &&
	git-tag -d mytag myhead &&
	! tag_exists mytag &&
	! tag_exists myhead
'

test_expect_success \
	'creating a tag with the name of another deleted one should succeed' '
	! tag_exists mytag &&
	git-tag mytag &&
	tag_exists mytag
'

test_expect_success \
	'trying to delete two tags, existing and not, should fail in the 2nd' '
	tag_exists mytag &&
	! tag_exists myhead &&
	! git-tag -d mytag anothertag &&
	! tag_exists mytag &&
	! tag_exists myhead
'

test_expect_failure 'trying to delete an already deleted tag should fail' \
	'git-tag -d mytag'

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
	git tag -l > actual
	git diff expect actual
'

cat >expect <<EOF
a1
aa1
cba
EOF
test_expect_success \
	'listing tags with substring as pattern must print those matching' '
	git-tag -l a > actual &&
	git diff expect actual
'

cat >expect <<EOF
v0.2.1
v1.0.1
v1.1.3
EOF
test_expect_success \
	'listing tags with substring as pattern must print those matching' '
	git-tag -l .1 > actual &&
	git diff expect actual
'

cat >expect <<EOF
t210
t211
EOF
test_expect_success \
	'listing tags with substring as pattern must print those matching' '
	git-tag -l t21 > actual &&
	git diff expect actual
'

cat >expect <<EOF
a1
aa1
EOF
test_expect_success \
	'listing tags using a name as pattern must print those matching' '
	git-tag -l a1 > actual &&
	git diff expect actual
'

cat >expect <<EOF
v1.0
v1.0.1
EOF
test_expect_success \
	'listing tags using a name as pattern must print those matching' '
	git-tag -l v1.0 > actual &&
	git diff expect actual
'

cat >expect <<EOF
v1.1.3
EOF
test_expect_success \
	'listing tags with ? in the pattern should print those matching' '
	git-tag -l "1.1?" > actual &&
	git diff expect actual
'

>expect
test_expect_success \
	'listing tags using v.* should print nothing because none have v.' '
	git-tag -l "v.*" > actual &&
	git diff expect actual
'

cat >expect <<EOF
v0.2.1
v1.0
v1.0.1
v1.1.3
EOF
test_expect_success \
	'listing tags using v* should print only those having v' '
	git-tag -l "v*" > actual &&
	git diff expect actual
'

# creating and verifying lightweight tags:

test_expect_success \
	'a non-annotated tag created without parameters should point to HEAD' '
	git-tag non-annotated-tag &&
	test $(git cat-file -t non-annotated-tag) = commit &&
	test $(git rev-parse non-annotated-tag) = $(git rev-parse HEAD)
'

test_expect_failure 'trying to verify an unknown tag should fail' \
	'git-tag -v unknown-tag'

test_expect_failure \
	'trying to verify a non-annotated and non-signed tag should fail' \
	'git-tag -v non-annotated-tag'

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
	git-tag -m "A message" annotated-tag &&
	get_tag_msg annotated-tag >actual &&
	git diff expect actual
'

cat >msgfile <<EOF
Another message
in a file.
EOF
get_tag_header file-annotated-tag $commit commit $time >expect
cat msgfile >>expect
test_expect_success \
	'creating an annotated tag with -F messagefile should succeed' '
	git-tag -F msgfile file-annotated-tag &&
	get_tag_msg file-annotated-tag >actual &&
	git diff expect actual
'

# blank and empty messages:

get_tag_header empty-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with an empty -m message should succeed' '
	git-tag -m "" empty-annotated-tag &&
	get_tag_msg empty-annotated-tag >actual &&
	git diff expect actual
'

>emptyfile
get_tag_header emptyfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with an empty -F messagefile should succeed' '
	git-tag -F emptyfile emptyfile-annotated-tag &&
	get_tag_msg emptyfile-annotated-tag >actual &&
	git diff expect actual
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
	git-tag -F blanksfile blanks-annotated-tag &&
	get_tag_msg blanks-annotated-tag >actual &&
	git diff expect actual
'

get_tag_header blank-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with blank -m message with spaces should succeed' '
	git-tag -m "     " blank-annotated-tag &&
	get_tag_msg blank-annotated-tag >actual &&
	git diff expect actual
'

echo '     ' >blankfile
echo ''      >>blankfile
echo '  '    >>blankfile
get_tag_header blankfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with blank -F messagefile with spaces should succeed' '
	git-tag -F blankfile blankfile-annotated-tag &&
	get_tag_msg blankfile-annotated-tag >actual &&
	git diff expect actual
'

printf '      ' >blanknonlfile
get_tag_header blanknonlfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with -F file of spaces and no newline should succeed' '
	git-tag -F blanknonlfile blanknonlfile-annotated-tag &&
	get_tag_msg blanknonlfile-annotated-tag >actual &&
	git diff expect actual
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
	git-tag -F commentsfile comments-annotated-tag &&
	get_tag_msg comments-annotated-tag >actual &&
	git diff expect actual
'

get_tag_header comment-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with a #comment in the -m message should succeed' '
	git-tag -m "#comment" comment-annotated-tag &&
	get_tag_msg comment-annotated-tag >actual &&
	git diff expect actual
'

echo '#comment' >commentfile
echo ''         >>commentfile
echo '####'     >>commentfile
get_tag_header commentfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with #comments in the -F messagefile should succeed' '
	git-tag -F commentfile commentfile-annotated-tag &&
	get_tag_msg commentfile-annotated-tag >actual &&
	git diff expect actual
'

printf '#comment' >commentnonlfile
get_tag_header commentnonlfile-annotated-tag $commit commit $time >expect
test_expect_success \
	'creating a tag with a file of #comment and no newline should succeed' '
	git-tag -F commentnonlfile commentnonlfile-annotated-tag &&
	get_tag_msg commentnonlfile-annotated-tag >actual &&
	git diff expect actual
'

# trying to verify annotated non-signed tags:

test_expect_success \
	'trying to verify an annotated non-signed tag should fail' '
	tag_exists annotated-tag &&
	! git-tag -v annotated-tag
'

test_expect_success \
	'trying to verify a file-annotated non-signed tag should fail' '
	tag_exists file-annotated-tag &&
	! git-tag -v file-annotated-tag
'

# creating and verifying signed tags:

gpg --version >/dev/null
if [ $? -eq 127 ]; then
	echo "Skipping signed tags tests, because gpg was not found"
	test_done
	exit
fi

# As said here: http://www.gnupg.org/documentation/faqs.html#q6.19
# the gpg version 1.0.6 didn't parse trust packets correctly, so for
# that version, creation of signed tags using the generated key fails.
case "$(gpg --version)" in
'gpg (GnuPG) 1.0.6'*)
	echo "Skipping signed tag tests, because a bug in 1.0.6 version"
	test_done
	exit
	;;
esac

# key generation info: gpg --homedir t/t7004 --gen-key
# Type DSA and Elgamal, size 2048 bits, no expiration date.
# Name and email: C O Mitter <committer@example.com>
# No password given, to enable non-interactive operation.

cp -R ../t7004 ./gpghome
chmod 0700 gpghome
export GNUPGHOME="$(pwd)/gpghome"

get_tag_header signed-tag $commit commit $time >expect
echo 'A signed tag message' >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success 'creating a signed tag with -m message should succeed' '
	git-tag -s -m "A signed tag message" signed-tag &&
	get_tag_msg signed-tag >actual &&
	git diff expect actual
'

test_expect_success 'verifying a signed tag should succeed' \
	'git-tag -v signed-tag'

test_expect_success 'verifying a forged tag should fail' '
	forged=$(git cat-file tag signed-tag |
		sed -e "s/signed-tag/forged-tag/" |
		git mktag) &&
	git tag forged-tag $forged &&
	! git-tag -v forged-tag
'

# blank and empty messages for signed tags:

get_tag_header empty-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with an empty -m message should succeed' '
	git-tag -s -m "" empty-signed-tag &&
	get_tag_msg empty-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v empty-signed-tag
'

>sigemptyfile
get_tag_header emptyfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with an empty -F messagefile should succeed' '
	git-tag -s -F sigemptyfile emptyfile-signed-tag &&
	get_tag_msg emptyfile-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v emptyfile-signed-tag
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
test_expect_success \
	'extra blanks in the message for a signed tag should be removed' '
	git-tag -s -F sigblanksfile blanks-signed-tag &&
	get_tag_msg blanks-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v blanks-signed-tag
'

get_tag_header blank-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with a blank -m message should succeed' '
	git-tag -s -m "     " blank-signed-tag &&
	get_tag_msg blank-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v blank-signed-tag
'

echo '     ' >sigblankfile
echo ''      >>sigblankfile
echo '  '    >>sigblankfile
get_tag_header blankfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with blank -F file with spaces should succeed' '
	git-tag -s -F sigblankfile blankfile-signed-tag &&
	get_tag_msg blankfile-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v blankfile-signed-tag
'

printf '      ' >sigblanknonlfile
get_tag_header blanknonlfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with spaces and no newline should succeed' '
	git-tag -s -F sigblanknonlfile blanknonlfile-signed-tag &&
	get_tag_msg blanknonlfile-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v signed-tag
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
test_expect_success \
	'creating a signed tag with a -F file with #comments should succeed' '
	git-tag -s -F sigcommentsfile comments-signed-tag &&
	get_tag_msg comments-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v comments-signed-tag
'

get_tag_header comment-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with #commented -m message should succeed' '
	git-tag -s -m "#comment" comment-signed-tag &&
	get_tag_msg comment-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v comment-signed-tag
'

echo '#comment' >sigcommentfile
echo ''         >>sigcommentfile
echo '####'     >>sigcommentfile
get_tag_header commentfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with #commented -F messagefile should succeed' '
	git-tag -s -F sigcommentfile commentfile-signed-tag &&
	get_tag_msg commentfile-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v commentfile-signed-tag
'

printf '#comment' >sigcommentnonlfile
get_tag_header commentnonlfile-signed-tag $commit commit $time >expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag with a #comment and no newline should succeed' '
	git-tag -s -F sigcommentnonlfile commentnonlfile-signed-tag &&
	get_tag_msg commentnonlfile-signed-tag >actual &&
	git diff expect actual &&
	git-tag -v commentnonlfile-signed-tag
'

# tags pointing to objects different from commits:

tree=$(git rev-parse HEAD^{tree})
blob=$(git rev-parse HEAD:foo)
tag=$(git rev-parse signed-tag)

get_tag_header tree-signed-tag $tree tree $time >expect
echo "A message for a tree" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag pointing to a tree should succeed' '
	git-tag -s -m "A message for a tree" tree-signed-tag HEAD^{tree} &&
	get_tag_msg tree-signed-tag >actual &&
	git diff expect actual
'

get_tag_header blob-signed-tag $blob blob $time >expect
echo "A message for a blob" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag pointing to a blob should succeed' '
	git-tag -s -m "A message for a blob" blob-signed-tag HEAD:foo &&
	get_tag_msg blob-signed-tag >actual &&
	git diff expect actual
'

get_tag_header tag-signed-tag $tag tag $time >expect
echo "A message for another tag" >>expect
echo '-----BEGIN PGP SIGNATURE-----' >>expect
test_expect_success \
	'creating a signed tag pointing to another tag should succeed' '
	git-tag -s -m "A message for another tag" tag-signed-tag signed-tag &&
	get_tag_msg tag-signed-tag >actual &&
	git diff expect actual
'

# try to verify without gpg:

rm -rf gpghome
test_expect_failure \
	'verify signed tag fails when public key is not present' \
	'git-tag -v signed-tag'

test_done
