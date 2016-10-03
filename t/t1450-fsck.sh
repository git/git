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

	test_must_fail git fsck 2>out &&
	cat out &&
	grep "$sha.*corrupt" out
'

test_expect_success 'branch pointing to non-commit' '
	git rev-parse HEAD^{tree} >.git/refs/heads/invalid &&
	test_when_finished "git update-ref -d refs/heads/invalid" &&
	test_must_fail git fsck 2>out &&
	cat out &&
	grep "not a commit" out
'

test_expect_success 'HEAD link pointing at a funny object' '
	test_when_finished "mv .git/SAVED_HEAD .git/HEAD" &&
	mv .git/HEAD .git/SAVED_HEAD &&
	echo 0000000000000000000000000000000000000000 >.git/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env GIT_DIR=.git git fsck 2>out &&
	cat out &&
	grep "detached HEAD points" out
'

test_expect_success 'HEAD link pointing at a funny place' '
	test_when_finished "mv .git/SAVED_HEAD .git/HEAD" &&
	mv .git/HEAD .git/SAVED_HEAD &&
	echo "ref: refs/funny/place" >.git/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env GIT_DIR=.git git fsck 2>out &&
	cat out &&
	grep "HEAD points to something strange" out
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
	test_must_fail git fsck 2>out &&
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
	test_must_fail git fsck 2>out &&
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
	test_must_fail git fsck 2>out &&
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
	test_must_fail git fsck 2>out &&
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
	test_must_fail git fsck 2>out &&
	cat out &&
	grep "error in commit $new.*integer overflow" out
'

test_expect_success 'commit with NUL in header' '
	git cat-file commit HEAD >basis &&
	sed "s/author ./author Q/" <basis | q_to_nul >commit-NUL-header &&
	new=$(git hash-object -t commit -w --stdin <commit-NUL-header) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck 2>out &&
	cat out &&
	grep "error in commit $new.*unterminated header: NUL at offset" out
'

test_expect_success 'tree object with duplicate entries' '
	test_when_finished "remove_object \$T" &&
	T=$(
		GIT_INDEX_FILE=test-index &&
		export GIT_INDEX_FILE &&
		rm -f test-index &&
		>x &&
		git add x &&
		T=$(git write-tree) &&
		(
			git cat-file tree $T &&
			git cat-file tree $T
		) |
		git hash-object -w -t tree --stdin
	) &&
	test_must_fail git fsck 2>out &&
	grep "error in tree .*contains duplicate file entries" out
'

test_expect_success 'unparseable tree object' '
	test_when_finished "git update-ref -d refs/heads/wrong" &&
	test_when_finished "remove_object \$tree_sha1" &&
	test_when_finished "remove_object \$commit_sha1" &&
	tree_sha1=$(printf "100644 \0twenty-bytes-of-junk" | git hash-object -t tree --stdin -w --literally) &&
	commit_sha1=$(git commit-tree $tree_sha1) &&
	git update-ref refs/heads/wrong $commit_sha1 &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error: empty filename in tree entry" out &&
	test_i18ngrep "$tree_sha1" out &&
	test_i18ngrep ! "fatal: empty filename in tree entry" out
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

