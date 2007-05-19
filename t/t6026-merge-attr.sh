#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='per path merge controlled by merge attribute'

. ./test-lib.sh

test_expect_success setup '

	for f in text binary union
	do
		echo Initial >$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Initial &&

	git branch side &&
	for f in text binary union
	do
		echo Master >>$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Master &&

	git checkout side &&
	for f in text binary union
	do
		echo Side >>$f && git add $f || break
	done &&
	test_tick &&
	git commit -m Side &&

	git tag anchor
'

test_expect_success merge '

	{
		echo "binary -merge"
		echo "union merge=union"
	} >.gitattributes &&

	if git merge master
	then
		echo Gaah, should have conflicted
		false
	else
		echo Ok, conflicted.
	fi
'

test_expect_success 'check merge result in index' '

	git ls-files -u | grep binary &&
	git ls-files -u | grep text &&
	! (git ls-files -u | grep union)

'

test_expect_success 'check merge result in working tree' '

	git cat-file -p HEAD:binary >binary-orig &&
	grep "<<<<<<<" text &&
	cmp binary-orig binary &&
	! grep "<<<<<<<" union &&
	grep Master union &&
	grep Side union

'

cat >./custom-merge <<\EOF
#!/bin/sh

orig="$1" ours="$2" theirs="$3" exit="$4"
(
	echo "orig is $orig"
	echo "ours is $ours"
	echo "theirs is $theirs"
	echo "=== orig ==="
	cat "$orig"
	echo "=== ours ==="
	cat "$ours"
	echo "=== theirs ==="
	cat "$theirs"
) >"$ours+"
cat "$ours+" >"$ours"
rm -f "$ours+"
exit "$exit"
EOF
chmod +x ./custom-merge

test_expect_success 'custom merge backend' '

	echo "* merge=union" >.gitattributes &&
	echo "text merge=custom" >>.gitattributes &&

	git reset --hard anchor &&
	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 0" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	git merge master &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(git-unpack-file master^:text) &&
	a=$(git-unpack-file side^:text) &&
	b=$(git-unpack-file master:text) &&
	sh -c "./custom-merge $o $a $b 0" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	rm -f $o $a $b
'

test_expect_success 'custom merge backend' '

	git reset --hard anchor &&
	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 1" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	if git merge master
	then
		echo "Eh? should have conflicted"
		false
	else
		echo "Ok, conflicted"
	fi &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(git-unpack-file master^:text) &&
	a=$(git-unpack-file anchor:text) &&
	b=$(git-unpack-file master:text) &&
	sh -c "./custom-merge $o $a $b 0" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	rm -f $o $a $b
'

test_done
