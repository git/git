#!/bin/sh

test_description='CRLF conversion all combinations'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

compare_files () {
	tr '\015\000' QN <"$1" >"$1".expect &&
	tr '\015\000' QN <"$2" | tr -d 'Z' >"$2".actual &&
	test_cmp "$1".expect "$2".actual &&
	rm "$1".expect "$2".actual
}

compare_ws_file () {
	pfx=$1
	exp=$2.expect
	act=$pfx.actual.$3
	tr '\015\000abcdef0123456789' QN00000000000000000 <"$2" |
		sed -e "s/0000*/$ZERO_OID/" >"$exp" &&
	tr '\015\000abcdef0123456789' QN00000000000000000 <"$3" |
		sed -e "s/0000*/$ZERO_OID/" >"$act" &&
	test_cmp "$exp" "$act" &&
	rm "$exp" "$act"
}

create_gitattributes () {
	{
		while test "$#" != 0
		do
			case "$1" in
			auto)	 echo '*.txt text=auto' ;;
			ident) echo '*.txt ident' ;;
			text)	 echo '*.txt text' ;;
			-text) echo '*.txt -text' ;;
			crlf)  echo '*.txt eol=crlf' ;;
			lf)    echo '*.txt eol=lf' ;;
			"") ;;
			*)
				echo >&2 invalid attribute: "$1"
				exit 1
				;;
			esac &&
			shift
		done
	} >.gitattributes
}

# Create 2 sets of files:
# The NNO files are "Not NOrmalized in the repo. We use CRLF_mix_LF and store
#   it under different names for the different test cases, see ${pfx}
#   Depending on .gitattributes they are normalized at the next commit (or not)
# The MIX files have different contents in the repo.
#   Depending on its contents, the "new safer autocrlf" may kick in.
create_NNO_MIX_files () {
	for crlf in false true input
	do
		for attr in "" auto text -text
		do
			for aeol in "" lf crlf
			do
				pfx=NNO_attr_${attr}_aeol_${aeol}_${crlf} &&
				cp CRLF_mix_LF ${pfx}_LF.txt &&
				cp CRLF_mix_LF ${pfx}_CRLF.txt &&
				cp CRLF_mix_LF ${pfx}_CRLF_mix_LF.txt &&
				cp CRLF_mix_LF ${pfx}_LF_mix_CR.txt &&
				cp CRLF_mix_LF ${pfx}_CRLF_nul.txt &&
				pfx=MIX_attr_${attr}_aeol_${aeol}_${crlf} &&
				cp LF          ${pfx}_LF.txt &&
				cp CRLF        ${pfx}_CRLF.txt &&
				cp CRLF_mix_LF ${pfx}_CRLF_mix_LF.txt &&
				cp LF_mix_CR   ${pfx}_LF_mix_CR.txt &&
				cp CRLF_nul    ${pfx}_CRLF_nul.txt ||
				return 1
			done
		done
	done
}

check_warning () {
	case "$1" in
	LF_CRLF) echo "LF will be replaced by CRLF" >"$2".expect ;;
	CRLF_LF) echo "CRLF will be replaced by LF" >"$2".expect ;;
	'')	                                    >"$2".expect ;;
	*) echo >&2 "Illegal 1": "$1" ; return false ;;
	esac
	sed -e "s/^.* \([^ ]* will be replaced by [^ ]*\) .*$/\1/" "$2" | uniq  >"$2".actual
	test_cmp "$2".expect "$2".actual
}

commit_check_warn () {
	crlf=$1
	attr=$2
	lfname=$3
	crlfname=$4
	lfmixcrlf=$5
	lfmixcr=$6
	crlfnul=$7
	pfx=crlf_${crlf}_attr_${attr}
	create_gitattributes "$attr" &&
	for f in LF CRLF LF_mix_CR CRLF_mix_LF LF_nul CRLF_nul
	do
		fname=${pfx}_$f.txt &&
		cp $f $fname &&
		git -c core.autocrlf=$crlf add $fname 2>"${pfx}_$f.err" ||
		return 1
	done &&
	git commit -m "core.autocrlf $crlf" &&
	check_warning "$lfname" ${pfx}_LF.err &&
	check_warning "$crlfname" ${pfx}_CRLF.err &&
	check_warning "$lfmixcrlf" ${pfx}_CRLF_mix_LF.err &&
	check_warning "$lfmixcr" ${pfx}_LF_mix_CR.err &&
	check_warning "$crlfnul" ${pfx}_CRLF_nul.err
}

