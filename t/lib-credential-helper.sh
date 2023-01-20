setup_credential_helper() {
	test_expect_success 'setup credential helper' '
		CREDENTIAL_HELPER="$TRASH_DIRECTORY/credential-helper.sh" &&
		export CREDENTIAL_HELPER &&
		echo $CREDENTIAL_HELPER &&

		write_script "$CREDENTIAL_HELPER" <<-\EOF
		cmd=$1
		teefile=$cmd-query.cred
		catfile=$cmd-reply.cred
		sed -n -e "/^$/q" -e "p" >> $teefile
		if test "$cmd" = "get"; then
			cat $catfile
		fi
		EOF
	'
}

set_credential_reply() {
	cat >"$TRASH_DIRECTORY/$1-reply.cred"
}

expect_credential_query() {
	cat >"$TRASH_DIRECTORY/$1-expect.cred" &&
	test_cmp "$TRASH_DIRECTORY/$1-expect.cred" \
		 "$TRASH_DIRECTORY/$1-query.cred"
}
