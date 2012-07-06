# Copyright (C) 2012
#     Charles Roussel <charles.roussel@ensimag.imag.fr>
#     Simon Cathebras <simon.cathebras@ensimag.imag.fr>
#     Julien Khayat <julien.khayat@ensimag.imag.fr>
#     Guillaume Sasdy <guillaume.sasdy@ensimag.imag.fr>
#     Simon Perrat <simon.perrat@ensimag.imag.fr>
# License: GPL v2 or later

#
# CONFIGURATION VARIABLES
# You might want to change these ones
#

. ./test.config

CURR_DIR=$(pwd)
TEST_OUTPUT_DIRECTORY=$(pwd)
TEST_DIRECTORY="$CURR_DIR"/../../../t

export TEST_OUTPUT_DIRECTORY TEST_DIRECTORY CURR_DIR

if test "$LIGHTTPD" = "false" ; then
	PORT=80
else
	WIKI_DIR_INST="$CURR_DIR/$WEB_WWW"
fi

# ================= Install part =================

error () {
	echo "$@" >&2
	exit 1
}

# config_lighttpd
#
# Create the configuration files and the folders necessary to start lighttpd.
# Overwrite any existing file.
config_lighttpd () {
	mkdir -p $WEB
	mkdir -p $WEB_TMP
	mkdir -p $WEB_WWW
	cat > $WEB/lighttpd.conf <<EOF
	server.document-root = "$CURR_DIR/$WEB_WWW"
	server.port = $PORT
	server.pid-file = "$CURR_DIR/$WEB_TMP/pid"

	server.modules = (
	"mod_rewrite",
	"mod_redirect",
	"mod_access",
	"mod_accesslog",
	"mod_fastcgi"
	)

	index-file.names = ("index.php" , "index.html")

	mimetype.assign		    = (
	".pdf"		=>	"application/pdf",
	".sig"		=>	"application/pgp-signature",
	".spl"		=>	"application/futuresplash",
	".class"	=>	"application/octet-stream",
	".ps"		=>	"application/postscript",
	".torrent"	=>	"application/x-bittorrent",
	".dvi"		=>	"application/x-dvi",
	".gz"		=>	"application/x-gzip",
	".pac"		=>	"application/x-ns-proxy-autoconfig",
	".swf"		=>	"application/x-shockwave-flash",
	".tar.gz"	=>	"application/x-tgz",
	".tgz"		=>	"application/x-tgz",
	".tar"		=>	"application/x-tar",
	".zip"		=>	"application/zip",
	".mp3"		=>	"audio/mpeg",
	".m3u"		=>	"audio/x-mpegurl",
	".wma"		=>	"audio/x-ms-wma",
	".wax"		=>	"audio/x-ms-wax",
	".ogg"		=>	"application/ogg",
	".wav"		=>	"audio/x-wav",
	".gif"		=>	"image/gif",
	".jpg"		=>	"image/jpeg",
	".jpeg"		=>	"image/jpeg",
	".png"		=>	"image/png",
	".xbm"		=>	"image/x-xbitmap",
	".xpm"		=>	"image/x-xpixmap",
	".xwd"		=>	"image/x-xwindowdump",
	".css"		=>	"text/css",
	".html"		=>	"text/html",
	".htm"		=>	"text/html",
	".js"		=>	"text/javascript",
	".asc"		=>	"text/plain",
	".c"		=>	"text/plain",
	".cpp"		=>	"text/plain",
	".log"		=>	"text/plain",
	".conf"		=>	"text/plain",
	".text"		=>	"text/plain",
	".txt"		=>	"text/plain",
	".dtd"		=>	"text/xml",
	".xml"		=>	"text/xml",
	".mpeg"		=>	"video/mpeg",
	".mpg"		=>	"video/mpeg",
	".mov"		=>	"video/quicktime",
	".qt"		=>	"video/quicktime",
	".avi"		=>	"video/x-msvideo",
	".asf"		=>	"video/x-ms-asf",
	".asx"		=>	"video/x-ms-asf",
	".wmv"		=>	"video/x-ms-wmv",
	".bz2"		=>	"application/x-bzip",
	".tbz"		=>	"application/x-bzip-compressed-tar",
	".tar.bz2"	=>	"application/x-bzip-compressed-tar",
	""		=>	"text/plain"
	)

	fastcgi.server = ( ".php" =>
	("localhost" =>
	( "socket" => "$CURR_DIR/$WEB_TMP/php.socket",
	"bin-path" => "$PHP_DIR/php-cgi -c $CURR_DIR/$WEB/php.ini"

	)
	)
	)
EOF

	cat > $WEB/php.ini <<EOF
	session.save_path ='$CURR_DIR/$WEB_TMP'
EOF
}

