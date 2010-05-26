#!/bin/sh

test_description='git fsck random collection of tests'

. ./test-lib.sh

test_expect_success setup '
	git config i18n.commitencoding ISO-8859-1 &&
	test_commit A fileA one &&
	git config --unset i18n.commitencoding &&
	git checkout HEAD^0 &&
	test_commit B fileB two &&
	git tag -d A B &&
	git reflog expire --expire=now --all
'

test_expect_success 'HEAD is part of refs' '
	test 0 = $(git fsck | wc -l)
'

test_expect_success 'loose objects borrowed from alternate are not missing' '
	mkdir another &&
	(
		cd another &&
		git init &&
		echo ../../../.git/objects >.git/objects/info/alternates &&
		test_commit C fileC one &&
		git fsck >out &&
		! grep "missing blob" out
	)
'

test_expect_success 'valid objects appear valid' '
	{ git fsck 2>out; true; } &&
	! grep error out &&
	! grep fatal out
'

# Corruption tests follow.  Make sure to remove all traces of the
# specific corruption you test afterwards, lest a later test trip over
# it.

test_expect_success 'object with bad sha1' '
	sha=$(echo blob | git hash-object -w --stdin) &&
	echo $sha &&
	old=$(echo $sha | sed "s+^..+&/+") &&
	new=$(dirname $old)/ffffffffffffffffffffffffffffffffffffff &&
	sha="$(dirname $new)$(basename $new)"
	mv .git/objects/$old .git/objects/$new &&
	git update-index --add --cacheinfo 100644 $sha foo &&
	tree=$(git write-tree) &&
	cmt=$(echo bogus | git commit-tree $tree) &&
	git update-ref refs/heads/bogus $cmt &&
	(git fsck 2>out; true) &&
	grep "$sha.*corrupt" out &&
	rm -f .git/objects/$new &&
	git update-ref -d refs/heads/bogus &&
	git read-tree -u --reset HEAD
'

test_expect_success 'branch pointing to non-commit' '
	git rev-parse HEAD^{tree} > .git/refs/heads/invalid &&
	git fsck 2>out &&
	grep "not a commit" out &&
	git update-ref -d refs/heads/invalid
'

new=nothing
test_expect_success 'email without @ is okay' '
	git cat-file commit HEAD >basis &&
	sed "s/@/AT/" basis >okay &&
	new=$(git hash-object -t commit -w --stdin <okay) &&
	echo "$new" &&
	git update-ref refs/heads/bogus "$new" &&
	git fsck 2>out &&
	cat out &&
	! grep "error in commit $new" out
'
git update-ref -d refs/heads/bogus
rm -f ".git/objects/$new"

new=nothing
test_expect_success 'email with embedded > is not okay' '
	git cat-file commit HEAD >basis &&
	sed "s/@[a-z]/&>/" basis >bad-email &&
	new=$(git hash-object -t commit -w --stdin <bad-email) &&
	echo "$new" &&
	git update-ref refs/heads/bogus "$new" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new" out
'
git update-ref -d refs/heads/bogus
rm -f ".git/objects/$new"

cat > invalid-tag <<EOF
object ffffffffffffffffffffffffffffffffffffffff
type commit
tag invalid
tagger T A Gger <tagger@example.com> 1234567890 -0000

This is an invalid tag.
EOF

test_expect_success 'tag pointing to nonexistent' '
	tag=$(git hash-object -t tag -w --stdin < invalid-tag) &&
	echo $tag > .git/refs/tags/invalid &&
	test_must_fail git fsck --tags >out &&
	cat out &&
	grep "broken link" out &&
	rm .git/refs/tags/invalid
'

cat > wrong-tag <<EOF
object $(echo blob | git hash-object -w --stdin)
type commit
tag wrong
tagger T A Gger <tagger@example.com> 1234567890 -0000

This is an invalid tag.
EOF

test_expect_success 'tag pointing to something else than its type' '
	tag=$(git hash-object -t tag -w --stdin < wrong-tag) &&
	echo $tag > .git/refs/tags/wrong &&
	test_must_fail git fsck --tags 2>out &&
	cat out &&
	grep "error in tag.*broken links" out &&
	rm .git/refs/tags/wrong
'



test_done