commit_chk_wrnNNO () {
	attr=$1 ; shift
	aeol=$1 ; shift
	crlf=$1 ; shift
	lfwarn=$1 ; shift
	crlfwarn=$1 ; shift
	lfmixcrlf=$1 ; shift
	lfmixcr=$1 ; shift
	crlfnul=$1 ; shift
	pfx=NNO_attr_${attr}_aeol_${aeol}_${crlf}

	test_expect_success 'setup commit NNO files' '
		#Commit files on top of existing file
		create_gitattributes "$attr" $aeol &&
		for f in LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul
		do
			fname=${pfx}_$f.txt &&
			cp $f $fname &&
			printf Z >>"$fname" &&
			git -c core.autocrlf=$crlf add $fname 2>"${pfx}_$f.err" ||
			return 1
		done
	'

	test_expect_success "commit NNO files crlf=$crlf attr=$attr LF" '
		check_warning "$lfwarn" ${pfx}_LF.err
	'
	test_expect_success "commit NNO files attr=$attr aeol=$aeol crlf=$crlf CRLF" '
		check_warning "$crlfwarn" ${pfx}_CRLF.err
	'

	test_expect_success "commit NNO files attr=$attr aeol=$aeol crlf=$crlf CRLF_mix_LF" '
		check_warning "$lfmixcrlf" ${pfx}_CRLF_mix_LF.err
	'

	test_expect_success "commit NNO files attr=$attr aeol=$aeol crlf=$crlf LF_mix_cr" '
		check_warning "$lfmixcr" ${pfx}_LF_mix_CR.err
	'

	test_expect_success "commit NNO files attr=$attr aeol=$aeol crlf=$crlf CRLF_nul" '
		check_warning "$crlfnul" ${pfx}_CRLF_nul.err
	'
}

# Commit a file with mixed line endings on top of different files
# in the index. Check for warnings
commit_MIX_chkwrn () {
	attr=$1 ; shift
	aeol=$1 ; shift
	crlf=$1 ; shift
	lfwarn=$1 ; shift
	crlfwarn=$1 ; shift
	lfmixcrlf=$1 ; shift
	lfmixcr=$1 ; shift
	crlfnul=$1 ; shift
	pfx=MIX_attr_${attr}_aeol_${aeol}_${crlf}

	test_expect_success 'setup commit file with mixed EOL' '
		#Commit file with CLRF_mix_LF on top of existing file
		create_gitattributes "$attr" $aeol &&
		for f in LF CRLF CRLF_mix_LF LF_mix_CR CRLF_nul
		do
			fname=${pfx}_$f.txt &&
			cp CRLF_mix_LF $fname &&
			printf Z >>"$fname" &&
			git -c core.autocrlf=$crlf add $fname 2>"${pfx}_$f.err" ||
			return 1
		done
	'

	test_expect_success "commit file with mixed EOL onto LF crlf=$crlf attr=$attr" '
		check_warning "$lfwarn" ${pfx}_LF.err
	'
	test_expect_success "commit file with mixed EOL onto CLRF attr=$attr aeol=$aeol crlf=$crlf" '
		check_warning "$crlfwarn" ${pfx}_CRLF.err
	'

	test_expect_success "commit file with mixed EOL onto CRLF_mix_LF attr=$attr aeol=$aeol crlf=$crlf" '
		check_warning "$lfmixcrlf" ${pfx}_CRLF_mix_LF.err
	'

	test_expect_success "commit file with mixed EOL onto LF_mix_cr attr=$attr aeol=$aeol crlf=$crlf " '
		check_warning "$lfmixcr" ${pfx}_LF_mix_CR.err
	'

	test_expect_success "commit file with mixed EOL onto CRLF_nul attr=$attr aeol=$aeol crlf=$crlf" '
		check_warning "$crlfnul" ${pfx}_CRLF_nul.err
	'
}


