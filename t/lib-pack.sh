# Support routines for hand-crafting weird or malicious packs.
#
# You can make a complete pack like:
#
#   pack_header 2 >foo.pack &&
#   pack_obj e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 >>foo.pack &&
#   pack_obj e68fe8129b546b101aee9510c5328e7f21ca1d18 >>foo.pack &&
#   pack_trailer foo.pack

# Print the big-endian 4-byte octal representation of $1
uint32_octal () {
	n=$1
	printf '\\%o' $(($n / 16777216)); n=$((n % 16777216))
	printf '\\%o' $(($n /    65536)); n=$((n %    65536))
	printf '\\%o' $(($n /      256)); n=$((n %      256))
	printf '\\%o' $(($n           ));
}

# Print the big-endian 4-byte binary representation of $1
uint32_binary () {
	printf "$(uint32_octal "$1")"
}

# Print a pack header, version 2, for a pack with $1 objects
pack_header () {
	printf 'PACK' &&
	printf '\0\0\0\2' &&
	uint32_binary "$1"
}

# Print the pack data for object $1, as a delta against object $2 (or as a full
# object if $2 is missing or empty). The output is suitable for including
# directly in the packfile, and represents the entirety of the object entry.
# Doing this on the fly (especially picking your deltas) is quite tricky, so we
# have hardcoded some well-known objects. See the case statements below for the
# complete list.
pack_obj () {
	case "$1" in
	# empty blob
	e69de29bb2d1d6434b8b29ae775ad8c2e48c5391)
		case "$2" in
		'')
			printf '\060\170\234\003\0\0\0\0\1'
			return
			;;
		esac
		;;

	# blob containing "\7\76"
	e68fe8129b546b101aee9510c5328e7f21ca1d18)
		case "$2" in
		'')
			printf '\062\170\234\143\267\3\0\0\116\0\106'
			return
			;;
		01d7713666f4de822776c7622c10f1b07de280dc)
			printf '\165\1\327\161\66\146\364\336\202\47\166' &&
			printf '\307\142\54\20\361\260\175\342\200\334\170' &&
			printf '\234\143\142\142\142\267\003\0\0\151\0\114'
			return
			;;
		esac
		;;

	# blob containing "\7\0"
	01d7713666f4de822776c7622c10f1b07de280dc)
		case "$2" in
		'')
			printf '\062\170\234\143\147\0\0\0\20\0\10'
			return
			;;
		e68fe8129b546b101aee9510c5328e7f21ca1d18)
			printf '\165\346\217\350\22\233\124\153\20\32\356' &&
			printf '\225\20\305\62\216\177\41\312\35\30\170\234' &&
			printf '\143\142\142\142\147\0\0\0\53\0\16'
			return
			;;
		esac
		;;
	esac

	echo >&2 "BUG: don't know how to print $1${2:+ (from $2)}"
	return 1
}

# Compute and append pack trailer to "$1"
pack_trailer () {
	test-sha1 -b <"$1" >trailer.tmp &&
	cat trailer.tmp >>"$1" &&
	rm -f trailer.tmp
}

# Remove any existing packs to make sure that
# whatever we index next will be the pack that we
# actually use.
clear_packs () {
	rm -f .git/objects/pack/*
}
