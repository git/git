#!/bin/sh

test_description='Deltification regression test'

../test-delta 2>/dev/null
test $? == 127 && {
	echo "* Skipping test-delta regression test."
	exit 0
}

. ./test-lib.sh

>empty
echo small >small
echo smallish >smallish
cat ../../COPYING >large
sed -e 's/GNU/G.N.U/g' large >largish

test_expect_success 'No regression in deltify code' \
'
fail=0
for src in empty small smallish large largish
do
    for dst in empty small smallish large largish
    do
	if  test-delta -d $src $dst delta-$src-$dst &&
	    test-delta -p $src delta-$src-$dst out-$src-$dst &&
	    cmp $dst out-$src-$dst
	then
	    echo "* OK ($src->$dst deitify and apply)"
	else
	    echo "* FAIL ($src->$dst deitify and apply)"
	    fail=1
	fi
    done
done
case "$fail" in
0) (exit 0) ;;
*) (exit $fail) ;;
esac
'

test_done
