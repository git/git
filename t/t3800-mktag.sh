#!/bin/sh
#
#

test_description='git mktag: tag object verify test'

. ./test-lib.sh

###########################################################
# check the tag.sig file, expecting verify_tag() to fail,
# and checking that the error message matches the pattern
# given in the expect.pat file.

check_verify_failure () {
	expect="$2"
	test_expect_success "$1" '
		test_must_fail git mktag <tag.sig 2>message &&
		grep "$expect" message
	'
}

test_expect_mktag_success() {
	test_expect_success "$1" '
		git hash-object -t tag -w --stdin <tag.sig >expected &&
		git fsck --strict &&

		git mktag <tag.sig >hash &&
		test_cmp expected hash &&
		test_when_finished "git update-ref -d refs/tags/mytag $(cat hash)" &&
		git update-ref refs/tags/mytag $(cat hash) $(test_oid zero) &&
		git fsck --strict
	'
}

###########################################################
# first create a commit, so we have a valid object/type
# for the tag.
test_expect_success 'setup' '
	test_commit A &&
	head=$(git rev-parse --verify HEAD)
'

############################################################
#  1. length check

cat >tag.sig <<EOF
too short for a tag
EOF

check_verify_failure 'Tag object length check' \
	'^error: .*size wrong.*$'

############################################################
#  2. object line label check

cat >tag.sig <<EOF
xxxxxx $head
type tag
tag mytag
tagger . <> 0 +0000

EOF

check_verify_failure '"object" line label check' '^error: char0: .*"object "$'

############################################################
#  3. object line hash check

cat >tag.sig <<EOF
object $(echo $head | tr 0-9a-f z)
type tag
tag mytag
tagger . <> 0 +0000

EOF

check_verify_failure '"object" line SHA1 check' '^error: char7: .*SHA1 hash$'

############################################################
#  4. type line label check

cat >tag.sig <<EOF
object $head
xxxx tag
tag mytag
tagger . <> 0 +0000

EOF

check_verify_failure '"type" line label check' '^error: char.*: .*"\\ntype "$'

############################################################
#  5. type line eol check

echo "object $head" >tag.sig
printf "type tagsssssssssssssssssssssssssssssss" >>tag.sig

check_verify_failure '"type" line eol check' '^error: char.*: .*"\\n"$'

############################################################
#  6. tag line label check #1

cat >tag.sig <<EOF
object $head
type tag
xxx mytag
tagger . <> 0 +0000

EOF

check_verify_failure '"tag" line label check #1' \
	'^error: char.*: no "tag " found$'

############################################################
#  7. tag line label check #2

cat >tag.sig <<EOF
object $head
type taggggggggggggggggggggggggggggggg
tag
EOF

check_verify_failure '"tag" line label check #2' \
	'^error: char.*: no "tag " found$'

############################################################
#  8. type line type-name length check

cat >tag.sig <<EOF
object $head
type taggggggggggggggggggggggggggggggg
tag mytag
EOF

check_verify_failure '"type" line type-name length check' \
	'^error: char.*: type too long$'

############################################################
#  9. verify object (SHA1/type) check

cat >tag.sig <<EOF
object $(test_oid deadbeef)
type tagggg
tag mytag
tagger . <> 0 +0000

EOF

check_verify_failure 'verify object (SHA1/type) check' \
	'^error: char7: could not verify object.*$'

############################################################
# 10. verify tag-name check

cat >tag.sig <<EOF
object $head
type commit
tag my	tag
tagger . <> 0 +0000

EOF

check_verify_failure 'verify tag-name check' \
	'^error: char.*: could not verify tag name$'

############################################################
# 11. tagger line label check #1

cat >tag.sig <<EOF
object $head
type commit
tag mytag

This is filler
EOF

check_verify_failure '"tagger" line label check #1' \
	'^error: char.*: could not find "tagger "$'

############################################################
# 12. tagger line label check #2

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger

This is filler
EOF

check_verify_failure '"tagger" line label check #2' \
	'^error: char.*: could not find "tagger "$'

############################################################
# 13. disallow missing tag author name

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger  <> 0 +0000

This is filler
EOF

check_verify_failure 'disallow missing tag author name' \
	'^error: char.*: missing tagger name$'

############################################################
# 14. disallow missing tag author name

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <
 > 0 +0000

EOF

check_verify_failure 'disallow malformed tagger' \
	'^error: char.*: malformed tagger field$'

############################################################
# 15. allow empty tag email

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <> 0 +0000

EOF

test_expect_mktag_success 'allow empty tag email'

############################################################
# 16. disallow spaces in tag email

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tag ger@example.com> 0 +0000

EOF

check_verify_failure 'disallow spaces in tag email' \
	'^error: char.*: malformed tagger field$'

############################################################
# 17. disallow missing tag timestamp

tr '_' ' ' >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com>__

EOF

check_verify_failure 'disallow missing tag timestamp' \
	'^error: char.*: missing tag timestamp$'

############################################################
# 18. detect invalid tag timestamp1

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> Tue Mar 25 15:47:44 2008

EOF

check_verify_failure 'detect invalid tag timestamp1' \
	'^error: char.*: missing tag timestamp$'

############################################################
# 19. detect invalid tag timestamp2

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 2008-03-31T12:20:15-0500

EOF

check_verify_failure 'detect invalid tag timestamp2' \
	'^error: char.*: malformed tag timestamp$'

############################################################
# 20. detect invalid tag timezone1

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 GMT

EOF

check_verify_failure 'detect invalid tag timezone1' \
	'^error: char.*: malformed tag timezone$'

############################################################
# 21. detect invalid tag timezone2

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 +  30

EOF

check_verify_failure 'detect invalid tag timezone2' \
	'^error: char.*: malformed tag timezone$'

############################################################
# 22. detect invalid tag timezone3

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -1430

EOF

check_verify_failure 'detect invalid tag timezone3' \
	'^error: char.*: malformed tag timezone$'

############################################################
# 23. detect invalid header entry

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -0500
this line should not be here

EOF

check_verify_failure 'detect invalid header entry' \
	'^error: char.*: trailing garbage in tag header$'

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -0500


this line comes after an extra newline
EOF

test_expect_mktag_success 'allow extra newlines at start of body'

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -0500

EOF

test_expect_mktag_success 'require a blank line before an empty body (1)'

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -0500
EOF

check_verify_failure 'require a blank line before an empty body (2)' \
	'^error: char.*: trailing garbage in tag header$'

############################################################
# 24. create valid tag

cat >tag.sig <<EOF
object $head
type commit
tag mytag
tagger T A Gger <tagger@example.com> 1206478233 -0500

EOF

test_expect_mktag_success 'create valid tag object'

test_done
