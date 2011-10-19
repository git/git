#!/bin/sh

# Check if gpg is available
gpg --version >/dev/null 2>/dev/null
if [ $? -eq 127 ]; then
	say "# gpg not found - skipping tag signing and verification tests"
else
	# As said here: http://www.gnupg.org/documentation/faqs.html#q6.19
	# the gpg version 1.0.6 didn't parse trust packets correctly, so for
	# that version, creation of signed tags using the generated key fails.
	case "$(gpg --version)" in
	'gpg (GnuPG) 1.0.6'*)
		say "Skipping signed tag tests, because a bug in 1.0.6 version"
		;;
	*)
		test_set_prereq GPG
		;;
	esac
fi

# key generation info: gpg --homedir t/t7004 --gen-key
# Type DSA and Elgamal, size 2048 bits, no expiration date.
# Name and email: C O Mitter <committer@example.com>
# No password given, to enable non-interactive operation.

cp -R "$TEST_DIRECTORY"/lib-gpg ./gpghome
chmod 0700 gpghome
GNUPGHOME="$(pwd)/gpghome"
export GNUPGHOME
