#!/bin/sh

# Try a set of credential helpers; the expected stdin,
# stdout and stderr should be provided on stdin,
# separated by "--".
check() {
	read_chunk >stdin &&
	read_chunk >expect-stdout &&
	read_chunk >expect-stderr &&
	test-credential "$@" <stdin >stdout 2>stderr &&
	test_cmp expect-stdout stdout &&
	test_cmp expect-stderr stderr
}

read_chunk() {
	while read line; do
		case "$line" in
		--) break ;;
		*) echo "$line" ;;
		esac
	done
}


cat >askpass <<\EOF
#!/bin/sh
echo >&2 askpass: $*
what=`echo $1 | cut -d" " -f1 | tr A-Z a-z | tr -cd a-z`
echo "askpass-$what"
EOF
chmod +x askpass
GIT_ASKPASS="$PWD/askpass"
export GIT_ASKPASS
