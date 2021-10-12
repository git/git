#!/bin/sh

test_description='Test native hooks extension'

. ./test-lib.sh

expected_platform=$(uname -s | tr A-Z a-z)

if [ $(expr substr $(uname -s | tr A-Z a-z) 1 5) == "mingw" ] ; then
    expected_platform="windows"
fi

test_expect_success 'set standard and native pre-commit hooks' '
	mkdir -p test-repo &&
	cd test-repo &&
	git init &&
	mkdir -p .git/hooks &&
	echo \#!/bin/sh > .git/hooks/pre-commit &&
	echo echo Hello generic. >> .git/hooks/pre-commit &&
	chmod u+x .git/hooks/pre-commit &&
	echo \#!/bin/sh > .git/hooks/pre-commit_${expected_platform} &&
	echo echo Hello ${expected_platform} >> .git/hooks/pre-commit_${expected_platform} &&
	chmod u+x .git/hooks/pre-commit_${expected_platform} &&
	echo test > README &&
	git add README &&
	git commit -am "1-2-3 this is a test." 2>out.txt &&
	cat out.txt | grep Hello\ ${expected_platform}
'

if [ ${expected_platform} != "windows" ] ; then
	# chmod does not work well on Windows.
	test_expect_success 'set standard and native pre-commit hooks but let the native one not executable' '
		mkdir -p test-repo &&
		cd test-repo &&
		git init &&
		mkdir -p .git/hooks &&
		echo \#!/bin/sh > .git/hooks/pre-commit &&
		echo echo Hello generic. >> .git/hooks/pre-commit &&
		chmod u+x .git/hooks/pre-commit &&
		echo \#!/bin/sh > .git/hooks/pre-commit_${expected_platform} &&
		echo echo Hello ${expected_platform} >> .git/hooks/pre-commit_${expected_platform} &&
		echo test > README &&
		git add README &&
		git commit -am "1-2-3 this is a test." 2>out.txt &&
		cat out.txt | grep Hello\ generic
	'

	test_expect_success 'set standard pre-commit hook only' '
		mkdir -p test-repo &&
		cd test-repo &&
		git init &&
		mkdir -p .git/hooks &&
		echo \#!/bin/sh > .git/hooks/pre-commit &&
		echo echo Hello standard hook. >> .git/hooks/pre-commit &&
		chmod u+x .git/hooks/pre-commit &&
		echo test > README &&
		git add README &&
		git commit -am "1-2-3 this is a test." 2>out.txt &&
		cat out.txt | grep Hello\ standard\ hook
	'
fi

test_done
