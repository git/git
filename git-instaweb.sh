#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#
USAGE='[--start] [--stop] [--restart]
  [--local] [--httpd=<httpd>] [--port=<port>] [--browser=<browser>]
  [--module-path=<path> (for Apache2 only)]'

. git-sh-setup

fqgitdir="$GIT_DIR"
local="`git config --bool --get instaweb.local`"
httpd="`git config --get instaweb.httpd`"
browser="`git config --get instaweb.browser`"
port=`git config --get instaweb.port`
module_path="`git config --get instaweb.modulepath`"

conf="$GIT_DIR/gitweb/httpd.conf"

# Defaults:

# if installed, it doesn't need further configuration (module_path)
test -z "$httpd" && httpd='lighttpd -f'

# probably the most popular browser among gitweb users
test -z "$browser" && browser='firefox'

# any untaken local port will do...
test -z "$port" && port=1234

start_httpd () {
	httpd_only="`echo $httpd | cut -f1 -d' '`"
	if case "$httpd_only" in /*) : ;; *) which $httpd_only >/dev/null;; esac
	then
		$httpd "$fqgitdir/gitweb/httpd.conf"
	else
		# many httpds are installed in /usr/sbin or /usr/local/sbin
		# these days and those are not in most users $PATHs
		# in addition, we may have generated a server script
		# in $fqgitdir/gitweb.
		for i in /usr/local/sbin /usr/sbin "$fqgitdir/gitweb"
		do
			if test -x "$i/$httpd_only"
			then
				# don't quote $httpd, there can be
				# arguments to it (-f)
				$i/$httpd "$fqgitdir/gitweb/httpd.conf"
				return
			fi
		done
		echo "$httpd_only not found. Install $httpd_only or use" \
		     "--httpd to specify another http daemon."
		exit 1
	fi
	if test $? != 0; then
		echo "Could not execute http daemon $httpd."
		exit 1
	fi
}

stop_httpd () {
	test -f "$fqgitdir/pid" && kill `cat "$fqgitdir/pid"`
}

while test $# != 0
do
	case "$1" in
	--stop|stop)
		stop_httpd
		exit 0
		;;
	--start|start)
		start_httpd
		exit 0
		;;
	--restart|restart)
		stop_httpd
		start_httpd
		exit 0
		;;
	--local|-l)
		local=true
		;;
	-d|--httpd|--httpd=*)
		case "$#,$1" in
		*,*=*)
			httpd=`expr "$1" : '-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			httpd="$2"
			shift ;;
		esac
		;;
	-b|--browser|--browser=*)
		case "$#,$1" in
		*,*=*)
			browser=`expr "$1" : '-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			browser="$2"
			shift ;;
		esac
		;;
	-p|--port|--port=*)
		case "$#,$1" in
		*,*=*)
			port=`expr "$1" : '-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			port="$2"
			shift ;;
		esac
		;;
	-m|--module-path=*|--module-path)
		case "$#,$1" in
		*,*=*)
			module_path=`expr "$1" : '-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			module_path="$2"
			shift ;;
		esac
		;;
	*)
		usage
		;;
	esac
	shift
done

mkdir -p "$GIT_DIR/gitweb/tmp"
GIT_EXEC_PATH="`git --exec-path`"
GIT_DIR="$fqgitdir"
export GIT_EXEC_PATH GIT_DIR


webrick_conf () {
	# generate a standalone server script in $fqgitdir/gitweb.
	cat >"$fqgitdir/gitweb/$httpd.rb" <<EOF
require 'webrick'
require 'yaml'
options = YAML::load_file(ARGV[0])
options[:StartCallback] = proc do
  File.open(options[:PidFile],"w") do |f|
    f.puts Process.pid
  end
end
options[:ServerType] = WEBrick::Daemon
server = WEBrick::HTTPServer.new(options)
['INT', 'TERM'].each do |signal|
  trap(signal) {server.shutdown}
end
server.start
EOF
	# generate a shell script to invoke the above ruby script,
	# which assumes _ruby_ is in the user's $PATH. that's _one_
	# portable way to run ruby, which could be installed anywhere,
	# really.
	cat >"$fqgitdir/gitweb/$httpd" <<EOF
#!/bin/sh
exec ruby "$fqgitdir/gitweb/$httpd.rb" \$*
EOF
	chmod +x "$fqgitdir/gitweb/$httpd"

	cat >"$conf" <<EOF
:Port: $port
:DocumentRoot: "$fqgitdir/gitweb"
:DirectoryIndex: ["gitweb.cgi"]
:PidFile: "$fqgitdir/pid"
EOF
	test "$local" = true && echo ':BindAddress: "127.0.0.1"' >> "$conf"
}

lighttpd_conf () {
	cat > "$conf" <<EOF
server.document-root = "$fqgitdir/gitweb"
server.port = $port
server.modules = ( "mod_cgi" )
server.indexfiles = ( "gitweb.cgi" )
server.pid-file = "$fqgitdir/pid"
cgi.assign = ( ".cgi" => "" )
mimetype.assign = ( ".css" => "text/css" )
EOF
	test x"$local" = xtrue && echo 'server.bind = "127.0.0.1"' >> "$conf"
}

apache2_conf () {
	test -z "$module_path" && module_path=/usr/lib/apache2/modules
	mkdir -p "$GIT_DIR/gitweb/logs"
	bind=
	test x"$local" = xtrue && bind='127.0.0.1:'
	echo 'text/css css' > $fqgitdir/mime.types
	cat > "$conf" <<EOF
ServerName "git-instaweb"
ServerRoot "$fqgitdir/gitweb"
DocumentRoot "$fqgitdir/gitweb"
PidFile "$fqgitdir/pid"
Listen $bind$port
EOF

	for mod in mime dir; do
		if test -e $module_path/mod_${mod}.so; then
			echo "LoadModule ${mod}_module " \
			     "$module_path/mod_${mod}.so" >> "$conf"
		fi
	done
	cat >> "$conf" <<EOF
TypesConfig $fqgitdir/mime.types
DirectoryIndex gitweb.cgi
EOF

	# check to see if Dennis Stosberg's mod_perl compatibility patch
	# (<20060621130708.Gcbc6e5c@leonov.stosberg.net>) has been applied
	if test -f "$module_path/mod_perl.so" && grep '^our $gitbin' \
				"$GIT_DIR/gitweb/gitweb.cgi" >/dev/null
	then
		# favor mod_perl if available
		cat >> "$conf" <<EOF
LoadModule perl_module $module_path/mod_perl.so
PerlPassEnv GIT_DIR
PerlPassEnv GIT_EXEC_DIR
<Location /gitweb.cgi>
	SetHandler perl-script
	PerlResponseHandler ModPerl::Registry
	PerlOptions +ParseHeaders
	Options +ExecCGI
</Location>
EOF
	else
		# plain-old CGI
		list_mods=`echo "$httpd" | sed "s/-f$/-l/"`
		$list_mods | grep 'mod_cgi\.c' >/dev/null 2>&1 || \
		echo "LoadModule cgi_module $module_path/mod_cgi.so" >> "$conf"
		cat >> "$conf" <<EOF
AddHandler cgi-script .cgi
<Location /gitweb.cgi>
	Options +ExecCGI
</Location>
EOF
	fi
}

script='
s#^\(my\|our\) $projectroot =.*#\1 $projectroot = "'$(dirname "$fqgitdir")'";#
s#\(my\|our\) $gitbin =.*#\1 $gitbin = "'$GIT_EXEC_PATH'";#
s#\(my\|our\) $projects_list =.*#\1 $projects_list = $projectroot;#
s#\(my\|our\) $git_temp =.*#\1 $git_temp = "'$fqgitdir/gitweb/tmp'";#'

gitweb_cgi () {
	cat > "$1.tmp" <<\EOFGITWEB
@@GITWEB_CGI@@
EOFGITWEB
	sed "$script" "$1.tmp"  > "$1"
	chmod +x "$1"
	rm -f "$1.tmp"
}

gitweb_css () {
	cat > "$1" <<\EOFGITWEB
@@GITWEB_CSS@@
EOFGITWEB
}

gitweb_cgi "$GIT_DIR/gitweb/gitweb.cgi"
gitweb_css "$GIT_DIR/gitweb/gitweb.css"

case "$httpd" in
*lighttpd*)
	lighttpd_conf
	;;
*apache2*)
	apache2_conf
	;;
webrick)
	webrick_conf
	;;
*)
	echo "Unknown httpd specified: $httpd"
	exit 1
	;;
esac

start_httpd
url=http://127.0.0.1:$port
"$browser" $url || echo $url
