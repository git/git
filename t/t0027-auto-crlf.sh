#!/bin/sh

test_description='CRLF conversion all combinations'

. ./test-lib.sh

if ! test_have_prereq EXPENSIVE
then
	skip_all="EXPENSIVE not set"
	test_done
fi

compare_files () {
	tr '\015\000' QN <"$1" >"$1".expect &&
	tr '\015\000' QN <"$2" >"$2".actual &&
	test_cmp "$1".expect "$2".actual &&
	rm "$1".expect "$2".actual
}

compare_ws_file () {
	pfx=$1
	exp=$2.expect
	act=$pfx.actual.$3
	tr '\015\000' QN <"$2" >"$exp" &&
	tr '\015\000' QN <"$3" >"$act" &&
	test_cmp $exp $act &&
	rm $exp $act
}

create_gitattributes () {
	attr=$1
	case "$attr" in
		auto)
		echo "*.txt text=auto" >.gitattributes
		;;
		text)
		echo "*.txt text" >.gitattributes
		;;
		-text)
		echo "*.txt -text" >.gitattributes
		;;
		crlf)
		echo "*.txt eol=crlf" >.gitattributes
		;;
		lf)
		echo "*.txt eol=lf" >.gitattributes
		;;
		"")
		echo >.gitattributes
		;;
		*)
		echo >&2 invalid attribute: $attr
		exit 1
		;;
	esac
}

check_warning () {
	case "$1" in
	LF_CRLF) grep "LF will be replaced by CRLF" $2;;
	CRLF_LF) grep "CRLF will be replaced by LF" $2;;
	'')
		>expect
		grep "will be replaced by" $2 >actual
		test_cmp expect actual
		;;
	*) false ;;
	esac
}

create_file_in_repo () {
	crlf=$1
	attr=$2
	lfname=$3
	crlfname=$4
	lfmixcrlf=$5
	lfmixcr=$6
	crlfnul=$7
	create_gitattributes "$attr" &&
	pfx=crlf_${crlf}_attr_${attr}
	for f in LF CRLF LF_mix_CR CRLF_mix_LF CRLF_nul
	do
		fname=${pfx}_$f.txt &&
		cp $f $fname &&
		git -c core.autocrlf=$crlf add $fname 2>"${pfx}_$f.err"
	done &&
	git commit -m "core.autocrlf $crlf" &&
	check_warning "$lfname" ${pfx}_LF.err &&
	check_warning "$crlfname" ${pfx}_CRLF.err &&
	check_warning "$lfmixcrlf" ${pfx}_CRLF_mix_LF.err &&
	check_warning "$lfmixcr" ${pfx}_LF_mix_CR.err &&
	check_warning "$crlfnul" ${pfx}_CRLF_nul.err
}

check_files_in_repo () {
	crlf=$1
	attr=$2
	lfname=$3
	crlfname=$4
	lfmixcrlf=$5
	lfmixcr=$6
	crlfnul=$7
	pfx=crlf_${crlf}_attr_${attr}_ &&
	compare_files $lfname ${pfx}LF.txt &&
	compare_files $crlfname ${pfx}CRLF.txt &&
	compare_files $lfmixcrlf ${pfx}CRLF_mix_LF.txt &&
	compare_files $lfmixcr ${pfx}LF_mix_CR.txt &&
	compare_files $crlfnul ${pfx}CRLF_nul.txt
}


check_files_in_ws () {
	eol=$1
	crlf=$2
	attr=$3
	lfname=$4
	crlfname=$5
	lfmixcrlf=$6
	lfmixcr=$7
	crlfnul=$8
	create_gitattributes $attr &&
	git config core.autocrlf $crlf &&
	pfx=eol_${eol}_crlf_${crlf}_attr_${attr}_ &&
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

	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$attr file=LF" "
		compare_ws_file $pfx $lfname    ${src}LF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$attr file=CRLF" "
		compare_ws_file $pfx $crlfname  ${src}CRLF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$attr file=CRLF_mix_LF" "
		compare_ws_file $pfx $lfmixcrlf ${src}CRLF_mix_LF.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$attr file=LF_mix_CR" "
		compare_ws_file $pfx $lfmixcr   ${src}LF_mix_CR.txt
	"
	test_expect_success "checkout core.eol=$eol core.autocrlf=$crlf gitattributes=$attr file=CRLF_nul" "
		compare_ws_file $pfx $crlfnul   ${src}CRLF_nul.txt
	"
}

#######
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
	printf "line1Q\r\nline2\r\nline3" | q_to_nul >CRLF_nul &&
	printf "line1Q\nline2\nline3" | q_to_nul >LF_nul
'



warn_LF_CRLF="LF will be replaced by CRLF"
warn_CRLF_LF="CRLF will be replaced by LF"

