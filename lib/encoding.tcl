# git-gui encoding support
# Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
# (Copied from gitk, commit fd8ccbec4f0161)

# This list of encoding names and aliases is distilled from
# https://www.iana.org/assignments/character-sets.
# Not all of them are supported by Tcl.
set encoding_aliases {
    { ANSI_X3.4-1968 iso-ir-6 ANSI_X3.4-1986 ISO_646.irv:1991 ASCII
      ISO646-US US-ASCII us IBM367 cp367 csASCII }
    { ISO-10646-UTF-1 csISO10646UTF1 }
    { ISO_646.basic:1983 ref csISO646basic1983 }
    { INVARIANT csINVARIANT }
    { ISO_646.irv:1983 iso-ir-2 irv csISO2IntlRefVersion }
    { BS_4730 iso-ir-4 ISO646-GB gb uk csISO4UnitedKingdom }
    { NATS-SEFI iso-ir-8-1 csNATSSEFI }
    { NATS-SEFI-ADD iso-ir-8-2 csNATSSEFIADD }
    { NATS-DANO iso-ir-9-1 csNATSDANO }
    { NATS-DANO-ADD iso-ir-9-2 csNATSDANOADD }
    { SEN_850200_B iso-ir-10 FI ISO646-FI ISO646-SE se csISO10Swedish }
    { SEN_850200_C iso-ir-11 ISO646-SE2 se2 csISO11SwedishForNames }
    { KS_C_5601-1987 iso-ir-149 KS_C_5601-1989 KSC_5601 korean csKSC56011987 }
    { ISO-2022-KR csISO2022KR }
    { EUC-KR csEUCKR }
    { ISO-2022-JP csISO2022JP }
    { ISO-2022-JP-2 csISO2022JP2 }
    { JIS_C6220-1969-jp JIS_C6220-1969 iso-ir-13 katakana x0201-7
      csISO13JISC6220jp }
    { JIS_C6220-1969-ro iso-ir-14 jp ISO646-JP csISO14JISC6220ro }
    { IT iso-ir-15 ISO646-IT csISO15Italian }
    { PT iso-ir-16 ISO646-PT csISO16Portuguese }
    { ES iso-ir-17 ISO646-ES csISO17Spanish }
    { greek7-old iso-ir-18 csISO18Greek7Old }
    { latin-greek iso-ir-19 csISO19LatinGreek }
    { DIN_66003 iso-ir-21 de ISO646-DE csISO21German }
    { NF_Z_62-010_(1973) iso-ir-25 ISO646-FR1 csISO25French }
    { Latin-greek-1 iso-ir-27 csISO27LatinGreek1 }
    { ISO_5427 iso-ir-37 csISO5427Cyrillic }
    { JIS_C6226-1978 iso-ir-42 csISO42JISC62261978 }
    { BS_viewdata iso-ir-47 csISO47BSViewdata }
    { INIS iso-ir-49 csISO49INIS }
    { INIS-8 iso-ir-50 csISO50INIS8 }
    { INIS-cyrillic iso-ir-51 csISO51INISCyrillic }
    { ISO_5427:1981 iso-ir-54 ISO5427Cyrillic1981 }
    { ISO_5428:1980 iso-ir-55 csISO5428Greek }
    { GB_1988-80 iso-ir-57 cn ISO646-CN csISO57GB1988 }
    { GB_2312-80 iso-ir-58 chinese csISO58GB231280 }
    { NS_4551-1 iso-ir-60 ISO646-NO no csISO60DanishNorwegian
      csISO60Norwegian1 }
    { NS_4551-2 ISO646-NO2 iso-ir-61 no2 csISO61Norwegian2 }
    { NF_Z_62-010 iso-ir-69 ISO646-FR fr csISO69French }
    { videotex-suppl iso-ir-70 csISO70VideotexSupp1 }
    { PT2 iso-ir-84 ISO646-PT2 csISO84Portuguese2 }
    { ES2 iso-ir-85 ISO646-ES2 csISO85Spanish2 }
    { MSZ_7795.3 iso-ir-86 ISO646-HU hu csISO86Hungarian }
    { JIS_C6226-1983 iso-ir-87 x0208 JIS_X0208-1983 csISO87JISX0208 }
    { greek7 iso-ir-88 csISO88Greek7 }
    { ASMO_449 ISO_9036 arabic7 iso-ir-89 csISO89ASMO449 }
    { iso-ir-90 csISO90 }
    { JIS_C6229-1984-a iso-ir-91 jp-ocr-a csISO91JISC62291984a }
    { JIS_C6229-1984-b iso-ir-92 ISO646-JP-OCR-B jp-ocr-b
      csISO92JISC62991984b }
    { JIS_C6229-1984-b-add iso-ir-93 jp-ocr-b-add csISO93JIS62291984badd }
    { JIS_C6229-1984-hand iso-ir-94 jp-ocr-hand csISO94JIS62291984hand }
    { JIS_C6229-1984-hand-add iso-ir-95 jp-ocr-hand-add
      csISO95JIS62291984handadd }
    { JIS_C6229-1984-kana iso-ir-96 csISO96JISC62291984kana }
    { ISO_2033-1983 iso-ir-98 e13b csISO2033 }
    { ANSI_X3.110-1983 iso-ir-99 CSA_T500-1983 NAPLPS csISO99NAPLPS }
    { ISO_8859-1:1987 iso-ir-100 ISO_8859-1 ISO-8859-1 latin1 l1 IBM819
      CP819 csISOLatin1 }
    { ISO_8859-2:1987 iso-ir-101 ISO_8859-2 ISO-8859-2 latin2 l2 csISOLatin2 }
    { T.61-7bit iso-ir-102 csISO102T617bit }
    { T.61-8bit T.61 iso-ir-103 csISO103T618bit }
    { ISO_8859-3:1988 iso-ir-109 ISO_8859-3 ISO-8859-3 latin3 l3 csISOLatin3 }
    { ISO_8859-4:1988 iso-ir-110 ISO_8859-4 ISO-8859-4 latin4 l4 csISOLatin4 }
    { ECMA-cyrillic iso-ir-111 KOI8-E csISO111ECMACyrillic }
    { CSA_Z243.4-1985-1 iso-ir-121 ISO646-CA csa7-1 ca csISO121Canadian1 }
    { CSA_Z243.4-1985-2 iso-ir-122 ISO646-CA2 csa7-2 csISO122Canadian2 }
    { CSA_Z243.4-1985-gr iso-ir-123 csISO123CSAZ24341985gr }
    { ISO_8859-6:1987 iso-ir-127 ISO_8859-6 ISO-8859-6 ECMA-114 ASMO-708
      arabic csISOLatinArabic }
    { ISO_8859-6-E csISO88596E ISO-8859-6-E }
    { ISO_8859-6-I csISO88596I ISO-8859-6-I }
    { ISO_8859-7:1987 iso-ir-126 ISO_8859-7 ISO-8859-7 ELOT_928 ECMA-118
      greek greek8 csISOLatinGreek }
    { T.101-G2 iso-ir-128 csISO128T101G2 }
    { ISO_8859-8:1988 iso-ir-138 ISO_8859-8 ISO-8859-8 hebrew
      csISOLatinHebrew }
    { ISO_8859-8-E csISO88598E ISO-8859-8-E }
    { ISO_8859-8-I csISO88598I ISO-8859-8-I }
    { CSN_369103 iso-ir-139 csISO139CSN369103 }
    { JUS_I.B1.002 iso-ir-141 ISO646-YU js yu csISO141JUSIB1002 }
    { ISO_6937-2-add iso-ir-142 csISOTextComm }
    { IEC_P27-1 iso-ir-143 csISO143IECP271 }
    { ISO_8859-5:1988 iso-ir-144 ISO_8859-5 ISO-8859-5 cyrillic
      csISOLatinCyrillic }
    { JUS_I.B1.003-serb iso-ir-146 serbian csISO146Serbian }
    { JUS_I.B1.003-mac macedonian iso-ir-147 csISO147Macedonian }
    { ISO_8859-9:1989 iso-ir-148 ISO_8859-9 ISO-8859-9 latin5 l5 csISOLatin5 }
    { greek-ccitt iso-ir-150 csISO150 csISO150GreekCCITT }
    { NC_NC00-10:81 cuba iso-ir-151 ISO646-CU csISO151Cuba }
    { ISO_6937-2-25 iso-ir-152 csISO6937Add }
    { GOST_19768-74 ST_SEV_358-88 iso-ir-153 csISO153GOST1976874 }
    { ISO_8859-supp iso-ir-154 latin1-2-5 csISO8859Supp }
    { ISO_10367-box iso-ir-155 csISO10367Box }
    { ISO-8859-10 iso-ir-157 l6 ISO_8859-10:1992 csISOLatin6 latin6 }
    { latin-lap lap iso-ir-158 csISO158Lap }
    { JIS_X0212-1990 x0212 iso-ir-159 csISO159JISX02121990 }
    { DS_2089 DS2089 ISO646-DK dk csISO646Danish }
    { us-dk csUSDK }
    { dk-us csDKUS }
    { JIS_X0201 X0201 csHalfWidthKatakana }
    { KSC5636 ISO646-KR csKSC5636 }
    { ISO-10646-UCS-2 csUnicode }
    { ISO-10646-UCS-4 csUCS4 }
    { DEC-MCS dec csDECMCS }
    { hp-roman8 roman8 r8 csHPRoman8 }
    { macintosh mac csMacintosh }
    { IBM037 cp037 ebcdic-cp-us ebcdic-cp-ca ebcdic-cp-wt ebcdic-cp-nl
      csIBM037 }
    { IBM038 EBCDIC-INT cp038 csIBM038 }
    { IBM273 CP273 csIBM273 }
    { IBM274 EBCDIC-BE CP274 csIBM274 }
    { IBM275 EBCDIC-BR cp275 csIBM275 }
    { IBM277 EBCDIC-CP-DK EBCDIC-CP-NO csIBM277 }
    { IBM278 CP278 ebcdic-cp-fi ebcdic-cp-se csIBM278 }
    { IBM280 CP280 ebcdic-cp-it csIBM280 }
    { IBM281 EBCDIC-JP-E cp281 csIBM281 }
    { IBM284 CP284 ebcdic-cp-es csIBM284 }
    { IBM285 CP285 ebcdic-cp-gb csIBM285 }
    { IBM290 cp290 EBCDIC-JP-kana csIBM290 }
    { IBM297 cp297 ebcdic-cp-fr csIBM297 }
    { IBM420 cp420 ebcdic-cp-ar1 csIBM420 }
    { IBM423 cp423 ebcdic-cp-gr csIBM423 }
    { IBM424 cp424 ebcdic-cp-he csIBM424 }
    { IBM437 cp437 437 csPC8CodePage437 }
    { IBM500 CP500 ebcdic-cp-be ebcdic-cp-ch csIBM500 }
    { IBM775 cp775 csPC775Baltic }
    { IBM850 cp850 850 csPC850Multilingual }
    { IBM851 cp851 851 csIBM851 }
    { IBM852 cp852 852 csPCp852 }
    { IBM855 cp855 855 csIBM855 }
    { IBM857 cp857 857 csIBM857 }
    { IBM860 cp860 860 csIBM860 }
    { IBM861 cp861 861 cp-is csIBM861 }
    { IBM862 cp862 862 csPC862LatinHebrew }
    { IBM863 cp863 863 csIBM863 }
    { IBM864 cp864 csIBM864 }
    { IBM865 cp865 865 csIBM865 }
    { IBM866 cp866 866 csIBM866 }
    { IBM868 CP868 cp-ar csIBM868 }
    { IBM869 cp869 869 cp-gr csIBM869 }
    { IBM870 CP870 ebcdic-cp-roece ebcdic-cp-yu csIBM870 }
    { IBM871 CP871 ebcdic-cp-is csIBM871 }
    { IBM880 cp880 EBCDIC-Cyrillic csIBM880 }
    { IBM891 cp891 csIBM891 }
    { IBM903 cp903 csIBM903 }
    { IBM904 cp904 904 csIBBM904 }
    { IBM905 CP905 ebcdic-cp-tr csIBM905 }
    { IBM918 CP918 ebcdic-cp-ar2 csIBM918 }
    { IBM1026 CP1026 csIBM1026 }
    { EBCDIC-AT-DE csIBMEBCDICATDE }
    { EBCDIC-AT-DE-A csEBCDICATDEA }
    { EBCDIC-CA-FR csEBCDICCAFR }
    { EBCDIC-DK-NO csEBCDICDKNO }
    { EBCDIC-DK-NO-A csEBCDICDKNOA }
    { EBCDIC-FI-SE csEBCDICFISE }
    { EBCDIC-FI-SE-A csEBCDICFISEA }
    { EBCDIC-FR csEBCDICFR }
    { EBCDIC-IT csEBCDICIT }
    { EBCDIC-PT csEBCDICPT }
    { EBCDIC-ES csEBCDICES }
    { EBCDIC-ES-A csEBCDICESA }
    { EBCDIC-ES-S csEBCDICESS }
    { EBCDIC-UK csEBCDICUK }
    { EBCDIC-US csEBCDICUS }
    { UNKNOWN-8BIT csUnknown8BiT }
    { MNEMONIC csMnemonic }
    { MNEM csMnem }
    { VISCII csVISCII }
    { VIQR csVIQR }
    { KOI8-R csKOI8R }
    { IBM00858 CCSID00858 CP00858 PC-Multilingual-850+euro }
    { IBM00924 CCSID00924 CP00924 ebcdic-Latin9--euro }
    { IBM01140 CCSID01140 CP01140 ebcdic-us-37+euro }
    { IBM01141 CCSID01141 CP01141 ebcdic-de-273+euro }
    { IBM01142 CCSID01142 CP01142 ebcdic-dk-277+euro ebcdic-no-277+euro }
    { IBM01143 CCSID01143 CP01143 ebcdic-fi-278+euro ebcdic-se-278+euro }
    { IBM01144 CCSID01144 CP01144 ebcdic-it-280+euro }
    { IBM01145 CCSID01145 CP01145 ebcdic-es-284+euro }
    { IBM01146 CCSID01146 CP01146 ebcdic-gb-285+euro }
    { IBM01147 CCSID01147 CP01147 ebcdic-fr-297+euro }
    { IBM01148 CCSID01148 CP01148 ebcdic-international-500+euro }
    { IBM01149 CCSID01149 CP01149 ebcdic-is-871+euro }
    { IBM1047 IBM-1047 }
    { PTCP154 csPTCP154 PT154 CP154 Cyrillic-Asian }
    { Amiga-1251 Ami1251 Amiga1251 Ami-1251 }
    { UNICODE-1-1 csUnicode11 }
    { CESU-8 csCESU-8 }
    { BOCU-1 csBOCU-1 }
    { UNICODE-1-1-UTF-7 csUnicode11UTF7 }
    { ISO-8859-14 iso-ir-199 ISO_8859-14:1998 ISO_8859-14 latin8 iso-celtic
      l8 }
    { ISO-8859-15 ISO_8859-15 Latin-9 }
    { ISO-8859-16 iso-ir-226 ISO_8859-16:2001 ISO_8859-16 latin10 l10 }
    { GBK CP936 MS936 windows-936 }
    { JIS_Encoding csJISEncoding }
    { Shift_JIS MS_Kanji csShiftJIS ShiftJIS Shift-JIS }
    { Extended_UNIX_Code_Packed_Format_for_Japanese csEUCPkdFmtJapanese
      EUC-JP }
    { Extended_UNIX_Code_Fixed_Width_for_Japanese csEUCFixWidJapanese }
    { ISO-10646-UCS-Basic csUnicodeASCII }
    { ISO-10646-Unicode-Latin1 csUnicodeLatin1 ISO-10646 }
    { ISO-Unicode-IBM-1261 csUnicodeIBM1261 }
    { ISO-Unicode-IBM-1268 csUnicodeIBM1268 }
    { ISO-Unicode-IBM-1276 csUnicodeIBM1276 }
    { ISO-Unicode-IBM-1264 csUnicodeIBM1264 }
    { ISO-Unicode-IBM-1265 csUnicodeIBM1265 }
    { ISO-8859-1-Windows-3.0-Latin-1 csWindows30Latin1 }
    { ISO-8859-1-Windows-3.1-Latin-1 csWindows31Latin1 }
    { ISO-8859-2-Windows-Latin-2 csWindows31Latin2 }
    { ISO-8859-9-Windows-Latin-5 csWindows31Latin5 }
    { Adobe-Standard-Encoding csAdobeStandardEncoding }
    { Ventura-US csVenturaUS }
    { Ventura-International csVenturaInternational }
    { PC8-Danish-Norwegian csPC8DanishNorwegian }
    { PC8-Turkish csPC8Turkish }
    { IBM-Symbols csIBMSymbols }
    { IBM-Thai csIBMThai }
    { HP-Legal csHPLegal }
    { HP-Pi-font csHPPiFont }
    { HP-Math8 csHPMath8 }
    { Adobe-Symbol-Encoding csHPPSMath }
    { HP-DeskTop csHPDesktop }
    { Ventura-Math csVenturaMath }
    { Microsoft-Publishing csMicrosoftPublishing }
    { Windows-31J csWindows31J }
    { GB2312 csGB2312 }
    { Big5 csBig5 }
}

