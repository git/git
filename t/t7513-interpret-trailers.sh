#!/bin/sh
#
# Copyright (c) 2013, 2014 Christian Couder
#

test_description='git interpret-trailers'

. ./test-lib.sh

# When we want one trailing space at the end of each line, let's use sed
# to make sure that these spaces are not removed by any automatic tool.

test_expect_success 'setup' '
	: >empty &&
	cat >basic_message <<-\EOF &&
		subject

		body
	EOF
	cat >complex_message_body <<-\EOF &&
		my subject

		my body which is long
		and contains some special
		chars like : = ? !

	EOF
	sed -e "s/ Z\$/ /" >complex_message_trailers <<-\EOF &&
		Fixes: Z
		Acked-by: Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	cat >basic_patch <<-\EOF
		---
		 foo.txt | 2 +-
		 1 file changed, 1 insertion(+), 1 deletion(-)

		diff --git a/foo.txt b/foo.txt
		index 0353767..1d91aa1 100644
		--- a/foo.txt
		+++ b/foo.txt
		@@ -1,3 +1,3 @@

		-bar
		+baz

		--
		1.9.rc0.11.ga562ddc

	EOF
'

test_expect_success 'with cmd' '
	test_when_finished "git config --remove-section trailer.bug" &&
	git config trailer.bug.key "Bug-maker: " &&
	git config trailer.bug.ifExists "add" &&
	git config trailer.bug.cmd "echo \"maybe is\"" &&
	cat >expected2 <<-EOF &&

	Bug-maker: maybe is him
	Bug-maker: maybe is me
	EOF
	git interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1' '
	test_when_finished "git config --remove-section trailer.bug" &&
	git config trailer.bug.key "Bug-maker: " &&
	git config trailer.bug.ifExists "add" &&
	git config trailer.bug.cmd "echo \"\$1\" is" &&
	cat >expected2 <<-EOF &&

	Bug-maker: him is him
	Bug-maker: me is me
	EOF
	git interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1 with sh -c' '
	test_when_finished "git config --remove-section trailer.bug" &&
	git config trailer.bug.key "Bug-maker: " &&
	git config trailer.bug.ifExists "replace" &&
	git config trailer.bug.cmd "sh -c \"echo who is \"\$1\"\"" &&
	cat >expected2 <<-EOF &&

	Bug-maker: who is me
	EOF
	git interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1 with shell script' '
	test_when_finished "git config --remove-section trailer.bug" &&
	git config trailer.bug.key "Bug-maker: " &&
	git config trailer.bug.ifExists "replace" &&
	git config trailer.bug.cmd "./echoscript" &&
	cat >expected2 <<-EOF &&

	Bug-maker: who is me
	EOF
	cat >echoscript <<-EOF &&
	#!/bin/sh
	echo who is "\$1"
	EOF
	chmod +x echoscript &&
	git interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'without config' '
	sed -e "s/ Z\$/ /" >expected <<-\EOF &&

		ack: Peff
		Reviewed-by: Z
		Acked-by: Johan
	EOF
	git interpret-trailers --trailer "ack = Peff" --trailer "Reviewed-by" \
		--trailer "Acked-by: Johan" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'without config in another order' '
	sed -e "s/ Z\$/ /" >expected <<-\EOF &&

		Acked-by: Johan
		Reviewed-by: Z
		ack: Peff
	EOF
	git interpret-trailers --trailer "Acked-by: Johan" --trailer "Reviewed-by" \
		--trailer "ack = Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success '--trim-empty without config' '
	cat >expected <<-\EOF &&

		ack: Peff
		Acked-by: Johan
	EOF
	git interpret-trailers --trim-empty --trailer ack=Peff \
		--trailer "Reviewed-by" --trailer "Acked-by: Johan" \
		--trailer "sob:" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with config option on the command line' '
	cat >expected <<-\EOF &&

		Acked-by: Johan
		Reviewed-by: Peff
	EOF
	{ echo && echo "Acked-by: Johan"; } |
	git -c "trailer.Acked-by.ifexists=addifdifferent" interpret-trailers \
		--trailer "Reviewed-by: Peff" --trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with only a title in the message' '
	cat >expected <<-\EOF &&
		area: change

		Reviewed-by: Peff
		Acked-by: Johan
	EOF
	echo "area: change" |
	git interpret-trailers --trailer "Reviewed-by: Peff" \
		--trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with a bodiless message that lacks a trailing newline after the subject' '
	cat >expected <<-\EOF &&
		area: change

		Reviewed-by: Peff
		Acked-by: Johan
	EOF
	printf "area: change" |
	git interpret-trailers --trailer "Reviewed-by: Peff" \
		--trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with a bodied message that lacks a trailing newline after the body' '
	cat >expected <<-\EOF &&
		area: change

		details about the change.

		Reviewed-by: Peff
		Acked-by: Johan
	EOF
	printf "area: change\n\ndetails about the change." |
	git interpret-trailers --trailer "Reviewed-by: Peff" \
		--trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with a message that lacks a trailing newline after the trailers' '
	cat >expected <<-\EOF &&
		area: change

		Reviewed-by: Peff
		Acked-by: Johan
	EOF
	printf "area: change\n\nReviewed-by: Peff" |
	git interpret-trailers --trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with multiline title in the message' '
	cat >expected <<-\EOF &&
		place of
		code: change

		Reviewed-by: Peff
		Acked-by: Johan
	EOF
	printf "%s\n" "place of" "code: change" |
	git interpret-trailers --trailer "Reviewed-by: Peff" \
		--trailer "Acked-by: Johan" >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines mixed with Signed-off-by' '
	cat >patch <<-\EOF &&

		this is not a trailer
		this is not a trailer
		Signed-off-by: a <a@example.com>
		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer
		this is not a trailer
		Signed-off-by: a <a@example.com>
		this is not a trailer
		token: value
	EOF
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines mixed with cherry picked from' '
	cat >patch <<-\EOF &&

		this is not a trailer
		this is not a trailer
		(cherry picked from commit x)
		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer
		this is not a trailer
		(cherry picked from commit x)
		this is not a trailer
		token: value
	EOF
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines mixed with a configured trailer' '
	cat >patch <<-\EOF &&

		this is not a trailer
		this is not a trailer
		My-trailer: x
		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer
		this is not a trailer
		My-trailer: x
		this is not a trailer
		token: value
	EOF
	test_config trailer.my.key "My-trailer: " &&
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines mixed with a non-configured trailer' '
	cat >patch <<-\EOF &&

		this is not a trailer
		this is not a trailer
		I-am-not-configured: x
		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer
		this is not a trailer
		I-am-not-configured: x
		this is not a trailer

		token: value
	EOF
	test_config trailer.my.key "My-trailer: " &&
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with all non-configured trailers' '
	cat >patch <<-\EOF &&

		I-am-not-configured: x
		I-am-also-not-configured: x
	EOF
	cat >expected <<-\EOF &&

		I-am-not-configured: x
		I-am-also-not-configured: x
		token: value
	EOF
	test_config trailer.my.key "My-trailer: " &&
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines only' '
	cat >patch <<-\EOF &&

		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer

		token: value
	EOF
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'line with leading whitespace is not trailer' '
	q_to_tab >patch <<-\EOF &&

		Qtoken: value
	EOF
	q_to_tab >expected <<-\EOF &&

		Qtoken: value

		token: value
	EOF
	git interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'multiline field treated as one trailer for 25% check' '
	q_to_tab >patch <<-\EOF &&

		Signed-off-by: a <a@example.com>
		name: value on
		Qmultiple lines
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
	EOF
	q_to_tab >expected <<-\EOF &&

		Signed-off-by: a <a@example.com>
		name: value on
		Qmultiple lines
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
		this is not a trailer
		name: value
	EOF
	git interpret-trailers --trailer "name: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'multiline field treated as atomic for placement' '
	q_to_tab >patch <<-\EOF &&

		another: trailer
		name: value on
		Qmultiple lines
		another: trailer
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: value on
		Qmultiple lines
		name: value
		another: trailer
	EOF
	test_config trailer.name.where after &&
	git interpret-trailers --trailer "name: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'multiline field treated as atomic for replacement' '
	q_to_tab >patch <<-\EOF &&

		another: trailer
		name: value on
		Qmultiple lines
		another: trailer
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		another: trailer
		name: value
	EOF
	test_config trailer.name.ifexists replace &&
	git interpret-trailers --trailer "name: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'multiline field treated as atomic for difference check' '
	q_to_tab >patch <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
	EOF
	test_config trailer.name.ifexists addIfDifferent &&

	q_to_tab >trailer <<-\EOF &&
		name: first line
		Qsecond line
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
	EOF
	git interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual &&

	q_to_tab >trailer <<-\EOF &&
		name: first line
		QQQQQsecond line
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
		name: first line
		QQQQQsecond line
	EOF
	git interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual &&

	q_to_tab >trailer <<-\EOF &&
		name: first line *DIFFERENT*
		Qsecond line
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
		name: first line *DIFFERENT*
		Qsecond line
	EOF
	git interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'multiline field treated as atomic for neighbor check' '
	q_to_tab >patch <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
	EOF
	test_config trailer.name.where after &&
	test_config trailer.name.ifexists addIfDifferentNeighbor &&

	q_to_tab >trailer <<-\EOF &&
		name: first line
		Qsecond line
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		another: trailer
	EOF
	git interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual &&

	q_to_tab >trailer <<-\EOF &&
		name: first line
		QQQQQsecond line
	EOF
	q_to_tab >expected <<-\EOF &&

		another: trailer
		name: first line
		Qsecond line
		name: first line
		QQQQQsecond line
		another: trailer
	EOF
	git interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup' '
	test_config trailer.ack.key "Acked-by: " &&
	cat >expected <<-\EOF &&

		Acked-by: Peff
	EOF
	git interpret-trailers --trim-empty --trailer "ack = Peff" empty >actual &&
	test_cmp expected actual &&
	git interpret-trailers --trim-empty --trailer "Acked-by = Peff" empty >actual &&
	test_cmp expected actual &&
	git interpret-trailers --trim-empty --trailer "Acked-by :Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and ":=" as separators' '
	test_config trailer.separators ":=" &&
	test_config trailer.ack.key "Acked-by= " &&
	cat >expected <<-\EOF &&

		Acked-by= Peff
	EOF
	git interpret-trailers --trim-empty --trailer "ack = Peff" empty >actual &&
	test_cmp expected actual &&
	git interpret-trailers --trim-empty --trailer "Acked-by= Peff" empty >actual &&
	test_cmp expected actual &&
	git interpret-trailers --trim-empty --trailer "Acked-by : Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and "%" as separators' '
	test_config trailer.separators "%" &&
	cat >expected <<-\EOF &&

		bug% 42
		count% 10
		bug% 422
	EOF
	git interpret-trailers --trim-empty --trailer "bug = 42" \
		--trailer count%10 --trailer "test: stuff" \
		--trailer "bug % 422" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with "%" as separators and a message with trailers' '
	test_config trailer.separators "%" &&
	cat >special_message <<-\EOF &&
		Special Message

		bug% 42
		count% 10
		bug% 422
	EOF
	cat >expected <<-\EOF &&
		Special Message

		bug% 42
		count% 10
		bug% 422
		count% 100
	EOF
	git interpret-trailers --trailer count%100 \
		special_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and ":=#" as separators' '
	test_config trailer.separators ":=#" &&
	test_config trailer.bug.key "Bug #" &&
	cat >expected <<-\EOF &&

		Bug #42
	EOF
	git interpret-trailers --trim-empty --trailer "bug = 42" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with commit basic message' '
	cat basic_message >expected &&
	echo >>expected &&
	git interpret-trailers <basic_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with basic patch' '
	cat basic_message >input &&
	cat basic_patch >>input &&
	cat basic_message >expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	git interpret-trailers <input >actual &&
	test_cmp expected actual
