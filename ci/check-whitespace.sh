#!/usr/bin/env bash

baseCommit=$1
outputFile=$2
url=$3

problems=()
commit=
commitText=
commitTextmd=
goodParent=

while read dash sha etc
do
	case "${dash}" in
	"---") # Line contains commit information.
		if test -z "${goodParent}"
		then
			# Assume the commit has no whitespace errors until detected otherwise.
			goodParent=${sha}
		fi

		commit="${sha}"
		commitText="${sha} ${etc}"
		commitTextmd="[${sha}](${url}/commit/${sha}) ${etc}"
		;;
	"")
		;;
	*) # Line contains whitespace error information for current commit.
		if test -n "${goodParent}"
		then
			problems+=("1) --- ${commitTextmd}")
			echo ""
			echo "--- ${commitText}"
			goodParent=
		fi

		case "${dash}" in
		*:[1-9]*:) # contains file and line number information
			dashend=${dash#*:}
			problems+=("[${dash}](${url}/blob/${commit}/${dash%%:*}#L${dashend%:}) ${sha} ${etc}")
			;;
		*)
			problems+=("\`${dash} ${sha} ${etc}\`")
			;;
		esac
		echo "${dash} ${sha} ${etc}"
		;;
	esac
done <<< "$(git log --check --pretty=format:"---% h% s" "${baseCommit}"..)"

if test ${#problems[*]} -gt 0
then
	if test -z "${goodParent}"
	then
		goodParent=${baseCommit: 0:7}
	fi

	echo "🛑 Please review the Summary output for further information."
	echo "### :x: A whitespace issue was found in one or more of the commits." >"$outputFile"
	echo "" >>"$outputFile"
	echo "Run these commands to correct the problem:" >>"$outputFile"
	echo "1. \`git rebase --whitespace=fix ${goodParent}\`" >>"$outputFile"
	echo "1. \`git push --force\`" >>"$outputFile"
	echo " " >>"$outputFile"
	echo "Errors:" >>"$outputFile"

	for i in "${problems[@]}"
	do
		echo "${i}" >>"$outputFile"
	done

	exit 2
fi