set encoding_groups {
    {"" ""
	{"Unicode" UTF-8}
	{"Western" ISO-8859-1}}
    {we "West European"
	{"Western" ISO-8859-15 CP-437 CP-850 MacRoman CP-1252 Windows-1252}
	{"Celtic" ISO-8859-14}
	{"Greek" ISO-8859-14 ISO-8859-7 CP-737 CP-869 MacGreek CP-1253 Windows-1253}
	{"Icelandic" MacIceland MacIcelandic CP-861}
	{"Nordic" ISO-8859-10 CP-865}
	{"Portuguese" CP-860}
	{"South European" ISO-8859-3}}
    {ee "East European"
	{"Baltic" CP-775 ISO-8859-4 ISO-8859-13 CP-1257 Windows-1257}
	{"Central European" CP-852 ISO-8859-2 MacCE CP-1250 Windows-1250}
	{"Croatian" MacCroatian}
	{"Cyrillic" CP-855 ISO-8859-5 ISO-IR-111 KOI8-R MacCyrillic CP-1251 Windows-1251}
	{"Russian" CP-866}
	{"Ukrainian" KOI8-U MacUkraine MacUkrainian}
	{"Romanian" ISO-8859-16 MacRomania MacRomanian}}
    {ea "East Asian"
	{"Generic" ISO-2022}
	{"Chinese Simplified" GB2312 GB1988 GB12345 GB2312-RAW GBK EUC-CN GB18030 HZ ISO-2022-CN}
	{"Chinese Traditional" Big5 Big5-HKSCS EUC-TW CP-950}
	{"Japanese" EUC-JP ISO-2022-JP Shift-JIS JIS-0212 JIS-0208 JIS-0201 CP-932 MacJapan}
	{"Korean" EUC-KR UHC JOHAB ISO-2022-KR CP-949 KSC5601}}
    {sa "SE & SW Asian"
	{"Armenian" ARMSCII-8}
	{"Georgian" GEOSTD8}
	{"Thai" TIS-620 ISO-8859-11 CP-874 Windows-874 MacThai}
	{"Turkish" CP-857 CP857 ISO-8859-9 MacTurkish CP-1254 Windows-1254}
	{"Vietnamese" TCVN VISCII VPS CP-1258 Windows-1258}
	{"Hindi" MacDevanagari}
	{"Gujarati" MacGujarati}
	{"Gurmukhi" MacGurmukhi}}
    {me "Middle Eastern"
	{"Arabic" ISO-8859-6 Windows-1256 CP-1256 CP-864 MacArabic}
	{"Farsi" MacFarsi}
	{"Hebrew" ISO-8859-8-I Windows-1255 CP-1255 ISO-8859-8 CP-862 MacHebrew}}
    {mi "Misc"
	{"7-bit" ASCII}
	{"16-bit" Unicode}
	{"Legacy" CP-863 EBCDIC}
	{"Symbol" Symbol Dingbats MacDingbats MacCentEuro}}
}

