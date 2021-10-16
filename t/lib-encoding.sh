# Encoding helpers

test_lazy_prereq NO_UTF16_BOM '
	test $(printf abc | iconv -f UTF-8 -t UTF-16 | wc -c) = 6
'

test_lazy_prereq NO_UTF32_BOM '
	test $(printf abc | iconv -f UTF-8 -t UTF-32 | wc -c) = 12
'

write_utf16 () {
	if test_have_prereq NO_UTF16_BOM
	then
		printf '\376\377'
	fi &&
	iconv -f UTF-8 -t UTF-16
}

write_utf32 () {
	if test_have_prereq NO_UTF32_BOM
	then
		printf '\0\0\376\377'
	fi &&
	iconv -f UTF-8 -t UTF-32
}
