#!/usr/bin/env bash
# Examples using bdl-lib.sh

# Source bdl-lib.sh
. bdl-lib.sh

# These output to the default bdl_dst=1
bdl
bdl "hi"
bdl 1 "hi"

echo -n >bdl_out.txt # Empty bdl_out.txt

# Output to a file as parameter
bdl bdl_out.txt "hi to bdl_out.txt"
cat bdl_out.txt

echo -n >bdl_out.txt # Empty bdl_out.txt

# Output to a file using bdl_dst
bdl_dst=bdl_out.txt
bdl "hi to bdl_out.txt"
cat bdl_out.txt
bdl_dst=1

echo -n >bdl_out.txt # Empty bdl_out.txt

# Push the current state and change bdl_dst to FD 5
bdl_push
bdl_dst=5
exec 5>bdl_out.txt # Redirect 5 to bdl_out.txt
bdl
bdl "This and previous line via bdl_dst=5"
bdl 5 "hi via 5 directly"
cat bdl_out.txt
bdl_pop

echo -n >bdl_out.txt # Empty bdl_out.txt

# No printing when parameter it 0
bdl_dst=1
bdl 0 "not printed"

echo -n >bdl_out.txt # Empty bdl_out.txt

# No printing when bdl_dst=0 but is printed if direct
bdl_dst=0
bdl
bdl bdl_out.txt "printed to" "bdl_out.txt"
bdl 1 "printed to 1 directly"
bdl "not printed"
cat bdl_out.txt

echo -n >bdl_out.txt # Empty bdl_out.txt

# This prints a "0" since there is only one parameter
bdl_dst=1
bdl 0

# Cleanup
rm bdl_out.txt
