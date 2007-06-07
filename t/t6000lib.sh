[ -d .git/refs/tags ] || mkdir -p .git/refs/tags

:> sed.script

# Answer the sha1 has associated with the tag. The tag must exist in .git or .git/refs/tags
tag()
{
	_tag=$1
	[ -f .git/refs/tags/$_tag ] || error "tag: \"$_tag\" does not exist"
	cat .git/refs/tags/$_tag
}

# Generate a commit using the text specified to make it unique and the tree
# named by the tag specified.
unique_commit()
{
	_text=$1
        _tree=$2
	shift 2
	echo $_text | git-commit-tree $(tag $_tree) "$@"
}

# Save the output of a command into the tag specified. Prepend
# a substitution script for the tag onto the front of sed.script
save_tag()
{
	_tag=$1
	[ -n "$_tag" ] || error "usage: save_tag tag commit-args ..."
	shift 1
	"$@" >.git/refs/tags/$_tag

        echo "s/$(tag $_tag)/$_tag/g" > sed.script.tmp
	cat sed.script >> sed.script.tmp
	rm sed.script
	mv sed.script.tmp sed.script
}

# Replace unhelpful sha1 hashses with their symbolic equivalents
entag()
{
	sed -f sed.script
}

# Execute a command after first saving, then setting the GIT_AUTHOR_EMAIL
# tag to a specified value. Restore the original value on return.
as_author()
{
	_author=$1
	shift 1
        _save=$GIT_AUTHOR_EMAIL

	export GIT_AUTHOR_EMAIL="$_author"
	"$@"
	if test -z "$_save"
	then
		unset GIT_AUTHOR_EMAIL
	else
		export GIT_AUTHOR_EMAIL="$_save"
	fi
}

commit_date()
{
        _commit=$1
	git-cat-file commit $_commit | sed -n "s/^committer .*> \([0-9]*\) .*/\1/p"
}

on_committer_date()
{
    _date=$1
    shift 1
    export GIT_COMMITTER_DATE="$_date"
    "$@"
    unset GIT_COMMITTER_DATE
}

# Execute a command and suppress any error output.
hide_error()
{
	"$@" 2>/dev/null
}

check_output()
{
	_name=$1
	shift 1
	if eval "$*" | entag > $_name.actual
	then
		diff $_name.expected $_name.actual
	else
		return 1;
	fi
}

# Turn a reasonable test description into a reasonable test name.
# All alphanums translated into -'s which are then compressed and stripped
# from front and back.
name_from_description()
{
        tr "'" '-' | tr '~`!@#$%^&*()_+={}[]|\;:"<>,/? ' '-' | tr -s '-' | tr '[A-Z]' '[a-z]' | sed "s/^-*//;s/-*\$//"
}


# Execute the test described by the first argument, by eval'ing
# command line specified in the 2nd argument. Check the status code
# is zero and that the output matches the stream read from
# stdin.
test_output_expect_success()
{
	_description=$1
        _test=$2
        [ $# -eq 2 ] || error "usage: test_output_expect_success description test <<EOF ... EOF"
        _name=$(echo $_description | name_from_description)
	cat > $_name.expected
	test_expect_success "$_description" "check_output $_name \"$_test\""
}