proc build_encoding_table {} {
	global encoding_aliases encoding_lookup_table

	# Prepare the lookup list; cannot use lsort -nocase because
	# of compatibility issues with older Tcl (e.g. in msysgit)
	set names [list]
	foreach item [encoding names] {
		lappend names [list [string tolower $item] $item]
	}
	set names [lsort -ascii -index 0 $names]
	# neither can we use lsearch -index
	set lnames [list]
	foreach item $names {
		lappend lnames [lindex $item 0]
	}

	foreach grp $encoding_aliases {
		set target {}
		foreach item $grp {
			set i [lsearch -sorted -ascii $lnames \
					[string tolower $item]]
			if {$i >= 0} {
				set target [lindex $names $i 1]
				break
			}
		}
		if {$target eq {}} continue
		foreach item $grp {
			set encoding_lookup_table([string tolower $item]) $target
		}
	}

	foreach item $names {
		set encoding_lookup_table([lindex $item 0]) [lindex $item 1]
	}
}

proc tcl_encoding {enc} {
	global encoding_lookup_table
	if {$enc eq {}} {
		return {}
	}
	if {![info exists encoding_lookup_table]} {
		build_encoding_table
	}
	set enc [string tolower $enc]
	if {![info exists encoding_lookup_table($enc)]} {
		# look for "isonnn" instead of "iso-nnn" or "iso_nnn"
		if {[regsub {^(iso|cp|ibm|jis)[-_]} $enc {\1} encx]} {
			set enc $encx
		}
	}
	if {[info exists encoding_lookup_table($enc)]} {
		return $encoding_lookup_table($enc)
	} else {
		return {}
	}
}

