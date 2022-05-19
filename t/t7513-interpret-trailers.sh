#!/bin/sh
#
# Copyright (c) 2013, 2014 Christian Couder
#

test_description='but interpret-trailers'

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

		diff --but a/foo.txt b/foo.txt
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
	test_when_finished "but config --remove-section trailer.bug" &&
	but config trailer.bug.key "Bug-maker: " &&
	but config trailer.bug.ifExists "add" &&
	but config trailer.bug.cmd "echo \"maybe is\"" &&
	cat >expected2 <<-EOF &&

	Bug-maker: maybe is him
	Bug-maker: maybe is me
	EOF
	but interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1' '
	test_when_finished "but config --remove-section trailer.bug" &&
	but config trailer.bug.key "Bug-maker: " &&
	but config trailer.bug.ifExists "add" &&
	but config trailer.bug.cmd "echo \"\$1\" is" &&
	cat >expected2 <<-EOF &&

	Bug-maker: him is him
	Bug-maker: me is me
	EOF
	but interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1 with sh -c' '
	test_when_finished "but config --remove-section trailer.bug" &&
	but config trailer.bug.key "Bug-maker: " &&
	but config trailer.bug.ifExists "replace" &&
	but config trailer.bug.cmd "sh -c \"echo who is \"\$1\"\"" &&
	cat >expected2 <<-EOF &&

	Bug-maker: who is me
	EOF
	but interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with cmd and $1 with shell script' '
	test_when_finished "but config --remove-section trailer.bug" &&
	but config trailer.bug.key "Bug-maker: " &&
	but config trailer.bug.ifExists "replace" &&
	but config trailer.bug.cmd "./echoscript" &&
	cat >expected2 <<-EOF &&

	Bug-maker: who is me
	EOF
	cat >echoscript <<-EOF &&
	#!/bin/sh
	echo who is "\$1"
	EOF
	chmod +x echoscript &&
	but interpret-trailers --trailer "bug: him" --trailer "bug:me" \
		>actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'without config' '
	sed -e "s/ Z\$/ /" >expected <<-\EOF &&

		ack: Peff
		Reviewed-by: Z
		Acked-by: Johan
	EOF
	but interpret-trailers --trailer "ack = Peff" --trailer "Reviewed-by" \
		--trailer "Acked-by: Johan" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'without config in another order' '
	sed -e "s/ Z\$/ /" >expected <<-\EOF &&

		Acked-by: Johan
		Reviewed-by: Z
		ack: Peff
	EOF
	but interpret-trailers --trailer "Acked-by: Johan" --trailer "Reviewed-by" \
		--trailer "ack = Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success '--trim-empty without config' '
	cat >expected <<-\EOF &&

		ack: Peff
		Acked-by: Johan
	EOF
	but interpret-trailers --trim-empty --trailer ack=Peff \
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
	but -c "trailer.Acked-by.ifexists=addifdifferent" interpret-trailers \
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
	but interpret-trailers --trailer "Reviewed-by: Peff" \
		--trailer "Acked-by: Johan" >actual &&
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
	but interpret-trailers --trailer "Reviewed-by: Peff" \
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
	but interpret-trailers --trailer "token: value" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with non-trailer lines mixed with cherry picked from' '
	cat >patch <<-\EOF &&

		this is not a trailer
		this is not a trailer
		(cherry picked from cummit x)
		this is not a trailer
	EOF
	cat >expected <<-\EOF &&

		this is not a trailer
		this is not a trailer
		(cherry picked from cummit x)
		this is not a trailer
		token: value
	EOF
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "token: value" patch >actual &&
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
	but interpret-trailers --trailer "name: value" patch >actual &&
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
	but interpret-trailers --trailer "name: value" patch >actual &&
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
	but interpret-trailers --trailer "name: value" patch >actual &&
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
	but interpret-trailers --trailer "$(cat trailer)" patch >actual &&
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
	but interpret-trailers --trailer "$(cat trailer)" patch >actual &&
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
	but interpret-trailers --trailer "$(cat trailer)" patch >actual &&
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
	but interpret-trailers --trailer "$(cat trailer)" patch >actual &&
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
	but interpret-trailers --trailer "$(cat trailer)" patch >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup' '
	but config trailer.ack.key "Acked-by: " &&
	cat >expected <<-\EOF &&

		Acked-by: Peff
	EOF
	but interpret-trailers --trim-empty --trailer "ack = Peff" empty >actual &&
	test_cmp expected actual &&
	but interpret-trailers --trim-empty --trailer "Acked-by = Peff" empty >actual &&
	test_cmp expected actual &&
	but interpret-trailers --trim-empty --trailer "Acked-by :Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and ":=" as separators' '
	but config trailer.separators ":=" &&
	but config trailer.ack.key "Acked-by= " &&
	cat >expected <<-\EOF &&

		Acked-by= Peff
	EOF
	but interpret-trailers --trim-empty --trailer "ack = Peff" empty >actual &&
	test_cmp expected actual &&
	but interpret-trailers --trim-empty --trailer "Acked-by= Peff" empty >actual &&
	test_cmp expected actual &&
	but interpret-trailers --trim-empty --trailer "Acked-by : Peff" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and "%" as separators' '
	but config trailer.separators "%" &&
	cat >expected <<-\EOF &&

		bug% 42
		count% 10
		bug% 422
	EOF
	but interpret-trailers --trim-empty --trailer "bug = 42" \
		--trailer count%10 --trailer "test: stuff" \
		--trailer "bug % 422" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with "%" as separators and a message with trailers' '
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
	but interpret-trailers --trailer count%100 \
		special_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with config setup and ":=#" as separators' '
	but config trailer.separators ":=#" &&
	but config trailer.bug.key "Bug #" &&
	cat >expected <<-\EOF &&

		Bug #42
	EOF
	but interpret-trailers --trim-empty --trailer "bug = 42" empty >actual &&
	test_cmp expected actual
