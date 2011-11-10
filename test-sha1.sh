#!/bin/sh

dd if=/dev/zero bs=1048576 count=100 2>/dev/null |
/usr/bin/time ./test-sha1 >/dev/null

while read expect cnt pfx
do
	case "$expect" in '#'*) continue ;; esac
	actual=`
		{
			test -z "$pfx" || echo "$pfx"
			dd if=/dev/zero bs=1048576 count=$cnt 2>/dev/null |
			perl -pe 'y/\000/g/'
		} | ./test-sha1 $cnt
	`
	if test "$expect" = "$actual"
	then
		echo "OK: $expect $cnt $pfx"
	else
		echo >&2 "OOPS: $cnt"
		echo >&2 "expect: $expect"
		echo >&2 "actual: $actual"
		exit 1
	fi
done <<EOF
da39a3ee5e6b4b0d3255bfef95601890afd80709 0
3f786850e387550fdab836ed7e6dc881de23001b 0 a
5277cbb45a15902137d332d97e89cf8136545485 0 ab
03cfd743661f07975fa2f1220c5194cbaff48451 0 abc
3330b4373640f9e4604991e73c7e86bfd8da2dc3 0 abcd
ec11312386ad561674f724b8cca7cf1796e26d1d 0 abcde
bdc37c074ec4ee6050d68bc133c6b912f36474df 0 abcdef
69bca99b923859f2dc486b55b87f49689b7358c7 0 abcdefg
e414af7161c9554089f4106d6f1797ef14a73666 0 abcdefgh
0707f2970043f9f7c22029482db27733deaec029 0 abcdefghi
a4dd8aa74a5636728fe52451636e2e17726033aa 1
9986b45e2f4d7086372533bb6953a8652fa3644a 1 frotz
23d8d4f788e8526b4877548a32577543cbaaf51f 10
8cd23f822ab44c7f481b8c92d591f6d1fcad431c 10 frotz
f3b5604a4e604899c1233edb3bf1cc0ede4d8c32 512
b095bd837a371593048136e429e9ac4b476e1bb3 512 frotz
08fa81d6190948de5ccca3966340cc48c10cceac 1200 xyzzy
e33a291f42c30a159733dd98b8b3e4ff34158ca0 4090 4G
#a3bf783bc20caa958f6cb24dd140a7b21984838d 9999 nitfol
EOF

exit

# generating test vectors
# inputs are number of megabytes followed by some random string to prefix.

while read cnt pfx
do
	actual=`
		{
			test -z "$pfx" || echo "$pfx"
			dd if=/dev/zero bs=1048576 count=$cnt 2>/dev/null |
			perl -pe 'y/\000/g/'
		} | sha1sum |
		sed -e 's/ .*//'
	`
	echo "$actual $cnt $pfx"
done <<EOF
0
0 a
0 ab
0 abc
0 abcd
0 abcde
0 abcdef
0 abcdefg
0 abcdefgh
0 abcdefghi
1
1 frotz
10
10 frotz
512
512 frotz
1200 xyzzy
4090 4G
9999 nitfol
EOF
