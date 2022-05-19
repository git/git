#!/bin/sh

test_description='but fsck random collection of tests

* (HEAD) B
* (main) A
'

. ./test-lib.sh

test_expect_success setup '
	but config gc.auto 0 &&
	but config i18n.cummitencoding ISO-8859-1 &&
	test_cummit A fileA one &&
	but config --unset i18n.cummitencoding &&
	but checkout HEAD^0 &&
	test_cummit B fileB two &&
	but tag -d A B &&
	but reflog expire --expire=now --all
'

test_expect_success 'loose objects borrowed from alternate are not missing' '
	mkdir another &&
	(
		cd another &&
		but init &&
		echo ../../../.but/objects >.but/objects/info/alternates &&
		test_cummit C fileC one &&
		but fsck --no-dangling >../actual 2>&1
	) &&
	test_must_be_empty actual
'

test_expect_success 'HEAD is part of refs, valid objects appear valid' '
	but fsck >actual 2>&1 &&
	test_must_be_empty actual
'

# Corruption tests follow.  Make sure to remove all traces of the
# specific corruption you test afterwards, lest a later test trip over
# it.

sha1_file () {
	but rev-parse --but-path objects/$(test_oid_to_path "$1")
}

remove_object () {
	rm "$(sha1_file "$1")"
}

test_expect_success 'object with hash mismatch' '
	but init --bare hash-mismatch &&
	(
		cd hash-mismatch &&

		oid=$(echo blob | but hash-object -w --stdin) &&
		oldoid=$oid &&
		old=$(test_oid_to_path "$oid") &&
		new=$(dirname $old)/$(test_oid ff_2) &&
		oid="$(dirname $new)$(basename $new)" &&

		mv objects/$old objects/$new &&
		but update-index --add --cacheinfo 100644 $oid foo &&
		tree=$(but write-tree) &&
		cmt=$(echo bogus | but cummit-tree $tree) &&
		but update-ref refs/heads/bogus $cmt &&

		test_must_fail but fsck 2>out &&
		grep "$oldoid: hash-path mismatch, found at: .*$new" out
	)
'

test_expect_success 'object with hash and type mismatch' '
	but init --bare hash-type-mismatch &&
	(
		cd hash-type-mismatch &&

		oid=$(echo blob | but hash-object -w --stdin -t garbage --literally) &&
		oldoid=$oid &&
		old=$(test_oid_to_path "$oid") &&
		new=$(dirname $old)/$(test_oid ff_2) &&
		oid="$(dirname $new)$(basename $new)" &&

		mv objects/$old objects/$new &&
		but update-index --add --cacheinfo 100644 $oid foo &&
		tree=$(but write-tree) &&
		cmt=$(echo bogus | but cummit-tree $tree) &&
		but update-ref refs/heads/bogus $cmt &&


		test_must_fail but fsck 2>out &&
		grep "^error: $oldoid: hash-path mismatch, found at: .*$new" out &&
		grep "^error: $oldoid: object is of unknown type '"'"'garbage'"'"'" out
	)
'

test_expect_success 'zlib corrupt loose object output ' '
	but init --bare corrupt-loose-output &&
	(
		cd corrupt-loose-output &&
		oid=$(but hash-object -w --stdin --literally </dev/null) &&
		oidf=objects/$(test_oid_to_path "$oid") &&
		chmod +w $oidf &&
		echo extra garbage >>$oidf &&

		cat >expect.error <<-EOF &&
		error: garbage at end of loose object '\''$oid'\''
		error: unable to unpack contents of ./$oidf
		error: $oid: object corrupt or missing: ./$oidf
		EOF
		test_must_fail but fsck 2>actual &&
		grep ^error: actual >error &&
		test_cmp expect.error error
	)
'

test_expect_success 'branch pointing to non-cummit' '
	but rev-parse HEAD^{tree} >.but/refs/heads/invalid &&
	test_when_finished "but update-ref -d refs/heads/invalid" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "not a cummit" out
'

test_expect_success 'HEAD link pointing at a funny object' '
	test_when_finished "mv .but/SAVED_HEAD .but/HEAD" &&
	mv .but/HEAD .but/SAVED_HEAD &&
	echo $ZERO_OID >.but/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env BUT_DIR=.but but fsck 2>out &&
	test_i18ngrep "detached HEAD points" out
