#!/usr/bin/env bash
#
# Check that commits after a specified point do not contain new or modified
# lines with whitespace errors. An optional formatted summary can be generated
# by providing an output file path and url as additional arguments.
#

baseCommit=$1
outputFile=$2
url=$3

if test "$#" -ne 1 && test "$#" -ne 3
then
	echo "USAGE: $0 <BASE_COMMIT> [<OUTPUT_FILE> <URL>]"
	exit 1
fi

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

	echo "A whitespace issue was found in onen of more of the commits."
	echo "Run the following command to resolve whitespace issues:"
	echo "git rebase --whitespace=fix ${goodParent}"

	# If target output file is provided, write formatted output.
	if test -n "$outputFile"
	then
		echo "🛑 Please review the Summary output for further information."
		(
			echo "### :x: A whitespace issue was found in one or more of the commits."
			echo ""
			echo "Run these commands to correct the problem:"
			echo "1. \`git rebase --whitespace=fix ${goodParent}\`"
			echo "1. \`git push --force\`"
			echo ""
			echo "Errors:"

			for i in "${problems[@]}"
			do
				echo "${i}"
			done
		) >"$outputFile"
	fi

	exit 2
fi