'

test_expect_success 'with commit complex message as argument' '
	test_config trailer.separators ":=" &&
	test_config trailer.ack.key "Acked-by= " &&
	cat complex_message_body complex_message_trailers >complex_message &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with 2 files arguments' '
	test_config trailer.separators ":=" &&
	test_config trailer.ack.key "Acked-by= " &&
	cat basic_message >>expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	git interpret-trailers complex_message input >actual &&
	test_cmp expected actual
'

# Cover multiple comment characters with the same test input.
for char in "#" ";"
do
	case "$char" in
	"#")
		# This is the default, so let's explicitly _not_
		# set any config to make sure it behaves as we expect.
		;;
	*)
		config="-c core.commentChar=$char"
		;;
	esac

	test_expect_success "with message that has comments ($char)" '
		cat basic_message >message_with_comments &&
		sed -e "s/ Z\$/ /" \
		    -e "s/#/$char/g" >>message_with_comments <<-EOF &&
			# comment

			# other comment
			Cc: Z
			# yet another comment
			Reviewed-by: Johan
			Reviewed-by: Z
			# last comment

		EOF
		cat basic_patch >>message_with_comments &&
		cat basic_message >expected &&
		sed -e "s/#/$char/g" >>expected <<-\EOF &&
			# comment

			Reviewed-by: Johan
			Cc: Peff
			# last comment

		EOF
		cat basic_patch >>expected &&
		git $config interpret-trailers \
			--trim-empty --trailer "Cc: Peff" \
			message_with_comments >actual &&
		test_cmp expected actual
	'
