#!/bin/sh

fixed=0
success=0
failed=0
broken=0
total=0

for file
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
			failed=$(($failed + $value)) ;;
		broken)
			broken=$(($broken + $value)) ;;
		total)
			total=$(($total + $value)) ;;
		esac
	done <"$file"
done

printf "%-8s%d\n" fixed $fixed
printf "%-8s%d\n" success $success
printf "%-8s%d\n" failed $failed
printf "%-8s%d\n" broken $broken
printf "%-8s%d\n" total $total