stats_ascii () {
	case "$1" in
	LF)
		echo lf
		;;
	CRLF)
		echo crlf
		;;
	CRLF_mix_LF)
		echo mixed
		;;
	LF_mix_CR|CRLF_nul|LF_nul|CRLF_mix_CR)
		echo "-text"
		;;
	*)
		echo error_invalid $1
		;;
	esac

}


# construct the attr/ returned by git ls-files --eol
# Take none (=empty), one or two args
# convert.c: eol=XX overrides text=auto
attr_ascii () {
	case $1,$2 in
	-text,*)   echo "-text" ;;
	text,)     echo "text" ;;
	text,lf)   echo "text eol=lf" ;;
	text,crlf) echo "text eol=crlf" ;;
	auto,)     echo "text=auto" ;;
	auto,lf)   echo "text=auto eol=lf" ;;
	auto,crlf) echo "text=auto eol=crlf" ;;
	lf,)       echo "text eol=lf" ;;
	crlf,)     echo "text eol=crlf" ;;
	,) echo "" ;;
	*) echo invalid_attr "$1,$2" ;;
	esac
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

check_in_repo_NNO () {
	attr=$1 ; shift
	aeol=$1 ; shift
	crlf=$1 ; shift
	lfname=$1 ; shift
	crlfname=$1 ; shift
	lfmixcrlf=$1 ; shift
	lfmixcr=$1 ; shift
	crlfnul=$1 ; shift
	pfx=NNO_attr_${attr}_aeol_${aeol}_${crlf}
	test_expect_success "compare_files $lfname ${pfx}_LF.txt" '
		compare_files $lfname ${pfx}_LF.txt
	'
	test_expect_success "compare_files $crlfname ${pfx}_CRLF.txt" '
		compare_files $crlfname ${pfx}_CRLF.txt
	'
	test_expect_success "compare_files $lfmixcrlf ${pfx}_CRLF_mix_LF.txt" '
		compare_files $lfmixcrlf ${pfx}_CRLF_mix_LF.txt
	'
	test_expect_success "compare_files $lfmixcr ${pfx}_LF_mix_CR.txt" '
		compare_files $lfmixcr ${pfx}_LF_mix_CR.txt
	'
	test_expect_success "compare_files $crlfnul ${pfx}_CRLF_nul.txt" '
		compare_files $crlfnul ${pfx}_CRLF_nul.txt
	'
}