done

test_expect_success 'with message that has an old style conflict block' '
	cat basic_message >message_with_comments &&
	sed -e "s/ Z\$/ /" >>message_with_comments <<-\EOF &&
		# comment

		# other comment
		Cc: Z
		# yet another comment
		Reviewed-by: Johan
		Reviewed-by: Z
		# last comment

		Conflicts:

	EOF
	cat basic_message >expected &&
	cat >>expected <<-\EOF &&
		# comment

		Reviewed-by: Johan
		Cc: Peff
		# last comment

		Conflicts:

	EOF
	git interpret-trailers --trim-empty --trailer "Cc: Peff" message_with_comments >actual &&
	test_cmp expected actual
'

test_expect_success 'with commit complex message and trailer args' '
	test_config trailer.separators ":=#" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
		Acked-by= Peff
		Bug #42
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with complex patch, args and --trim-empty' '
	test_config trailer.separators ":=#" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	cat complex_message >complex_patch &&
	cat basic_patch >>complex_patch &&
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Acked-by= Peff
		Bug #42
	EOF
	cat basic_patch >>expected &&
	git interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "bug: 42" <complex_patch >actual &&
	test_cmp expected actual
'

test_expect_success 'in-place editing with basic patch' '
	cat basic_message >message &&
	cat basic_patch >>message &&
	cat basic_message >expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	git interpret-trailers --in-place message &&
	test_cmp expected message
