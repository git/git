#!/bin/sh

failed_tests=
fixed=0
success=0
failed=0
broken=0
total=0
missing_prereq=

for file in "$1"/t*-*.counts
do
	while read type value
	do
		case $type in
		'')
			continue ;;
		fixed)
			fixed=$(($fixed + $value)) ;;
		success)
			success=$(($success + $value)) ;;
		failed)
			failed=$(($failed + $value))
			if test $value != 0
			then
				testnum=$(expr "$file" : 'test-results/\(t[0-9]*\)-')
				failed_tests="$failed_tests $testnum"
			fi
			;;
		broken)
			broken=$(($broken + $value)) ;;
		total)
			total=$(($total + $value)) ;;
		missing_prereq)
			missing_prereq="$missing_prereq,$value" ;;
		esac
	done <"$file"
done

if test -n "$missing_prereq"
then
	unique_missing_prereq=$(
		echo $missing_prereq |
		tr -s "," "\n" |
		grep -v '^$' |
		sort -u |
		paste -s -d ' ')
	if test -n "$unique_missing_prereq"
	then
		printf "\nmissing prereq: $unique_missing_prereq\n\n"
	fi
fi

if test -n "$failed_tests"
then
	printf "\nfailed test(s):$failed_tests\n\n"
fi

printf "%-8s%d\n" fixed $fixed
printf "%-8s%d\n" success $success
printf "%-8s%d\n" failed $failed
printf "%-8s%d\n" broken $broken
printf "%-8s%d\n" total $total
