#!/bin/sh

test_description='CRLF conversion all combinations'

. ./test-lib.sh

if ! test_have_prereq EXPENSIVE
then
	skip_all="EXPENSIVE not set"
	test_done
fi


compare_files()
{
	od -c <"$1" >"$1".expect &&
	od -c <"$2" >"$2".actual &&
	test_cmp "$1".expect "$2".actual &&
	rm "$1".expect "$2".actual
}

compare_ws_file()
{
	pfx=$1
	exp=$2.expect
	act=$pfx.actual.$3
	od -c <"$2" >"$exp" &&
	od -c <"$3" >"$act" &&
	test_cmp $exp $act &&
	rm $exp $act
}

create_gitattributes()
{
	txtbin=$1
	case "$txtbin" in
		auto)
		echo "*.txt text=auto" >.gitattributes
		;;
		text)
		echo "*.txt text" >.gitattributes
		;;
		-text)
		echo "*.txt -text" >.gitattributes
		;;
		*)
		echo >.gitattributes
		;;
	esac
}

create_file_in_repo()
{
	crlf=$1
	txtbin=$2
	create_gitattributes "$txtbin" &&
	for f in LF CRLF LF_mix_CR CRLF_mix_LF CRLF_nul
	do
		pfx=crlf_${crlf}_attr_${txtbin}_$f.txt &&
		cp $f $pfx && git -c core.autocrlf=$crlf add $pfx
	done &&
	git commit -m "core.autocrlf $crlf"
}

check_files_in_repo()
{
	crlf=$1
	txtbin=$2
	lfname=$3
	crlfname=$4
	lfmixcrlf=$5
	lfmixcr=$6
	crlfnul=$7
	pfx=crlf_${crlf}_attr_${txtbin}_ &&
	compare_files $lfname ${pfx}LF.txt &&
	compare_files $crlfname ${pfx}CRLF.txt &&
	compare_files $lfmixcrlf ${pfx}CRLF_mix_LF.txt &&
	compare_files $lfmixcr ${pfx}LF_mix_CR.txt &&
	compare_files $crlfnul ${pfx}CRLF_nul.txt
}


check_files_in_ws()
{
	eol=$1
	crlf=$2
	txtbin=$3
	lfname=$4
	crlfname=$5
	lfmixcrlf=$6
	lfmixcr=$7
	crlfnul=$8
	create_gitattributes $txtbin &&
	git config core.autocrlf $crlf &&
	pfx=eol_${eol}_crlf_${crlf}_attr_${txtbin}_ &&
	src=crlf_false_attr__ &&
	for f in LF CRLF LF_mix_CR CRLF_mix_LF CRLF_nul
	do
		rm $src$f.txt &&
		if test -z "$eol"; then
			git checkout $src$f.txt
		else
			git -c core.eol=$eol checkout $src$f.txt
		fi
	done


	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$txtbin file=LF" "
		compare_ws_file $pfx $lfname    ${src}LF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$txtbin file=CRLF" "
		compare_ws_file $pfx $crlfname  ${src}CRLF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$txtbin file=CRLF_mix_LF" "
		compare_ws_file $pfx $lfmixcrlf ${src}CRLF_mix_LF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$txtbin file=LF_mix_CR" "
		compare_ws_file $pfx $lfmixcr   ${src}LF_mix_CR.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$txtbin file=CRLF_nul" "
		compare_ws_file $pfx $crlfnul   ${src}CRLF_nul.txt
	"
}

#######
(
	type od >/dev/null &&
	printf "line1Q\r\nline2\r\nline3" | q_to_nul >CRLF_nul &&
	cat >expect <<-EOF &&
	0000000 l i n e 1 \0 \r \n l i n e 2 \r \n l
	0000020 i n e 3
	0000024
EOF
	od -c CRLF_nul | sed -e "s/[ 	][	 ]*/ /g" -e "s/ *$//" >actual
	test_cmp expect actual &&
	rm expect actual
) || {
		skip_all="od not found or od -c not usable"
		exit 0
		test_done
}