'

test_expect_success 'in-place editing with additional trailer' '
	cat basic_message >message &&
	cat basic_patch >>message &&
	cat basic_message >expected &&
	echo >>expected &&
	cat >>expected <<-\EOF &&
		Reviewed-by: Alice
	EOF
	cat basic_patch >>expected &&
	git interpret-trailers --trailer "Reviewed-by: Alice" --in-place message &&
	test_cmp expected message
'

test_expect_success 'in-place editing on stdin disallowed' '
	test_must_fail git interpret-trailers --trailer "Reviewed-by: Alice" --in-place < basic_message
'

test_expect_success 'in-place editing on non-existing file' '
	test_must_fail git interpret-trailers --trailer "Reviewed-by: Alice" --in-place nonexisting &&
	test_path_is_missing nonexisting
'

test_expect_success POSIXPERM,SANITY "in-place editing doesn't clobber original file on error" '
	cat basic_message >message &&
	chmod -r message &&
	test_must_fail git interpret-trailers --trailer "Reviewed-by: Alice" --in-place message &&
	chmod +r message &&
	test_cmp message basic_message
'

test_expect_success 'using "where = before"' '
	test_config trailer.separators ":=#" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
		Acked-by= Peff
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--where after"' '
	test_config trailer.separators ":=" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "before" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --where after --trailer "ack: Peff" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "--where after" with "--no-where"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "before" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Peff
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --where after --no-where --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

