# Library of git-bundle related functions.

# Display the pack data contained in the bundle file, bypassing the
# header that contains the signature, prerequisites and references.
convert_bundle_to_pack () {
	while read x && test -n "$x"
	do
		:;
	done
	cat
}

# Check count of objects in a bundle file.
# We can use "--thin" opiton to check thin pack, which must be fixed by
# command `git-index-pack --fix-thin --stdin`.
test_bundle_object_count () {
	thin=
	if test "$1" = "--thin"
	then
		thin=t
		shift
	fi
	if test $# -ne 2
	then
		echo >&2 "args should be: <bundle> <count>"
		return 1
	fi
	bundle=$1
	pack=$bundle.pack
	convert_bundle_to_pack <"$bundle" >"$pack" &&
	if test -n "$thin"
	then
		mv "$pack" "$bundle.thin.pack" &&
		git index-pack --stdin --fix-thin "$pack" <"$bundle.thin.pack"
	else
		git index-pack "$pack"
	fi || return 1
	count=$(git show-index <"${pack%pack}idx" | wc -l) &&
	test $2 = $count && return 0
	echo >&2 "error: object count for $bundle is $count, not $2"
	return 1
}