'

test_expect_success 'with cummit basic message' '
	cat basic_message >expected &&
	echo >>expected &&
	but interpret-trailers <basic_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with basic patch' '
	cat basic_message >input &&
	cat basic_patch >>input &&
	cat basic_message >expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	but interpret-trailers <input >actual &&
	test_cmp expected actual
'

test_expect_success 'with cummit complex message as argument' '
	cat complex_message_body complex_message_trailers >complex_message &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	but interpret-trailers complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with 2 files arguments' '
	cat basic_message >>expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	but interpret-trailers complex_message input >actual &&
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
		but $config interpret-trailers \
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
	but interpret-trailers --trim-empty --trailer "Cc: Peff" message_with_comments >actual &&
	test_cmp expected actual
'

test_expect_success 'with cummit complex message and trailer args' '
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
		Acked-by= Peff
		Bug #42
	EOF
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with complex patch, args and --trim-empty' '
	cat complex_message >complex_patch &&
	cat basic_patch >>complex_patch &&
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Acked-by= Peff
		Bug #42
	EOF
	cat basic_patch >>expected &&
	but interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "bug: 42" <complex_patch >actual &&
	test_cmp expected actual
'

test_expect_success 'in-place editing with basic patch' '
	cat basic_message >message &&
	cat basic_patch >>message &&
	cat basic_message >expected &&
	echo >>expected &&
	cat basic_patch >>expected &&
	but interpret-trailers --in-place message &&
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
	but interpret-trailers --trailer "Reviewed-by: Alice" --in-place message &&
	test_cmp expected message
'

test_expect_success 'in-place editing on stdin disallowed' '
	test_must_fail but interpret-trailers --trailer "Reviewed-by: Alice" --in-place < basic_message
'

test_expect_success 'in-place editing on non-existing file' '
	test_must_fail but interpret-trailers --trailer "Reviewed-by: Alice" --in-place nonexisting &&
	test_path_is_missing nonexisting
