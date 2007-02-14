#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='Quoting paths in diff output.
'

. ./test-lib.sh

P0='pathname'
P1='pathname	with HT'
P2='pathname with SP'
P3='pathname
with LF'

test_expect_success setup '
	echo P0.0 >"$P0.0" &&
	echo P0.1 >"$P0.1" &&
	echo P0.2 >"$P0.2" &&
	echo P0.3 >"$P0.3" &&
	echo P1.0 >"$P1.0" &&
	echo P1.2 >"$P1.2" &&
	echo P1.3 >"$P1.3" &&
	git add . &&
	git commit -m initial &&
	git mv "$P0.0" "R$P0.0" &&
	git mv "$P0.1" "R$P1.0" &&
	git mv "$P0.2" "R$P2.0" &&
	git mv "$P0.3" "R$P3.0" &&
	git mv "$P1.0" "R$P0.1" &&
	git mv "$P1.2" "R$P2.1" &&
	git mv "$P1.3" "R$P3.1" &&
	:
'

cat >expect <<\EOF
 rename pathname.1 => "Rpathname\twith HT.0" (100%)
 rename pathname.3 => "Rpathname\nwith LF.0" (100%)
 rename "pathname\twith HT.3" => "Rpathname\nwith LF.1" (100%)
 rename pathname.2 => Rpathname with SP.0 (100%)
 rename "pathname\twith HT.2" => Rpathname with SP.1 (100%)
 rename pathname.0 => Rpathname.0 (100%)
 rename "pathname\twith HT.0" => Rpathname.1 (100%)
EOF
test_expect_success 'git diff --summary -M HEAD' '
	git diff --summary -M HEAD >actual &&
	diff -u expect actual
'

cat >expect <<\EOF
 pathname.1 => "Rpathname\twith HT.0"            |    0 
 pathname.3 => "Rpathname\nwith LF.0"            |    0 
 "pathname\twith HT.3" => "Rpathname\nwith LF.1" |    0 
 pathname.2 => Rpathname with SP.0               |    0 
 "pathname\twith HT.2" => Rpathname with SP.1    |    0 
 pathname.0 => Rpathname.0                       |    0 
 "pathname\twith HT.0" => Rpathname.1            |    0 
 7 files changed, 0 insertions(+), 0 deletions(-)
EOF
test_expect_success 'git diff --stat -M HEAD' '
	git diff --stat -M HEAD >actual &&
	diff -u expect actual
'

test_done