checkout_files () {
	attr=$1 ; shift
	ident=$1; shift
	aeol=$1 ; shift
	crlf=$1 ; shift
	ceol=$1 ; shift
	lfname=$1 ; shift
	crlfname=$1 ; shift
	lfmixcrlf=$1 ; shift
	lfmixcr=$1 ; shift
	crlfnul=$1 ; shift
	test_expect_success "setup config for checkout attr=$attr ident=$ident aeol=$aeol core.autocrlf=$crlf" '
		create_gitattributes "$attr" $ident $aeol &&
		git config core.autocrlf $crlf
	'
	pfx=eol_${ceol}_crlf_${crlf}_attr_${attr}_ &&
	for f in LF CRLF LF_mix_CR CRLF_mix_LF LF_nul
	do
		test_expect_success "setup $f checkout ${ceol:+ with -c core.eol=$ceol}"  '
			rm -f crlf_false_attr__$f.txt &&
			git ${ceol:+-c core.eol=$ceol} checkout -- crlf_false_attr__$f.txt
		'
	done

	test_expect_success "ls-files --eol attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol" '
		test_when_finished "rm expect actual" &&
		sort <<-EOF >expect &&
		i/crlf w/$(stats_ascii $crlfname) attr/$(attr_ascii $attr $aeol) crlf_false_attr__CRLF.txt
		i/mixed w/$(stats_ascii $lfmixcrlf) attr/$(attr_ascii $attr $aeol) crlf_false_attr__CRLF_mix_LF.txt
		i/lf w/$(stats_ascii $lfname) attr/$(attr_ascii $attr $aeol) crlf_false_attr__LF.txt
		i/-text w/$(stats_ascii $lfmixcr) attr/$(attr_ascii $attr $aeol) crlf_false_attr__LF_mix_CR.txt
		i/-text w/$(stats_ascii $crlfnul) attr/$(attr_ascii $attr $aeol) crlf_false_attr__CRLF_nul.txt
		i/-text w/$(stats_ascii $crlfnul) attr/$(attr_ascii $attr $aeol) crlf_false_attr__LF_nul.txt
		EOF
		git ls-files --eol crlf_false_attr__* >tmp &&
		sed -e "s/	/ /g" -e "s/  */ /g" tmp |
		sort >actual &&
		test_cmp expect actual
	'
	test_expect_success "checkout attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol file=LF" "
		compare_ws_file $pfx $lfname    crlf_false_attr__LF.txt
	"
	test_expect_success "checkout attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol file=CRLF" "
		compare_ws_file $pfx $crlfname  crlf_false_attr__CRLF.txt
	"
	test_expect_success "checkout attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol file=CRLF_mix_LF" "
		compare_ws_file $pfx $lfmixcrlf crlf_false_attr__CRLF_mix_LF.txt
	"
	test_expect_success "checkout attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol file=LF_mix_CR" "
		compare_ws_file $pfx $lfmixcr   crlf_false_attr__LF_mix_CR.txt
	"
	test_expect_success "checkout attr=$attr $ident aeol=$aeol core.autocrlf=$crlf core.eol=$ceol file=LF_nul" "
		compare_ws_file $pfx $crlfnul   crlf_false_attr__LF_nul.txt
	"
}

# Test control characters
# NUL SOH CR EOF==^Z
test_expect_success 'ls-files --eol -o Text/Binary' '
	test_when_finished "rm expect actual TeBi_*" &&
	STRT=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA &&
	STR=$STRT$STRT$STRT$STRT &&
	printf "${STR}BBB\001" >TeBi_127_S &&
	printf "${STR}BBBB\001">TeBi_128_S &&
	printf "${STR}BBB\032" >TeBi_127_E &&
	printf "\032${STR}BBB" >TeBi_E_127 &&
	printf "${STR}BBBB\000">TeBi_128_N &&
	printf "${STR}BBB\012">TeBi_128_L &&
	printf "${STR}BBB\015">TeBi_127_C &&
	printf "${STR}BB\015\012" >TeBi_126_CL &&
	printf "${STR}BB\015\012\015" >TeBi_126_CLC &&
	sort <<-\EOF >expect &&
	i/ w/-text TeBi_127_S
	i/ w/none TeBi_128_S
	i/ w/none TeBi_127_E
	i/ w/-text TeBi_E_127
	i/ w/-text TeBi_128_N
	i/ w/lf TeBi_128_L
	i/ w/-text TeBi_127_C
	i/ w/crlf TeBi_126_CL
	i/ w/-text TeBi_126_CLC
	EOF
	git ls-files --eol -o >tmp &&
	sed -n -e "/TeBi_/{s!attr/[	]*!!g
	s!	! !g
	s!  *! !g
	p
	}" tmp | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'setup main' '
	echo >.gitattributes &&
	git checkout -b main &&
	git add .gitattributes &&
	git commit -m "add .gitattributes" . &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\nLINEONE\nLINETWO\nLINETHREE"     >LF &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\r\nLINEONE\r\nLINETWO\r\nLINETHREE" >CRLF &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\nLINEONE\r\nLINETWO\nLINETHREE"   >CRLF_mix_LF &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\nLINEONE\nLINETWO\rLINETHREE"     >LF_mix_CR &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\r\nLINEONE\r\nLINETWO\rLINETHREE"   >CRLF_mix_CR &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\r\nLINEONEQ\r\nLINETWO\r\nLINETHREE" | q_to_nul >CRLF_nul &&
	printf "\$Id: 0000000000000000000000000000000000000000 \$\nLINEONEQ\nLINETWO\nLINETHREE" | q_to_nul >LF_nul &&
	create_NNO_MIX_files &&
	git -c core.autocrlf=false add NNO_*.txt MIX_*.txt &&
	git commit -m "mixed line endings" &&
	test_tick