'

test_expect_success POSIXPERM,SANITY "in-place editing doesn't clobber original file on error" '
	cat basic_message >message &&
	chmod -r message &&
	test_must_fail but interpret-trailers --trailer "Reviewed-by: Alice" --in-place message &&
	chmod +r message &&
	test_cmp message basic_message
'

test_expect_success 'using "where = before"' '
	but config trailer.bug.where "before" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
		Acked-by= Peff
	EOF
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--where after"' '
	but config trailer.ack.where "before" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	but interpret-trailers --where after --trailer "ack: Peff" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = before" with "--no-where"' '
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Peff
		Acked-by= Z
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	but interpret-trailers --where after --no-where --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = after"' '
	but config trailer.ack.where "after" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Acked-by= Peff
		Reviewed-by: Z
		Signed-off-by: Z
	EOF
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "bug: 42" complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = end"' '
	but config trailer.review.key "Reviewed-by" &&
	but config trailer.review.where "end" &&
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
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "Reviewed-by: Junio" --trailer "Reviewed-by: Johannes" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = start"' '
	but config trailer.review.key "Reviewed-by" &&
	but config trailer.review.where "start" &&
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
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "Reviewed-by: Junio" --trailer "Reviewed-by: Johannes" \
		complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = before" for a token in the middle of the message' '
	but config trailer.review.key "Reviewed-by:" &&
	but config trailer.review.where "before" &&
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
	but interpret-trailers --trailer "ack: Peff" --trailer "bug: 42" \
		--trailer "review: Johan" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "where = before" and --trim-empty' '
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Bug #46
		Bug #42
		Acked-by= Peff
		Reviewed-by:Johan
	EOF
	but interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "bug: 42" --trailer "review: Johan" \
		--trailer "Bug: 46" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'the default is "ifExists = addIfDifferentNeighbor"' '
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
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" --trailer "ack: Peff" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'default "ifExists" is now "addIfDifferent"' '
	but config trailer.ifexists "addIfDifferent" &&
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
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" --trailer "ack: Peff" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferent" with "where = end"' '
	but config trailer.ack.ifExists "addIfDifferent" &&
	but config trailer.ack.where "end" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Acked-by= Peff
	EOF
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferent" with "where = before"' '
	but config trailer.ack.ifExists "addIfDifferent" &&
	but config trailer.ack.where "before" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Fixes: Z
		Acked-by= Peff
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
	EOF
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor" with "where = end"' '
	but config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	but config trailer.ack.where "end" &&
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
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "ack: Junio" --trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor"  with "where = after"' '
	but config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	but config trailer.ack.where "after" &&
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
	but interpret-trailers --trailer "ack: Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "ack: Junio" --trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = addIfDifferentNeighbor" and --trim-empty' '
	but config trailer.ack.ifExists "addIfDifferentNeighbor" &&
	cat complex_message_body >expected &&
	cat >>expected <<-\EOF &&
		Bug #42
		Acked-by= Peff
		Acked-by= Junio
		Acked-by= Peff
	EOF
	but interpret-trailers --trim-empty --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = add" with "where = end"' '
	but config trailer.ack.ifExists "add" &&
	but config trailer.ack.where "end" &&
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
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "bug: 42" --trailer "Tested-by: Johannes" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = add" with "where = after"' '
	but config trailer.ack.ifExists "add" &&
	but config trailer.ack.where "after" &&
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
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "ack: Junio" --trailer "bug: 42" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--if-exists replace"' '
	but config trailer.fix.key "Fixes: " &&
	but config trailer.fix.ifExists "add" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Bug #42
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Fixes: 22
	EOF
	but interpret-trailers --if-exists replace --trailer "review:" \
		--trailer "fix=53" --trailer "fix=22" --trailer "bug: 42" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = replace"' '
	but config trailer.fix.key "Fixes: " &&
	but config trailer.fix.ifExists "replace" &&
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
	but interpret-trailers --trailer "review:" \
		--trailer "fix=53" --trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = replace" with "where = after"' '
	but config trailer.fix.where "after" &&
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
	but interpret-trailers --trailer "review:" \
		--trailer "fix=53" --trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifExists = doNothing"' '
	but config trailer.fix.ifExists "doNothing" &&
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
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'the default is "ifMissing = add"' '
	but config trailer.cc.key "Cc: " &&
	but config trailer.cc.where "before" &&
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
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'overriding configuration with "--if-missing doNothing"' '
	but config trailer.ifmissing "add" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	but interpret-trailers --if-missing doNothing \
		--trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'when default "ifMissing" is "doNothing"' '
	but config trailer.ifmissing "doNothing" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Acked-by= Junio
		Acked-by= Peff
		Reviewed-by:
		Signed-off-by: Z
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual &&
	but config trailer.ifmissing "add"
