#!/bin/sh

test_description='git fsck random collection of tests

* (HEAD) B
* (main) A
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
	git reflog expire --expire=now --all
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
	test_must_be_empty actual
'

test_expect_success 'HEAD is part of refs, valid objects appear valid' '
	git fsck >actual 2>&1 &&
	test_must_be_empty actual
'

# Corruption tests follow.  Make sure to remove all traces of the
# specific corruption you test afterwards, lest a later test trip over
# it.

sha1_file () {
	git rev-parse --git-path objects/$(test_oid_to_path "$1")
}

remove_object () {
	rm "$(sha1_file "$1")"
}

test_expect_success 'object with hash mismatch' '
	git init --bare hash-mismatch &&
	(
		cd hash-mismatch &&

		oid=$(echo blob | git hash-object -w --stdin) &&
		oldoid=$oid &&
		old=$(test_oid_to_path "$oid") &&
		new=$(dirname $old)/$(test_oid ff_2) &&
		oid="$(dirname $new)$(basename $new)" &&

		mv objects/$old objects/$new &&
		git update-index --add --cacheinfo 100644 $oid foo &&
		tree=$(git write-tree) &&
		cmt=$(echo bogus | git commit-tree $tree) &&
		git update-ref refs/heads/bogus $cmt &&

		test_must_fail git fsck 2>out &&
		grep "$oldoid: hash-path mismatch, found at: .*$new" out
	)
'

test_expect_success 'object with hash and type mismatch' '
	git init --bare hash-type-mismatch &&
	(
		cd hash-type-mismatch &&

		oid=$(echo blob | git hash-object -w --stdin -t garbage --literally) &&
		oldoid=$oid &&
		old=$(test_oid_to_path "$oid") &&
		new=$(dirname $old)/$(test_oid ff_2) &&
		oid="$(dirname $new)$(basename $new)" &&

		mv objects/$old objects/$new &&
		git update-index --add --cacheinfo 100644 $oid foo &&
		tree=$(git write-tree) &&
		cmt=$(echo bogus | git commit-tree $tree) &&
		git update-ref refs/heads/bogus $cmt &&


		test_must_fail git fsck 2>out &&
		grep "^error: $oldoid: hash-path mismatch, found at: .*$new" out &&
		grep "^error: $oldoid: object is of unknown type '"'"'garbage'"'"'" out
	)
'

test_expect_success 'zlib corrupt loose object output ' '
	git init --bare corrupt-loose-output &&
	(
		cd corrupt-loose-output &&
		oid=$(git hash-object -w --stdin --literally </dev/null) &&
		oidf=objects/$(test_oid_to_path "$oid") &&
		chmod +w $oidf &&
		echo extra garbage >>$oidf &&

		cat >expect.error <<-EOF &&
		error: garbage at end of loose object '\''$oid'\''
		error: unable to unpack contents of ./$oidf
		error: $oid: object corrupt or missing: ./$oidf
		EOF
		test_must_fail git fsck 2>actual &&
		grep ^error: actual >error &&
		test_cmp expect.error error
	)
'

test_expect_success 'branch pointing to non-commit' '
	git rev-parse HEAD^{tree} >.git/refs/heads/invalid &&
	test_when_finished "git update-ref -d refs/heads/invalid" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "not a commit" out
'

test_expect_success 'HEAD link pointing at a funny object' '
	test_when_finished "mv .git/SAVED_HEAD .git/HEAD" &&
	mv .git/HEAD .git/SAVED_HEAD &&
	echo $ZERO_OID >.git/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env GIT_DIR=.git git fsck 2>out &&
	test_i18ngrep "detached HEAD points" out
'

test_expect_success 'HEAD link pointing at a funny place' '
	test_when_finished "mv .git/SAVED_HEAD .git/HEAD" &&
	mv .git/HEAD .git/SAVED_HEAD &&
	echo "ref: refs/funny/place" >.git/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env GIT_DIR=.git git fsck 2>out &&
	test_i18ngrep "HEAD points to something strange" out
'

