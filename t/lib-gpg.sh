# We always set GNUPGHOME, even if no usable GPG was found, as
#
# - It does not hurt, and
#
# - we cannot set global environment variables in lazy prereqs because they are
#   executed in an eval'ed subshell that changes the working directory to a
#   temporary one.

GNUPGHOME="$PWD/gpghome"
export GNUPGHOME

test_lazy_prereq GPG '
	gpg_version=$(gpg --version 2>&1)
	test $? != 127 || exit 1

	# As said here: http://www.gnupg.org/documentation/faqs.html#q6.19
	# the gpg version 1.0.6 did not parse trust packets correctly, so for
	# that version, creation of signed tags using the generated key fails.
	case "$gpg_version" in
	"gpg (GnuPG) 1.0.6"*)
		say "Your version of gpg (1.0.6) is too buggy for testing"
		exit 1
		;;
	*)
		# Available key info:
		# * Type DSA and Elgamal, size 2048 bits, no expiration date,
		#   name and email: C O Mitter <committer@example.com>
		# * Type RSA, size 2048 bits, no expiration date,
		#   name and email: Eris Discordia <discord@example.net>
		# No password given, to enable non-interactive operation.
		# To generate new key:
		#	gpg --homedir /tmp/gpghome --gen-key
		# To write armored exported key to keyring:
		#	gpg --homedir /tmp/gpghome --export-secret-keys \
		#		--armor 0xDEADBEEF >> lib-gpg/keyring.gpg
		#	gpg --homedir /tmp/gpghome --export \
		#		--armor 0xDEADBEEF >> lib-gpg/keyring.gpg
		# To export ownertrust:
		#	gpg --homedir /tmp/gpghome --export-ownertrust \
		#		> lib-gpg/ownertrust
		mkdir "$GNUPGHOME" &&
		chmod 0700 "$GNUPGHOME" &&
		(gpgconf --kill all || : ) &&
		gpg --homedir "${GNUPGHOME}" --import \
			"$TEST_DIRECTORY"/lib-gpg/keyring.gpg &&
		gpg --homedir "${GNUPGHOME}" --import-ownertrust \
			"$TEST_DIRECTORY"/lib-gpg/ownertrust &&
		gpg --homedir "${GNUPGHOME}" </dev/null >/dev/null \
			--sign -u committer@example.com
		;;
	esac
'

test_lazy_prereq GPGSM '
	test_have_prereq GPG &&
	# Available key info:
	# * see t/lib-gpg/gpgsm-gen-key.in
	# To generate new certificate:
	#  * no passphrase
	#	gpgsm --homedir /tmp/gpghome/ \
	#		-o /tmp/gpgsm.crt.user \
	#		--generate-key \
	#		--batch t/lib-gpg/gpgsm-gen-key.in
	# To import certificate:
	#	gpgsm --homedir /tmp/gpghome/ \
	#		--import /tmp/gpgsm.crt.user
	# To export into a .p12 we can later import:
	#	gpgsm --homedir /tmp/gpghome/ \
	#		-o t/lib-gpg/gpgsm_cert.p12 \
	#		--export-secret-key-p12 "committer@example.com"
	echo | gpgsm --homedir "${GNUPGHOME}" \
		--passphrase-fd 0 --pinentry-mode loopback \
		--import "$TEST_DIRECTORY"/lib-gpg/gpgsm_cert.p12 &&

	gpgsm --homedir "${GNUPGHOME}" -K --with-colons |
	awk -F ":" "/^fpr:/ {printf \"%s S relax\\n\", \$10}" \
		>"${GNUPGHOME}/trustlist.txt" &&
	(gpgconf --reload all || : ) &&

	echo hello | gpgsm --homedir "${GNUPGHOME}" >/dev/null \
	       -u committer@example.com -o /dev/null --sign -
'

test_lazy_prereq RFC1991 '
	test_have_prereq GPG &&
	echo | gpg --homedir "${GNUPGHOME}" -b --rfc1991 >/dev/null
'

GPGSSH_KEY_PRIMARY="${GNUPGHOME}/ed25519_ssh_signing_key"
GPGSSH_KEY_SECONDARY="${GNUPGHOME}/rsa_2048_ssh_signing_key"
GPGSSH_KEY_UNTRUSTED="${GNUPGHOME}/untrusted_ssh_signing_key"
GPGSSH_KEY_EXPIRED="${GNUPGHOME}/expired_ssh_signing_key"
GPGSSH_KEY_NOTYETVALID="${GNUPGHOME}/notyetvalid_ssh_signing_key"
GPGSSH_KEY_TIMEBOXEDVALID="${GNUPGHOME}/timeboxed_valid_ssh_signing_key"
GPGSSH_KEY_TIMEBOXEDINVALID="${GNUPGHOME}/timeboxed_invalid_ssh_signing_key"
GPGSSH_KEY_WITH_PASSPHRASE="${GNUPGHOME}/protected_ssh_signing_key"
GPGSSH_KEY_ECDSA="${GNUPGHOME}/ecdsa_ssh_signing_key"
GPGSSH_KEY_PASSPHRASE="super_secret"
GPGSSH_ALLOWED_SIGNERS="${GNUPGHOME}/ssh.all_valid.allowedSignersFile"

GPGSSH_GOOD_SIGNATURE_TRUSTED='Good "git" signature for'
GPGSSH_GOOD_SIGNATURE_UNTRUSTED='Good "git" signature with'
GPGSSH_KEY_NOT_TRUSTED="No principal matched"
GPGSSH_BAD_SIGNATURE="Signature verification failed"