test_expect_success 'add files empty attr' '
	create_file_in_repo false ""     ""        ""        ""        ""        "" &&
	create_file_in_repo true  ""     "LF_CRLF" ""        "LF_CRLF" ""        "" &&
	create_file_in_repo input ""     ""        "CRLF_LF" "CRLF_LF" ""        ""
'

test_expect_success 'add files attr=auto' '
	create_file_in_repo false "auto" ""        "CRLF_LF" "CRLF_LF" ""        "" &&
	create_file_in_repo true  "auto" "LF_CRLF" ""        "LF_CRLF" ""        "" &&
	create_file_in_repo input "auto" ""        "CRLF_LF" "CRLF_LF" ""        ""
'

test_expect_success 'add files attr=text' '
	create_file_in_repo false "text" ""        "CRLF_LF" "CRLF_LF" ""        "CRLF_LF" &&
	create_file_in_repo true  "text" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" ""        &&
	create_file_in_repo input "text" ""        "CRLF_LF" "CRLF_LF" ""        "CRLF_LF"
'

test_expect_success 'add files attr=-text' '
	create_file_in_repo false "-text" ""       ""        ""        ""        "" &&
	create_file_in_repo true  "-text" ""       ""        ""        ""        "" &&
	create_file_in_repo input "-text" ""       ""        ""        ""        ""
'

test_expect_success 'add files attr=lf' '
	create_file_in_repo false "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF" &&
	create_file_in_repo true  "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF" &&
	create_file_in_repo input "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF"
'

test_expect_success 'add files attr=crlf' '
	create_file_in_repo false "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" "" &&
	create_file_in_repo true  "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" "" &&
	create_file_in_repo input "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" ""
'

test_expect_success 'create files cleanup' '
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
# - check_files_in_ws will check multiple files with a combination of settings
#   and attributes (core.autocrlf=input is forbidden with core.eol=crlf)
# - parameter $1 : core.eol               lf | crlf
# - parameter $2 : core.autocrlf          false | true | input
# - parameter $3 : text in .gitattributs  "" (empty) | auto | text | -text
# - parameter $4 : reference for a file with only LF in the repo
# - parameter $5 : reference for a file with only CRLF in the repo
# - parameter $6 : reference for a file with mixed LF and CRLF in the repo
# - parameter $7 : reference for a file with LF and CR in the repo (does somebody uses this ?)
# - parameter $8 : reference for a file with CRLF and a NUL (should be handled as binary when auto)

#                                            What we have in the repo:
#                														 ----------------- EOL in repo ----------------
#                														 LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
#                   settings with checkout:
#                   core.   core.   .gitattr
#                    eol     acrlf
#                                            ----------------------------------------------
#                                            What we want to have in the working tree:
if test_have_prereq MINGW
then
MIX_CRLF_LF=CRLF
MIX_LF_CR=CRLF_mix_CR
NL=CRLF
else
MIX_CRLF_LF=CRLF_mix_LF
MIX_LF_CR=LF_mix_CR
NL=LF
fi
export CRLF_MIX_LF_CR MIX NL

check_files_in_ws    lf      false  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      true   ""       CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      input  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      false "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws    lf      input "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      false "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    lf      input "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      input "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      false "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      true  "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      input "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    lf      false "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    lf      true  "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    lf      input "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul

check_files_in_ws    crlf    false  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    true   ""       CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    false "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    false "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    crlf    true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    crlf    false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    false "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    true  "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    crlf    false "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    crlf    true  "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul

check_files_in_ws    ""      false  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      true   ""       CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      input  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      false "auto"    $NL   CRLF  $MIX_CRLF_LF LF_mix_CR    CRLF_nul
check_files_in_ws    ""      true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws    ""      input "auto"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      false "text"    $NL   CRLF  $MIX_CRLF_LF $MIX_LF_CR   CRLF_nul
check_files_in_ws    ""      true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    ""      input "text"    LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      input "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      false "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      true  "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      input "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    ""      false "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    ""      true  "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    ""      input "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul

check_files_in_ws    native  false  ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  true   ""       CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  false "auto"    $NL   CRLF  $MIX_CRLF_LF LF_mix_CR    CRLF_nul
check_files_in_ws    native  true  "auto"    CRLF  CRLF  CRLF         LF_mix_CR    CRLF_nul
check_files_in_ws    native  false "text"    $NL   CRLF  $MIX_CRLF_LF $MIX_LF_CR   CRLF_nul
check_files_in_ws    native  true  "text"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    native  false "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  true  "-text"   LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  false "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  true  "lf"      LF    CRLF  CRLF_mix_LF  LF_mix_CR    CRLF_nul
check_files_in_ws    native  false "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
check_files_in_ws    native  true  "crlf"    CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul

test_done