# Check whether using "--no-where" clears out only the "--where after", such
# that we still use the configuration in trailer.where (which is different from
# the hardcoded default (in WHERE_END) assuming the absence of .gitconfig).
# Here, the "start" setting of trailer.where is respected, so the new "Acked-by"
# and "Bug" trailers are placed at the beginning, and not at the end which is
# the harcoded default.
test_expect_success 'using "--where after" with "--no-where" defaults to configuration' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.separators ":=#" &&
	test_config trailer.where "start" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Acked-by= Peff
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --where after --no-where --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

# The "--where after" will only get respected for the trailer that came
# immediately after it. For the next trailer (Bug #42), we default to using the
# hardcoded WHERE_END because we don't have any "trailer.where" or
# "trailer.bug.where" configured.
test_expect_success 'using "--no-where" defaults to harcoded default if nothing configured' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
		Bug #42
	EOF
	git interpret-trailers --where after --trailer "ack: Peff" --no-where \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = after"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = end"' '
	test_config trailer.review.key "Reviewed-by" &&
	test_config trailer.review.where "end" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
		Reviewed-by: Junio
		Reviewed-by: Johannes
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "Reviewed-by: Junio" --trailer "Reviewed-by: Johannes" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = start"' '
	test_config trailer.review.key "Reviewed-by" &&
	test_config trailer.review.where "start" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Reviewed-by: Johannes
		Reviewed-by: Junio
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "Reviewed-by: Junio" --trailer "Reviewed-by: Johannes" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = before" for a token in the middle of the message' '
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.review.where "before" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by:Johan
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "bug: 42" \
		--trailer "review: Johan" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = before" and --trim-empty' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Bug #46
		Bug #42
		Acked-by= Peff
		Reviewed-by:Johan
	EOF
	git interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "bug: 42" --trailer "review: Johan" \
		--trailer "Bug: 46" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'the default is "ifExists = addIfDifferentNeighbor"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.review.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" --trailer "ack: Peff" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'default "ifExists" is now "addIfDifferent"' '
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Acked-by= Junio
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" --trailer "ack: Peff" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferent" with "where = end"' '
	test_config trailer.ack.ifExists "addIfDifferent" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "end" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Acked-by= Peff
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferent" with "where = before"' '
	test_config trailer.ack.ifExists "addIfDifferent" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "before" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Peff
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor" with "where = end"' '
	test_config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "end" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Acked-by= Peff
		Acked-by= Junio
		Tested-by: Jakub
		Acked-by= Junio
		Acked-by= Peff
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "ack: Junio" --trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor"  with "where = after"' '
	test_config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
		Tested-by: Jakub
	EOF
	git interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "ack: Junio" --trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor" and --trim-empty' '
	test_config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Bug #42
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
	EOF
	git interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = add" with "where = end"' '
	test_config trailer.ack.ifExists "add" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "end" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Acked-by= Peff
		Acked-by= Peff
		Tested-by: Jakub
		Acked-by= Junio
		Tested-by: Johannes
		Acked-by= Peff
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "bug: 42" --trailer "Tested-by: Johannes" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = add" with "where = after"' '
	test_config trailer.ack.ifExists "add" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--if-exists replace"' '
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "add" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.review.where "before" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Fixes: 22
	EOF
	git interpret-trailers --if-exists replace --trailer "review:" \
		--trailer "fix=53" --trailer "fix=22" --trailer "bug: 42" \
		<complex_message >actual &&
	test_cmp expected actual
