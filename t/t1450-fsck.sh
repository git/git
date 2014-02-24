#!/bin/sh

test_description='git fsck random collection of tests

* (HEAD) B
* (master) A
'

. ./test-lib.sh

test_expect_success setup '
	git config gc.auto 0 &&
	git config i18n.commitencoding ISO-8859-1 &&
	test_commit A fileA one &&
	git config --unset i18n.commitencoding &&
	git checkout HEAD^0 &&
	test_commit B fileB two &&
	git tag -d A B &&
	git reflog expire --expire=now --all &&
	>empty
'

test_expect_success 'loose objects borrowed from alternate are not missing' '
	mkdir another &&
	(
		cd another &&
		git init &&
		echo ../../../.git/objects >.git/objects/info/alternates &&
		test_commit C fileC one &&
		git fsck --no-dangling >../actual 2>&1
	) &&
	test_cmp empty actual
'

test_expect_success 'HEAD is part of refs, valid objects appear valid' '
	git fsck >actual 2>&1 &&
	test_cmp empty actual
'

# Corruption tests follow.  Make sure to remove all traces of the
# specific corruption you test afterwards, lest a later test trip over
# it.

test_expect_success 'setup: helpers for corruption tests' '
	sha1_file() {
		echo "$*" | sed "s#..#.git/objects/&/#"
	} &&

	remove_object() {
		file=$(sha1_file "$*") &&
		test -e "$file" &&
		rm -f "$file"
	}
'

test_expect_success 'object with bad sha1' '
	sha=$(echo blob | git hash-object -w --stdin) &&
	old=$(echo $sha | sed "s+^..+&/+") &&
	new=$(dirname $old)/ffffffffffffffffffffffffffffffffffffff &&
	sha="$(dirname $new)$(basename $new)" &&
	mv .git/objects/$old .git/objects/$new &&
	test_when_finished "remove_object $sha" &&
	git update-index --add --cacheinfo 100644 $sha foo &&
	test_when_finished "git read-tree -u --reset HEAD" &&
	tree=$(git write-tree) &&
	test_when_finished "remove_object $tree" &&
	cmt=$(echo bogus | git commit-tree $tree) &&
	test_when_finished "remove_object $cmt" &&
	git update-ref refs/heads/bogus $cmt &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&

	test_might_fail git fsck 2>out &&
	cat out &&
	grep "$sha.*corrupt" out
'

test_expect_success 'branch pointing to non-commit' '
	git rev-parse HEAD^{tree} >.git/refs/heads/invalid &&
	test_when_finished "git update-ref -d refs/heads/invalid" &&
	git fsck 2>out &&
	cat out &&
	grep "not a commit" out
'

test_expect_success 'email without @ is okay' '
	git cat-file commit HEAD >basis &&
	sed "s/@/AT/" basis >okay &&
	new=$(git hash-object -t commit -w --stdin <okay) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	! grep "commit $new" out
'

test_expect_success 'email with embedded > is not okay' '
	git cat-file commit HEAD >basis &&
	sed "s/@[a-z]/&>/" basis >bad-email &&
	new=$(git hash-object -t commit -w --stdin <bad-email) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new" out
'

test_expect_success 'missing < email delimiter is reported nicely' '
	git cat-file commit HEAD >basis &&
	sed "s/<//" basis >bad-email-2 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-2) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new.* - bad name" out
'

test_expect_success 'missing email is reported nicely' '
	git cat-file commit HEAD >basis &&
	sed "s/[a-z]* <[^>]*>//" basis >bad-email-3 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-3) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new.* - missing email" out
'

test_expect_success '> in name is reported' '
	git cat-file commit HEAD >basis &&
	sed "s/ </> </" basis >bad-email-4 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-4) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new" out
'

# date is 2^64 + 1
test_expect_success 'integer overflow in timestamps is reported' '
	git cat-file commit HEAD >basis &&
	sed "s/^\\(author .*>\\) [0-9]*/\\1 18446744073709551617/" \
		<basis >bad-timestamp &&
	new=$(git hash-object -t commit -w --stdin <bad-timestamp) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	grep "error in commit $new.*integer overflow" out
