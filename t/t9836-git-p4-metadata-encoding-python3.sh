#!/bin/sh

test_description='git p4 metadata encoding

This test checks that the import process handles inconsistent text
encoding in p4 metadata (author names, commit messages, etc) without
failing, and produces maximally sane output in git.'

. ./lib-git-p4.sh

###############################
## SECTION REPEATED IN t9835 ##
###############################

# These tests are specific to Python 3. Write a custom script that executes
# git-p4 directly with the Python 3 interpreter to ensure that we use that
# version even if Git was compiled with Python 2.
python_target_binary=$(which python3)
if test -n "$python_target_binary"
then
	mkdir temp_python
	PATH="$(pwd)/temp_python:$PATH"
	export PATH

	write_script temp_python/git-p4-python3 <<-EOF
	exec "$python_target_binary" "$(git --exec-path)/git-p4" "\$@"
	EOF
fi

git p4-python3 >err
if ! grep 'valid commands' err
then
	skip_all="skipping python3 git p4 tests; python3 not available"
	test_done
fi

remove_user_cache () {
	rm "$HOME/.gitp4-usercache.txt" || true
}

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&

		p4_add_user "utf8_author" "ǣuthor" &&
		P4USER=utf8_author &&
		touch file1 &&
		p4 add file1 &&
		p4 submit -d "first CL has some utf-8 tǣxt" &&

		p4_add_user "latin1_author" "$(echo æuthor |
			iconv -f utf8 -t latin1)" &&
		P4USER=latin1_author &&
		touch file2 &&
		p4 add file2 &&
		p4 submit -d "$(echo second CL has some latin-1 tæxt |
			iconv -f utf8 -t latin1)" &&

		p4_add_user "cp1252_author" "$(echo æuthœr |
			iconv -f utf8 -t cp1252)" &&
		P4USER=cp1252_author &&
		touch file3 &&
		p4 add file3 &&
		p4 submit -d "$(echo third CL has sœme cp-1252 tæxt |
		  iconv -f utf8 -t cp1252)" &&

		p4_add_user "cp850_author" "$(echo Åuthor |
			iconv -f utf8 -t cp850)" &&
		P4USER=cp850_author &&
		touch file4 &&
		p4 add file4 &&
		p4 submit -d "$(echo fourth CL hÅs some cp850 text |
			iconv -f utf8 -t cp850)"
	)
'

test_expect_success 'clone non-utf8 repo with strict encoding' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	test_must_fail git -c git-p4.metadataDecodingStrategy=strict p4-python3 clone --dest="$git" //depot@all 2>err &&
	grep "Decoding perforce metadata failed!" err
'

test_expect_success 'check utf-8 contents with passthrough strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=passthrough p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "some utf-8 tǣxt" actual &&
		grep "ǣuthor" actual
	)
'

test_expect_success 'check latin-1 contents corrupted in git with passthrough strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=passthrough p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		badly_encoded_in_git=$(echo "some latin-1 tæxt" | iconv -f utf8 -t latin1) &&
		grep "$badly_encoded_in_git" actual &&
		bad_author_in_git="$(echo æuthor | iconv -f utf8 -t latin1)" &&
		grep "$bad_author_in_git" actual
	)
'

test_expect_success 'check utf-8 contents with fallback strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "some utf-8 tǣxt" actual &&
		grep "ǣuthor" actual
	)
'

test_expect_success 'check latin-1 contents with fallback strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "some latin-1 tæxt" actual &&
		grep "æuthor" actual
	)
'

test_expect_success 'check cp-1252 contents with fallback strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "sœme cp-1252 tæxt" actual &&
		grep "æuthœr" actual
	)
'

test_expect_success 'check cp850 contents parsed with correct fallback' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback -c git-p4.metadataFallbackEncoding=cp850 p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "hÅs some cp850 text" actual &&
		grep "Åuthor" actual
	)
'

test_expect_success 'check cp850-only contents escaped when cp1252 is fallback' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "h%8Fs some cp850 text" actual &&
		grep "%8Futhor" actual
	)
'

test_expect_success 'check cp-1252 contents on later sync after clone with fallback strategy' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git -c git-p4.metadataDecodingStrategy=fallback p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$cli" &&
		P4USER=cp1252_author &&
		touch file10 &&
		p4 add file10 &&
		p4 submit -d "$(echo later CL has sœme more cp-1252 tæxt |
			iconv -f utf8 -t cp1252)"
	) &&
	(
		cd "$git" &&

		git p4-python3 sync --branch=master &&

		git log p4/master >actual &&
		grep "sœme more cp-1252 tæxt" actual &&
		grep "æuthœr" actual
	)
'

############################
## / END REPEATED SECTION ##
############################


test_expect_success 'fallback (both utf-8 and cp-1252 contents handled) is the default with python3' '
	test_when_finished cleanup_git &&
	test_when_finished remove_user_cache &&
	git p4-python3 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git log >actual &&
		grep "sœme cp-1252 tæxt" actual &&
		grep "æuthœr" actual
	)
'

test_done