'

# "trailer.ifexists" is set to "doNothing", so using "--no-if-exists" defaults
# to this "doNothing" behavior. So the "Fixes: 53" trailer does not get added.
test_expect_success 'using "--if-exists replace" with "--no-if-exists" defaults to configuration' '
	test_config trailer.ifexists "doNothing" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by: Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	git interpret-trailers --if-exists replace --no-if-exists --trailer "Fixes: 53" \
		<complex_message >actual &&
	test_cmp expected actual
'

# No "ifexists" configuration is set, so using "--no-if-exists" makes it default
# to addIfDifferentNeighbor. Because we do have a different neighbor "Fixes: 53"
# (because it got added by overriding with "--if-exists replace" earlier in the
# arguments list), we add "Signed-off-by: addme".
test_expect_success 'using "--no-if-exists" defaults to hardcoded default if nothing configured' '
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Acked-by: Z
		Reviewed-by: Z
		Signed-off-by: Z
		Fixes: 53
		Signed-off-by: addme
	EOF
	git interpret-trailers --if-exists replace --trailer "Fixes: 53" --no-if-exists \
		--trailer "Signed-off-by: addme" <complex_message >actual &&
	test_cmp expected actual
'

# The second "Fixes: 53" trailer is discarded, because the "--no-if-exists" here
# makes us default to addIfDifferentNeighbor, and we already added the "Fixes:
# 53" trailer earlier in the argument list.
test_expect_success 'using "--no-if-exists" defaults to hardcoded default if nothing configured (no addition)' '
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Acked-by: Z
		Reviewed-by: Z
		Signed-off-by: Z
		Fixes: 53
	EOF
	git interpret-trailers --if-exists replace --trailer "Fixes: 53" --no-if-exists \
		--trailer "Fixes: 53" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = replace"' '
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "replace" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
		Fixes: 22
	EOF
	git interpret-trailers --trailer "review:" \
		--trailer "fix=53" --trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = replace" with "where = after"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "replace" &&
	test_config trailer.fix.where "after" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: 22
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" \
		--trailer "fix=53" --trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = doNothing"' '
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'the default is "ifMissing = add"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.cc.key "Cc: " &&
	test_config trailer.cc.where "before" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Cc: Linus
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--if-missing doNothing"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.ifmissing "add" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --if-missing doNothing \
		--trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'when default "ifMissing" is "doNothing"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.ifmissing "doNothing" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifMissing = add" with "where = end"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.cc.key "Cc: " &&
	test_config trailer.cc.ifMissing "add" &&
	test_config trailer.cc.where "end" &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
		Cc: Linus
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "cc=Linus" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifMissing = add" with "where = before"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.cc.key "Cc: " &&
	test_config trailer.cc.ifMissing "add" &&
	test_config trailer.cc.where "before" &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Cc: Linus
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "cc=Linus" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifMissing = doNothing"' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.cc.ifMissing "doNothing" &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

# Ignore the "IgnoredTrailer" because of "--if-missing doNothing", but also
# ignore the "StillIgnoredTrailer" because we set "trailer.ifMissing" to
# "doNothing" in configuration.
test_expect_success 'using "--no-if-missing" defaults to configuration' '
	test_config trailer.ifMissing "doNothing" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
			Fixes: Z
			Acked-by: Z
			Reviewed-by: Z
			Signed-off-by: Z
	EOF
	git interpret-trailers --if-missing doNothing --trailer "IgnoredTrailer: ignoreme" --no-if-missing \
			--trailer "StillIgnoredTrailer: ignoreme" <complex_message >actual &&
	test_cmp expected actual
'