'



warn_LF_CRLF="LF will be replaced by CRLF"
warn_CRLF_LF="CRLF will be replaced by LF"

# WILC stands for "Warn if (this OS) converts LF into CRLF".
# WICL: Warn if CRLF becomes LF
# WAMIX: Mixed line endings: either CRLF->LF or LF->CRLF
if test_have_prereq NATIVE_CRLF
then
	WILC=LF_CRLF
	WICL=
	WAMIX=LF_CRLF
else
	WILC=
	WICL=CRLF_LF
	WAMIX=CRLF_LF
fi

#                         attr   LF        CRLF      CRLFmixLF LFmixCR   CRLFNUL
test_expect_success 'commit files empty attr' '
	commit_check_warn false ""     ""        ""        ""        ""        "" &&
	commit_check_warn true  ""     "LF_CRLF" ""        "LF_CRLF" ""        "" &&
	commit_check_warn input ""     ""        "CRLF_LF" "CRLF_LF" ""        ""
'

test_expect_success 'commit files attr=auto' '
	commit_check_warn false "auto" "$WILC"   "$WICL"   "$WAMIX"  ""        "" &&
	commit_check_warn true  "auto" "LF_CRLF" ""        "LF_CRLF" ""        "" &&
	commit_check_warn input "auto" ""        "CRLF_LF" "CRLF_LF" ""        ""
'

test_expect_success 'commit files attr=text' '
	commit_check_warn false "text" "$WILC"   "$WICL"   "$WAMIX"  "$WILC"   "$WICL"   &&
	commit_check_warn true  "text" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" ""        &&
	commit_check_warn input "text" ""        "CRLF_LF" "CRLF_LF" ""        "CRLF_LF"
'

test_expect_success 'commit files attr=-text' '
	commit_check_warn false "-text" ""       ""        ""        ""        "" &&
	commit_check_warn true  "-text" ""       ""        ""        ""        "" &&
	commit_check_warn input "-text" ""       ""        ""        ""        ""
'

test_expect_success 'commit files attr=lf' '
	commit_check_warn false "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF" &&
	commit_check_warn true  "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF" &&
	commit_check_warn input "lf"    ""       "CRLF_LF" "CRLF_LF"  ""       "CRLF_LF"
'

test_expect_success 'commit files attr=crlf' '
	commit_check_warn false "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" "" &&
	commit_check_warn true  "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" "" &&
	commit_check_warn input "crlf" "LF_CRLF" ""        "LF_CRLF" "LF_CRLF" ""
'

# Commit "CRLFmixLF" on top of these files already in the repo:
#                                         mixed     mixed     mixed       mixed       mixed
#                                         onto      onto      onto        onto        onto
#                 attr                    LF        CRLF      CRLFmixLF   LF_mix_CR   CRLFNUL
commit_MIX_chkwrn ""      ""      false   ""        ""        ""          ""          ""
commit_MIX_chkwrn ""      ""      true    "LF_CRLF" ""        ""          "LF_CRLF"   "LF_CRLF"
commit_MIX_chkwrn ""      ""      input   "CRLF_LF" ""        ""          "CRLF_LF"   "CRLF_LF"

commit_MIX_chkwrn "auto"  ""      false   "$WAMIX"  ""        ""          "$WAMIX"    "$WAMIX"
commit_MIX_chkwrn "auto"  ""      true    "LF_CRLF" ""        ""          "LF_CRLF"   "LF_CRLF"
commit_MIX_chkwrn "auto"  ""      input   "CRLF_LF" ""        ""          "CRLF_LF"   "CRLF_LF"