test_expect_success 'HEAD link pointing at a funny object (from different wt)' '
	test_when_finished "mv .git/SAVED_HEAD .git/HEAD" &&
	test_when_finished "rm -rf .git/worktrees wt" &&
	git worktree add wt &&
	mv .git/HEAD .git/SAVED_HEAD &&
	echo $ZERO_OID >.git/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail git -C wt fsck 2>out &&
	test_i18ngrep "main-worktree/HEAD: detached HEAD points" out
'

test_expect_success 'other worktree HEAD link pointing at a funny object' '
	test_when_finished "rm -rf .git/worktrees other" &&
	git worktree add other &&
	echo $ZERO_OID >.git/worktrees/other/HEAD &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD: detached HEAD points" out
'

test_expect_success 'other worktree HEAD link pointing at missing object' '
	test_when_finished "rm -rf .git/worktrees other" &&
	git worktree add other &&
	echo "Contents missing from repo" | git hash-object --stdin >.git/worktrees/other/HEAD &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD: invalid sha1 pointer" out
'

test_expect_success 'other worktree HEAD link pointing at a funny place' '
	test_when_finished "rm -rf .git/worktrees other" &&
	git worktree add other &&
	echo "ref: refs/funny/place" >.git/worktrees/other/HEAD &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD points to something strange" out
'

test_expect_success 'commit with multiple signatures is okay' '
	git cat-file commit HEAD >basis &&
	cat >sigs <<-EOF &&
	gpgsig -----BEGIN PGP SIGNATURE-----
	  VGhpcyBpcyBub3QgcmVhbGx5IGEgc2lnbmF0dXJlLg==
	  -----END PGP SIGNATURE-----
	gpgsig-sha256 -----BEGIN PGP SIGNATURE-----
	  VGhpcyBpcyBub3QgcmVhbGx5IGEgc2lnbmF0dXJlLg==
	  -----END PGP SIGNATURE-----
	EOF
	sed -e "/^committer/q" basis >okay &&
	cat sigs >>okay &&
	echo >>okay &&
	sed -e "1,/^$/d" basis >>okay &&
	cat okay &&
	new=$(git hash-object -t commit -w --stdin <okay) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
	cat out &&
	! grep "commit $new" out
'

test_expect_success 'email without @ is okay' '
	git cat-file commit HEAD >basis &&
	sed "s/@/AT/" basis >okay &&
	new=$(git hash-object -t commit -w --stdin <okay) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	git fsck 2>out &&
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
	test_i18ngrep "error in commit $new" out
'

test_expect_success 'missing < email delimiter is reported nicely' '
	git cat-file commit HEAD >basis &&
	sed "s/<//" basis >bad-email-2 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-2) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in commit $new.* - bad name" out
'

test_expect_success 'missing email is reported nicely' '
	git cat-file commit HEAD >basis &&
	sed "s/[a-z]* <[^>]*>//" basis >bad-email-3 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-3) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in commit $new.* - missing email" out
'

test_expect_success '> in name is reported' '
	git cat-file commit HEAD >basis &&
	sed "s/ </> </" basis >bad-email-4 &&
	new=$(git hash-object -t commit -w --stdin <bad-email-4) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in commit $new" out
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
	test_i18ngrep "error in commit $new.*integer overflow" out
'

test_expect_success 'commit with NUL in header' '
	git cat-file commit HEAD >basis &&
	sed "s/author ./author Q/" <basis | q_to_nul >commit-NUL-header &&
	new=$(git hash-object -t commit -w --stdin <commit-NUL-header) &&
	test_when_finished "remove_object $new" &&
	git update-ref refs/heads/bogus "$new" &&
	test_when_finished "git update-ref -d refs/heads/bogus" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in commit $new.*unterminated header: NUL at offset" out
'

test_expect_success 'tree object with duplicate entries' '
	test_when_finished "for i in \$T; do remove_object \$i; done" &&
	T=$(
		GIT_INDEX_FILE=test-index &&
		export GIT_INDEX_FILE &&
		rm -f test-index &&
		>x &&
		git add x &&
		git rev-parse :x &&
		T=$(git write-tree) &&
		echo $T &&
		(
			git cat-file tree $T &&
			git cat-file tree $T
		) |
		git hash-object -w -t tree --stdin
	) &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in tree .*contains duplicate file entries" out
