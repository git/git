#!/bin/sh

cat <<EOF
/* Automatically generated by bugreport-generate-config-safelist.sh */


static const char *bugreport_config_safelist[] = {
EOF

# cat all regular files in Documentation/config
find Documentation/config -type f -exec cat {} \; |
# print the command name which matches the annotate-bugreport macro
sed -n 's/^\(.*\) \+annotate:bugreport\[include\].* ::$/  "\1",/p' | sort

cat <<EOF
};
EOF
