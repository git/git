#!/bin/sh

test_description='more git add -u'

. ./test-lib.sh

_z40=0000000000000000000000000000000000000000

test_expect_success setup '
	>xyzzy &&
	_empty=$(git hash-object --stdin <xyzzy) &&
	>yomin &&
	>caskly &&
	ln -s frotz nitfol &&
	mkdir rezrov &&
	>rezrov/bozbar &&
	git add caskly xyzzy yomin nitfol rezrov/bozbar &&

	test_tick &&
	git commit -m initial

'

test_expect_success modify '
	rm -f xyzzy yomin nitfol caskly &&
	# caskly disappears (not a submodule)
	mkdir caskly &&
	# nitfol changes from symlink to regular
	>nitfol &&
	# rezrov/bozbar disappears
	rm -fr rezrov &&
	ln -s xyzzy rezrov &&
	# xyzzy disappears (not a submodule)
	mkdir xyzzy &&
	echo gnusto >xyzzy/bozbar &&
	# yomin gets replaced with a submodule
	mkdir yomin &&
	>yomin/yomin &&
	(
		cd yomin &&
		git init &&
		git add yomin &&
		git commit -m "sub initial"
	) &&
	yomin=$(GIT_DIR=yomin/.git git rev-parse HEAD) &&
	# yonk is added and then turned into a submodule
	# this should appear as T in diff-files and as A in diff-index
	>yonk &&
	git add yonk &&
	rm -f yonk &&
	mkdir yonk &&
	>yonk/yonk &&
	(
		cd yonk &&
		git init &&
		git add yonk &&
		git commit -m "sub initial"
	) &&
	yonk=$(GIT_DIR=yonk/.git git rev-parse HEAD) &&
	# zifmia is added and then removed
	# this should appear in diff-files but not in diff-index.
	>zifmia &&
	git add zifmia &&
	rm -f zifmia &&
	mkdir zifmia &&
	{
		git ls-tree -r HEAD |
		sed -e "s/^/:/" -e "
			/	caskly/{
				s/	caskly/ $_z40 D&/
				s/blob/000000/
			}
			/	nitfol/{
				s/	nitfol/ $_z40 T&/
				s/blob/100644/
			}
			/	rezrov.bozbar/{
				s/	rezrov.bozbar/ $_z40 D&/
				s/blob/000000/
			}
			/	xyzzy/{
				s/	xyzzy/ $_z40 D&/
				s/blob/000000/
			}
			/	yomin/{
			    s/	yomin/ $_z40 T&/
				s/blob/160000/
			}
		"
	} >expect &&
	{
		cat expect
		echo ":100644 160000 $_empty $_z40 T	yonk"
		echo ":100644 000000 $_empty $_z40 D	zifmia"
	} >expect-files &&
	{
		cat expect
		echo ":000000 160000 $_z40 $_z40 A	yonk"
	} >expect-index &&
	{
		echo "100644 $_empty 0	nitfol"
		echo "160000 $yomin 0	yomin"
		echo "160000 $yonk 0	yonk"
	} >expect-final
'

test_expect_success diff-files '
	git diff-files --raw >actual &&
	diff -u expect-files actual
'

test_expect_success diff-index '
	git diff-index --raw HEAD -- >actual &&
	diff -u expect-index actual
'

test_expect_success 'add -u' '
	rm -f ".git/saved-index" &&
	cp -p ".git/index" ".git/saved-index" &&
	git add -u &&
	git ls-files -s >actual &&
	diff -u expect-final actual
'

test_expect_success 'commit -a' '
	if test -f ".git/saved-index"
	then
		rm -f ".git/index" &&
		mv ".git/saved-index" ".git/index"
	fi &&
	git commit -m "second" -a &&
	git ls-files -s >actual &&
	diff -u expect-final actual &&
	rm -f .git/index &&
	git read-tree HEAD &&
	git ls-files -s >actual &&
	diff -u expect-final actual
'

test_done