'

test_expect_success 'HEAD link pointing at a funny place' '
	test_when_finished "mv .but/SAVED_HEAD .but/HEAD" &&
	mv .but/HEAD .but/SAVED_HEAD &&
	echo "ref: refs/funny/place" >.but/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail env BUT_DIR=.but but fsck 2>out &&
	test_i18ngrep "HEAD points to something strange" out
'

test_expect_success 'HEAD link pointing at a funny object (from different wt)' '
	test_when_finished "mv .but/SAVED_HEAD .but/HEAD" &&
	test_when_finished "rm -rf .but/worktrees wt" &&
	but worktree add wt &&
	mv .but/HEAD .but/SAVED_HEAD &&
	echo $ZERO_OID >.but/HEAD &&
	# avoid corrupt/broken HEAD from interfering with repo discovery
	test_must_fail but -C wt fsck 2>out &&
	test_i18ngrep "main-worktree/HEAD: detached HEAD points" out
'

test_expect_success 'other worktree HEAD link pointing at a funny object' '
	test_when_finished "rm -rf .but/worktrees other" &&
	but worktree add other &&
	echo $ZERO_OID >.but/worktrees/other/HEAD &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD: detached HEAD points" out
'

test_expect_success 'other worktree HEAD link pointing at missing object' '
	test_when_finished "rm -rf .but/worktrees other" &&
	but worktree add other &&
	echo "Contents missing from repo" | but hash-object --stdin >.but/worktrees/other/HEAD &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD: invalid sha1 pointer" out
'

test_expect_success 'other worktree HEAD link pointing at a funny place' '
	test_when_finished "rm -rf .but/worktrees other" &&
	but worktree add other &&
	echo "ref: refs/funny/place" >.but/worktrees/other/HEAD &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "worktrees/other/HEAD points to something strange" out
'

test_expect_success 'cummit with multiple signatures is okay' '
	but cat-file commit HEAD >basis &&
	cat >sigs <<-EOF &&
	gpgsig -----BEGIN PGP SIGNATURE-----
	  VGhpcyBpcyBub3QgcmVhbGx5IGEgc2lnbmF0dXJlLg==
	  -----END PGP SIGNATURE-----
	gpgsig-sha256 -----BEGIN PGP SIGNATURE-----
	  VGhpcyBpcyBub3QgcmVhbGx5IGEgc2lnbmF0dXJlLg==
	  -----END PGP SIGNATURE-----
	EOF
	sed -e "/^cummitter/q" basis >okay &&
	cat sigs >>okay &&
	echo >>okay &&
	sed -e "1,/^$/d" basis >>okay &&
	cat okay &&
	new=$(but hash-object -t cummit -w --stdin <okay) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	but fsck 2>out &&
	cat out &&
	! grep "cummit $new" out
'

test_expect_success 'email without @ is okay' '
	but cat-file commit HEAD >basis &&
	sed "s/@/AT/" basis >okay &&
	new=$(but hash-object -t cummit -w --stdin <okay) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	but fsck 2>out &&
	! grep "cummit $new" out
'

test_expect_success 'email with embedded > is not okay' '
	but cat-file commit HEAD >basis &&
	sed "s/@[a-z]/&>/" basis >bad-email &&
	new=$(but hash-object -t cummit -w --stdin <bad-email) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new" out
'

test_expect_success 'missing < email delimiter is reported nicely' '
	but cat-file commit HEAD >basis &&
	sed "s/<//" basis >bad-email-2 &&
	new=$(but hash-object -t cummit -w --stdin <bad-email-2) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new.* - bad name" out
'

test_expect_success 'missing email is reported nicely' '
	but cat-file commit HEAD >basis &&
	sed "s/[a-z]* <[^>]*>//" basis >bad-email-3 &&
	new=$(but hash-object -t cummit -w --stdin <bad-email-3) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new.* - missing email" out
'

test_expect_success '> in name is reported' '
	but cat-file commit HEAD >basis &&
	sed "s/ </> </" basis >bad-email-4 &&
	new=$(but hash-object -t cummit -w --stdin <bad-email-4) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new" out
'