# start_lighttpd
#
# Start or restart daemon lighttpd. If restart, rewrite configuration files.
start_lighttpd () {
	if test -f "$WEB_TMP/pid"; then
		echo "Instance already running. Restarting..."
		stop_lighttpd
	fi
	config_lighttpd
	"$LIGHTTPD_DIR"/lighttpd -f "$WEB"/lighttpd.conf

	if test $? -ne 0 ; then
		echo "Could not execute http deamon lighttpd"
		exit 1
	fi
}

# stop_lighttpd
#
# Kill daemon lighttpd and removes files and folders associated.
stop_lighttpd () {
	test -f "$WEB_TMP/pid" && kill $(cat "$WEB_TMP/pid")
	rm -rf "$WEB"
}

# Create the SQLite database of the MediaWiki. If the database file already
# exists, it will be deleted.
# This script should be runned from the directory where $FILES_FOLDER is
# located.
create_db () {
	rm -f "$TMP/$DB_FILE"

	echo "Generating the SQLite database file. It can take some time ..."
	# Run the php script to generate the SQLite database file
	# with cURL calls.
	php "$FILES_FOLDER/$DB_INSTALL_SCRIPT" $(basename "$DB_FILE" .sqlite) \
		"$WIKI_ADMIN" "$WIKI_PASSW" "$TMP" "$PORT"

	if [ ! -f "$TMP/$DB_FILE" ] ; then
		error "Can't create database file $TMP/$DB_FILE. Try to run ./install-wiki.sh delete first."
	fi

	# Copy the generated database file into the directory the
	# user indicated.
	cp "$TMP/$DB_FILE" "$FILES_FOLDER" ||
		error "Unable to copy $TMP/$DB_FILE to $FILES_FOLDER"
}

