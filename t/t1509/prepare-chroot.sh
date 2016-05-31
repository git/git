#!/bin/sh

die() {
	echo >&2 "$@"
	exit 1
}

xmkdir() {
	while [ -n "$1" ]; do
		[ -d "$1" ] || mkdir "$1" || die "Unable to mkdir $1"
		shift
	done
}

R="$1"

[ "$(id -u)" -eq 0 ] && die "This script should not be run as root, what if it does rm -rf /?"
[ -n "$R" ] || die "usage: prepare-chroot.sh <root>"
[ -x git ] || die "This script needs to be executed at git source code's top directory"
if [ -x /bin/busybox ]; then
	BB=/bin/busybox
elif [ -x /usr/bin/busybox ]; then
	BB=/usr/bin/busybox
else
	die "You need busybox"
fi

xmkdir "$R" "$R/bin" "$R/etc" "$R/lib" "$R/dev"
touch "$R/dev/null"
echo "root:x:0:0:root:/:/bin/sh" > "$R/etc/passwd"
echo "$(id -nu):x:$(id -u):$(id -g)::$(pwd)/t:/bin/sh" >> "$R/etc/passwd"
echo "root::0:root" > "$R/etc/group"
echo "$(id -ng)::$(id -g):$(id -nu)" >> "$R/etc/group"

[ -x "$R$BB" ] || cp $BB "$R/bin/busybox"
for cmd in sh su ls expr tr basename rm mkdir mv id uname dirname cat true sed diff; do
	ln -f -s /bin/busybox "$R/bin/$cmd"
done

mkdir -p "$R$(pwd)"
rsync --exclude-from t/t1509/excludes -Ha . "$R$(pwd)"
# Fake perl to reduce dependency, t1509 does not use perl, but some
# env might slip through, see test-lib.sh, unset.*PERL_PATH
sed 's|^PERL_PATH=.*|PERL_PATH=/bin/true|' GIT-BUILD-OPTIONS > "$R$(pwd)/GIT-BUILD-OPTIONS"
for cmd in git $BB;do 
	ldd $cmd | grep '/' | sed 's,.*\s\(/[^ ]*\).*,\1,' | while read i; do
		mkdir -p "$R$(dirname $i)"
		cp "$i" "$R/$i"
	done
done
cat <<EOF
All is set up in $R, execute t1509 with the following commands:

sudo chroot $R /bin/su - $(id -nu)
IKNOWWHATIAMDOING=YES ./t1509-root-worktree.sh -v -i

When you are done, simply delete $R to clean up
EOF
