#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

PERL='@@PERL@@'
OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git instaweb [options] (--start | --stop | --restart)
--
l,local        only bind on 127.0.0.1
p,port=        the port to bind to
d,httpd=       the command to launch
b,browser=     the browser to launch
m,module-path= the module path (only needed for apache2)
 Action
stop           stop the web server
start          start the web server
restart        restart the web server
"

. git-sh-setup

fqgitdir="$GIT_DIR"
local="$(git config --bool --get instaweb.local)"
httpd="$(git config --get instaweb.httpd)"
root="$(git config --get instaweb.gitwebdir)"
port=$(git config --get instaweb.port)
module_path="$(git config --get instaweb.modulepath)"
action="browse"

conf="$GIT_DIR/gitweb/httpd.conf"

# Defaults:

# if installed, it doesn't need further configuration (module_path)
test -z "$httpd" && httpd='lighttpd -f'

# Default is @@GITWEBDIR@@
test -z "$root" && root='@@GITWEBDIR@@'

# any untaken local port will do...
test -z "$port" && port=1234

resolve_full_httpd () {
	case "$httpd" in
	*apache2*|*lighttpd*|*httpd*)
		# yes, *httpd* covers *lighttpd* above, but it is there for clarity
		# ensure that the apache2/lighttpd command ends with "-f"
		if ! echo "$httpd" | sane_grep -- '-f *$' >/dev/null 2>&1
		then
			httpd="$httpd -f"
		fi
		;;
	*plackup*)
		# server is started by running via generated gitweb.psgi in $fqgitdir/gitweb
		full_httpd="$fqgitdir/gitweb/gitweb.psgi"
		httpd_only="${httpd%% *}" # cut on first space
		return
		;;
	*webrick*)
		# server is started by running via generated webrick.rb in
		# $fqgitdir/gitweb
		full_httpd="$fqgitdir/gitweb/webrick.rb"
		httpd_only="${httpd%% *}" # cut on first space
		return
		;;
	esac

	httpd_only="$(echo $httpd | cut -f1 -d' ')"
	if case "$httpd_only" in /*) : ;; *) which $httpd_only >/dev/null 2>&1;; esac
	then
		full_httpd=$httpd
	else
		# many httpds are installed in /usr/sbin or /usr/local/sbin
		# these days and those are not in most users $PATHs
		# in addition, we may have generated a server script
		# in $fqgitdir/gitweb.
		for i in /usr/local/sbin /usr/sbin "$root" "$fqgitdir/gitweb"
		do
			if test -x "$i/$httpd_only"
			then
				full_httpd=$i/$httpd
				return
			fi
		done

		echo >&2 "$httpd_only not found. Install $httpd_only or use" \
		     "--httpd to specify another httpd daemon."
		exit 1
	fi
}

start_httpd () {
	if test -f "$fqgitdir/pid"; then
		say "Instance already running. Restarting..."
		stop_httpd
	fi

	# here $httpd should have a meaningful value
	resolve_full_httpd
	mkdir -p "$fqgitdir/gitweb/$httpd_only"
	conf="$fqgitdir/gitweb/$httpd_only.conf"

	# generate correct config file if it doesn't exist
	test -f "$conf" || configure_httpd
	test -f "$fqgitdir/gitweb/gitweb_config.perl" || gitweb_conf

	# don't quote $full_httpd, there can be arguments to it (-f)
	case "$httpd" in
	*mongoose*|*plackup*)
		#These servers don't have a daemon mode so we'll have to fork it
		$full_httpd "$conf" &
		#Save the pid before doing anything else (we'll print it later)
		pid=$!

		if test $? != 0; then
			echo "Could not execute http daemon $httpd."
			exit 1
		fi

		cat > "$fqgitdir/pid" <<EOF
$pid
EOF
		;;
	*)
		$full_httpd "$conf"
		if test $? != 0; then
			echo "Could not execute http daemon $httpd."
			exit 1
		fi
		;;
	esac
}

stop_httpd () {
	test -f "$fqgitdir/pid" && kill $(cat "$fqgitdir/pid")
	rm -f "$fqgitdir/pid"
}

httpd_is_ready () {
	"$PERL" -MIO::Socket::INET -e "
local \$| = 1; # turn on autoflush
exit if (IO::Socket::INET->new('127.0.0.1:$port'));
print 'Waiting for \'$httpd\' to start ..';
do {
	print '.';
	sleep(1);
} until (IO::Socket::INET->new('127.0.0.1:$port'));
print qq! (done)\n!;
"
}

while test $# != 0
do
	case "$1" in
	--stop|stop)
		action="stop"
		;;
	--start|start)
		action="start"
		;;
	--restart|restart)
		action="restart"
		;;
	-l|--local)
		local=true
		;;
	-d|--httpd)
		shift
		httpd="$1"
		;;
	-b|--browser)
		shift
		browser="$1"
		;;
	-p|--port)
		shift
		port="$1"
		;;
	-m|--module-path)
		shift
		module_path="$1"
		;;
	--)
		;;
	*)
		usage
		;;
	esac
	shift
done

mkdir -p "$GIT_DIR/gitweb/tmp"
GIT_EXEC_PATH="$(git --exec-path)"
GIT_DIR="$fqgitdir"
GITWEB_CONFIG="$fqgitdir/gitweb/gitweb_config.perl"
export GIT_EXEC_PATH GIT_DIR GITWEB_CONFIG

webrick_conf () {
	# webrick seems to have no way of passing arbitrary environment
	# variables to the underlying CGI executable, so we wrap the
	# actual gitweb.cgi using a shell script to force it
  wrapper="$fqgitdir/gitweb/$httpd/wrapper.sh"
	cat > "$wrapper" <<EOF
#!/bin/sh
# we use this shell script wrapper around the real gitweb.cgi since
# there appears to be no other way to pass arbitrary environment variables
# into the CGI process
GIT_EXEC_PATH=$GIT_EXEC_PATH GIT_DIR=$GIT_DIR GITWEB_CONFIG=$GITWEB_CONFIG
export GIT_EXEC_PATH GIT_DIR GITWEB_CONFIG
exec $root/gitweb.cgi
EOF
	chmod +x "$wrapper"

	# This assumes _ruby_ is in the user's $PATH. that's _one_
	# portable way to run ruby, which could be installed anywhere, really.
	# generate a standalone server script in $fqgitdir/gitweb.
	cat >"$fqgitdir/gitweb/$httpd.rb" <<EOF
#!/usr/bin/env ruby
require 'webrick'
require 'logger'
options = {
  :Port => $port,
  :DocumentRoot => "$root",
  :Logger => Logger.new('$fqgitdir/gitweb/error.log'),
  :AccessLog => [
    [ Logger.new('$fqgitdir/gitweb/access.log'),
      WEBrick::AccessLog::COMBINED_LOG_FORMAT ]
  ],
  :DirectoryIndex => ["gitweb.cgi"],
  :CGIInterpreter => "$wrapper",
  :StartCallback => lambda do
    File.open("$fqgitdir/pid", "w") { |f| f.puts Process.pid }
  end,
  :ServerType => WEBrick::Daemon,
}
options[:BindAddress] = '127.0.0.1' if "$local" == "true"
server = WEBrick::HTTPServer.new(options)
['INT', 'TERM'].each do |signal|
  trap(signal) {server.shutdown}
end
server.start
EOF
	chmod +x "$fqgitdir/gitweb/$httpd.rb"
	# configuration is embedded in server script file, webrick.rb
	rm -f "$conf"
}

lighttpd_conf () {
	cat > "$conf" <<EOF
server.document-root = "$root"
server.port = $port
server.modules = ( "mod_setenv", "mod_cgi" )
server.indexfiles = ( "gitweb.cgi" )
server.pid-file = "$fqgitdir/pid"
server.errorlog = "$fqgitdir/gitweb/$httpd_only/error.log"

# to enable, add "mod_access", "mod_accesslog" to server.modules
# variable above and uncomment this
#accesslog.filename = "$fqgitdir/gitweb/$httpd_only/access.log"

setenv.add-environment = ( "PATH" => env.PATH, "GITWEB_CONFIG" => env.GITWEB_CONFIG )

cgi.assign = ( ".cgi" => "" )

# mimetype mapping
mimetype.assign             = (
  ".pdf"          =>      "application/pdf",
  ".sig"          =>      "application/pgp-signature",
  ".spl"          =>      "application/futuresplash",
  ".class"        =>      "application/octet-stream",
  ".ps"           =>      "application/postscript",
  ".torrent"      =>      "application/x-bittorrent",
  ".dvi"          =>      "application/x-dvi",
  ".gz"           =>      "application/x-gzip",
  ".pac"          =>      "application/x-ns-proxy-autoconfig",
  ".swf"          =>      "application/x-shockwave-flash",
  ".tar.gz"       =>      "application/x-tgz",
  ".tgz"          =>      "application/x-tgz",
  ".tar"          =>      "application/x-tar",
  ".zip"          =>      "application/zip",
  ".mp3"          =>      "audio/mpeg",
  ".m3u"          =>      "audio/x-mpegurl",
  ".wma"          =>      "audio/x-ms-wma",
  ".wax"          =>      "audio/x-ms-wax",
  ".ogg"          =>      "application/ogg",
  ".wav"          =>      "audio/x-wav",
  ".gif"          =>      "image/gif",
  ".jpg"          =>      "image/jpeg",
  ".jpeg"         =>      "image/jpeg",
  ".png"          =>      "image/png",
  ".xbm"          =>      "image/x-xbitmap",
  ".xpm"          =>      "image/x-xpixmap",
  ".xwd"          =>      "image/x-xwindowdump",
  ".css"          =>      "text/css",
  ".html"         =>      "text/html",
  ".htm"          =>      "text/html",
  ".js"           =>      "text/javascript",
  ".asc"          =>      "text/plain",
  ".c"            =>      "text/plain",
  ".cpp"          =>      "text/plain",
  ".log"          =>      "text/plain",
  ".conf"         =>      "text/plain",
  ".text"         =>      "text/plain",
  ".txt"          =>      "text/plain",
  ".dtd"          =>      "text/xml",
  ".xml"          =>      "text/xml",
  ".mpeg"         =>      "video/mpeg",
  ".mpg"          =>      "video/mpeg",
  ".mov"          =>      "video/quicktime",
  ".qt"           =>      "video/quicktime",
  ".avi"          =>      "video/x-msvideo",
  ".asf"          =>      "video/x-ms-asf",
  ".asx"          =>      "video/x-ms-asf",
  ".wmv"          =>      "video/x-ms-wmv",
  ".bz2"          =>      "application/x-bzip",
  ".tbz"          =>      "application/x-bzip-compressed-tar",
  ".tar.bz2"      =>      "application/x-bzip-compressed-tar",
  ""              =>      "text/plain"
 )
EOF
	test x"$local" = xtrue && echo 'server.bind = "127.0.0.1"' >> "$conf"
}

apache2_conf () {
	if test -z "$module_path"
	then
		test -d "/usr/lib/httpd/modules" &&
			module_path="/usr/lib/httpd/modules"
		test -d "/usr/lib/apache2/modules" &&
			module_path="/usr/lib/apache2/modules"
	fi
	bind=
	test x"$local" = xtrue && bind='127.0.0.1:'
	echo 'text/css css' > "$fqgitdir/mime.types"
	cat > "$conf" <<EOF
ServerName "git-instaweb"
ServerRoot "$root"
DocumentRoot "$root"
ErrorLog "$fqgitdir/gitweb/$httpd_only/error.log"
CustomLog "$fqgitdir/gitweb/$httpd_only/access.log" combined
PidFile "$fqgitdir/pid"
Listen $bind$port
EOF

	for mod in mime dir env log_config
	do
		if test -e $module_path/mod_${mod}.so
		then
			echo "LoadModule ${mod}_module " \
			     "$module_path/mod_${mod}.so" >> "$conf"
		fi
	done
	cat >> "$conf" <<EOF
TypesConfig "$fqgitdir/mime.types"
DirectoryIndex gitweb.cgi
EOF

	# check to see if Dennis Stosberg's mod_perl compatibility patch
	# (<20060621130708.Gcbc6e5c@leonov.stosberg.net>) has been applied
	if test -f "$module_path/mod_perl.so" &&
	   sane_grep 'MOD_PERL' "$root/gitweb.cgi" >/dev/null
	then
		# favor mod_perl if available
		cat >> "$conf" <<EOF
LoadModule perl_module $module_path/mod_perl.so
PerlPassEnv GIT_DIR
PerlPassEnv GIT_EXEC_PATH
PerlPassEnv GITWEB_CONFIG
<Location /gitweb.cgi>
	SetHandler perl-script
	PerlResponseHandler ModPerl::Registry
	PerlOptions +ParseHeaders
	Options +ExecCGI
</Location>
EOF
	else
		# plain-old CGI
		resolve_full_httpd
		list_mods=$(echo "$full_httpd" | sed 's/-f$/-l/')
		$list_mods | sane_grep 'mod_cgi\.c' >/dev/null 2>&1 || \
		if test -f "$module_path/mod_cgi.so"
		then
			echo "LoadModule cgi_module $module_path/mod_cgi.so" >> "$conf"
		else
			$list_mods | grep 'mod_cgid\.c' >/dev/null 2>&1 || \
			if test -f "$module_path/mod_cgid.so"
			then
				echo "LoadModule cgid_module $module_path/mod_cgid.so" \
					>> "$conf"
			else
				echo "You have no CGI support!"
				exit 2
			fi
			echo "ScriptSock logs/gitweb.sock" >> "$conf"
		fi
		cat >> "$conf" <<EOF
PassEnv GIT_DIR
PassEnv GIT_EXEC_PATH
PassEnv GITWEB_CONFIG
AddHandler cgi-script .cgi
<Location /gitweb.cgi>
	Options +ExecCGI
</Location>
EOF
	fi
}

mongoose_conf() {
	cat > "$conf" <<EOF
# Mongoose web server configuration file.
# Lines starting with '#' and empty lines are ignored.
# For detailed description of every option, visit
# http://code.google.com/p/mongoose/wiki/MongooseManual

root		$root
ports		$port
index_files	gitweb.cgi
#ssl_cert	$fqgitdir/gitweb/ssl_cert.pem
error_log	$fqgitdir/gitweb/$httpd_only/error.log
access_log	$fqgitdir/gitweb/$httpd_only/access.log

#cgi setup
cgi_env		PATH=$PATH,GIT_DIR=$GIT_DIR,GIT_EXEC_PATH=$GIT_EXEC_PATH,GITWEB_CONFIG=$GITWEB_CONFIG
cgi_interp	$PERL
cgi_ext		cgi,pl

# mimetype mapping
mime_types	.gz=application/x-gzip,.tar.gz=application/x-tgz,.tgz=application/x-tgz,.tar=application/x-tar,.zip=application/zip,.gif=image/gif,.jpg=image/jpeg,.jpeg=image/jpeg,.png=image/png,.css=text/css,.html=text/html,.htm=text/html,.js=text/javascript,.c=text/plain,.cpp=text/plain,.log=text/plain,.conf=text/plain,.text=text/plain,.txt=text/plain,.dtd=text/xml,.bz2=application/x-bzip,.tbz=application/x-bzip-compressed-tar,.tar.bz2=application/x-bzip-compressed-tar
EOF
}

plackup_conf () {
	# generate a standalone 'plackup' server script in $fqgitdir/gitweb
	# with embedded configuration; it does not use "$conf" file
	cat > "$fqgitdir/gitweb/gitweb.psgi" <<EOF
#!$PERL

# gitweb - simple web interface to track changes in git repositories
#          PSGI wrapper and server starter (see http://plackperl.org)

use strict;

use IO::Handle;
use Plack::MIME;
use Plack::Builder;
use Plack::App::WrapCGI;
use CGI::Emulate::PSGI 0.07; # minimum version required to work with gitweb

# mimetype mapping (from lighttpd_conf)
Plack::MIME->add_type(
	".pdf"          =>      "application/pdf",
	".sig"          =>      "application/pgp-signature",
	".spl"          =>      "application/futuresplash",
	".class"        =>      "application/octet-stream",
	".ps"           =>      "application/postscript",
	".torrent"      =>      "application/x-bittorrent",
	".dvi"          =>      "application/x-dvi",
	".gz"           =>      "application/x-gzip",
	".pac"          =>      "application/x-ns-proxy-autoconfig",
	".swf"          =>      "application/x-shockwave-flash",
	".tar.gz"       =>      "application/x-tgz",
	".tgz"          =>      "application/x-tgz",
	".tar"          =>      "application/x-tar",
	".zip"          =>      "application/zip",
	".mp3"          =>      "audio/mpeg",
	".m3u"          =>      "audio/x-mpegurl",
	".wma"          =>      "audio/x-ms-wma",
	".wax"          =>      "audio/x-ms-wax",
	".ogg"          =>      "application/ogg",
	".wav"          =>      "audio/x-wav",
	".gif"          =>      "image/gif",
	".jpg"          =>      "image/jpeg",
	".jpeg"         =>      "image/jpeg",
	".png"          =>      "image/png",
	".xbm"          =>      "image/x-xbitmap",
	".xpm"          =>      "image/x-xpixmap",
	".xwd"          =>      "image/x-xwindowdump",
	".css"          =>      "text/css",
	".html"         =>      "text/html",
	".htm"          =>      "text/html",
	".js"           =>      "text/javascript",
	".asc"          =>      "text/plain",
	".c"            =>      "text/plain",
	".cpp"          =>      "text/plain",
	".log"          =>      "text/plain",
	".conf"         =>      "text/plain",
	".text"         =>      "text/plain",
	".txt"          =>      "text/plain",
	".dtd"          =>      "text/xml",
	".xml"          =>      "text/xml",
	".mpeg"         =>      "video/mpeg",
	".mpg"          =>      "video/mpeg",
	".mov"          =>      "video/quicktime",
	".qt"           =>      "video/quicktime",
	".avi"          =>      "video/x-msvideo",
	".asf"          =>      "video/x-ms-asf",
	".asx"          =>      "video/x-ms-asf",
	".wmv"          =>      "video/x-ms-wmv",
	".bz2"          =>      "application/x-bzip",
	".tbz"          =>      "application/x-bzip-compressed-tar",
	".tar.bz2"      =>      "application/x-bzip-compressed-tar",
	""              =>      "text/plain"
);

my \$app = builder {
	# to be able to override \$SIG{__WARN__} to log build time warnings
	use CGI::Carp; # it sets \$SIG{__WARN__} itself

	my \$logdir = "$fqgitdir/gitweb/$httpd_only";
	open my \$access_log_fh, '>>', "\$logdir/access.log"
		or die "Couldn't open access log '\$logdir/access.log': \$!";
	open my \$error_log_fh,  '>>', "\$logdir/error.log"
		or die "Couldn't open error log '\$logdir/error.log': \$!";

	\$access_log_fh->autoflush(1);
	\$error_log_fh->autoflush(1);

	# redirect build time warnings to error.log
	\$SIG{'__WARN__'} = sub {
		my \$msg = shift;
		# timestamp warning like in CGI::Carp::warn
		my \$stamp = CGI::Carp::stamp();
		\$msg =~ s/^/\$stamp/gm;
		print \$error_log_fh \$msg;
	};

	# write errors to error.log, access to access.log
	enable 'AccessLog',
		format => "combined",
		logger => sub { print \$access_log_fh @_; };
	enable sub {
		my \$app = shift;
		sub {
			my \$env = shift;
			\$env->{'psgi.errors'} = \$error_log_fh;
			\$app->(\$env);
		}
	};
	# gitweb currently doesn't work with $SIG{CHLD} set to 'IGNORE',
	# because it uses 'close $fd or die...' on piped filehandle $fh
	# (which causes the parent process to wait for child to finish).
	enable_if { \$SIG{'CHLD'} eq 'IGNORE' } sub {
		my \$app = shift;
		sub {
			my \$env = shift;
			local \$SIG{'CHLD'} = 'DEFAULT';
			local \$SIG{'CLD'}  = 'DEFAULT';
			\$app->(\$env);
		}
	};
	# serve static files, i.e. stylesheet, images, script
	enable 'Static',
		path => sub { m!\.(js|css|png)\$! && s!^/gitweb/!! },
		root => "$root/",
		encoding => 'utf-8'; # encoding for 'text/plain' files
	# convert CGI application to PSGI app
	Plack::App::WrapCGI->new(script => "$root/gitweb.cgi")->to_app;
};

# make it runnable as standalone app,
# like it would be run via 'plackup' utility
if (caller) {
	return \$app;
} else {
	require Plack::Runner;

	my \$runner = Plack::Runner->new();
	\$runner->parse_options(qw(--env deployment --port $port),
				"$local" ? qw(--host 127.0.0.1) : ());
	\$runner->run(\$app);
}
__END__
EOF

	chmod a+x "$fqgitdir/gitweb/gitweb.psgi"
	# configuration is embedded in server script file, gitweb.psgi
	rm -f "$conf"
}

gitweb_conf() {
	cat > "$fqgitdir/gitweb/gitweb_config.perl" <<EOF
#!/usr/bin/perl
our \$projectroot = "$(dirname "$fqgitdir")";
our \$git_temp = "$fqgitdir/gitweb/tmp";
our \$projects_list = \$projectroot;

\$feature{'remote_heads'}{'default'} = [1];
EOF
}

configure_httpd() {
	case "$httpd" in
	*lighttpd*)
		lighttpd_conf
		;;
	*apache2*|*httpd*)
		apache2_conf
		;;
	webrick)
		webrick_conf
		;;
	*mongoose*)
		mongoose_conf
		;;
	*plackup*)
		plackup_conf
		;;
	*)
		echo "Unknown httpd specified: $httpd"
		exit 1
		;;
	esac
}

case "$action" in
stop)
	stop_httpd
	exit 0
	;;
start)
	start_httpd
	exit 0
	;;
restart)
	stop_httpd
	start_httpd
	exit 0
	;;
esac

gitweb_conf

resolve_full_httpd
mkdir -p "$fqgitdir/gitweb/$httpd_only"
conf="$fqgitdir/gitweb/$httpd_only.conf"

configure_httpd

start_httpd
url=http://127.0.0.1:$port

if test -n "$browser"; then
	httpd_is_ready && git web--browse -b "$browser" $url || echo $url
else
	httpd_is_ready && git web--browse -c "instaweb.browser" $url || echo $url
fi