#                 attr                    LF        CRLF      CRLFmixLF   LF_mix_CR   CRLFNUL
commit_chk_wrnNNO ""      ""      false   ""        ""        ""          ""          ""
commit_chk_wrnNNO ""      ""      true    LF_CRLF   ""        ""          ""          ""
commit_chk_wrnNNO ""      ""      input   ""        ""        ""          ""          ""

commit_chk_wrnNNO "auto"  ""      false   "$WILC"   ""        ""          ""          ""
commit_chk_wrnNNO "auto"  ""      true    LF_CRLF   ""        ""          ""          ""
commit_chk_wrnNNO "auto"  ""      input   ""        ""        ""          ""          ""
for crlf in true false input
do
	commit_chk_wrnNNO -text ""      $crlf   ""        ""        ""          ""          ""
	commit_chk_wrnNNO -text lf      $crlf   ""        ""        ""          ""          ""
	commit_chk_wrnNNO -text crlf    $crlf   ""        ""        ""          ""          ""
	commit_chk_wrnNNO ""    lf      $crlf   ""       CRLF_LF    CRLF_LF      ""         CRLF_LF
	commit_chk_wrnNNO ""    crlf    $crlf   LF_CRLF   ""        LF_CRLF     LF_CRLF     ""
	commit_chk_wrnNNO auto  lf    	$crlf   ""        ""        ""          ""          ""
	commit_chk_wrnNNO auto  crlf  	$crlf   LF_CRLF   ""        ""          ""          ""
	commit_chk_wrnNNO text  lf    	$crlf   ""       CRLF_LF    CRLF_LF     ""          CRLF_LF
	commit_chk_wrnNNO text  crlf  	$crlf   LF_CRLF   ""        LF_CRLF     LF_CRLF     ""
done

commit_chk_wrnNNO "text"  ""      false   "$WILC"   "$WICL"   "$WAMIX"    "$WILC"     "$WICL"
commit_chk_wrnNNO "text"  ""      true    LF_CRLF   ""        LF_CRLF     LF_CRLF     ""
commit_chk_wrnNNO "text"  ""      input   ""        CRLF_LF   CRLF_LF     ""          CRLF_LF

test_expect_success 'commit NNO and cleanup' '
	git commit -m "commit files on top of NNO" &&
	rm -f *.txt &&
	git -c core.autocrlf=false reset --hard
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

for crlf in true false input
do
	#                 attr  aeol           LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLFNUL
	check_in_repo_NNO ""    ""     $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO -text ""     $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO -text lf     $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO -text crlf   $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO auto  ""     $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO auto  lf     $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO auto  crlf   $crlf   LF  CRLF  CRLF_mix_LF  LF_mix_CR  CRLF_nul
	check_in_repo_NNO text  ""     $crlf   LF  LF    LF           LF_mix_CR  LF_nul
	check_in_repo_NNO text  lf     $crlf   LF  LF    LF           LF_mix_CR  LF_nul
	check_in_repo_NNO text  crlf   $crlf   LF  LF    LF           LF_mix_CR  LF_nul
done
################################################################################
# Check how files in the repo are changed when they are checked out
# How to read the table below:
# - checkout_files will check multiple files with a combination of settings
#   and attributes (core.autocrlf=input is forbidden with core.eol=crlf)
#
# - parameter $1 	: text in .gitattributs  "" (empty) | auto | text | -text
# - parameter $2 	: ident                  "" | i (i == ident)
# - parameter $3 	: eol in .gitattributs   "" (empty) | lf | crlf
# - parameter $4 	: core.autocrlf          false | true | input
# - parameter $5 	: core.eol               "" | lf | crlf | "native"
# - parameter $6 	: reference for a file with only LF in the repo
# - parameter $7 	: reference for a file with only CRLF in the repo
# - parameter $8 	: reference for a file with mixed LF and CRLF in the repo
# - parameter $9 	: reference for a file with LF and CR in the repo
# - parameter $10 : reference for a file with CRLF and a NUL (should be handled as binary when auto)

