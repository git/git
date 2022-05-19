#!/bin/sh
#
# Copyright (c) 2012 Torsten Bögershausen
#

test_description='utf-8 decomposed (nfd) converted to precomposed (nfc)'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if ! test_have_prereq UTF8_NFD_TO_NFC
then
	skip_all="filesystem does not corrupt utf-8"
	test_done
fi

# create utf-8 variables
Adiarnfc=$(printf '\303\204')
Adiarnfd=$(printf 'A\314\210')

Odiarnfc=$(printf '\303\226')
Odiarnfd=$(printf 'O\314\210')
AEligatu=$(printf '\303\206')
Invalidu=$(printf '\303\377')


#Create a string with 255 bytes (decomposed)
Alongd=$Adiarnfd$Adiarnfd$Adiarnfd$Adiarnfd$Adiarnfd$Adiarnfd$Adiarnfd #21 Byte
Alongd=$Alongd$Alongd$Alongd                                           #63 Byte
Alongd=$Alongd$Alongd$Alongd$Alongd$Adiarnfd                           #255 Byte

#Create a string with 254 bytes (precomposed)
Alongc=$AEligatu$AEligatu$AEligatu$AEligatu$AEligatu #10 Byte
Alongc=$Alongc$Alongc$Alongc$Alongc$Alongc           #50 Byte
Alongc=$Alongc$Alongc$Alongc$Alongc$Alongc           #250 Byte
Alongc=$Alongc$AEligatu$AEligatu                     #254 Byte

test_expect_success "detect if nfd needed" '
	precomposeunicode=$(but config core.precomposeunicode) &&
	test "$precomposeunicode" = true &&
	but config core.precomposeunicode true
'
test_expect_success "setup" '
	>x &&
	but add x &&
	but cummit -m "1st cummit" &&
	but rm x &&
	but cummit -m "rm x"
'
test_expect_success "setup case mac" '
	but checkout -b mac_os
'
# This will test nfd2nfc in but diff
test_expect_success "but diff f.Adiar" '
	touch f.$Adiarnfc &&
	but add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	but diff f.$Adiarnfd >expect &&
	but diff f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	but reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in but diff-files
test_expect_success "but diff-files f.Adiar" '
	touch f.$Adiarnfc &&
	but add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	but diff-files f.$Adiarnfd >expect &&
	but diff-files f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	but reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in but diff-index
test_expect_success "but diff-index f.Adiar" '
	touch f.$Adiarnfc &&
	but add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	but diff-index HEAD f.$Adiarnfd >expect &&
	but diff-index HEAD f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	but reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in readdir()
test_expect_success "add file Adiarnfc" '
	echo f.Adiarnfc >f.$Adiarnfc &&
	but add f.$Adiarnfc &&
	but cummit -m "add f.$Adiarnfc"
'
# This will test nfd2nfc in but diff-tree
test_expect_success "but diff-tree f.Adiar" '
	echo f.Adiarnfc >>f.$Adiarnfc &&
	but diff-tree HEAD f.$Adiarnfd >expect &&
	but diff-tree HEAD f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	but checkout f.$Adiarnfc &&
	rm expect actual
'
# This will test nfd2nfc in but stage()
test_expect_success "stage file d.Adiarnfd/f.Adiarnfd" '
	mkdir d.$Adiarnfd &&
	echo d.$Adiarnfd/f.$Adiarnfd >d.$Adiarnfd/f.$Adiarnfd &&
	but stage d.$Adiarnfd/f.$Adiarnfd &&
	but cummit -m "add d.$Adiarnfd/f.$Adiarnfd"
'
test_expect_success "add link Adiarnfc" '
	ln -s d.$Adiarnfd/f.$Adiarnfd l.$Adiarnfc &&
	but add l.$Adiarnfc &&
	but cummit -m "add l.Adiarnfc"
'
# This will test but log
test_expect_success "but log f.Adiar" '
	but log f.$Adiarnfc > f.Adiarnfc.log &&
	but log f.$Adiarnfd > f.Adiarnfd.log &&
	test -s f.Adiarnfc.log &&
	test -s f.Adiarnfd.log &&
	test_cmp f.Adiarnfc.log f.Adiarnfd.log &&
	rm f.Adiarnfc.log f.Adiarnfd.log
