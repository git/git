#!/bin/sh

gpg_version=$(gpg --version 2>&1)
if test $? = 127; then
	say "You do not seem to have gpg installed"
else
	# As said here: http://www.gnupg.org/documentation/faqs.html#q6.19
	# the gpg version 1.0.6 didn't parse trust packets correctly, so for
	# that version, creation of signed tags using the generated key fails.
	case "$gpg_version" in
	'gpg (GnuPG) 1.0.6'*)
		say "Your version of gpg (1.0.6) is too buggy for testing"
		;;
	*)
		# key generation info: gpg --homedir t/lib-gpg --gen-key
		# Type DSA and Elgamal, size 2048 bits, no expiration date.
		# Name and email: C O Mitter <committer@example.com>
		# No password given, to enable non-interactive operation.
		cp -R "$TEST_DIRECTORY"/lib-gpg ./gpghome
		chmod 0700 gpghome
		GNUPGHOME="$(pwd)/gpghome"
		export GNUPGHOME
		test_set_prereq GPG
		;;
	esac
fi

sanitize_pgp() {
	perl -ne '
		/^-----END PGP/ and $in_pgp = 0;
		print unless $in_pgp;
		/^-----BEGIN PGP/ and $in_pgp = 1;
	'
}
