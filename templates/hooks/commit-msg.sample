#!/bin/sh
#
# An example hook script to check the commit log message.
# Called by "git commit" with one argument, the name of the file
# that has the commit message.  The hook should exit with non-zero
# status after issuing an appropriate message if it wants to stop the
# commit.  The hook is allowed to edit the commit message file.
#
# To enable this hook, rename this file to "commit-msg".

# Uncomment the below to add a Signed-off-by line to the message.
# Doing this in a hook is a bad idea in general, but the prepare-commit-msg
# hook is more suited to it.
#
# SOB=$(git var GIT_AUTHOR_IDENT | sed -n 's/^\(.*>\).*$/Signed-off-by: \1/p')
# grep -qs "^$SOB" "$1" || echo "$SOB" >> "$1"

# This example catches duplicate Signed-off-by lines and messages that
# would confuse 'git am'.

ret=0

test "" = "$(grep '^Signed-off-by: ' "$1" |
	 sort | uniq -c | sed -e '/^[ 	]*1[ 	]/d')" || {
	echo >&2 Duplicate Signed-off-by lines.
	ret=1
}

comment_re="$(
	{
		git config --get-regexp "^core\.comment(char|string)\$" ||
			echo '#'
	} | sed -n -e '
		${
			s/^[^ ]* //
			s|[][*./\]|\\&|g
			s/^auto$/[#;@!$%^&|:]/
			p
		}'
)"
scissors_line="^${comment_re} -\{8,\} >8 -\{8,\}\$"
comment_line="^${comment_re}.*"
blank_line='^[ 	]*$'
# Disallow lines starting with "diff -" or "Index: " in the body of the
# message. Stop looking if we see a scissors line.
line="$(sed -n -e "
	# Skip comments and blank lines at the start of the file.
	/${scissors_line}/q
	/${comment_line}/d
	/${blank_line}/d
	# The first paragraph will become the subject header so
	# does not need to be checked.
	: subject
	n
	/${scissors_line}/q
	/${blank_line}/!b subject
	# Check the body of the message for problematic
	# prefixes.
	: body
	n
	/${scissors_line}/q
	/${comment_line}/b body
	/^diff -/{p;q;}
	/^Index: /{p;q;}
	b body
	" "$1")"
if test -n "$line"
then
	echo >&2 "Message contains a diff that will confuse 'git am'."
	echo >&2 "To fix this indent the diff."
	ret=1
fi

exit $ret