'

test_expect_success 'tag pointing to nonexistent' '
	cat >invalid-tag <<-\EOF &&
	object ffffffffffffffffffffffffffffffffffffffff
	type commit
	tag invalid
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(git hash-object -t tag -w --stdin <invalid-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.git/refs/tags/invalid &&
	test_when_finished "git update-ref -d refs/tags/invalid" &&
	test_must_fail git fsck --tags >out &&
	cat out &&
	grep "broken link" out
'

test_expect_success 'tag pointing to something else than its type' '
	sha=$(echo blob | git hash-object -w --stdin) &&
	test_when_finished "remove_object $sha" &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type commit
	tag wrong
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(git hash-object -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.git/refs/tags/wrong &&
	test_when_finished "git update-ref -d refs/tags/wrong" &&
	test_must_fail git fsck --tags
'

test_expect_success 'cleaned up' '
	git fsck >actual 2>&1 &&
	test_cmp empty actual
'

test_expect_success 'rev-list --verify-objects' '
	git rev-list --verify-objects --all >/dev/null 2>out &&
	test_cmp empty out
'

test_expect_success 'rev-list --verify-objects with bad sha1' '
	sha=$(echo blob | git hash-object -w --stdin) &&
	old=$(echo $sha | sed "s+^..+&/+") &&
	new=$(dirname $old)/ffffffffffffffffffffffffffffffffffffff &&
	sha="$(dirname $new)$(basename $new)" &&
	mv .git/objects/$old .git/objects/$new &&
	test_when_finished "remove_object $sha" &&
	git update-index --add --cacheinfo 100644 $sha foo &&
	test_when_finished "git read-tree -u --reset HEAD" &&
	tree=$(git write-tree) &&
	test_when_finished "remove_object $tree" &&
	cmt=$(echo bogus | git commit-tree $tree) &&
	test_when_finished "remove_object $cmt" &&
	git update-ref refs/heads/bogus $cmt &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&

	test_might_fail git rev-list --verify-objects refs/heads/bogus >/dev/null 2>out &&
	cat out &&
	grep -q "error: sha1 mismatch 63ffffffffffffffffffffffffffffffffffffff" out
'

_bz='\0'
_bz5="$_bz$_bz$_bz$_bz$_bz"
_bz20="$_bz5$_bz5$_bz5$_bz5"

test_expect_success 'fsck notices blob entry pointing to null sha1' '
	(git init null-blob &&
	 cd null-blob &&
	 sha=$(printf "100644 file$_bz$_bz20" |
	       git hash-object -w --stdin -t tree) &&
	  git fsck 2>out &&
	  cat out &&
	  grep "warning.*null sha1" out
	)
'

test_expect_success 'fsck notices submodule entry pointing to null sha1' '
	(git init null-commit &&
	 cd null-commit &&
	 sha=$(printf "160000 submodule$_bz$_bz20" |
	       git hash-object -w --stdin -t tree) &&
	  git fsck 2>out &&
	  cat out &&
	  grep "warning.*null sha1" out
	)
'

test_expect_success 'fsck notices "." and ".." in trees' '
	(
		git init dots &&
		cd dots &&
		blob=$(echo foo | git hash-object -w --stdin) &&
		tab=$(printf "\\t") &&
		git mktree <<-EOF &&
		100644 blob $blob$tab.
		100644 blob $blob$tab..
		EOF
		git fsck 2>out &&
		cat out &&
		grep "warning.*\\." out
	)
'

test_expect_success 'fsck notices ".git" in trees' '
	(
		git init dotgit &&
		cd dotgit &&
		blob=$(echo foo | git hash-object -w --stdin) &&
		tab=$(printf "\\t") &&
		git mktree <<-EOF &&
		100644 blob $blob$tab.git
		EOF
		git fsck 2>out &&
		cat out &&
		grep "warning.*\\.git" out
	)
'

test_done