# Add the "AddedTrailer" because the "--no-if-missing" clears the "--if-missing
# doNothing" from earlier in the argument list.
test_expect_success 'using "--no-if-missing" defaults to hardcoded default if nothing configured' '
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
			Fixes: Z
			Acked-by: Z
			Reviewed-by: Z
			Signed-off-by: Z
			AddedTrailer: addme
	EOF
	git interpret-trailers --if-missing doNothing --trailer "IgnoredTrailer: ignoreme" --no-if-missing \
			--trailer "AddedTrailer: addme" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'default "where" is now "after"' '
	git config trailer.where "after" &&
	test_config trailer.ack.ifExists "add" &&
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.ack.where "after" &&
	test_config trailer.bug.key "Bug #" &&
	test_config trailer.bug.where "before" &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=#" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
		Tested-by: Jakub
		Tested-by: Johannes
	EOF
	git interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "bug: 42" --trailer "Tested-by: Johannes" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with simple command' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"A U Thor <author@example.com>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.sign.ifExists "addIfDifferentNeighbor" &&
	test_config trailer.sign.where "after" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with command using committer information' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"\$GIT_COMMITTER_NAME <\$GIT_COMMITTER_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.sign.ifExists "addIfDifferent" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: C O Mitter <committer@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with command using author information' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.ifExists "doNothing" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.sign.ifExists "addIfDifferentNeighbor" &&
	test_config trailer.sign.where "after" &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'setup a commit' '
	echo "Content of the first commit." > a.txt &&
	git add a.txt &&
	git commit -m "Add file a.txt"
'

test_expect_success 'cmd takes precedence over command' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.command "git log -1 --oneline --format=\"%h (%s)\" \
		--abbrev-commit --abbrev=14 \$ARG" &&
	test_config trailer.fix.cmd "test -n \"\$1\" && git log -1 --oneline --format=\"%h (%aN)\" \
	--abbrev-commit --abbrev=14 \"\$1\" || true" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "replace" &&
	test_config trailer.fix.where "after" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	FIXED=$(git log -1 --oneline --format="%h (%aN)" --abbrev-commit --abbrev=14 HEAD) &&
	cat complex_message_body >expected2 &&
	sed -e "s/ Z\$/ /" >>expected2 <<-EOF &&
		Fixes: $FIXED
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with command using $ARG' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.command "git log -1 --oneline --format=\"%h (%s)\" --abbrev-commit --abbrev=14 \$ARG" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "replace" &&
	test_config trailer.fix.where "after" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	FIXED=$(git log -1 --oneline --format="%h (%s)" --abbrev-commit --abbrev=14 HEAD) &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-EOF &&
		Fixes: $FIXED
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with failing command using $ARG' '
	test_config trailer.ack.key "Acked-by= " &&
	test_config trailer.fix.command "false \$ARG" &&
	test_config trailer.fix.key "Fixes: " &&
	test_config trailer.fix.ifExists "replace" &&
	test_config trailer.fix.where "after" &&
	test_config trailer.review.key "Reviewed-by:" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.ifexists "addIfDifferent" &&
	test_config trailer.separators ":=" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with empty tokens' '
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.sign.key "Signed-off-by: " &&
	test_config trailer.ifexists "addIfDifferent" &&
	cat >expected <<-EOF &&

		Signed-off-by: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer ":" --trailer ":test" >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with command but no key' '
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.ifexists "addIfDifferent" &&
	cat >expected <<-EOF &&

		sign: A U Thor <author@example.com>
	EOF
	git interpret-trailers >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with no command and no key' '
	test_config trailer.review.where "before" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	test_config trailer.ifexists "addIfDifferent" &&
	cat >expected <<-EOF &&

		review: Junio
		sign: A U Thor <author@example.com>
	EOF
	git interpret-trailers --trailer "review:Junio" >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with cut line' '
	test_config trailer.review.where "before" &&
	test_config trailer.sign.command "echo \"\$GIT_AUTHOR_NAME <\$GIT_AUTHOR_EMAIL>\"" &&
	cat >expected <<-\EOF &&
		my subject

		review: Brian
		sign: A U Thor <author@example.com>
		# ------------------------ >8 ------------------------
		ignore this
	EOF
	git interpret-trailers --trailer review:Brian >actual <<-\EOF &&
		my subject
		# ------------------------ >8 ------------------------
		ignore this
	EOF
	test_cmp expected actual
