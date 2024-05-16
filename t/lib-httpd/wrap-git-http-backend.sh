if [ "$REQUEST_METHOD" = "GET" ]; then
	"$GIT_EXEC_PATH"/git-http-backend "$@"
elif [ "$REQUEST_METHOD" = "POST" ]; then
	printf "Status: 429 Too Many Requests\n"
	echo
	printf "Too many requests"
fi
