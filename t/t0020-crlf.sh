#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

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
	git add . &&

	git commit -m initial &&

	one=`git rev-parse HEAD:one` &&
	dir=`git rev-parse HEAD:dir` &&
	two=`git rev-parse HEAD:dir/two` &&

	for w in Some extra lines here; do echo $w; done >>one &&
	git diff >patch.file &&
	patched=`git hash-object --stdin <one` &&
	git read-tree --reset -u HEAD &&

	echo happy.
'

test_expect_success 'update with autocrlf=input' '

	rm -f tmp one dir/two &&
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

	rm -f tmp one dir/two &&
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

	rm -f tmp one dir/two &&
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

	rm -f tmp one dir/two &&
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

	rm -f tmp one dir/two &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply patch.file &&
	test "$patched" = "`git hash-object --stdin <one`" || {
		echo "Eh?  apply without index"
		false
	}
'

test_expect_success 'apply patch --cached (autocrlf=input)' '

	rm -f tmp one dir/two &&
	git repo-config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	test "$patched" = `git rev-parse :one` || {
		echo "Eh?  apply with --cached"
		false
	}
'

test_expect_success 'apply patch --index (autocrlf=input)' '

	rm -f tmp one dir/two &&
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

	rm -f tmp one dir/two &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	# Sore thumb
	remove_cr one >tmp && mv -f tmp one &&

	git apply patch.file &&
	test "$patched" = "`git hash-object --stdin <one`" || {
		echo "Eh?  apply without index"
		false
	}
'

test_expect_success 'apply patch --cached (autocrlf=true)' '

	rm -f tmp one dir/two &&
	git repo-config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	test "$patched" = `git rev-parse :one` || {
		echo "Eh?  apply without index"
		false
	}
'

test_done