# date is 2^64 + 1
test_expect_success 'integer overflow in timestamps is reported' '
	but cat-file commit HEAD >basis &&
	sed "s/^\\(author .*>\\) [0-9]*/\\1 18446744073709551617/" \
		<basis >bad-timestamp &&
	new=$(but hash-object -t cummit -w --stdin <bad-timestamp) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new.*integer overflow" out
'

test_expect_success 'cummit with NUL in header' '
	but cat-file commit HEAD >basis &&
	sed "s/author ./author Q/" <basis | q_to_nul >CUMMIT-NUL-header &&
	new=$(but hash-object -t cummit -w --stdin <CUMMIT-NUL-header) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $new.*unterminated header: NUL at offset" out
'

test_expect_success 'tree object with duplicate entries' '
	test_when_finished "for i in \$T; do remove_object \$i; done" &&
	T=$(
		BUT_INDEX_FILE=test-index &&
		export BUT_INDEX_FILE &&
		rm -f test-index &&
		>x &&
		but add x &&
		but rev-parse :x &&
		T=$(but write-tree) &&
		echo $T &&
		(
			but cat-file tree $T &&
			but cat-file tree $T
		) |
		but hash-object -w -t tree --stdin
	) &&
	test_must_fail but fsck 2>out &&
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
		blob=$(echo blob | but hash-object -w --stdin) &&
		printf "100644 blob %s\t%s\n" $blob x.2 >tree &&
		tree=$(but mktree <tree) &&
		for name in $names
		do
			case "$name" in
			*/) printf "040000 tree %s\t%s\n" $tree "${name%/}" ;;
			*)  printf "100644 blob %s\t%s\n" $blob "$name" ;;
			esac
		done >badtree &&
		badtree=$(but mktree <badtree) &&
		test_must_fail but fsck 2>out &&
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

	test_when_finished "but update-ref -d refs/heads/wrong" &&
	test_when_finished "remove_object \$tree_sha1" &&
	test_when_finished "remove_object \$cummit_sha1" &&
	junk=$(test_oid junk) &&
	tree_sha1=$(printf "100644 \0$junk" | but hash-object -t tree --stdin -w --literally) &&
	cummit_sha1=$(but cummit-tree $tree_sha1) &&
	but update-ref refs/heads/wrong $cummit_sha1 &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error: empty filename in tree entry" out &&
	test_i18ngrep "$tree_sha1" out &&
	test_i18ngrep ! "fatal: empty filename in tree entry" out
'

test_expect_success 'tree entry with type mismatch' '
	test_when_finished "remove_object \$blob" &&
	test_when_finished "remove_object \$tree" &&
	test_when_finished "remove_object \$cummit" &&
	test_when_finished "but update-ref -d refs/heads/type_mismatch" &&
	blob=$(echo blob | but hash-object -w --stdin) &&
	blob_bin=$(echo $blob | hex2oct) &&
	tree=$(
		printf "40000 dir\0${blob_bin}100644 file\0${blob_bin}" |
		but hash-object -t tree --stdin -w --literally
	) &&
	cummit=$(but cummit-tree $tree) &&
	but update-ref refs/heads/type_mismatch $cummit &&
	test_must_fail but fsck >out 2>&1 &&
	test_i18ngrep "is a blob, not a tree" out &&
	test_i18ngrep ! "dangling blob" out
'