test_expect_success 'setup master' '
	echo >.gitattributes &&
	git checkout -b master &&
	git add .gitattributes &&
	git commit -m "add .gitattributes" "" &&
	printf "line1\nline2\nline3"     >LF &&
	printf "line1\r\nline2\r\nline3" >CRLF &&
	printf "line1\r\nline2\nline3"   >CRLF_mix_LF &&
	printf "line1\nline2\rline3"     >LF_mix_CR &&
	printf "line1\r\nline2\rline3"   >CRLF_mix_CR &&
	printf "line1Q\nline2\nline3" | q_to_nul >LF_nul
'
#  CRLF_nul had been created above

test_expect_success 'create files' '
	create_file_in_repo false "" &&
	create_file_in_repo true  "" &&
	create_file_in_repo input "" &&

	create_file_in_repo false "auto" &&
	create_file_in_repo true  "auto" &&
	create_file_in_repo input "auto" &&

	create_file_in_repo false "text" &&
	create_file_in_repo true  "text" &&
	create_file_in_repo input "text" &&

	create_file_in_repo false "-text" &&
	create_file_in_repo true  "-text" &&
	create_file_in_repo input "-text" &&
	rm -f *.txt &&
	git reset --hard
'

test_expect_success 'commit empty gitattribues' '
	check_files_in_repo false ""      LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul &&
	check_files_in_repo true  ""      LF LF   LF          LF_mix_CR CRLF_nul &&
	check_files_in_repo input ""      LF LF   LF          LF_mix_CR CRLF_nul
'

test_expect_success 'commit text=auto' '
	check_files_in_repo false "auto"  LF LF   LF          LF_mix_CR CRLF_nul &&
	check_files_in_repo true  "auto"  LF LF   LF          LF_mix_CR CRLF_nul &&
	check_files_in_repo input "auto"  LF LF   LF          LF_mix_CR CRLF_nul
'

test_expect_success 'commit text' '
	check_files_in_repo false "text"  LF LF   LF          LF_mix_CR LF_nul &&
	check_files_in_repo true  "text"  LF LF   LF          LF_mix_CR LF_nul &&
	check_files_in_repo input "text"  LF LF   LF          LF_mix_CR LF_nul
'

test_expect_success 'commit -text' '
	check_files_in_repo false "-text" LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul &&
	check_files_in_repo true  "-text" LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul &&
	check_files_in_repo input "-text" LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul
'

################################################################################
# Check how files in the repo are changed when they are checked out
# How to read the table below:
# - check_files_in_ws will check multiple files, see below
# - parameter $1 : core.eol               lf | crlf
# - parameter $2 : core.autocrlf          false | true | input
# - parameter $3 : text in .gitattributs  "" (empty) | auto | text | -text
# - parameter $4 : reference for a file with only LF in the repo
# - parameter $5 : reference for a file with only CRLF in the repo
# - parameter $6 : reference for a file with mixed LF and CRLF in the repo
# - parameter $7 : reference for a file with LF and CR in the repo (does somebody uses this ?)
# - parameter $8 : reference for a file with CRLF and a NUL (should be handled as binary when auto)

check_files_in_ws lf      false  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      true   ""       CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      input  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

check_files_in_ws lf      false "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws lf      input "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

check_files_in_ws lf      false "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws lf      input "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

check_files_in_ws lf      false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws lf      input "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

###########
#core.autocrlf=input is forbidden with core.eol=crlf
check_files_in_ws crlf    false ""        LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws crlf    true  ""        CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

check_files_in_ws crlf    false "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws crlf    true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul

check_files_in_ws crlf    false "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws crlf    true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul

check_files_in_ws crlf    false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws crlf    true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

if test_have_prereq MINGW
then
check_files_in_ws ""      false ""        LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws ""      true  ""        CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws ""      false "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws ""      true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws ""      false "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws ""      true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws ""      false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws ""      true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul

check_files_in_ws native  false ""        LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws native  true  ""        CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws native  false "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws native  true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws native  false "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws native  true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws native  false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws native  true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
fi

test_done