'

test_expect_success 'using "ifMissing = add" with "where = end"' '
	but config trailer.cc.key "Cc: " &&
	but config trailer.cc.where "end" &&
	but config trailer.cc.ifMissing "add" &&
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
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "cc=Linus" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifMissing = add" with "where = before"' '
	but config trailer.cc.key "Cc: " &&
	but config trailer.cc.where "before" &&
	but config trailer.cc.ifMissing "add" &&
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
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "ack: Junio" --trailer "fix=22" \
		--trailer "bug: 42" --trailer "cc=Linus" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'using "ifMissing = doNothing"' '
	but config trailer.cc.ifMissing "doNothing" &&
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
	but interpret-trailers --trailer "review:" --trailer "fix=53" \
		--trailer "cc=Linus" --trailer "ack: Junio" \
		--trailer "fix=22" --trailer "bug: 42" --trailer "ack: Peff" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'default "where" is now "after"' '
	but config trailer.where "after" &&
	but config --unset trailer.ack.where &&
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
	but interpret-trailers --trailer "ack: Peff" \
		--trailer "Acked-by= Peff" --trailer "review:" \
		--trailer "Tested-by: Jakub" --trailer "ack: Junio" \
		--trailer "bug: 42" --trailer "Tested-by: Johannes" \
		--trailer "ack: Peff" <complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with simple command' '
	but config trailer.sign.key "Signed-off-by: " &&
	but config trailer.sign.where "after" &&
	but config trailer.sign.ifExists "addIfDifferentNeighbor" &&
	but config trailer.sign.command "echo \"A U Thor <author@example.com>\"" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with command using cummitter information' '
	but config trailer.sign.ifExists "addIfDifferent" &&
	but config trailer.sign.command "echo \"\$BUT_CUMMITTER_NAME <\$BUT_CUMMITTER_EMAIL>\"" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: C O Mitter <cummitter@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with command using author information' '
	but config trailer.sign.key "Signed-off-by: " &&
	but config trailer.sign.where "after" &&
	but config trailer.sign.ifExists "addIfDifferentNeighbor" &&
	but config trailer.sign.command "echo \"\$BUT_AUTHOR_NAME <\$BUT_AUTHOR_EMAIL>\"" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-\EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=22" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'setup a cummit' '
	echo "Content of the first cummit." > a.txt &&
	but add a.txt &&
	but cummit -m "Add file a.txt"
'

test_expect_success 'cmd takes precedence over command' '
	test_when_finished "but config --unset trailer.fix.cmd" &&
	but config trailer.fix.ifExists "replace" &&
	but config trailer.fix.cmd "test -n \"\$1\" && but log -1 --oneline --format=\"%h (%aN)\" \
	--abbrev-cummit --abbrev=14 \"\$1\" || true" &&
	but config trailer.fix.command "but log -1 --oneline --format=\"%h (%s)\" \
		--abbrev-cummit --abbrev=14 \$ARG" &&
	FIXED=$(but log -1 --oneline --format="%h (%aN)" --abbrev-cummit --abbrev=14 HEAD) &&
	cat complex_message_body >expected2 &&
	sed -e "s/ Z\$/ /" >>expected2 <<-EOF &&
		Fixes: $FIXED
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual2 &&
	test_cmp expected2 actual2