test_expect_success 'tag with incorrect tag name & missing tagger' '
	sha=$(git rev-parse HEAD) &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type commit
	tag wrong name format

	This is an invalid tag.
	EOF

	tag=$(git hash-object -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.git/refs/tags/wrong &&
	test_when_finished "git update-ref -d refs/tags/wrong" &&
	git fsck --tags 2>out &&

	cat >expect <<-EOF &&
	warning in tag $tag: badTagName: invalid '\''tag'\'' name: wrong name format
	warning in tag $tag: missingTaggerEntry: invalid format - expected '\''tagger'\'' line
	EOF
	test_cmp expect out
'

test_expect_success 'tag with bad tagger' '
	sha=$(git rev-parse HEAD) &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type commit
	tag not-quite-wrong
	tagger Bad Tagger Name

	This is an invalid tag.
	EOF

	tag=$(git hash-object --literally -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.git/refs/tags/wrong &&
	test_when_finished "git update-ref -d refs/tags/wrong" &&
	test_must_fail git fsck --tags 2>out &&
	grep "error in tag .*: invalid author/committer" out
'

test_expect_success 'tag with NUL in header' '
	sha=$(git rev-parse HEAD) &&
	q_to_nul >tag-NUL-header <<-EOF &&
	object $sha
	type commit
	tag contains-Q-in-header
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(git hash-object --literally -t tag -w --stdin <tag-NUL-header) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.git/refs/tags/wrong &&
	test_when_finished "git update-ref -d refs/tags/wrong" &&
	test_must_fail git fsck --tags 2>out &&
	cat out &&
	grep "error in tag $tag.*unterminated header: NUL at offset" out
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

test_expect_success 'force fsck to ignore double author' '
	git cat-file commit HEAD >basis &&
	sed "s/^author .*/&,&/" <basis | tr , \\n >multiple-authors &&
	new=$(git hash-object -t commit -w --stdin <multiple-authors) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck &&
	git -c fsck.multipleAuthors=ignore fsck
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

while read name path pretty; do
	while read mode type; do
		: ${pretty:=$path}
		test_expect_success "fsck notices $pretty as $type" '
		(
			git init $name-$type &&
			cd $name-$type &&
			echo content >file &&
			git add file &&
			git commit -m base &&
			blob=$(git rev-parse :file) &&
			tree=$(git rev-parse HEAD^{tree}) &&
			value=$(eval "echo \$$type") &&
			printf "$mode $type %s\t%s" "$value" "$path" >bad &&
			bad_tree=$(git mktree <bad) &&
			git fsck 2>out &&
			cat out &&
			grep "warning.*tree $bad_tree" out
		)'
	done <<-\EOF
	100644 blob
	040000 tree
	EOF
done <<-EOF
dot .
dotdot ..
dotgit .git
dotgit-case .GIT
dotgit-unicode .gI${u200c}T .gI{u200c}T
dotgit-case2 .Git
git-tilde1 git~1
dotgitdot .git.
dot-backslash-case .\\\\.GIT\\\\foobar
dotgit-case-backslash .git\\\\foobar
EOF

test_expect_success 'fsck allows .Ňit' '
	(
		git init not-dotgit &&
		cd not-dotgit &&
		echo content >file &&
		git add file &&
		git commit -m base &&
		blob=$(git rev-parse :file) &&
		printf "100644 blob $blob\t.\\305\\207it" >tree &&
		tree=$(git mktree <tree) &&
		git fsck 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'NUL in commit' '
	rm -fr nul-in-commit &&
	git init nul-in-commit &&
	(
		cd nul-in-commit &&
		git commit --allow-empty -m "initial commitQNUL after message" &&
		git cat-file commit HEAD >original &&
		q_to_nul <original >munged &&
		git hash-object -w -t commit --stdin <munged >name &&
		git branch bad $(cat name) &&

		test_must_fail git -c fsck.nulInCommit=error fsck 2>warn.1 &&
		grep nulInCommit warn.1 &&
		git fsck 2>warn.2 &&
		grep nulInCommit warn.2
	)
'

# create a static test repo which is broken by omitting
# one particular object ($1, which is looked up via rev-parse
# in the new repository).
create_repo_missing () {
	rm -rf missing &&
	git init missing &&
	(
		cd missing &&
		git commit -m one --allow-empty &&
		mkdir subdir &&
		echo content >subdir/file &&
		git add subdir/file &&
		git commit -m two &&
		unrelated=$(echo unrelated | git hash-object --stdin -w) &&
		git tag -m foo tag $unrelated &&
		sha1=$(git rev-parse --verify "$1") &&
		path=$(echo $sha1 | sed 's|..|&/|') &&
		rm .git/objects/$path
	)
}

test_expect_success 'fsck notices missing blob' '
	create_repo_missing HEAD:subdir/file &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices missing subtree' '
	create_repo_missing HEAD:subdir &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices missing root tree' '
	create_repo_missing HEAD^{tree} &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices missing parent' '
	create_repo_missing HEAD^ &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices missing tagged object' '
	create_repo_missing tag^{blob} &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices ref pointing to missing commit' '
	create_repo_missing HEAD &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck notices ref pointing to missing tag' '
	create_repo_missing tag &&
	test_must_fail git -C missing fsck
'

test_expect_success 'fsck --connectivity-only' '
	rm -rf connectivity-only &&
	git init connectivity-only &&
	(
		cd connectivity-only &&
		touch empty &&
		git add empty &&
		test_commit empty &&
		empty=.git/objects/e6/9de29bb2d1d6434b8b29ae775ad8c2e48c5391 &&
		rm -f $empty &&
		echo invalid >$empty &&
		test_must_fail git fsck --strict &&
		git fsck --strict --connectivity-only &&
		tree=$(git rev-parse HEAD:) &&
		suffix=${tree#??} &&
		tree=.git/objects/${tree%$suffix}/$suffix &&
		rm -f $tree &&
		echo invalid >$tree &&
		test_must_fail git fsck --strict --connectivity-only
	)
'

remove_loose_object () {
	sha1="$(git rev-parse "$1")" &&
	remainder=${sha1#??} &&
	firsttwo=${sha1%$remainder} &&
	rm .git/objects/$firsttwo/$remainder
}

test_expect_success 'fsck --name-objects' '
	rm -rf name-objects &&
	git init name-objects &&
	(
		cd name-objects &&
		test_commit julius caesar.t &&
		test_commit augustus &&
		test_commit caesar &&
		remove_loose_object $(git rev-parse julius:caesar.t) &&
		test_must_fail git fsck --name-objects >out &&
		tree=$(git rev-parse --verify julius:) &&
		grep "$tree (\(refs/heads/master\|HEAD\)@{[0-9]*}:" out
	)
'

test_done
