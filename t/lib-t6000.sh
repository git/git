: included from 6002 and others

>sed.script

# Answer the sha1 has associated with the tag. The tag must exist under refs/tags
tag () {
	_tag=$1
	but rev-parse --verify "refs/tags/$_tag" ||
	error "tag: \"$_tag\" does not exist"
}

# Generate a cummit using the text specified to make it unique and the tree
# named by the tag specified.
unique_cummit () {
	_text=$1
	_tree=$2
	shift 2
	echo "$_text" | but cummit-tree $(tag "$_tree") "$@"
}

# Save the output of a command into the tag specified. Prepend
# a substitution script for the tag onto the front of sed.script
save_tag () {
	_tag=$1
	test -n "$_tag" || error "usage: save_tag tag cummit-args ..."
	shift 1

	but update-ref "refs/tags/$_tag" $("$@")

	echo "s/$(tag $_tag)/$_tag/g" >sed.script.tmp
	cat sed.script >>sed.script.tmp
	rm sed.script
	mv sed.script.tmp sed.script
}

# Replace unhelpful sha1 hashes with their symbolic equivalents
entag () {
	sed -f sed.script
}

# Execute a command after first saving, then setting the BUT_AUTHOR_EMAIL
# tag to a specified value. Restore the original value on return.
as_author () {
	_author=$1
	shift 1
	_save=$BUT_AUTHOR_EMAIL

	BUT_AUTHOR_EMAIL="$_author"
	export BUT_AUTHOR_EMAIL
	"$@"
	if test -z "$_save"
	then
		unset BUT_AUTHOR_EMAIL
	else
		BUT_AUTHOR_EMAIL="$_save"
		export BUT_AUTHOR_EMAIL
	fi
}

cummit_date () {
	_cummit=$1
	but cat-file cummit $_cummit |
	sed -n "s/^cummitter .*> \([0-9]*\) .*/\1/p"
}

# Assign the value of fake date to a variable, but
# allow fairly common "1971-08-16 00:00" to be omittd
assign_fake_date () {
	case "$2" in
	??:??:??)	eval "$1='1971-08-16 $2'" ;;
	??:??)		eval "$1='1971-08-16 00:$2'" ;;
	??)		eval "$1='1971-08-16 00:00:$2'" ;;
	*)		eval "$1='$2'" ;;
	esac
}

on_cummitter_date () {
	assign_fake_date BUT_CUMMITTER_DATE "$1"
	export BUT_CUMMITTER_DATE
	shift 1
	"$@"
}

on_dates () {
	assign_fake_date BUT_CUMMITTER_DATE "$1"
	assign_fake_date BUT_AUTHOR_DATE "$2"
	export BUT_CUMMITTER_DATE BUT_AUTHOR_DATE
	shift 2
	"$@"
}

# Execute a command and suppress any error output.
hide_error () {
	"$@" 2>/dev/null
}

check_output () {
	_name=$1
	shift 1
	if eval "$*" | entag >"$_name.actual"
	then
		test_cmp "$_name.expected" "$_name.actual"
	else
		return 1
	fi
}

# Turn a reasonable test description into a reasonable test name.
# All alphanums translated into -'s which are then compressed and stripped
# from front and back.
name_from_description () {
	perl -pe '
		s/[^A-Za-z0-9.]/-/g;
		s/-+/-/g;
		s/-$//;
		s/^-//;
		y/A-Z/a-z/;
	'
}


# Execute the test described by the first argument, by eval'ing
# command line specified in the 2nd argument. Check the status code
# is zero and that the output matches the stream read from
# stdin.
test_output_expect_success()
{
	_description=$1
	_test=$2
	test $# -eq 2 ||
	error "usage: test_output_expect_success description test <<EOF ... EOF"

	_name=$(echo $_description | name_from_description)
	cat >"$_name.expected"
	test_expect_success "$_description" "check_output $_name \"$_test\""
}