'

test_expect_success 'only trailers' '
	test_config trailer.sign.command "echo config-value" &&
	test_config trailer.ifexists "addIfDifferent" &&
	cat >expected <<-\EOF &&
		existing: existing-value
		sign: config-value
		added: added-value
	EOF
	git interpret-trailers \
		--trailer added:added-value \
		--only-trailers >actual <<-\EOF &&
		my subject

		my body

		existing: existing-value
	EOF
	test_cmp expected actual
'

test_expect_success 'only-trailers omits non-trailer in middle of block' '
	test_config trailer.sign.command "echo config-value" &&
	cat >expected <<-\EOF &&
		Signed-off-by: nobody <nobody@nowhere>
		Signed-off-by: somebody <somebody@somewhere>
		sign: config-value
	EOF
	git interpret-trailers --only-trailers >actual <<-\EOF &&
		subject

		it is important that the trailers below are signed-off-by
		so that they meet the "25% trailers Git knows about" heuristic

		Signed-off-by: nobody <nobody@nowhere>
		this is not a trailer
		Signed-off-by: somebody <somebody@somewhere>
	EOF
	test_cmp expected actual
'

test_expect_success 'only input' '
	test_config trailer.sign.command "echo config-value" &&
	cat >expected <<-\EOF &&
		existing: existing-value
	EOF
	git interpret-trailers \
		--only-trailers --only-input >actual <<-\EOF &&
		my subject

		my body

		existing: existing-value
	EOF
	test_cmp expected actual
'

test_expect_success 'unfold' '
	cat >expected <<-\EOF &&
		foo: continued across several lines
	EOF
	# pass through tr to make leading and trailing whitespace more obvious
	tr _ " " <<-\EOF |
		my subject

		my body

		foo:_
		__continued
		___across
		____several
		_____lines
		___
	EOF
	git interpret-trailers --only-trailers --only-input --unfold >actual &&
	test_cmp expected actual
'

test_expect_success 'handling of --- lines in input' '
	echo "real-trailer: just right" >expected &&

	git interpret-trailers --parse >actual <<-\EOF &&
	subject

	body

	not-a-trailer: too soon
	------ this is just a line in the commit message with a bunch of
	------ dashes; it does not have any syntactic meaning.

	real-trailer: just right
	---
	below the dashed line may be a patch, etc.

	not-a-trailer: too late
	EOF

	test_cmp expected actual
'

test_expect_success 'suppress --- handling' '
	echo "real-trailer: just right" >expected &&

	git interpret-trailers --parse --no-divider >actual <<-\EOF &&
	subject

	This commit message has a "---" in it, but because we tell
	interpret-trailers not to respect that, it has no effect.

	not-a-trailer: too soon
	---

	This is still the commit message body.

	real-trailer: just right
	EOF

	test_cmp expected actual
'

test_expect_success 'suppressing --- does not disable cut-line handling' '
	echo "real-trailer: before the cut" >expected &&

	git interpret-trailers --parse --no-divider >actual <<-\EOF &&
	subject

	This input has a cut-line in it; we should stop parsing when we see it
	and consider only trailers before that line.

	real-trailer: before the cut

	# ------------------------ >8 ------------------------
	# Nothing below this line counts as part of the commit message.
	not-a-trailer: too late
	EOF

	test_cmp expected actual
'

test_expect_success 'handling of --- lines in conjunction with cut-lines' '
	echo "my-trailer: here" >expected &&

	git interpret-trailers --parse >actual <<-\EOF &&
	subject

	my-trailer: here
	---
	# ------------------------ >8 ------------------------
	EOF

	test_cmp expected actual
'

test_done
