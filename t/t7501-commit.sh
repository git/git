#!/bin/sh
#
# Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>
#

# FIXME: Test the various index usages, -i and -o, test reflog,
# signoff

test_description='git commit'
. ./test-lib.sh

test_tick

test_expect_success \
	"initial status" \
	"echo 'bongo bongo' >file &&
	 git add file && \
	 git status | grep 'Initial commit'"

test_expect_success \
	"fail initial amend" \
	"test_must_fail git commit --amend"

test_expect_success \
	"initial commit" \
	"git commit -m initial"

test_expect_success \
	"invalid options 1" \
	"test_must_fail git commit -m foo -m bar -F file"

test_expect_success \
	"invalid options 2" \
	"test_must_fail git commit -C HEAD -m illegal"

test_expect_success \
	"using paths with -a" \
	"echo King of the bongo >file &&
	test_must_fail git commit -m foo -a file"

test_expect_success PERL \
	"using paths with --interactive" \
	"echo bong-o-bong >file &&
	! (echo 7 | git commit -m foo --interactive file)"

test_expect_success \
	"using invalid commit with -C" \
	"test_must_fail git commit -C bogus"

test_expect_success \
	"testing nothing to commit" \
	"test_must_fail git commit -m initial"

test_expect_success \
	"next commit" \
	"echo 'bongo bongo bongo' >file \
	 git commit -m next -a"

test_expect_success \
	"commit message from non-existing file" \
	"echo 'more bongo: bongo bongo bongo bongo' >file && \
	 test_must_fail git commit -F gah -a"

# Empty except stray tabs and spaces on a few lines.
sed -e 's/@$//' >msg <<EOF
		@

  @
Signed-off-by: hula
EOF
test_expect_success \
	"empty commit message" \
	"test_must_fail git commit -F msg -a"

test_expect_success \
	"commit message from file" \
	"echo 'this is the commit message, coming from a file' >msg && \
	 git commit -F msg -a"

cat >editor <<\EOF
#!/bin/sh
sed -e "s/a file/an amend commit/g" < "$1" > "$1-"
mv "$1-" "$1"
EOF
chmod 755 editor

test_expect_success \
	"amend commit" \
	"EDITOR=./editor git commit --amend"

test_expect_success \
	"passing -m and -F" \
	"echo 'enough with the bongos' >file && \
	 test_must_fail git commit -F msg -m amending ."

test_expect_success \
	"using message from other commit" \
	"git commit -C HEAD^ ."

cat >editor <<\EOF
#!/bin/sh
sed -e "s/amend/older/g"  < "$1" > "$1-"
mv "$1-" "$1"
EOF
chmod 755 editor

test_expect_success \
	"editing message from other commit" \
	"echo 'hula hula' >file && \
	 EDITOR=./editor git commit -c HEAD^ -a"

test_expect_success \
	"message from stdin" \
	"echo 'silly new contents' >file && \
	 echo commit message from stdin | git commit -F - -a"

test_expect_success \
	"overriding author from command line" \
	"echo 'gak' >file && \
	 git commit -m 'author' --author 'Rubber Duck <rduck@convoy.org>' -a >output 2>&1"

test_expect_success \
	"commit --author output mentions author" \
	"grep Rubber.Duck output"

test_expect_success PERL \
	"interactive add" \
	"echo 7 | git commit --interactive | grep 'What now'"

test_expect_success \
	"showing committed revisions" \
	"git rev-list HEAD >current"

cat >editor <<\EOF
#!/bin/sh
sed -e "s/good/bad/g" < "$1" > "$1-"
mv "$1-" "$1"
EOF
chmod 755 editor

cat >msg <<EOF
A good commit message.
EOF

test_expect_success \
	'editor not invoked if -F is given' '
	 echo "moo" >file &&
	 EDITOR=./editor git commit -a -F msg &&
	 git show -s --pretty=format:"%s" | grep -q good &&
	 echo "quack" >file &&
	 echo "Another good message." | EDITOR=./editor git commit -a -F - &&
	 git show -s --pretty=format:"%s" | grep -q good
	 '
# We could just check the head sha1, but checking each commit makes it
# easier to isolate bugs.

cat >expected <<\EOF
72c0dc9855b0c9dadcbfd5a31cab072e0cb774ca
9b88fc14ce6b32e3d9ee021531a54f18a5cf38a2
3536bbb352c3a1ef9a420f5b4242d48578b92aa7
d381ac431806e53f3dd7ac2f1ae0534f36d738b9
4fd44095ad6334f3ef72e4c5ec8ddf108174b54a
402702b49136e7587daa9280e91e4bb7cb2179f7
EOF