'

test_expect_success 'with command using $ARG' '
	but config trailer.fix.ifExists "replace" &&
	but config trailer.fix.command "but log -1 --oneline --format=\"%h (%s)\" --abbrev-cummit --abbrev=14 \$ARG" &&
	FIXED=$(but log -1 --oneline --format="%h (%s)" --abbrev-cummit --abbrev=14 HEAD) &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-EOF &&
		Fixes: $FIXED
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with failing command using $ARG' '
	but config trailer.fix.ifExists "replace" &&
	but config trailer.fix.command "false \$ARG" &&
	cat complex_message_body >expected &&
	sed -e "s/ Z\$/ /" >>expected <<-EOF &&
		Fixes: Z
		Acked-by= Z
		Reviewed-by:
		Signed-off-by: Z
		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:" --trailer "fix=HEAD" \
		<complex_message >actual &&
	test_cmp expected actual
'

test_expect_success 'with empty tokens' '
	but config --unset trailer.fix.command &&
	cat >expected <<-EOF &&

		Signed-off-by: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer ":" --trailer ":test" >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with command but no key' '
	but config --unset trailer.sign.key &&
	cat >expected <<-EOF &&

		sign: A U Thor <author@example.com>
	EOF
	but interpret-trailers >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with no command and no key' '
	but config --unset trailer.review.key &&
	cat >expected <<-EOF &&

		review: Junio
		sign: A U Thor <author@example.com>
	EOF
	but interpret-trailers --trailer "review:Junio" >actual <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'with cut line' '
	cat >expected <<-\EOF &&
		my subject

		review: Brian
		sign: A U Thor <author@example.com>
		# ------------------------ >8 ------------------------
		ignore this
	EOF
	but interpret-trailers --trailer review:Brian >actual <<-\EOF &&
		my subject
		# ------------------------ >8 ------------------------
		ignore this
	EOF
	test_cmp expected actual
'

test_expect_success 'only trailers' '
	but config trailer.sign.command "echo config-value" &&
	cat >expected <<-\EOF &&
		existing: existing-value
		sign: config-value
		added: added-value
	EOF
	but interpret-trailers \
		--trailer added:added-value \
		--only-trailers >actual <<-\EOF &&
		my subject

		my body

		existing: existing-value
	EOF
	test_cmp expected actual
'

test_expect_success 'only-trailers omits non-trailer in middle of block' '
	but config trailer.sign.command "echo config-value" &&
	cat >expected <<-\EOF &&
		Signed-off-by: nobody <nobody@nowhere>
		Signed-off-by: somebody <somebody@somewhere>
		sign: config-value
	EOF
	but interpret-trailers --only-trailers >actual <<-\EOF &&
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
	but config trailer.sign.command "echo config-value" &&
	cat >expected <<-\EOF &&
		existing: existing-value
	EOF
	but interpret-trailers \
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
	but interpret-trailers --only-trailers --only-input --unfold >actual &&
	test_cmp expected actual
'

test_expect_success 'handling of --- lines in input' '
	echo "real-trailer: just right" >expected &&

	but interpret-trailers --parse >actual <<-\EOF &&
	subject

	body

	not-a-trailer: too soon
	------ this is just a line in the cummit message with a bunch of
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

	but interpret-trailers --parse --no-divider >actual <<-\EOF &&
	subject

	This cummit message has a "---" in it, but because we tell
	interpret-trailers not to respect that, it has no effect.

	not-a-trailer: too soon
	---

	This is still the cummit message body.

	real-trailer: just right
	EOF

	test_cmp expected actual
'

test_done