'

check_duplicate_names () {
	expect=$1 &&
	shift &&
	names=$@ &&
	test_expect_$expect "tree object with duplicate names: $names" '
		test_when_finished "remove_object \$blob" &&
		test_when_finished "remove_object \$tree" &&
		test_when_finished "remove_object \$badtree" &&
		blob=$(echo blob | git hash-object -w --stdin) &&
		printf "100644 blob %s\t%s\n" $blob x.2 >tree &&
		tree=$(git mktree <tree) &&
		for name in $names
		do
			case "$name" in
			*/) printf "040000 tree %s\t%s\n" $tree "${name%/}" ;;
			*)  printf "100644 blob %s\t%s\n" $blob "$name" ;;
			esac
		done >badtree &&
		badtree=$(git mktree <badtree) &&
		test_must_fail git fsck 2>out &&
		test_i18ngrep "$badtree" out &&
		test_i18ngrep "error in tree .*contains duplicate file entries" out
	'
}

check_duplicate_names success x x.1 x/
check_duplicate_names success x x.1.2 x.1/ x/
check_duplicate_names success x x.1 x.1.2 x/

test_expect_success 'unparseable tree object' '
	test_oid_cache <<-\EOF &&
	junk sha1:twenty-bytes-of-junk
	junk sha256:twenty-bytes-of-junk-twelve-more
	EOF

	test_when_finished "git update-ref -d refs/heads/wrong" &&
	test_when_finished "remove_object \$tree_sha1" &&
	test_when_finished "remove_object \$commit_sha1" &&
	junk=$(test_oid junk) &&
	tree_sha1=$(printf "100644 \0$junk" | git hash-object -t tree --stdin -w --literally) &&
	commit_sha1=$(git commit-tree $tree_sha1) &&
	git update-ref refs/heads/wrong $commit_sha1 &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error: empty filename in tree entry" out &&
	test_i18ngrep "$tree_sha1" out &&
	test_i18ngrep ! "fatal: empty filename in tree entry" out
'

test_expect_success 'tree entry with type mismatch' '
	test_when_finished "remove_object \$blob" &&
	test_when_finished "remove_object \$tree" &&
	test_when_finished "remove_object \$commit" &&
	test_when_finished "git update-ref -d refs/heads/type_mismatch" &&
	blob=$(echo blob | git hash-object -w --stdin) &&
	blob_bin=$(echo $blob | hex2oct) &&
	tree=$(
		printf "40000 dir\0${blob_bin}100644 file\0${blob_bin}" |
		git hash-object -t tree --stdin -w --literally
	) &&
	commit=$(git commit-tree $tree) &&
	git update-ref refs/heads/type_mismatch $commit &&
	test_must_fail git fsck >out 2>&1 &&
	test_i18ngrep "is a blob, not a tree" out &&
	test_i18ngrep ! "dangling blob" out
'

test_expect_success 'tree entry with bogus mode' '
	test_when_finished "remove_object \$blob" &&
	test_when_finished "remove_object \$tree" &&
	blob=$(echo blob | git hash-object -w --stdin) &&
	blob_oct=$(echo $blob | hex2oct) &&
	tree=$(printf "100000 foo\0${blob_oct}" |
	       git hash-object -t tree --stdin -w --literally) &&
	git fsck 2>err &&
	cat >expect <<-EOF &&
	warning in tree $tree: badFilemode: contains bad file modes
	EOF
	test_cmp expect err
'

test_expect_success 'tag pointing to nonexistent' '
	badoid=$(test_oid deadbeef) &&
	cat >invalid-tag <<-EOF &&
	object $badoid
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
	test_i18ngrep "broken link" out
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
	test_i18ngrep "error in tag .*: invalid author/committer" out
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
	test_i18ngrep "error in tag $tag.*unterminated header: NUL at offset" out
'

test_expect_success 'cleaned up' '
	git fsck >actual 2>&1 &&
	test_must_be_empty actual
'