test_expect_success \
    'validate git rev-list output.' \
    'test_cmp expected current'

test_expect_success 'partial commit that involves removal (1)' '

	git rm --cached file &&
	mv file elif &&
	git add elif &&
	git commit -m "Partial: add elif" elif &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "A	elif" >expected &&
	test_cmp expected current

'

test_expect_success 'partial commit that involves removal (2)' '

	git commit -m "Partial: remove file" file &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "D	file" >expected &&
	test_cmp expected current

'

test_expect_success 'partial commit that involves removal (3)' '

	git rm --cached elif &&
	echo elif >elif &&
	git commit -m "Partial: modify elif" elif &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "M	elif" >expected &&
	test_cmp expected current

'

author="The Real Author <someguy@his.email.org>"
test_expect_success 'amend commit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --author="$author" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'amend commit to fix date' '

	test_tick &&
	newtick=$GIT_AUTHOR_DATE &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $newtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --date="$newtick" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'sign off (1)' '

	echo 1 >positive &&
	git add positive &&
	git commit -s -m "thank you" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo thank you
		echo
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'sign off (2)' '

	echo 2 >positive &&
	git add positive &&
	existing="Signed-off-by: Watch This <watchthis@example.com>" &&
	git commit -s -m "thank you

$existing" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo thank you
		echo
		echo $existing
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'signoff gap' '

	echo 3 >positive &&
	git add positive &&
	alt="Alt-RFC-822-Header: Value" &&
	git commit -s -m "welcome

$alt" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	(
		echo welcome
		echo
		echo $alt
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'signoff gap 2' '

	echo 4 >positive &&
	git add positive &&
	alt="fixed: 34" &&
	git commit -s -m "welcome

We have now
$alt" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	(
		echo welcome
		echo
		echo We have now
		echo $alt
		echo
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'multiple -m' '

	>negative &&
	git add negative &&
	git commit -m "one" -m "two" -m "three" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo one
		echo
		echo two
		echo
		echo three
	) >expected &&
	test_cmp expected actual

'

author="The Real Author <someguy@his.email.org>"
test_expect_success 'amend commit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --author="$author" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'git commit <file> with dirty index' '
	echo tacocat > elif &&
	echo tehlulz > chz &&
	git add chz &&
	git commit elif -m "tacocat is a palindrome" &&
	git show --stat | grep elif &&
	git diff --cached | grep chz
'

test_expect_success 'same tree (single parent)' '

	git reset --hard

	if git commit -m empty
	then
		echo oops -- should have complained
		false
	else
		: happy
	fi

'

test_expect_success 'same tree (single parent) --allow-empty' '

	git commit --allow-empty -m "forced empty" &&
	git cat-file commit HEAD | grep forced

'

test_expect_success 'same tree (merge and amend merge)' '

	git checkout -b side HEAD^ &&
	echo zero >zero &&
	git add zero &&
	git commit -m "add zero" &&
	git checkout master &&

	git merge -s ours side -m "empty ok" &&
	git diff HEAD^ HEAD >actual &&
	: >expected &&
	test_cmp expected actual &&

	git commit --amend -m "empty really ok" &&
	git diff HEAD^ HEAD >actual &&
	: >expected &&
	test_cmp expected actual

'

test_expect_success 'amend using the message from another commit' '

	git reset --hard &&
	test_tick &&
	git commit --allow-empty -m "old commit" &&
	old=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty -m "new commit" &&
	new=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty --amend -C "$old" &&
	git show --pretty="format:%ad %s" "$old" >expected &&
	git show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend using the message from a commit named with tag' '

	git reset --hard &&
	test_tick &&
	git commit --allow-empty -m "old commit" &&
	old=$(git rev-parse --verify HEAD) &&
	git tag -a -m "tag on old" tagged-old HEAD &&
	test_tick &&
	git commit --allow-empty -m "new commit" &&
	new=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty --amend -C tagged-old &&
	git show --pretty="format:%ad %s" "$old" >expected &&
	git show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend can copy notes' '

	git config notes.rewrite.amend true &&
	git config notes.rewriteRef "refs/notes/*" &&
	test_commit foo &&
	git notes add -m"a note" &&
	test_tick &&
	git commit --amend -m"new foo" &&
	test "$(git notes show)" = "a note"

'

test_done
