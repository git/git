# Library of git-cat-file related test functions.

# Print a string without a trailing newline.
echo_without_newline () {
	printf '%s' "$*"
}

# Print a string without newlines and replace them with a NULL character (\0).
echo_without_newline_nul () {
	echo_without_newline "$@" | tr '\n' '\0'
}

# Calculate the length of a string.
strlen () {
	echo_without_newline "$1" | wc -c | sed -e 's/^ *//'
}
