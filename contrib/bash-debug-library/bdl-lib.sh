# ##################################################################
# Bash Debug logger scriplet, source into your script to use.
#
# See README for more information.
# ##################################################################

BDL_LOADED=t

# Prompt waitng for a return, q will exit
bdl_pause () {
	read -p "Line ${BASH_LINENO}: $@" bdl_pause_v_
	[[ "$bdl_pause_v_" == "q" ]] && exit 1
}

# Initialize bdl variables if user didn't
[[ "$bdl_dst" == "" ]] && bdl_dst=1
[[ "$bdl_call_depth" == "" ]] && bdl_call_depth=0
[[ "$bdl_call_stack_view" == "" ]] && bdl_call_stack_view=f
bdl_push_result=""


# Initialize priviate bdl variables
_bdl_call_lineno_offset_array=()
_bdl_call_save=()
_bdl_call_save_idx=0

# Push bdl state and initialize source line offset meta data.
#
# $1 Optional value for bdl_call_depth
# $2 Optional text of a script with where bdl calls
#    will be found and used to compute lineno info.
bdl_push () {
	# Push the bdl data
	_bdl_call_save[${_bdl_call_save_idx}]="bdl_dst=${bdl_dst}; \
bdl_call_depth=$bdl_call_depth; \
bdl_call_stack_view=$bdl_call_stack_view"
	_bdl_call_save_idx=$((_bdl_call_save_idx+1))

	# Set call depth
	if test "$1" != ""
	then
		bdl_call_depth=$1
		shift
	fi

	# Process text for bdl commands and add source line offset, slo@=
	if test "$1" != ""
	then
		# Convert "bdl to bdl slo@=<source line offset> so
		# bdl can compute line line number.
		IFS=$'\n' read -d '' -r -a test_run_script_array <<< "$@"
		bdl_push_result=
		for i in "${!test_run_script_array[@]}"; do
			ln=${test_run_script_array[$i]}
			ln=$(sed -E "s/([[:space:]]*)bdl slo@=[[:digit:]]+([[:space:]]+|$)|([[:space:]]*)bdl([[:space:]]+|$)/\1\3bdl slo@=$((i+1))\2\4/g" <<< "$ln")
			bdl_push_result+=$(echo "$ln")
			bdl_push_result+=$'\n'
		done
	fi
}

# Pop a previously save state.
bdl_pop () {
	_bdl_call_save_idx=$((_bdl_call_save_idx-1))
	eval "${_bdl_call_save[$_bdl_call_save_idx]}"
}

# Write debug info with no source or line number
bdl_nsl () {
	if (( $# > 1 )); then
		bdl_nsl_v_=$1
		shift
	else
		bdl_nsl_v_=$bdl_dst
	fi

	[[ "$bdl_nsl_v_" = "0" ]] && return 0
	if [[ "$bdl_nsl_v_" != "" && "$@" != "" ]]; then
		if [[ $bdl_nsl_v_ =~ ^[0-9] ]]; then
			# There's probably a better way, but this "works":
			case $bdl_nsl_v_ in
				1) echo "$@" 1>&1 ;;
				2) echo "$@" 1>&2 ;;
				3) echo "$@" 1>&3 ;;
				4) echo "$@" 1>&4 ;;
				5) echo "$@" 1>&5 ;;
				6) echo "$@" 1>&6 ;;
				7) echo "$@" 1>&7 ;;
				8) echo "$@" 1>&8 ;;
				9) echo "$@" 1>&9 ;;
				*) : ;; # 0 and all other characters are nop's
			esac
		else
			echo "$@" >> $bdl_nsl_v_
		fi
	fi
	bdl_nsl_v_=
	return 0
}

# Write debug info with file name and line number.
bdl () {
	# View the call stack
	if test "${bdl_call_stack_view}" != "f"
	then
		for i in "${!BASH_SOURCE[@]}"; do
			(( $i == 0 )) && ln=${LINENO} || ln=${BASH_LINENO[${i}-1]}
			bdl_nsl "[$i] ${BASH_SOURCE[$i]##*/}:${FUNCNAME[$i]}:${ln}"
		done
	fi

	# Process named parameter which must be the first parameter
	case $1 in
		slo@=*) bdl_offset="${1##*slo@=}"; shift ;;
	esac

	bdl_ln=${BASH_LINENO[${bdl_call_depth}]}
	if test "$bdl_offset" != ""
	then
		bdl_ln=$((bdl_ln+bdl_offset))
		bdl_offset=
	fi

	if (( $# <= 1 )); then
		bdl_nsl $bdl_dst "${BASH_SOURCE[${bdl_call_depth}+1]##*/}:${bdl_ln}:${@:+ }$@"
	else
		v_=$1
		shift
		bdl_nsl $v_ "${BASH_SOURCE[${bdl_call_depth}+1]##*/}:${bdl_ln}:${@:+ }$@"
	fi
	return 0
}
