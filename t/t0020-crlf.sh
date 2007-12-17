#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

q_to_nul () {
	tr Q '\000'
}

append_cr () {
	sed -e 's/$/Q/' | tr Q '\015'
}

remove_cr () {
	tr '\015' Q <"$1" | grep Q >/dev/null &&
	tr '\015' Q <"$1" | sed -ne 's/Q$//p'
}

test_expect_success setup '

	git repo-config core.autocrlf false &&

	for w in Hello world how are you; do echo $w; done >one &&
	mkdir dir &&
	for w in I am very very fine thank you; do echo $w; done >dir/two &&
	for w in Oh here is NULQin text here; do echo $w; done | q_to_nul >three &&
	git add . &&

	git commit -m initial &&

	one=`git rev-parse HEAD:one` &&
	dir=`git rev-parse HEAD:dir` &&
	two=`git rev-parse HEAD:dir/two` &&
	three=`git rev-parse HEAD:three` &&

	for w in Some extra lines here; do echo $w; done >>one &&
	git diff >patch.file &&
	patched=`git hash-object --stdin <one` &&
	git read-tree --reset -u HEAD &&

	echo happy.
'

test_expect_success 'update with autocrlf=input' '

	rm -f tmp one dir/two three &&
	git read-tree --reset -u HEAD &&
	git repo-config core.autocrlf input &&

	for f in one dir/two
	do
		append_cr <$f >tmp && mv -f tmp $f &&
		git update-index -- $f || {
			echo Oops
			false
			break
		}
	done &&

	differs=`git diff-index --cached HEAD` &&
	test -z "$differs" || {
		echo Oops "$differs"
		false
	}

'

test_expect_success 'update with autocrlf=true' '

	rm -f tmp one dir/two three &&
	git read-tree --reset -u HEAD &&
	git repo-config core.autocrlf true &&

	for f in one dir/two
	do
		append_cr <$f >tmp && mv -f tmp $f &&
		git update-index -- $f || {
			echo "Oops $f"
			false
			break
		}
	done &&

	differs=`git diff-index --cached HEAD` &&
	test -z "$differs" || {
		echo Oops "$differs"
		false
	}

'

test_expect_success 'checkout with autocrlf=true' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	for f in one dir/two
	do
		remove_cr "$f" >tmp && mv -f tmp $f &&
		git update-index -- $f || {
			echo "Eh? $f"
			false
			break
		}
	done &&
	test "$one" = `git hash-object --stdin <one` &&
	test "$two" = `git hash-object --stdin <dir/two` &&
	differs=`git diff-index --cached HEAD` &&
	test -z "$differs" || {
		echo Oops "$differs"
		false
	}
'

test_expect_success 'checkout with autocrlf=input' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	for f in one dir/two
	do
		if remove_cr "$f" >/dev/null
		then
			echo "Eh? $f"
			false
			break
		else
			git update-index -- $f
		fi
	done &&
	test "$one" = `git hash-object --stdin <one` &&
	test "$two" = `git hash-object --stdin <dir/two` &&
	differs=`git diff-index --cached HEAD` &&
	test -z "$differs" || {
		echo Oops "$differs"
		false
	}
'

test_expect_success 'apply patch (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply patch.file &&
	test "$patched" = "`git hash-object --stdin <one`" || {
		echo "Eh?  apply without index"
		false
	}
'

test_expect_success 'apply patch --cached (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	test "$patched" = `git rev-parse :one` || {
		echo "Eh?  apply with --cached"
		false
	}
'

test_expect_success 'apply patch --index (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply --index patch.file &&
	test "$patched" = `git rev-parse :one` &&
	test "$patched" = `git hash-object --stdin <one` || {
		echo "Eh?  apply with --index"
		false
	}
'

test_expect_success 'apply patch (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply patch.file &&
	test "$patched" = "`remove_cr one | git hash-object --stdin`" || {
		echo "Eh?  apply without index"
		false
	}
'

test_expect_success 'apply patch --cached (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	test "$patched" = `git rev-parse :one` || {
		echo "Eh?  apply without index"
		false
	}
'

test_expect_success 'apply patch --index (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply --index patch.file &&
	test "$patched" = `git rev-parse :one` &&
	test "$patched" = "`remove_cr one | git hash-object --stdin`" || {
		echo "Eh?  apply with --index"
		false
	}
'

test_expect_success '.gitattributes says two is binary' '

	rm -f tmp one dir/two three &&
	echo "two -crlf" >.gitattributes &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	if remove_cr dir/two >/dev/null
	then
		echo "Huh?"
		false
	else
		: happy
	fi &&

	if remove_cr one >/dev/null
	then
		: happy
	else
		echo "Huh?"
		false
	fi &&

	if remove_cr three >/dev/null
	then
		echo "Huh?"
		false
	else
		: happy
	fi
'

test_expect_success '.gitattributes says two is input' '

	rm -f tmp one dir/two three &&
	echo "two crlf=input" >.gitattributes &&
	git read-tree --reset -u HEAD &&

	if remove_cr dir/two >/dev/null
	then
		echo "Huh?"
		false
	else
		: happy
	fi
'

test_expect_success '.gitattributes says two and three are text' '

	rm -f tmp one dir/two three &&
	echo "t* crlf" >.gitattributes &&
	git read-tree --reset -u HEAD &&

	if remove_cr dir/two >/dev/null
	then
		: happy
	else
		echo "Huh?"
		false
	fi &&

	if remove_cr three >/dev/null
	then
		: happy
	else
		echo "Huh?"
		false
	fi
'

test_expect_success 'in-tree .gitattributes (1)' '

	echo "one -crlf" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m "Add .gitattributes" &&

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset -u HEAD &&

	if remove_cr one >/dev/null
	then
		echo "Eh? one should not have CRLF"
		false
	else
		: happy
	fi &&
	remove_cr three >/dev/null || {
		echo "Eh? three should still have CRLF"
		false
	}
'

test_expect_success 'in-tree .gitattributes (2)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -f -q -u -a &&

	if remove_cr one >/dev/null
	then
		echo "Eh? one should not have CRLF"
		false
	else
		: happy
	fi &&
	remove_cr three >/dev/null || {
		echo "Eh? three should still have CRLF"
		false
	}
'

test_expect_success 'in-tree .gitattributes (3)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -u .gitattributes &&
	git checkout-index -u one dir/two three &&

	if remove_cr one >/dev/null
	then
		echo "Eh? one should not have CRLF"
		false
	else
		: happy
	fi &&
	remove_cr three >/dev/null || {
		echo "Eh? three should still have CRLF"
		false
	}
'

test_expect_success 'in-tree .gitattributes (4)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -u one dir/two three &&
	git checkout-index -u .gitattributes &&

	if remove_cr one >/dev/null
	then
		echo "Eh? one should not have CRLF"
		false
	else
		: happy
	fi &&
	remove_cr three >/dev/null || {
		echo "Eh? three should still have CRLF"
		false
	}
'

test_expect_success 'invalid .gitattributes (must not crash)' '

	echo "three +crlf" >>.gitattributes &&
	git diff

'

test_done