test_lazy_prereq GPGSSH '
	ssh_version=$(ssh-keygen -Y find-principals -n "git" 2>&1)
	test $? != 127 || exit 1
	echo $ssh_version | grep -q "find-principals:missing signature file"
	test $? = 0 || exit 1;

	# Setup some keys and an allowed signers file
	mkdir -p "${GNUPGHOME}" &&
	chmod 0700 "${GNUPGHOME}" &&
	(setfacl -k "${GNUPGHOME}" 2>/dev/null || true) &&
	ssh-keygen -t ed25519 -N "" -C "git ed25519 key" -f "${GPGSSH_KEY_PRIMARY}" >/dev/null &&
	ssh-keygen -t rsa -b 2048 -N "" -C "git rsa2048 key" -f "${GPGSSH_KEY_SECONDARY}" >/dev/null &&
	ssh-keygen -t ed25519 -N "${GPGSSH_KEY_PASSPHRASE}" -C "git ed25519 encrypted key" -f "${GPGSSH_KEY_WITH_PASSPHRASE}" >/dev/null &&
	ssh-keygen -t ecdsa -N "" -f "${GPGSSH_KEY_ECDSA}" >/dev/null &&
	ssh-keygen -t ed25519 -N "" -C "git ed25519 key" -f "${GPGSSH_KEY_UNTRUSTED}" >/dev/null &&

	cat >"${GPGSSH_ALLOWED_SIGNERS}" <<-EOF &&
	"principal with number 1" $(cat "${GPGSSH_KEY_PRIMARY}.pub")"
	"principal with number 2" $(cat "${GPGSSH_KEY_SECONDARY}.pub")"
	"principal with number 3" $(cat "${GPGSSH_KEY_WITH_PASSPHRASE}.pub")"
	"principal with number 4" $(cat "${GPGSSH_KEY_ECDSA}.pub")"
	EOF

	# Verify if at least one key and ssh-keygen works as expected
	echo "testpayload" |
	ssh-keygen -Y sign -n "git" -f "${GPGSSH_KEY_PRIMARY}" >gpgssh_prereq.sig &&
	ssh-keygen -Y find-principals -f "${GPGSSH_ALLOWED_SIGNERS}" -s gpgssh_prereq.sig &&
	echo "testpayload" |
	ssh-keygen -Y verify -n "git" -f "${GPGSSH_ALLOWED_SIGNERS}" -I "principal with number 1" -s gpgssh_prereq.sig
'

test_lazy_prereq GPGSSH_VERIFYTIME '
	test_have_prereq GPGSSH &&
	# Check if ssh-keygen has a verify-time option by passing an invalid date to it
	ssh-keygen -Overify-time=INVALID -Y check-novalidate -n "git" -s doesnotmatter 2>&1 | grep -q -F "Invalid \"verify-time\"" &&

	# Set up keys with key lifetimes
	ssh-keygen -t ed25519 -N "" -C "timeboxed valid key" -f "${GPGSSH_KEY_TIMEBOXEDVALID}" >/dev/null &&
	key_valid=$(cat "${GPGSSH_KEY_TIMEBOXEDVALID}.pub") &&
	ssh-keygen -t ed25519 -N "" -C "timeboxed invalid key" -f "${GPGSSH_KEY_TIMEBOXEDINVALID}" >/dev/null &&
	key_invalid=$(cat "${GPGSSH_KEY_TIMEBOXEDINVALID}.pub") &&
	ssh-keygen -t ed25519 -N "" -C "expired key" -f "${GPGSSH_KEY_EXPIRED}" >/dev/null &&
	key_expired=$(cat "${GPGSSH_KEY_EXPIRED}.pub") &&
	ssh-keygen -t ed25519 -N "" -C "not yet valid key" -f "${GPGSSH_KEY_NOTYETVALID}" >/dev/null &&
	key_notyetvalid=$(cat "${GPGSSH_KEY_NOTYETVALID}.pub") &&

	# Timestamps outside of test_tick span
	ts2005a=20050401000000 ts2005b=200504020000 &&
	# Timestamps within test_tick span
	ts2005c=20050407000000 ts2005d=200504100000 &&
	# Definitely not yet valid / expired timestamps
	ts2000=20000101000000 ts2999=29990101000000 &&

	cat >>"${GPGSSH_ALLOWED_SIGNERS}" <<-EOF &&
	"timeboxed valid key" valid-after="$ts2005c",valid-before="$ts2005d" $key_valid"
	"timeboxed invalid key" valid-after="$ts2005a",valid-before="$ts2005b" $key_invalid"
	"principal with expired key" valid-before="$ts2000" $key_expired"
	"principal with not yet valid key" valid-after="$ts2999" $key_notyetvalid"
	EOF

	# and verify ssh-keygen verifies the key lifetime
	echo "testpayload" |
	ssh-keygen -Y sign -n "git" -f "${GPGSSH_KEY_EXPIRED}" >gpgssh_verifytime_prereq.sig &&
	! (ssh-keygen -Y verify -n "git" -f "${GPGSSH_ALLOWED_SIGNERS}" -I "principal with expired key" -s gpgssh_verifytime_prereq.sig)
'

sanitize_pgp() {
	perl -ne '
		/^-----END PGP/ and $in_pgp = 0;
		print unless $in_pgp;
		/^-----BEGIN PGP/ and $in_pgp = 1;
	'
}
