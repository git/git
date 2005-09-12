#!/bin/sh
flags=
while :; do
  pattern="$1"
  case "$pattern" in
  -i|-I|-a|-E|-H|-h|-l)
    flags="$flags $pattern"
    shift
    ;;
  -e)
    pattern="$2"
    shift
    break
    ;;
  -*)
    echo "unknown flag $pattern" >&2
    exit 1
    ;;
  *)
    break
    ;;
  esac
done
shift
git-ls-files -z "$@" | xargs -0 grep $flags -e "$pattern"
