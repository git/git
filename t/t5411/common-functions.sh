# Create commits in <repo> and assign each commit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_commits_in <repo> A B C
#
# NOTE: Never calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_commits_in () {
	repo="$1" &&
	if ! parent=$(git -C "$repo" rev-parse HEAD^{} --)
	then
		parent=
	fi &&
	T=$(git -C "$repo" write-tree) &&
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		test_tick &&
		if test -z "$parent"
		then
			oid=$(echo $name | git -C "$repo" commit-tree $T)
		else
			oid=$(echo $name | git -C "$repo" commit-tree -p $parent $T)
		fi &&
		eval $name=$oid &&
		parent=$oid &&
		shift ||
		return 1
	done &&
	git -C "$repo" update-ref refs/heads/main $oid
}

# Format the output of git-push, git-show-ref and other commands to make a
# user-friendly and stable text.  We can easily prepare the expect text
# without having to worry about future changes of the commit ID and spaces
# of the output.  Single quotes are replaced with double quotes, because
# it is boring to prepare unquoted single quotes in expect text.  We also
# remove some locale error messages, which break test if we turn on
# `GIT_TEST_GETTEXT_POISON=true` in order to test unintentional translations
# on plumbing commands.
make_user_friendly_and_stable_output () {
	sed \
		-e "s/  *\$//" \
		-e "s/   */ /g" \
		-e "s/'/\"/g" \
		-e "s/	/    /g" \
		-e "s/$A/<COMMIT-A>/g" \
		-e "s/$B/<COMMIT-B>/g" \
		-e "s/$TAG/<TAG-v123>/g" \
		-e "s/$ZERO_OID/<ZERO-OID>/g" \
		-e "s/$(echo $A | cut -c1-7)[0-9a-f]*/<OID-A>/g" \
		-e "s/$(echo $B | cut -c1-7)[0-9a-f]*/<OID-B>/g" \
		-e "s#To $URL_PREFIX/upstream.git#To <URL/of/upstream.git>#" \
		-e "/^error: / d"
}