'
# This will test but ls-files
test_expect_success "but lsfiles f.Adiar" '
	but ls-files f.$Adiarnfc > f.Adiarnfc.log &&
	but ls-files f.$Adiarnfd > f.Adiarnfd.log &&
	test -s f.Adiarnfc.log &&
	test -s f.Adiarnfd.log &&
	test_cmp f.Adiarnfc.log f.Adiarnfd.log &&
	rm f.Adiarnfc.log f.Adiarnfd.log
'
# This will test but mv
test_expect_success "but mv" '
	but mv f.$Adiarnfd f.$Odiarnfc &&
	but mv d.$Adiarnfd d.$Odiarnfc &&
	but mv l.$Adiarnfd l.$Odiarnfc &&
	but cummit -m "mv Adiarnfd Odiarnfc"
'
# Files can be checked out as nfc
# And the link has been corrected from nfd to nfc
test_expect_success "but checkout nfc" '
	rm f.$Odiarnfc &&
	but checkout f.$Odiarnfc
'
# Make it possible to checkout files with their NFD names
test_expect_success "but checkout file nfd" '
	rm -f f.* &&
	but checkout f.$Odiarnfd
'
# Make it possible to checkout links with their NFD names
test_expect_success "but checkout link nfd" '
	rm l.* &&
	but checkout l.$Odiarnfd
'
test_expect_success "setup case mac2" '
	but checkout main &&
	but reset --hard &&
	but checkout -b mac_os_2
'
# This will test nfd2nfc in but cummit
test_expect_success "cummit file d2.Adiarnfd/f.Adiarnfd" '
	mkdir d2.$Adiarnfd &&
	echo d2.$Adiarnfd/f.$Adiarnfd >d2.$Adiarnfd/f.$Adiarnfd &&
	but add d2.$Adiarnfd/f.$Adiarnfd &&
	but cummit -m "add d2.$Adiarnfd/f.$Adiarnfd" -- d2.$Adiarnfd/f.$Adiarnfd
'
test_expect_success "setup for long decomposed filename" '
	but checkout main &&
	but reset --hard &&
	but checkout -b mac_os_long_nfd_fn
'
test_expect_success "Add long decomposed filename" '
	echo longd >$Alongd &&
	but add * &&
	but cummit -m "Long filename"
'
test_expect_success "setup for long precomposed filename" '
	but checkout main &&
	but reset --hard &&
	but checkout -b mac_os_long_nfc_fn
'
test_expect_success "Add long precomposed filename" '
	echo longc >$Alongc &&
	but add * &&
	but cummit -m "Long filename"
'

test_expect_failure 'handle existing decomposed filenames' '
	echo content >"verbatim.$Adiarnfd" &&
	but -c core.precomposeunicode=false add "verbatim.$Adiarnfd" &&
	but cummit -m "existing decomposed file" &&
	but ls-files --exclude-standard -o "verbatim*" >untracked &&
	test_must_be_empty untracked
'

test_expect_success "unicode decomposed: but restore -p . " '
	DIRNAMEPWD=dir.Odiarnfc &&
	DIRNAMEINREPO=dir.$Adiarnfc &&
	export DIRNAMEPWD DIRNAMEINREPO &&
	but init "$DIRNAMEPWD" &&
	(
		cd "$DIRNAMEPWD" &&
		mkdir "$DIRNAMEINREPO" &&
		cd "$DIRNAMEINREPO" &&
		echo "Initial" >file &&
		but add file &&
		echo "More stuff" >>file &&
		echo y | but restore -p .
	)
'

# Test if the global core.precomposeunicode stops autosensing
# Must be the last test case
test_expect_success "respect but config --global core.precomposeunicode" '
	but config --global core.precomposeunicode true &&
	rm -rf .but &&
	but init &&
	precomposeunicode=$(but config core.precomposeunicode) &&
	test "$precomposeunicode" = "true"
'

test_done
