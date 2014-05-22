#!/bin/sh

printf "Status: 500 Intentional Breakage\n"

printf "Content-Type: "
case "$PATH_INFO" in
*html*)
	printf "text/html"
	;;
*text*)
	printf "text/plain"
	;;
esac
printf "\n"

printf "\n"
printf "this is the error message\n"
