#!/bin/sh
#
# Copyright (c) 2012 Torsten Bögershausen
#

test_description='utf-8 decomposed (nfd) converted to precomposed (nfc)'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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


ls_files_nfc_nfd () {
	test_when_finished "git config --global --unset core.precomposeunicode" &&
	prglbl=$1
	prlocl=$2
	aumlcreat=$3
	aumllist=$4
	git config --global core.precomposeunicode $prglbl &&
	(
		rm -rf .git &&
		mkdir -p "somewhere/$prglbl/$prlocl/$aumlcreat" &&
		mypwd=$PWD &&
		cd "somewhere/$prglbl/$prlocl/$aumlcreat" &&
		git init &&
		git config core.precomposeunicode $prlocl &&
		git --literal-pathspecs ls-files "$mypwd/somewhere/$prglbl/$prlocl/$aumllist" 2>err &&
		>expected &&
		test_cmp expected err
	)
}

test_expect_success "detect if nfd needed" '
	precomposeunicode=$(git config core.precomposeunicode) &&
	test "$precomposeunicode" = true &&
	git config core.precomposeunicode true
'
test_expect_success "setup" '
	>x &&
	git add x &&
	git commit -m "1st commit" &&
	git rm x &&
	git commit -m "rm x"
'
test_expect_success "setup case mac" '
	git checkout -b mac_os
'
# This will test nfd2nfc in git diff
test_expect_success "git diff f.Adiar" '
	touch f.$Adiarnfc &&
	git add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	git diff f.$Adiarnfd >expect &&
	git diff f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	git reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in git diff-files
test_expect_success "git diff-files f.Adiar" '
	touch f.$Adiarnfc &&
	git add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	git diff-files f.$Adiarnfd >expect &&
	git diff-files f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	git reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in git diff-index
test_expect_success "git diff-index f.Adiar" '
	touch f.$Adiarnfc &&
	git add f.$Adiarnfc &&
	echo f.Adiarnfc >f.$Adiarnfc &&
	git diff-index HEAD f.$Adiarnfd >expect &&
	git diff-index HEAD f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	git reset HEAD f.Adiarnfc &&
	rm f.$Adiarnfc expect actual
'
# This will test nfd2nfc in readdir()
test_expect_success "add file Adiarnfc" '
	echo f.Adiarnfc >f.$Adiarnfc &&
	git add f.$Adiarnfc &&
	git commit -m "add f.$Adiarnfc"
'
# This will test nfd2nfc in git diff-tree
test_expect_success "git diff-tree f.Adiar" '
	echo f.Adiarnfc >>f.$Adiarnfc &&
	git diff-tree HEAD f.$Adiarnfd >expect &&
	git diff-tree HEAD f.$Adiarnfc >actual &&
	test_cmp expect actual &&
	git checkout f.$Adiarnfc &&
	rm expect actual
'
# This will test nfd2nfc in git stage()
test_expect_success "stage file d.Adiarnfd/f.Adiarnfd" '
	mkdir d.$Adiarnfd &&
	echo d.$Adiarnfd/f.$Adiarnfd >d.$Adiarnfd/f.$Adiarnfd &&
	git stage d.$Adiarnfd/f.$Adiarnfd &&
	git commit -m "add d.$Adiarnfd/f.$Adiarnfd"
'
test_expect_success "add link Adiarnfc" '
	ln -s d.$Adiarnfd/f.$Adiarnfd l.$Adiarnfc &&
	git add l.$Adiarnfc &&
	git commit -m "add l.Adiarnfc"
'
# This will test git log
test_expect_success "git log f.Adiar" '
	git log f.$Adiarnfc > f.Adiarnfc.log &&
	git log f.$Adiarnfd > f.Adiarnfd.log &&
	test -s f.Adiarnfc.log &&
	test -s f.Adiarnfd.log &&
	test_cmp f.Adiarnfc.log f.Adiarnfd.log &&
	rm f.Adiarnfc.log f.Adiarnfd.log
'
# This will test git ls-files
test_expect_success "git lsfiles f.Adiar" '
	git ls-files f.$Adiarnfc > f.Adiarnfc.log &&
	git ls-files f.$Adiarnfd > f.Adiarnfd.log &&
	test -s f.Adiarnfc.log &&
	test -s f.Adiarnfd.log &&
	test_cmp f.Adiarnfc.log f.Adiarnfd.log &&
	rm f.Adiarnfc.log f.Adiarnfd.log