test_expect_success 'rev-list --verify-objects' '
	git rev-list --verify-objects --all >/dev/null 2>out &&
	test_must_be_empty out
'

test_expect_success 'rev-list --verify-objects with bad sha1' '
	sha=$(echo blob | git hash-object -w --stdin) &&
	old=$(test_oid_to_path $sha) &&
	new=$(dirname $old)/$(test_oid ff_2) &&
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
	test_i18ngrep -q "error: hash mismatch $(dirname $new)$(test_oid ff_2)" out
'

# An actual bit corruption is more likely than swapped commits, but
# this provides an easy way to have commits which don't match their purported
# hashes, but which aren't so broken we can't read them at all.
test_expect_success 'rev-list --verify-objects notices swapped commits' '
	git init swapped-commits &&
	(
		cd swapped-commits &&
		test_commit one &&
		test_commit two &&
		one_oid=$(git rev-parse HEAD) &&
		two_oid=$(git rev-parse HEAD^) &&
		one=.git/objects/$(test_oid_to_path $one_oid) &&
		two=.git/objects/$(test_oid_to_path $two_oid) &&
		mv $one tmp &&
		mv $two $one &&
		mv tmp $two &&
		test_must_fail git rev-list --verify-objects HEAD
	)
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
_bzoid=$(printf $ZERO_OID | sed -e 's/00/\\0/g')

test_expect_success 'fsck notices blob entry pointing to null sha1' '
	(git init null-blob &&
	 cd null-blob &&
	 sha=$(printf "100644 file$_bz$_bzoid" |
	       git hash-object -w --stdin -t tree) &&
	  git fsck 2>out &&
	  test_i18ngrep "warning.*null sha1" out
	)
'

test_expect_success 'fsck notices submodule entry pointing to null sha1' '
	(git init null-commit &&
	 cd null-commit &&
	 sha=$(printf "160000 submodule$_bz$_bzoid" |
	       git hash-object -w --stdin -t tree) &&
	  git fsck 2>out &&
	  test_i18ngrep "warning.*null sha1" out
	)
'