test_expect_success 'tag pointing to nonexistent' '
	badoid=$(test_oid deadbeef) &&
	cat >invalid-tag <<-EOF &&
	object $badoid
	type cummit
	tag invalid
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(but hash-object -t tag -w --stdin <invalid-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.but/refs/tags/invalid &&
	test_when_finished "but update-ref -d refs/tags/invalid" &&
	test_must_fail but fsck --tags >out &&
	test_i18ngrep "broken link" out
'

test_expect_success 'tag pointing to something else than its type' '
	sha=$(echo blob | but hash-object -w --stdin) &&
	test_when_finished "remove_object $sha" &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type cummit
	tag wrong
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(but hash-object -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.but/refs/tags/wrong &&
	test_when_finished "but update-ref -d refs/tags/wrong" &&
	test_must_fail but fsck --tags
'

test_expect_success 'tag with incorrect tag name & missing tagger' '
	sha=$(but rev-parse HEAD) &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type cummit
	tag wrong name format

	This is an invalid tag.
	EOF

	tag=$(but hash-object -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.but/refs/tags/wrong &&
	test_when_finished "but update-ref -d refs/tags/wrong" &&
	but fsck --tags 2>out &&

	cat >expect <<-EOF &&
	warning in tag $tag: badTagName: invalid '\''tag'\'' name: wrong name format
	warning in tag $tag: missingTaggerEntry: invalid format - expected '\''tagger'\'' line
	EOF
	test_cmp expect out
'

test_expect_success 'tag with bad tagger' '
	sha=$(but rev-parse HEAD) &&
	cat >wrong-tag <<-EOF &&
	object $sha
	type cummit
	tag not-quite-wrong
	tagger Bad Tagger Name

	This is an invalid tag.
	EOF

	tag=$(but hash-object --literally -t tag -w --stdin <wrong-tag) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.but/refs/tags/wrong &&
	test_when_finished "but update-ref -d refs/tags/wrong" &&
	test_must_fail but fsck --tags 2>out &&
	test_i18ngrep "error in tag .*: invalid author/cummitter" out
'

test_expect_success 'tag with NUL in header' '
	sha=$(but rev-parse HEAD) &&
	q_to_nul >tag-NUL-header <<-EOF &&
	object $sha
	type cummit
	tag contains-Q-in-header
	tagger T A Gger <tagger@example.com> 1234567890 -0000

	This is an invalid tag.
	EOF

	tag=$(but hash-object --literally -t tag -w --stdin <tag-NUL-header) &&
	test_when_finished "remove_object $tag" &&
	echo $tag >.but/refs/tags/wrong &&
	test_when_finished "but update-ref -d refs/tags/wrong" &&
	test_must_fail but fsck --tags 2>out &&
	test_i18ngrep "error in tag $tag.*unterminated header: NUL at offset" out
'

test_expect_success 'cleaned up' '
	but fsck >actual 2>&1 &&
	test_must_be_empty actual
'

test_expect_success 'rev-list --verify-objects' '
	but rev-list --verify-objects --all >/dev/null 2>out &&
	test_must_be_empty out
'

test_expect_success 'rev-list --verify-objects with bad sha1' '
	sha=$(echo blob | but hash-object -w --stdin) &&
	old=$(test_oid_to_path $sha) &&
	new=$(dirname $old)/$(test_oid ff_2) &&
	sha="$(dirname $new)$(basename $new)" &&
	mv .but/objects/$old .but/objects/$new &&
	test_when_finished "remove_object $sha" &&
	but update-index --add --cacheinfo 100644 $sha foo &&
	test_when_finished "but read-tree -u --reset HEAD" &&
	tree=$(but write-tree) &&
	test_when_finished "remove_object $tree" &&
	cmt=$(echo bogus | but cummit-tree $tree) &&
	test_when_finished "remove_object $cmt" &&
	but update-ref refs/heads/bogus $cmt &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&

	test_might_fail but rev-list --verify-objects refs/heads/bogus >/dev/null 2>out &&
	test_i18ngrep -q "error: hash mismatch $(dirname $new)$(test_oid ff_2)" out
'

test_expect_success 'force fsck to ignore double author' '
	but cat-file commit HEAD >basis &&
	sed "s/^author .*/&,&/" <basis | tr , \\n >multiple-authors &&
	new=$(but hash-object -t cummit -w --stdin <multiple-authors) &&
	test_when_finished "remove_object $new" &&
	but update-ref refs/heads/bogus "$new" &&
	test_when_finished "but update-ref -d refs/heads/bogus" &&
	test_must_fail but fsck &&
	but -c fsck.multipleAuthors=ignore fsck
'

_bz='\0'
_bzoid=$(printf $ZERO_OID | sed -e 's/00/\\0/g')

test_expect_success 'fsck notices blob entry pointing to null sha1' '
	(but init null-blob &&
	 cd null-blob &&
	 sha=$(printf "100644 file$_bz$_bzoid" |
	       but hash-object -w --stdin -t tree) &&
	  but fsck 2>out &&
	  test_i18ngrep "warning.*null sha1" out
	)
'

test_expect_success 'fsck notices submodule entry pointing to null sha1' '
	(but init null-cummit &&
	 cd null-cummit &&
	 sha=$(printf "160000 submodule$_bz$_bzoid" |
	       but hash-object -w --stdin -t tree) &&
	  but fsck 2>out &&
	  test_i18ngrep "warning.*null sha1" out
	)
'

while read name path pretty; do
	while read mode type; do
		: ${pretty:=$path}
		test_expect_success "fsck notices $pretty as $type" '
		(
			but init $name-$type &&
			cd $name-$type &&
			but config core.protectNTFS false &&
			echo content >file &&
			but add file &&
			but cummit -m base &&
			blob=$(but rev-parse :file) &&
			tree=$(but rev-parse HEAD^{tree}) &&
			value=$(eval "echo \$$type") &&
			printf "$mode $type %s\t%s" "$value" "$path" >bad &&
			bad_tree=$(but mktree <bad) &&
			but fsck 2>out &&
			test_i18ngrep "warning.*tree $bad_tree" out
		)'
	done <<-\EOF
	100644 blob
	040000 tree
	EOF
done <<-EOF
dot .
dotdot ..
dotbut .but
dotbut-case .BUT
dotbut-unicode .gI${u200c}T .gI{u200c}T
dotbut-case2 .Git
but-tilde1 but~1
dotbutdot .but.
dot-backslash-case .\\\\.BUT\\\\foobar
dotbut-case-backslash .but\\\\foobar
EOF

test_expect_success 'fsck allows .Ňit' '
	(
		but init not-dotbut &&
		cd not-dotbut &&
		echo content >file &&
		but add file &&
		but cummit -m base &&
		blob=$(but rev-parse :file) &&
		printf "100644 blob $blob\t.\\305\\207it" >tree &&
		tree=$(but mktree <tree) &&
		but fsck 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'NUL in cummit' '
	rm -fr nul-in-cummit &&
	but init nul-in-cummit &&
	(
		cd nul-in-cummit &&
		but cummit --allow-empty -m "initial cummitQNUL after message" &&
		but cat-file commit HEAD >original &&
		q_to_nul <original >munged &&
		but hash-object -w -t cummit --stdin <munged >name &&
		but branch bad $(cat name) &&

		test_must_fail but -c fsck.nulIncummit=error fsck 2>warn.1 &&
		test_i18ngrep nulIncummit warn.1 &&
		but fsck 2>warn.2 &&
		test_i18ngrep nulIncummit warn.2
	)
'

# create a static test repo which is broken by omitting
# one particular object ($1, which is looked up via rev-parse
# in the new repository).
create_repo_missing () {
	rm -rf missing &&
	but init missing &&
	(
		cd missing &&
		but cummit -m one --allow-empty &&
		mkdir subdir &&
		echo content >subdir/file &&
		but add subdir/file &&
		but cummit -m two &&
		unrelated=$(echo unrelated | but hash-object --stdin -w) &&
		but tag -m foo tag $unrelated &&
		sha1=$(but rev-parse --verify "$1") &&
		path=$(echo $sha1 | sed 's|..|&/|') &&
		rm .but/objects/$path
	)
}

test_expect_success 'fsck notices missing blob' '
	create_repo_missing HEAD:subdir/file &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices missing subtree' '
	create_repo_missing HEAD:subdir &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices missing root tree' '
	create_repo_missing HEAD^{tree} &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices missing parent' '
	create_repo_missing HEAD^ &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices missing tagged object' '
	create_repo_missing tag^{blob} &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices ref pointing to missing cummit' '
	create_repo_missing HEAD &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck notices ref pointing to missing tag' '
	create_repo_missing tag &&
	test_must_fail but -C missing fsck
'

test_expect_success 'fsck --connectivity-only' '
	rm -rf connectivity-only &&
	but init connectivity-only &&
	(
		cd connectivity-only &&
		touch empty &&
		but add empty &&
		test_cummit empty &&

		# Drop the index now; we want to be sure that we
		# recursively notice the broken objects
		# because they are reachable from refs, not because
		# they are in the index.
		rm -f .but/index &&

		# corrupt the blob, but in a way that we can still identify
		# its type. That lets us see that --connectivity-only is
		# not actually looking at the contents, but leaves it
		# free to examine the type if it chooses.
		empty=.but/objects/$(test_oid_to_path $EMPTY_BLOB) &&
		blob=$(echo unrelated | but hash-object -w --stdin) &&
		mv -f $(sha1_file $blob) $empty &&

		test_must_fail but fsck --strict &&
		but fsck --strict --connectivity-only &&
		tree=$(but rev-parse HEAD:) &&
		suffix=${tree#??} &&
		tree=.but/objects/${tree%$suffix}/$suffix &&
		rm -f $tree &&
		echo invalid >$tree &&
		test_must_fail but fsck --strict --connectivity-only
	)
'

test_expect_success 'fsck --connectivity-only with explicit head' '
	rm -rf connectivity-only &&
	but init connectivity-only &&
	(
		cd connectivity-only &&
		test_cummit foo &&
		rm -f .but/index &&
		tree=$(but rev-parse HEAD^{tree}) &&
		remove_object $(but rev-parse HEAD:foo.t) &&
		test_must_fail but fsck --connectivity-only $tree
	)
'

test_expect_success 'fsck --name-objects' '
	rm -rf name-objects &&
	but init name-objects &&
	(
		cd name-objects &&
		but config core.logAllRefUpdates false &&
		test_cummit julius caesar.t &&
		test_cummit augustus44 &&
		test_cummit caesar  &&
		remove_object $(but rev-parse julius:caesar.t) &&
		tree=$(but rev-parse --verify julius:) &&
		but tag -d julius &&
		test_must_fail but fsck --name-objects >out &&
		test_i18ngrep "$tree (refs/tags/augustus44\\^:" out
	)
'

test_expect_success 'alternate objects are correctly blamed' '
	test_when_finished "rm -rf alt.but .but/objects/info/alternates" &&
	name=$(test_oid numeric) &&
	path=$(test_oid_to_path "$name") &&
	but init --bare alt.but &&
	echo "../../alt.but/objects" >.but/objects/info/alternates &&
	mkdir alt.but/objects/$(dirname $path) &&
	>alt.but/objects/$(dirname $path)/$(basename $path) &&
	test_must_fail but fsck >out 2>&1 &&
	test_i18ngrep alt.but out
'

test_expect_success 'fsck errors in packed objects' '
	but cat-file commit HEAD >basis &&
	sed "s/</one/" basis >one &&
	sed "s/</foo/" basis >two &&
	one=$(but hash-object -t cummit -w one) &&
	two=$(but hash-object -t cummit -w two) &&
	pack=$(
		{
			echo $one &&
			echo $two
		} | but pack-objects .but/objects/pack/pack
	) &&
	test_when_finished "rm -f .but/objects/pack/pack-$pack.*" &&
	remove_object $one &&
	remove_object $two &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "error in cummit $one.* - bad name" out &&
	test_i18ngrep "error in cummit $two.* - bad name" out &&
	! grep corrupt out
'

test_expect_success 'fsck fails on corrupt packfile' '
	hsh=$(but cummit-tree -m mycommit HEAD^{tree}) &&
	pack=$(echo $hsh | but pack-objects .but/objects/pack/pack) &&

	# Corrupt the first byte of the first object. (It contains 3 type bits,
	# at least one of which is not zero, so setting the first byte to 0 is
	# sufficient.)
	chmod a+w .but/objects/pack/pack-$pack.pack &&
	printf "\0" | dd of=.but/objects/pack/pack-$pack.pack bs=1 conv=notrunc seek=12 &&

	test_when_finished "rm -f .but/objects/pack/pack-$pack.*" &&
	remove_object $hsh &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "checksum mismatch" out
'

test_expect_success 'fsck finds problems in duplicate loose objects' '
	rm -rf broken-duplicate &&
	but init broken-duplicate &&
	(
		cd broken-duplicate &&
		test_cummit duplicate &&
		# no "-d" here, so we end up with duplicates
		but repack &&
		# now corrupt the loose copy
		file=$(sha1_file "$(but rev-parse HEAD)") &&
		rm "$file" &&
		echo broken >"$file" &&
		test_must_fail but fsck
	)
'

test_expect_success 'fsck detects trailing loose garbage (cummit)' '
	but cat-file commit HEAD >basis &&
	echo bump-cummit-sha1 >>basis &&
	cummit=$(but hash-object -w -t cummit basis) &&
	file=$(sha1_file $cummit) &&
	test_when_finished "remove_object $cummit" &&
	chmod +w "$file" &&
	echo garbage >>"$file" &&
	test_must_fail but fsck 2>out &&
	test_i18ngrep "garbage.*$cummit" out
'

test_expect_success 'fsck detects trailing loose garbage (large blob)' '
	blob=$(echo trailing | but hash-object -w --stdin) &&
	file=$(sha1_file $blob) &&
	test_when_finished "remove_object $blob" &&
	chmod +w "$file" &&
	echo garbage >>"$file" &&
	test_must_fail but -c core.bigfilethreshold=5 fsck 2>out &&
	test_i18ngrep "garbage.*$blob" out
'

test_expect_success 'fsck detects truncated loose object' '
	# make it big enough that we know we will truncate in the data
	# portion, not the header
	test-tool genrandom truncate 4096 >file &&
	blob=$(but hash-object -w file) &&
	file=$(sha1_file $blob) &&
	test_when_finished "remove_object $blob" &&
	test_copy_bytes 1024 <"$file" >tmp &&
	rm "$file" &&
	mv -f tmp "$file" &&

	# check both regular and streaming code paths
	test_must_fail but fsck 2>out &&
	test_i18ngrep corrupt.*$blob out &&

	test_must_fail but -c core.bigfilethreshold=128 fsck 2>out &&
	test_i18ngrep corrupt.*$blob out
'

# for each of type, we have one version which is referenced by another object
# (and so while unreachable, not dangling), and another variant which really is
# dangling.
test_expect_success 'create dangling-object repository' '
	but init dangling &&
	(
		cd dangling &&
		blob=$(echo not-dangling | but hash-object -w --stdin) &&
		dblob=$(echo dangling | but hash-object -w --stdin) &&
		tree=$(printf "100644 blob %s\t%s\n" $blob one | but mktree) &&
		dtree=$(printf "100644 blob %s\t%s\n" $blob two | but mktree) &&
		cummit=$(but cummit-tree $tree) &&
		dcummit=$(but cummit-tree -p $cummit $tree) &&

		cat >expect <<-EOF
		dangling blob $dblob
		dangling cummit $dcummit
		dangling tree $dtree
		EOF
	)
'

test_expect_success 'fsck notices dangling objects' '
	(
		cd dangling &&
		but fsck >actual &&
		# the output order is non-deterministic, as it comes from a hash
		sort <actual >actual.sorted &&
		test_cmp expect actual.sorted
	)
'

test_expect_success 'fsck --connectivity-only notices dangling objects' '
	(
		cd dangling &&
		but fsck --connectivity-only >actual &&
		# the output order is non-deterministic, as it comes from a hash
		sort <actual >actual.sorted &&
		test_cmp expect actual.sorted
	)
'

test_expect_success 'fsck $name notices bogus $name' '
	test_must_fail but fsck bogus &&
	test_must_fail but fsck $ZERO_OID
'

test_expect_success 'bogus head does not fallback to all heads' '
	# set up a case that will cause a reachability complaint
	echo to-be-deleted >foo &&
	but add foo &&
	blob=$(but rev-parse :foo) &&
	test_when_finished "but rm --cached foo" &&
	remove_object $blob &&
	test_must_fail but fsck $ZERO_OID >out 2>&1 &&
	! grep $blob out
'

# Corrupt the checksum on the index.
# Add 1 to the last byte in the SHA.
corrupt_index_checksum () {
    perl -w -e '
	use Fcntl ":seek";
	open my $fh, "+<", ".but/index" or die "open: $!";
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
	cp .but/index .but/index.backup &&
	test_when_finished "mv .but/index.backup .but/index" &&
	corrupt_index_checksum &&
	test_must_fail but fsck --cache 2>errors &&
	test_i18ngrep "bad index file" errors
'

test_expect_success 'fsck error and recovery on invalid object type' '
	but init --bare garbage-type &&
	(
		cd garbage-type &&

		garbage_blob=$(but hash-object --stdin -w -t garbage --literally </dev/null) &&

		cat >err.expect <<-\EOF &&
		fatal: invalid object type
		EOF
		test_must_fail but fsck >out 2>err &&
		grep -e "^error" -e "^fatal" err >errors &&
		test_line_count = 1 errors &&
		grep "$garbage_blob: object is of unknown type '"'"'garbage'"'"':" err
	)
'

test_done
