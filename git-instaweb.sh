#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

PERL='@@PERL@@'
OPTIONS_KEEPDASHDASH=
OPTIONS_STUCKLONG=
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

SUBDIRECTORY_OK=Yes
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
		if ! echo "$httpd" | grep -- '-f *$' >/dev/null 2>&1
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
	*python*)
		# server is started by running via generated gitweb.py in
		# $fqgitdir/gitweb
		full_httpd="$fqgitdir/gitweb/gitweb.py"
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
		echo "Instance already running. Restarting..."
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
	*mongoose*|*plackup*|*python*)
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
#!@SHELL_PATH@
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
	for candidate in \
		/etc/httpd \
		/usr/lib/apache2 \
		/usr/lib/httpd ;
	do
		if test -d "$candidate/modules"
		then
			module_path="$candidate/modules"
			break
		fi
	done
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

	for mod in mpm_event mpm_prefork mpm_worker
	do
		if test -e $module_path/mod_${mod}.so
		then
			echo "LoadModule ${mod}_module " \
			     "$module_path/mod_${mod}.so" >> "$conf"
			# only one mpm module permitted
			break
		fi
	done
	for mod in mime dir env log_config authz_core unixd
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

	if test -f "$module_path/mod_perl.so"
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
		$list_mods | grep 'mod_cgi\.c' >/dev/null 2>&1 || \
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

python_conf() {
	# Python's builtin http.server and its CGI support is very limited.
	# CGI handler is capable of running CGI script only from inside a directory.
	# Trying to set cgi_directories=["/"] will add double slash to SCRIPT_NAME
	# and that in turn breaks gitweb's relative link generation.

	# create a simple web root where $fqgitdir/gitweb/$httpd_only is our root
	mkdir -p "$fqgitdir/gitweb/$httpd_only/cgi-bin"
	# Python http.server follows the symlinks
	ln -sf "$root/gitweb.cgi" "$fqgitdir/gitweb/$httpd_only/cgi-bin/gitweb.cgi"
	ln -sf "$root/static" "$fqgitdir/gitweb/$httpd_only/"

	# generate a standalone 'python http.server' script in $fqgitdir/gitweb
	# This asumes that python is in user's $PATH
	# This script is Python 2 and 3 compatible
	cat > "$fqgitdir/gitweb/gitweb.py" <<EOF
#!/usr/bin/env python
import os
import sys

# Open log file in line buffering mode
accesslogfile = open("$fqgitdir/gitweb/access.log", 'a', buffering=1)
errorlogfile = open("$fqgitdir/gitweb/error.log", 'a', buffering=1)

# and replace our stdout and stderr with log files
# also do a lowlevel duplicate of the logfile file descriptors so that
# our CGI child process writes any stderr warning also to the log file
_orig_stdout_fd = sys.stdout.fileno()
sys.stdout.close()
os.dup2(accesslogfile.fileno(), _orig_stdout_fd)
sys.stdout = accesslogfile

_orig_stderr_fd = sys.stderr.fileno()
sys.stderr.close()
os.dup2(errorlogfile.fileno(), _orig_stderr_fd)
sys.stderr = errorlogfile

from functools import partial

if sys.version_info < (3, 0):  # Python 2
	from CGIHTTPServer import CGIHTTPRequestHandler
	from BaseHTTPServer import HTTPServer as ServerClass
else:  # Python 3
	from http.server import CGIHTTPRequestHandler
	from http.server import HTTPServer as ServerClass


# Those environment variables will be passed to the cgi script
os.environ.update({
	"GIT_EXEC_PATH": "$GIT_EXEC_PATH",
	"GIT_DIR": "$GIT_DIR",
	"GITWEB_CONFIG": "$GITWEB_CONFIG"
})


class GitWebRequestHandler(CGIHTTPRequestHandler):

	def log_message(self, format, *args):
		# Write access logs to stdout
		sys.stdout.write("%s - - [%s] %s\n" %
				(self.address_string(),
				self.log_date_time_string(),
				format%args))

	def do_HEAD(self):
		self.redirect_path()
		CGIHTTPRequestHandler.do_HEAD(self)

	def do_GET(self):
		if self.path == "/":
			self.send_response(303, "See Other")
			self.send_header("Location", "/cgi-bin/gitweb.cgi")
			self.end_headers()
			return
		self.redirect_path()
		CGIHTTPRequestHandler.do_GET(self)

	def do_POST(self):
		self.redirect_path()
		CGIHTTPRequestHandler.do_POST(self)

	# rewrite path of every request that is not gitweb.cgi to out of cgi-bin
	def redirect_path(self):
		if not self.path.startswith("/cgi-bin/gitweb.cgi"):
			self.path = self.path.replace("/cgi-bin/", "/")

	# gitweb.cgi is the only thing that is ever going to be run here.
	# Ignore everything else
	def is_cgi(self):
		result = False
		if self.path.startswith('/cgi-bin/gitweb.cgi'):
			result = CGIHTTPRequestHandler.is_cgi(self)
		return result


bind = "127.0.0.1"
if "$local" == "true":
	bind = "0.0.0.0"

# Set our http root directory
# This is a work around for a missing directory argument in older Python versions
# as this was added to SimpleHTTPRequestHandler in Python 3.7
os.chdir("$fqgitdir/gitweb/$httpd_only/")

GitWebRequestHandler.protocol_version = "HTTP/1.0"
httpd = ServerClass((bind, $port), GitWebRequestHandler)

sa = httpd.socket.getsockname()
print("Serving HTTP on", sa[0], "port", sa[1], "...")
httpd.serve_forever()
EOF

	chmod a+x "$fqgitdir/gitweb/gitweb.py"
}

gitweb_conf() {
	cat > "$fqgitdir/gitweb/gitweb_config.perl" <<EOF
#!@@PERL@@
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
	*python*)
		python_conf
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