if test_have_prereq NATIVE_CRLF
then
MIX_CRLF_LF=CRLF
MIX_LF_CR=CRLF_mix_CR
NL=CRLF
LFNUL=CRLF_nul
else
MIX_CRLF_LF=CRLF_mix_LF
MIX_LF_CR=LF_mix_CR
NL=LF
LFNUL=LF_nul
fi
export CRLF_MIX_LF_CR MIX NL

# Same handling with and without ident
for id in "" ident
do
	for ceol in lf crlf native
	do
		for crlf in true false input
		do
			# -text overrides core.autocrlf and core.eol
			# text and eol=crlf or eol=lf override core.autocrlf and core.eol
			checkout_files -text "$id" ""     "$crlf" "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
			checkout_files -text "$id" "lf"   "$crlf" "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
			checkout_files -text "$id" "crlf" "$crlf" "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
			# text
			checkout_files text  "$id" "lf"   "$crlf" "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
			checkout_files text  "$id" "crlf" "$crlf" "$ceol"  CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
			# currently the same as text, eol=XXX
			checkout_files auto  "$id" "lf"   "$crlf" "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
			checkout_files auto  "$id" "crlf" "$crlf" "$ceol"  CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		done

		# core.autocrlf false, different core.eol
		checkout_files   ""    "$id" ""     false   "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		# core.autocrlf true
		checkout_files   ""    "$id" ""     true    "$ceol"  CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		# text: core.autocrlf = true overrides core.eol
		checkout_files   auto  "$id" ""     true    "$ceol"  CRLF  CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		checkout_files   text  "$id" ""     true    "$ceol"  CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
		# text: core.autocrlf = input overrides core.eol
		checkout_files   text  "$id" ""     input   "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		checkout_files   auto  "$id" ""     input   "$ceol"  LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
		# text=auto + eol=XXX
	done
	# text: core.autocrlf=false uses core.eol
	checkout_files     text  "$id" ""     false   crlf     CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
	checkout_files     text  "$id" ""     false   lf       LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
	# text: core.autocrlf=false and core.eol unset(or native) uses native eol
	checkout_files     text  "$id" ""     false   ""       $NL   CRLF  $MIX_CRLF_LF $MIX_LF_CR   $LFNUL
	checkout_files     text  "$id" ""     false   native   $NL   CRLF  $MIX_CRLF_LF $MIX_LF_CR   $LFNUL
	# auto: core.autocrlf=false and core.eol unset(or native) uses native eol
	checkout_files     auto  "$id" ""     false   ""       $NL   CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
	checkout_files     auto  "$id" ""     false   native   $NL   CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
	# core.autocrlf false, .gitattributes sets eol
	checkout_files     ""    "$id" "lf"   false   ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
	checkout_files     ""    "$id" "crlf" false   ""       CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
	# core.autocrlf true, .gitattributes sets eol
	checkout_files     ""    "$id" "lf"   true    ""       LF    CRLF  CRLF_mix_LF  LF_mix_CR    LF_nul
	checkout_files     ""    "$id" "crlf" true    ""       CRLF  CRLF  CRLF         CRLF_mix_CR  CRLF_nul
done

# Should be the last test case: remove some files from the worktree
test_expect_success 'ls-files --eol -d -z' '
	rm crlf_false_attr__CRLF.txt crlf_false_attr__CRLF_mix_LF.txt crlf_false_attr__LF.txt .gitattributes &&
	cat >expect <<-\EOF &&
	i/crlf w/ crlf_false_attr__CRLF.txt
	i/lf w/ .gitattributes
	i/lf w/ crlf_false_attr__LF.txt
	i/mixed w/ crlf_false_attr__CRLF_mix_LF.txt
	EOF
	git ls-files --eol -d >tmp &&
	sed -e "s!attr/[^	]*!!g" -e "s/	/ /g" -e "s/  */ /g" tmp |
	sort >actual &&
	test_cmp expect actual
'

test_done