while read name path pretty; do
	while read mode type; do
		: ${pretty:=$path}
		test_expect_success "fsck notices $pretty as $type" '
		(
			git init $name-$type &&
			cd $name-$type &&
			git config core.protectNTFS false &&
			echo content >file &&
			git add file &&
			git commit -m base &&
			blob=$(git rev-parse :file) &&
			tree=$(git rev-parse HEAD^{tree}) &&
			value=$(eval "echo \$$type") &&
			printf "$mode $type %s\t%s" "$value" "$path" >bad &&
			bad_tree=$(git mktree <bad) &&
			git fsck 2>out &&
			test_i18ngrep "warning.*tree $bad_tree" out
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
		test_i18ngrep nulInCommit warn.1 &&
		git fsck 2>warn.2 &&
		test_i18ngrep nulInCommit warn.2
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

		# Drop the index now; we want to be sure that we
		# recursively notice the broken objects
		# because they are reachable from refs, not because
		# they are in the index.
		rm -f .git/index &&

		# corrupt the blob, but in a way that we can still identify
		# its type. That lets us see that --connectivity-only is
		# not actually looking at the contents, but leaves it
		# free to examine the type if it chooses.
		empty=.git/objects/$(test_oid_to_path $EMPTY_BLOB) &&
		blob=$(echo unrelated | git hash-object -w --stdin) &&
		mv -f $(sha1_file $blob) $empty &&

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

test_expect_success 'fsck --connectivity-only with explicit head' '
	rm -rf connectivity-only &&
	git init connectivity-only &&
	(
		cd connectivity-only &&
		test_commit foo &&
		rm -f .git/index &&
		tree=$(git rev-parse HEAD^{tree}) &&
		remove_object $(git rev-parse HEAD:foo.t) &&
		test_must_fail git fsck --connectivity-only $tree
	)
'

test_expect_success 'fsck --name-objects' '
	rm -rf name-objects &&
	git init name-objects &&
	(
		cd name-objects &&
		git config core.logAllRefUpdates false &&
		test_commit julius caesar.t &&
		test_commit augustus44 &&
		test_commit caesar  &&
		remove_object $(git rev-parse julius:caesar.t) &&
		tree=$(git rev-parse --verify julius:) &&
		git tag -d julius &&
		test_must_fail git fsck --name-objects >out &&
		test_i18ngrep "$tree (refs/tags/augustus44\\^:" out
	)
'

test_expect_success 'alternate objects are correctly blamed' '
	test_when_finished "rm -rf alt.git .git/objects/info/alternates" &&
	name=$(test_oid numeric) &&
	path=$(test_oid_to_path "$name") &&
	git init --bare alt.git &&
	echo "../../alt.git/objects" >.git/objects/info/alternates &&
	mkdir alt.git/objects/$(dirname $path) &&
	>alt.git/objects/$(dirname $path)/$(basename $path) &&
	test_must_fail git fsck >out 2>&1 &&
	test_i18ngrep alt.git out
'

test_expect_success 'fsck errors in packed objects' '
	git cat-file commit HEAD >basis &&
	sed "s/</one/" basis >one &&
	sed "s/</foo/" basis >two &&
	one=$(git hash-object -t commit -w one) &&
	two=$(git hash-object -t commit -w two) &&
	pack=$(
		{
			echo $one &&
			echo $two
		} | git pack-objects .git/objects/pack/pack
	) &&
	test_when_finished "rm -f .git/objects/pack/pack-$pack.*" &&
	remove_object $one &&
	remove_object $two &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "error in commit $one.* - bad name" out &&
	test_i18ngrep "error in commit $two.* - bad name" out &&
	! grep corrupt out
'

test_expect_success 'fsck fails on corrupt packfile' '
	hsh=$(git commit-tree -m mycommit HEAD^{tree}) &&
	pack=$(echo $hsh | git pack-objects .git/objects/pack/pack) &&

	# Corrupt the first byte of the first object. (It contains 3 type bits,
	# at least one of which is not zero, so setting the first byte to 0 is
	# sufficient.)
	chmod a+w .git/objects/pack/pack-$pack.pack &&
	printf "\0" | dd of=.git/objects/pack/pack-$pack.pack bs=1 conv=notrunc seek=12 &&

	test_when_finished "rm -f .git/objects/pack/pack-$pack.*" &&
	remove_object $hsh &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "checksum mismatch" out
'

test_expect_success 'fsck finds problems in duplicate loose objects' '
	rm -rf broken-duplicate &&
	git init broken-duplicate &&
	(
		cd broken-duplicate &&
		test_commit duplicate &&
		# no "-d" here, so we end up with duplicates
		git repack &&
		# now corrupt the loose copy
		oid="$(git rev-parse HEAD)" &&
		file=$(sha1_file "$oid") &&
		rm "$file" &&
		echo broken >"$file" &&
		test_must_fail git fsck 2>err &&

		cat >expect <<-EOF &&
		error: inflate: data stream error (incorrect header check)
		error: unable to unpack header of $file
		error: $oid: object corrupt or missing: $file
		EOF
		grep "^error: " err >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fsck detects trailing loose garbage (commit)' '
	git cat-file commit HEAD >basis &&
	echo bump-commit-sha1 >>basis &&
	commit=$(git hash-object -w -t commit basis) &&
	file=$(sha1_file $commit) &&
	test_when_finished "remove_object $commit" &&
	chmod +w "$file" &&
	echo garbage >>"$file" &&
	test_must_fail git fsck 2>out &&
	test_i18ngrep "garbage.*$commit" out
'

test_expect_success 'fsck detects trailing loose garbage (large blob)' '
	blob=$(echo trailing | git hash-object -w --stdin) &&
	file=$(sha1_file $blob) &&
	test_when_finished "remove_object $blob" &&
	chmod +w "$file" &&
	echo garbage >>"$file" &&
	test_must_fail git -c core.bigfilethreshold=5 fsck 2>out &&
	test_i18ngrep "garbage.*$blob" out
'

test_expect_success 'fsck detects truncated loose object' '
	# make it big enough that we know we will truncate in the data
	# portion, not the header
	test-tool genrandom truncate 4096 >file &&
	blob=$(git hash-object -w file) &&
	file=$(sha1_file $blob) &&
	test_when_finished "remove_object $blob" &&
	test_copy_bytes 1024 <"$file" >tmp &&
	rm "$file" &&
	mv -f tmp "$file" &&

	# check both regular and streaming code paths
	test_must_fail git fsck 2>out &&
	test_i18ngrep corrupt.*$blob out &&

	test_must_fail git -c core.bigfilethreshold=128 fsck 2>out &&
	test_i18ngrep corrupt.*$blob out
'

# for each of type, we have one version which is referenced by another object
# (and so while unreachable, not dangling), and another variant which really is
# dangling.
test_expect_success 'create dangling-object repository' '
	git init dangling &&
	(
		cd dangling &&
		blob=$(echo not-dangling | git hash-object -w --stdin) &&
		dblob=$(echo dangling | git hash-object -w --stdin) &&
		tree=$(printf "100644 blob %s\t%s\n" $blob one | git mktree) &&
		dtree=$(printf "100644 blob %s\t%s\n" $blob two | git mktree) &&
		commit=$(git commit-tree $tree) &&
		dcommit=$(git commit-tree -p $commit $tree) &&

		cat >expect <<-EOF
		dangling blob $dblob
		dangling commit $dcommit
		dangling tree $dtree
		EOF
	)
'

test_expect_success 'fsck notices dangling objects' '
	(
		cd dangling &&
		git fsck >actual &&
		# the output order is non-deterministic, as it comes from a hash
		sort <actual >actual.sorted &&
		test_cmp expect actual.sorted
	)
'

test_expect_success 'fsck --connectivity-only notices dangling objects' '
	(
		cd dangling &&
		git fsck --connectivity-only >actual &&
		# the output order is non-deterministic, as it comes from a hash
		sort <actual >actual.sorted &&
		test_cmp expect actual.sorted
	)
'

test_expect_success 'fsck $name notices bogus $name' '
	test_must_fail git fsck bogus &&
	test_must_fail git fsck $ZERO_OID
'

test_expect_success 'bogus head does not fallback to all heads' '
	# set up a case that will cause a reachability complaint
	echo to-be-deleted >foo &&
	git add foo &&
	blob=$(git rev-parse :foo) &&
	test_when_finished "git rm --cached foo" &&
	remove_object $blob &&
	test_must_fail git fsck $ZERO_OID >out 2>&1 &&
	! grep $blob out
'

# Corrupt the checksum on the index.
# Add 1 to the last byte in the SHA.
corrupt_index_checksum () {
    perl -w -e '
	use Fcntl ":seek";
	open my $fh, "+<", ".git/index" or die "open: $!";
	binmode $fh;
	seek $fh, -1, SEEK_END or die "seek: $!";
	read $fh, my $in_byte, 1 or die "read: $!";

	$in_value = unpack("C", $in_byte);
	$out_value = ($in_value + 1) & 255;

	$out_byte = pack("C", $out_value);

	seek $fh, -1, SEEK_END or die "seek: $!";
	print $fh $out_byte;
	close $fh or die "close: $!";
    '
}

# Corrupt the checksum on the index and then
# verify that only fsck notices.
test_expect_success 'detect corrupt index file in fsck' '
	cp .git/index .git/index.backup &&
	test_when_finished "mv .git/index.backup .git/index" &&
	corrupt_index_checksum &&
	test_must_fail git fsck --cache 2>errors &&
	test_i18ngrep "bad index file" errors
'

test_expect_success 'fsck error and recovery on invalid object type' '
	git init --bare garbage-type &&
	(
		cd garbage-type &&

		garbage_blob=$(git hash-object --stdin -w -t garbage --literally </dev/null) &&

		cat >err.expect <<-\EOF &&
		fatal: invalid object type
		EOF
		test_must_fail git fsck >out 2>err &&
		grep -e "^error" -e "^fatal" err >errors &&
		test_line_count = 1 errors &&
		grep "$garbage_blob: object is of unknown type '"'"'garbage'"'"':" err
	)
'

test_done
