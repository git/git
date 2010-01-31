#!/bin/sh

# This script displays the distribution of longest common hash prefixes.
# This can be used to determine the minimum prefix length to use
# for object names to be unique.

git rev-list --objects --all | sort | perl -lne '
  substr($_, 40) = "";
  # uncomment next line for a distribution of bits instead of hex chars
  # $_ = unpack("B*",pack("H*",$_));
  if (defined $p) {
    ($p ^ $_) =~ /^(\0*)/;
    $common = length $1;
    if (defined $pcommon) {
      $count[$pcommon > $common ? $pcommon : $common]++;
    } else {
      $count[$common]++; # first item
    }
  }
  $p = $_;
  $pcommon = $common;
  END {
    $count[$common]++; # last item
    print "$_: $count[$_]" for 0..$#count;
  }
'