# Install a wiki in your web server directory.
wiki_install () {
	if test $LIGHTTPD = "true" ; then
		start_lighttpd
	fi

	SERVER_ADDR=$SERVER_ADDR:$PORT
	# In this part, we change directory to $TMP in order to download,
	# unpack and copy the files of MediaWiki
	(
	mkdir -p "$WIKI_DIR_INST/$WIKI_DIR_NAME"
	if [ ! -d "$WIKI_DIR_INST/$WIKI_DIR_NAME" ] ; then
		error "Folder $WIKI_DIR_INST/$WIKI_DIR_NAME doesn't exist.
		Please create it and launch the script again."
	fi

	# Fetch MediaWiki's archive if not already present in the TMP directory
	cd "$TMP"
	if [ ! -f "$MW_VERSION.tar.gz" ] ; then
		echo "Downloading $MW_VERSION sources ..."
		wget "http://download.wikimedia.org/mediawiki/1.19/mediawiki-1.19.0.tar.gz" ||
			error "Unable to download "\
			"http://download.wikimedia.org/mediawiki/1.19/"\
			"mediawiki-1.19.0.tar.gz. "\
			"Please fix your connection and launch the script again."
		echo "$MW_VERSION.tar.gz downloaded in `pwd`. "\
			"You can delete it later if you want."
	else
		echo "Reusing existing $MW_VERSION.tar.gz downloaded in `pwd`."
	fi
	archive_abs_path=$(pwd)/"$MW_VERSION.tar.gz"
	cd "$WIKI_DIR_INST/$WIKI_DIR_NAME/" ||
		error "can't cd to $WIKI_DIR_INST/$WIKI_DIR_NAME/"
	tar xzf "$archive_abs_path" --strip-components=1 ||
		error "Unable to extract WikiMedia's files from $archive_abs_path to "\
			"$WIKI_DIR_INST/$WIKI_DIR_NAME"
	) || exit 1

	create_db

	# Copy the generic LocalSettings.php in the web server's directory
	# And modify parameters according to the ones set at the top
	# of this script.
	# Note that LocalSettings.php is never modified.
	if [ ! -f "$FILES_FOLDER/LocalSettings.php" ] ; then
		error "Can't find $FILES_FOLDER/LocalSettings.php " \
			"in the current folder. "\
		"Please run the script inside its folder."
	fi
	cp "$FILES_FOLDER/LocalSettings.php" \
		"$FILES_FOLDER/LocalSettings-tmp.php" ||
		error "Unable to copy $FILES_FOLDER/LocalSettings.php " \
		"to $FILES_FOLDER/LocalSettings-tmp.php"

	# Parse and set the LocalSettings file of the user according to the
	# CONFIGURATION VARIABLES section at the beginning of this script
	file_swap="$FILES_FOLDER/LocalSettings-swap.php"
	sed "s,@WG_SCRIPT_PATH@,/$WIKI_DIR_NAME," \
		"$FILES_FOLDER/LocalSettings-tmp.php" > "$file_swap"
	mv "$file_swap" "$FILES_FOLDER/LocalSettings-tmp.php"
	sed "s,@WG_SERVER@,http://$SERVER_ADDR," \
		"$FILES_FOLDER/LocalSettings-tmp.php" > "$file_swap"
	mv "$file_swap" "$FILES_FOLDER/LocalSettings-tmp.php"
	sed "s,@WG_SQLITE_DATADIR@,$TMP," \
		"$FILES_FOLDER/LocalSettings-tmp.php" > "$file_swap"
	mv "$file_swap" "$FILES_FOLDER/LocalSettings-tmp.php"
	sed "s,@WG_SQLITE_DATAFILE@,$( basename $DB_FILE .sqlite)," \
		"$FILES_FOLDER/LocalSettings-tmp.php" > "$file_swap"
	mv "$file_swap" "$FILES_FOLDER/LocalSettings-tmp.php"

	mv "$FILES_FOLDER/LocalSettings-tmp.php" \
		"$WIKI_DIR_INST/$WIKI_DIR_NAME/LocalSettings.php" ||
		error "Unable to move $FILES_FOLDER/LocalSettings-tmp.php" \
		"in $WIKI_DIR_INST/$WIKI_DIR_NAME"
	echo "File $FILES_FOLDER/LocalSettings.php is set in" \
		" $WIKI_DIR_INST/$WIKI_DIR_NAME"

	echo "Your wiki has been installed. You can check it at
		http://$SERVER_ADDR/$WIKI_DIR_NAME"
}

# Reset the database of the wiki and the password of the admin
#
# Warning: This function must be called only in a subdirectory of t/ directory
wiki_reset () {
	# Copy initial database of the wiki
	if [ ! -f "../$FILES_FOLDER/$DB_FILE" ] ; then
		error "Can't find ../$FILES_FOLDER/$DB_FILE in the current folder."
	fi
	cp "../$FILES_FOLDER/$DB_FILE" "$TMP" ||
		error "Can't copy ../$FILES_FOLDER/$DB_FILE in $TMP"
	echo "File $FILES_FOLDER/$DB_FILE is set in $TMP"
}

# Delete the wiki created in the web server's directory and all its content
# saved in the database.
wiki_delete () {
	if test $LIGHTTPD = "true"; then
		stop_lighttpd
	else
		# Delete the wiki's directory.
		rm -rf "$WIKI_DIR_INST/$WIKI_DIR_NAME" ||
			error "Wiki's directory $WIKI_DIR_INST/" \
			"$WIKI_DIR_NAME could not be deleted"
		# Delete the wiki's SQLite database.
		rm -f "$TMP/$DB_FILE" ||
			error "Database $TMP/$DB_FILE could not be deleted."
	fi

	# Delete the wiki's SQLite database
	rm -f "$TMP/$DB_FILE" || error "Database $TMP/$DB_FILE could not be deleted."
	rm -f "$FILES_FOLDER/$DB_FILE"
	rm -rf "$TMP/$MW_VERSION"
}
