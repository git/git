# Create commits in <repo> and assign each commit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_commits_in <repo> A B C
#
# NOTE: Never calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_commits_in () {
	repo="$1" && test -d "$repo" ||
	error "Repository $repo does not exist."
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		shift &&
		test_commit -C "$repo" --no-tag "$name" &&
		eval $name=$(git -C "$repo" rev-parse HEAD)
	done
}

get_abbrev_oid () {
	oid=$1 &&
	suffix=${oid#???????} &&
	oid=${oid%$suffix} &&
	if test -n "$oid"
	then
		echo "$oid"
	else
		echo "undefined-oid"
	fi
}

# Format the output of git-fetch, git-ls-remote and other commands to make a
# user-friendly and stable text.  We can easily prepare the expect text
# without having to worry about changes of the commit ID (full or abbrev.)
# of the output.  Single quotes are replaced with double quotes, because
# it is boring to prepare unquoted single quotes in expect text.
make_user_friendly_and_stable_output () {
	tr '\0' '@' | sed \
		-e "s/'/\"/g" \
		-e "s/@.*//g" \
		-e "s/$(get_abbrev_oid $A)[0-9a-f]*/<COMMIT-A>/g" \
		-e "s/$(get_abbrev_oid $B)[0-9a-f]*/<COMMIT-B>/g" \
		-e "s/$(get_abbrev_oid $C)[0-9a-f]*/<COMMIT-C>/g" \
		-e "s/$(get_abbrev_oid $D)[0-9a-f]*/<COMMIT-D>/g" \
		-e "s/$(get_abbrev_oid $TAG)[0-9a-f]*/<COMMIT-TAG-v123>/g" \
		-e "s/$ZERO_OID/<ZERO-OID>/g" \
		-e "s#$BAREREPO_PREFIX/bare_repo.git#<URL/of/bare_repo.git>#" \
		-e 's/^[0-9a-f]\{4\}//g'

}

filter_out_hide_refs_output() {
	make_user_friendly_and_stable_output | sed 's/^[0-9a-f]\{4\}//g'
}

format_and_save_expect () {
	sed -e 's/^> //' -e 's/Z$//' >expect
}

test_cmp_refs () {
	indir=
	if test "$1" = "-C"
	then
		shift
		indir="$1"
		shift
	fi
	indir=${indir:+"$indir"/}
	cat >show-ref.expect &&
	git ${indir:+ -C "$indir"} show-ref >show-ref.pristine &&
	make_user_friendly_and_stable_output <show-ref.pristine >show-ref.filtered &&
	test_cmp show-ref.expect show-ref.filtered
}
