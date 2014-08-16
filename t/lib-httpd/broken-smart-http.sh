printf "Content-Type: text/%s\n" "html"
echo
printf "%s\n" "001e# service=git-upload-pack"
printf "%s"   "0000"
printf "%s%c%s%s\n" \
	"00a58681d9f286a48b08f37b3a095330da16689e3693 HEAD" \
	0 \
	" include-tag multi_ack_detailed multi_ack ofs-delta" \
	" side-band side-band-64k thin-pack no-progress shallow no-done "
printf "%s"   "0000"
