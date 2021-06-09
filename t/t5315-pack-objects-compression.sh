#!/bin/sh

test_description='pack-object compression configuration'

. ./test-lib.sh

test_expect_success setup '
	printf "%2000000s" X |
	git hash-object -w --stdin >object-name &&
	# make sure it resulted in a loose object
	ob=$(sed -e "s/\(..\).*/\1/" object-name) &&
	ject=$(sed -e "s/..\(.*\)/\1/" object-name) &&
	test -f .git/objects/$ob/$ject
'

while read expect config
do
	test_expect_success "pack-objects with $config" '
		test_when_finished "rm -f pack-*.*" &&
		git $config pack-objects pack <object-name &&
		sz=$(test_file_size pack-*.pack) &&
		case "$expect" in
		small) test "$sz" -le 100000 ;;
		large) test "$sz" -ge 100000 ;;
		esac
	'
done <<\EOF
large -c core.compression=0
small -c core.compression=9
large -c core.compression=0 -c pack.compression=0
large -c core.compression=9 -c pack.compression=0
small -c core.compression=0 -c pack.compression=9
small -c core.compression=9 -c pack.compression=9
large -c pack.compression=0
small -c pack.compression=9
EOF

test_done