proc force_path_encoding {path enc} {
	global path_encoding_overrides last_encoding_override

	set enc [tcl_encoding $enc]
	if {$enc eq {}} {
		catch { unset last_encoding_override }
		catch { unset path_encoding_overrides($path) }
	} else {
		set last_encoding_override $enc
		if {$path ne {}} {
			set path_encoding_overrides($path) $enc
		}
	}
}

proc get_path_encoding {path} {
	global path_encoding_overrides last_encoding_override

	if {[info exists last_encoding_override]} {
		set tcl_enc $last_encoding_override
	} else {
		set tcl_enc [tcl_encoding [get_config gui.encoding]]
	}
	if {$tcl_enc eq {}} {
		set tcl_enc [encoding system]
	}
	if {$path ne {}} {
		if {[info exists path_encoding_overrides($path)]} {
			set enc2 $path_encoding_overrides($path)
		} else {
			set enc2 [tcl_encoding [gitattr $path encoding $tcl_enc]]
		}
		if {$enc2 ne {}} {
			set tcl_enc $enc2
		}
	}
	return $tcl_enc
}

proc build_encoding_submenu {parent grp cmd} {
	global used_encodings

	set mid [lindex $grp 0]
	set gname [mc [lindex $grp 1]]

	set smenu {}
	foreach subset [lrange $grp 2 end] {
		set name [mc [lindex $subset 0]]

		foreach enc [lrange $subset 1 end] {
			set tcl_enc [tcl_encoding $enc]
			if {$tcl_enc eq {}} continue

			if {$smenu eq {}} {
				if {$mid eq {}} {
					set smenu $parent
				} else {
					set smenu "$parent.$mid"
					menu $smenu
					$parent add cascade \
						-label $gname \
						-menu $smenu
				}
			}

			if {$name ne {}} {
				set lbl "$name ($enc)"
			} else {
				set lbl $enc
			}
			$smenu add command \
				-label $lbl \
				-command [concat $cmd [list $tcl_enc]]

			lappend used_encodings $tcl_enc
		}
	}
}

proc popup_btn_menu {m b} {
	tk_popup $m [winfo pointerx $b] [winfo pointery $b]
}

proc build_encoding_menu {emenu cmd {nodef 0}} {
	$emenu configure -postcommand \
		[list do_build_encoding_menu $emenu $cmd $nodef]
}

proc do_build_encoding_menu {emenu cmd {nodef 0}} {
	global used_encodings encoding_groups

	$emenu configure -postcommand {}

	if {!$nodef} {
		$emenu add command \
			-label [mc "Default"] \
			-command [concat $cmd [list {}]]
	}
	set sysenc [encoding system]
	$emenu add command \
		-label [mc "System (%s)" $sysenc] \
		-command [concat $cmd [list $sysenc]]

	# Main encoding tree
	set used_encodings [list identity]
	$emenu add separator
	foreach grp $encoding_groups {
		build_encoding_submenu $emenu $grp $cmd
	}

	# Add unclassified encodings
	set unused_grp [list [mc Other]]
	foreach enc [encoding names] {
		if {[lsearch -exact $used_encodings $enc] < 0} {
			lappend unused_grp $enc
		}
	}
	build_encoding_submenu $emenu [list other [mc Other] $unused_grp] $cmd
}
