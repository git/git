#!/bin/sh

out_prefix=$(dirname "$0")/../test-results/valgrind.out
output=
count=0
total_count=0
missing_message=
new_line='
'

# start outputting the current valgrind error in $out_prefix.++$count,
# and the test case which failed in the corresponding .message file
start_output () {
	test -z "$output" || return

	# progress
	total_count=$(($total_count+1))
	test -t 2 && printf "\rFound %d errors" $total_count >&2

	count=$(($count+1))
	output=$out_prefix.$count
	: > $output

	echo "*** $1 ***" > $output.message
}

finish_output () {
	test ! -z "$output" || return
	output=

	# if a test case has more than one valgrind error, we need to
	# copy the last .message file to the previous errors
	test -z "$missing_message" || {
		while test $missing_message -lt $count
		do
			cp $out_prefix.$count.message \
				$out_prefix.$missing_message.message
			missing_message=$(($missing_message+1))
		done
		missing_message=
	}
}

# group the valgrind errors by backtrace
output_all () {
	last_line=
	j=0
	i=1
	while test $i -le $count
	do
		# output <number> <backtrace-in-one-line>
		echo "$i $(tr '\n' ' ' < $out_prefix.$i)"
		i=$(($i+1))
	done |
	sort -t ' ' -k 2 | # order by <backtrace-in-one-line>
	while read number line
	do
		# find duplicates, do not output backtrace twice
		if test "$line" != "$last_line"
		then
			last_line=$line
			j=$(($j+1))
			printf "\nValgrind error $j:\n\n"
			cat $out_prefix.$number
			printf "\nfound in:\n"
		fi
		# print the test case where this came from
		printf "\n"
		cat $out_prefix.$number.message
	done
}

handle_one () {
	OLDIFS=$IFS
	IFS="$new_line"
	while read line
	do
		case "$line" in
		# backtrace, possibly a new one
		==[0-9]*)

			# Does the current valgrind error have a message yet?
			case "$output" in
			*.message)
				test -z "$missing_message" &&
				missing_message=$count
				output=
			esac

			start_output $(basename $1)
			echo "$line" |
			sed 's/==[0-9]*==/==valgrind==/' >> $output
			;;
		# end of backtrace
		'}')
			test -z "$output" || {
				echo "$line" >> $output
				test $output = ${output%.message} &&
				output=$output.message
			}
			;;
		# end of test case
		'')
			finish_output
			;;
		# normal line; if $output is set, print the line
		*)
			test -z "$output" || echo "$line" >> $output
			;;
		esac
	done < $1
	IFS=$OLDIFS

	# just to be safe
	finish_output
}

for test_script in "$(dirname "$0")"/../test-results/*.out
do
	handle_one $test_script
done

output_all