'
# This will test git mv
test_expect_success "git mv" '
	git mv f.$Adiarnfd f.$Odiarnfc &&
	git mv d.$Adiarnfd d.$Odiarnfc &&
	git mv l.$Adiarnfd l.$Odiarnfc &&
	git commit -m "mv Adiarnfd Odiarnfc"
'
# Files can be checked out as nfc
# And the link has been corrected from nfd to nfc
test_expect_success "git checkout nfc" '
	rm f.$Odiarnfc &&
	git checkout f.$Odiarnfc
'
# Make it possible to checkout files with their NFD names
test_expect_success "git checkout file nfd" '
	rm -f f.* &&
	git checkout f.$Odiarnfd
'
# Make it possible to checkout links with their NFD names
test_expect_success "git checkout link nfd" '
	rm l.* &&
	git checkout l.$Odiarnfd
'
test_expect_success "setup case mac2" '
	git checkout main &&
	git reset --hard &&
	git checkout -b mac_os_2
'
# This will test nfd2nfc in git commit
test_expect_success "commit file d2.Adiarnfd/f.Adiarnfd" '
	mkdir d2.$Adiarnfd &&
	echo d2.$Adiarnfd/f.$Adiarnfd >d2.$Adiarnfd/f.$Adiarnfd &&
	git add d2.$Adiarnfd/f.$Adiarnfd &&
	git commit -m "add d2.$Adiarnfd/f.$Adiarnfd" -- d2.$Adiarnfd/f.$Adiarnfd
'
test_expect_success "setup for long decomposed filename" '
	git checkout main &&
	git reset --hard &&
	git checkout -b mac_os_long_nfd_fn
'
test_expect_success "Add long decomposed filename" '
	echo longd >$Alongd &&
	git add * &&
	git commit -m "Long filename"
'
test_expect_success "setup for long precomposed filename" '
	git checkout main &&
	git reset --hard &&
	git checkout -b mac_os_long_nfc_fn
'
test_expect_success "Add long precomposed filename" '
	echo longc >$Alongc &&
	git add * &&
	git commit -m "Long filename"
'

test_expect_failure 'handle existing decomposed filenames' '
	echo content >"verbatim.$Adiarnfd" &&
	git -c core.precomposeunicode=false add "verbatim.$Adiarnfd" &&
	git commit -m "existing decomposed file" &&
	git ls-files --exclude-standard -o "verbatim*" >untracked &&
	test_must_be_empty untracked
'

test_expect_success "unicode decomposed: git restore -p . " '
	DIRNAMEPWD=dir.Odiarnfc &&
	DIRNAMEINREPO=dir.$Adiarnfc &&
	export DIRNAMEPWD DIRNAMEINREPO &&
	git init "$DIRNAMEPWD" &&
	(
		cd "$DIRNAMEPWD" &&
		mkdir "$DIRNAMEINREPO" &&
		cd "$DIRNAMEINREPO" &&
		echo "Initial" >file &&
		git add file &&
		echo "More stuff" >>file &&
		echo y | git restore -p .
	)
'

# Test if the global core.precomposeunicode stops autosensing
test_expect_success "respect git config --global core.precomposeunicode" '
	test_when_finished "git config --global --unset core.precomposeunicode" &&
	git config --global core.precomposeunicode true &&
	rm -rf .git &&
	git init &&
	precomposeunicode=$(git config core.precomposeunicode) &&
	test "$precomposeunicode" = "true"
'

test_expect_success "ls-files false false nfd nfd" '
	ls_files_nfc_nfd false false $Adiarnfd $Adiarnfd
'

test_expect_success "ls-files false true nfd nfd" '
	ls_files_nfc_nfd false true $Adiarnfd $Adiarnfd
'

test_expect_success "ls-files true false nfd nfd" '
	ls_files_nfc_nfd true false $Adiarnfd $Adiarnfd
'

test_expect_success "ls-files true true nfd nfd" '
	ls_files_nfc_nfd true true $Adiarnfd $Adiarnfd
'

test_done
