#!/usr/bin/perl

# gitweb - simple web interface to track changes in git repositories
#
# (C) 2005-2006, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke
#
# This program is licensed under the GPLv2

use strict;
use warnings;
use CGI qw(:standard :escapeHTML -nosticky);
use CGI::Util qw(unescape);
use CGI::Carp qw(fatalsToBrowser);
use Encode;
use Fcntl ':mode';
binmode STDOUT, ':utf8';

my $cgi = new CGI;
my $version =		"267";
my $my_url =		$cgi->url();
my $my_uri =		$cgi->url(-absolute => 1);
my $rss_link =		"";

# absolute fs-path which will be prepended to the project path
#my $projectroot =	"/pub/scm";
my $projectroot =	"/home/kay/public_html/pub/scm";

# location of the git-core binaries
my $gitbin =		"/usr/bin";

# location for temporary files needed for diffs
my $git_temp =		"/tmp/gitweb";

# target of the home link on top of all pages
my $home_link =		$my_uri;

# html text to include at home page
my $home_text =		"indextext.html";

# URI of default stylesheet
my $stylesheet = 	"gitweb.css";

# source of projects list
#my $projects_list =	$projectroot;
my $projects_list =	"index/index.aux";

# default blob_plain mimetype and default charset for text/plain blob
my $default_blob_plain_mimetype = 'text/plain';
my $default_text_plain_charset  = undef;

# file to use for guessing MIME types before trying /etc/mime.types
# (relative to the current git repository)
my $mimetypes_file              = undef;


# input validation and dispatch
my $action = $cgi->param('a');
if (defined $action) {
	if ($action =~ m/[^0-9a-zA-Z\.\-_]/) {
		undef $action;
		die_error(undef, "Invalid action parameter.");
	}
	if ($action eq "git-logo.png") {
		git_logo();
		exit;
	} elsif ($action eq "opml") {
		git_opml();
		exit;
	}
}

my $order = $cgi->param('o');
if (defined $order) {
	if ($order =~ m/[^0-9a-zA-Z_]/) {
		undef $order;
		die_error(undef, "Invalid order parameter.");
	}
}

my $project = $cgi->param('p');
if (defined $project) {
	$project = validate_input($project);
	if (!defined($project)) {
		die_error(undef, "Invalid project parameter.");
	}
	if (!(-d "$projectroot/$project")) {
		undef $project;
		die_error(undef, "No such directory.");
	}
	if (!(-e "$projectroot/$project/HEAD")) {
		undef $project;
		die_error(undef, "No such project.");
	}
	$rss_link = "<link rel=\"alternate\" title=\"" . esc_param($project) . " log\" href=\"" .
		    "$my_uri?" . esc_param("p=$project;a=rss") . "\" type=\"application/rss+xml\"/>";
	$ENV{'GIT_DIR'} = "$projectroot/$project";
} else {
	git_project_list();
	exit;
}

my $file_name = $cgi->param('f');
if (defined $file_name) {
	$file_name = validate_input($file_name);
	if (!defined($file_name)) {
		die_error(undef, "Invalid file parameter.");
	}
}

my $hash = $cgi->param('h');
if (defined $hash) {
	$hash = validate_input($hash);
	if (!defined($hash)) {
		die_error(undef, "Invalid hash parameter.");
	}
}

my $hash_parent = $cgi->param('hp');
if (defined $hash_parent) {
	$hash_parent = validate_input($hash_parent);
	if (!defined($hash_parent)) {
		die_error(undef, "Invalid hash parent parameter.");
	}
}

my $hash_base = $cgi->param('hb');
if (defined $hash_base) {
	$hash_base = validate_input($hash_base);
	if (!defined($hash_base)) {
		die_error(undef, "Invalid hash base parameter.");
	}
}

my $page = $cgi->param('pg');
if (defined $page) {
	if ($page =~ m/[^0-9]$/) {
		undef $page;
		die_error(undef, "Invalid page parameter.");
	}
}

my $searchtext = $cgi->param('s');
if (defined $searchtext) {
	if ($searchtext =~ m/[^a-zA-Z0-9_\.\/\-\+\:\@ ]/) {
		undef $searchtext;
		die_error(undef, "Invalid search parameter.");
	}
	$searchtext = quotemeta $searchtext;
}

sub validate_input {
	my $input = shift;

	if ($input =~ m/^[0-9a-fA-F]{40}$/) {
		return $input;
	}
	if ($input =~ m/(^|\/)(|\.|\.\.)($|\/)/) {
		return undef;
	}
	if ($input =~ m/[^a-zA-Z0-9_\x80-\xff\ \t\.\/\-\+\#\~\%]/) {
		return undef;
	}
	return $input;
}

if (!defined $action || $action eq "summary") {
	git_summary();
	exit;
} elsif ($action eq "heads") {
	git_heads();
	exit;
} elsif ($action eq "tags") {
	git_tags();
	exit;
} elsif ($action eq "blob") {
	git_blob();
	exit;
} elsif ($action eq "blob_plain") {
	git_blob_plain();
	exit;
} elsif ($action eq "tree") {
	git_tree();
	exit;
} elsif ($action eq "rss") {
	git_rss();
	exit;
} elsif ($action eq "commit") {
	git_commit();
	exit;
} elsif ($action eq "log") {
	git_log();
	exit;
} elsif ($action eq "blobdiff") {
	git_blobdiff();
	exit;
} elsif ($action eq "blobdiff_plain") {
	git_blobdiff_plain();
	exit;
} elsif ($action eq "commitdiff") {
	git_commitdiff();
	exit;
} elsif ($action eq "commitdiff_plain") {
	git_commitdiff_plain();
	exit;
} elsif ($action eq "history") {
	git_history();
	exit;
} elsif ($action eq "search") {
	git_search();
	exit;
} elsif ($action eq "shortlog") {
	git_shortlog();
	exit;
} elsif ($action eq "tag") {
	git_tag();
	exit;
} elsif ($action eq "blame") {
	git_blame();
	exit;
} else {
	undef $action;
	die_error(undef, "Unknown action.");
	exit;
}

# quote unsafe chars, but keep the slash, even when it's not
# correct, but quoted slashes look too horrible in bookmarks
sub esc_param {
	my $str = shift;
	$str =~ s/([^A-Za-z0-9\-_.~();\/;?:@&=])/sprintf("%%%02X", ord($1))/eg;
	$str =~ s/\+/%2B/g;
	$str =~ s/ /\+/g;
	return $str;
}

# replace invalid utf8 character with SUBSTITUTION sequence
sub esc_html {
	my $str = shift;
	$str = decode("utf8", $str, Encode::FB_DEFAULT);
	$str = escapeHTML($str);
	return $str;
}

# git may return quoted and escaped filenames
sub unquote {
	my $str = shift;
	if ($str =~ m/^"(.*)"$/) {
		$str = $1;
		$str =~ s/\\([0-7]{1,3})/chr(oct($1))/eg;
	}
	return $str;
}

sub git_header_html {
	my $status = shift || "200 OK";
	my $expires = shift;

	my $title = "git";
	if (defined $project) {
		$title .= " - $project";
		if (defined $action) {
			$title .= "/$action";
			if (defined $file_name) {
				$title .= " - $file_name";
				if ($action eq "tree" && $file_name !~ m|/$|) {
					$title .= "/";
				}
			}
		}
	}
	print $cgi->header(-type=>'text/html',  -charset => 'utf-8', -status=> $status, -expires => $expires);
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface v$version, (C) 2005-2006, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke -->
<head>
<meta http-equiv="content-type" content="text/html; charset=utf-8"/>
<meta name="robots" content="index, nofollow"/>
<link rel="stylesheet" type="text/css" href="$stylesheet"/>
<title>$title</title>
$rss_link
</head>
<body>
EOF
	print "<div class=\"page_header\">\n" .
	      "<a href=\"http://www.kernel.org/pub/software/scm/git/docs/\" title=\"git documentation\">" .
	      "<img src=\"$my_uri?" . esc_param("a=git-logo.png") . "\" width=\"72\" height=\"27\" alt=\"git\" style=\"float:right; border-width:0px;\"/>" .
	      "</a>\n";
	print $cgi->a({-href => esc_param($home_link)}, "projects") . " / ";
	if (defined $project) {
		print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, esc_html($project));
		if (defined $action) {
			print " / $action";
		}
		print "\n";
		if (!defined $searchtext) {
			$searchtext = "";
		}
		my $search_hash;
		if (defined $hash) {
			$search_hash = $hash;
		} else {
			$search_hash  = "HEAD";
		}
		$cgi->param("a", "search");
		$cgi->param("h", $search_hash);
		print $cgi->startform(-method => "get", -action => $my_uri) .
		      "<div class=\"search\">\n" .
		      $cgi->hidden(-name => "p") . "\n" .
		      $cgi->hidden(-name => "a") . "\n" .
		      $cgi->hidden(-name => "h") . "\n" .
		      $cgi->textfield(-name => "s", -value => $searchtext) . "\n" .
		      "</div>" .
		      $cgi->end_form() . "\n";
	}
	print "</div>\n";
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_read_description($project);
		if (defined $descr) {
			print "<div class=\"page_footer_text\">" . esc_html($descr) . "</div>\n";
		}
		print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=rss"), -class => "rss_logo"}, "RSS") . "\n";
	} else {
		print $cgi->a({-href => "$my_uri?" . esc_param("a=opml"), -class => "rss_logo"}, "OPML") . "\n";
	}
	print "</div>\n" .
	      "</body>\n" .
	      "</html>";
}

sub die_error {
	my $status = shift || "403 Forbidden";
	my $error = shift || "Malformed query, file missing or permission denied"; 

	git_header_html($status);
	print "<div class=\"page_body\">\n" .
	      "<br/><br/>\n" .
	      "$status - $error\n" .
	      "<br/>\n" .
	      "</div>\n";
	git_footer_html();
	exit;
}

sub git_get_type {
	my $hash = shift;

	open my $fd, "-|", "$gitbin/git-cat-file -t $hash" or return;
	my $type = <$fd>;
	close $fd or return;
	chomp $type;
	return $type;
}

sub git_read_head {
	my $project = shift;
	my $oENV = $ENV{'GIT_DIR'};
	my $retval = undef;
	$ENV{'GIT_DIR'} = "$projectroot/$project";
	if (open my $fd, "-|", "$gitbin/git-rev-parse", "--verify", "HEAD") {
		my $head = <$fd>;
		close $fd;
		if (defined $head && $head =~ /^([0-9a-fA-F]{40})$/) {
			$retval = $1;
		}
	}
	if (defined $oENV) {
		$ENV{'GIT_DIR'} = $oENV;
	}
	return $retval;
}

sub git_read_hash {
	my $path = shift;

	open my $fd, "$projectroot/$path" or return undef;
	my $head = <$fd>;
	close $fd;
	chomp $head;
	if ($head =~ m/^[0-9a-fA-F]{40}$/) {
		return $head;
	}
}

sub git_read_description {
	my $path = shift;

	open my $fd, "$projectroot/$path/description" or return undef;
	my $descr = <$fd>;
	close $fd;
	chomp $descr;
	return $descr;
}

sub git_read_tag {
	my $tag_id = shift;
	my %tag;
	my @comment;

	open my $fd, "-|", "$gitbin/git-cat-file tag $tag_id" or return;
	$tag{'id'} = $tag_id;
	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m/^object ([0-9a-fA-F]{40})$/) {
			$tag{'object'} = $1;
		} elsif ($line =~ m/^type (.+)$/) {
			$tag{'type'} = $1;
		} elsif ($line =~ m/^tag (.+)$/) {
			$tag{'name'} = $1;
		} elsif ($line =~ m/^tagger (.*) ([0-9]+) (.*)$/) {
			$tag{'author'} = $1;
			$tag{'epoch'} = $2;
			$tag{'tz'} = $3;
		} elsif ($line =~ m/--BEGIN/) {
			push @comment, $line;
			last;
		} elsif ($line eq "") {
			last;
		}
	}
	push @comment, <$fd>;
	$tag{'comment'} = \@comment;
	close $fd or return;
	if (!defined $tag{'name'}) {
		return
	};
	return %tag
}

sub age_string {
	my $age = shift;
	my $age_str;

	if ($age > 60*60*24*365*2) {
		$age_str = (int $age/60/60/24/365);
		$age_str .= " years ago";
	} elsif ($age > 60*60*24*(365/12)*2) {
		$age_str = int $age/60/60/24/(365/12);
		$age_str .= " months ago";
	} elsif ($age > 60*60*24*7*2) {
		$age_str = int $age/60/60/24/7;
		$age_str .= " weeks ago";
	} elsif ($age > 60*60*24*2) {
		$age_str = int $age/60/60/24;
		$age_str .= " days ago";
	} elsif ($age > 60*60*2) {
		$age_str = int $age/60/60;
		$age_str .= " hours ago";
	} elsif ($age > 60*2) {
		$age_str = int $age/60;
		$age_str .= " min ago";
	} elsif ($age > 2) {
		$age_str = int $age;
		$age_str .= " sec ago";
	} else {
		$age_str .= " right now";
	}
	return $age_str;
}

sub git_read_commit {
	my $commit_id = shift;
	my $commit_text = shift;

	my @commit_lines;
	my %co;

	if (defined $commit_text) {
		@commit_lines = @$commit_text;
	} else {
		$/ = "\0";
		open my $fd, "-|", "$gitbin/git-rev-list --header --parents --max-count=1 $commit_id" or return;
		@commit_lines = split '\n', <$fd>;
		close $fd or return;
		$/ = "\n";
		pop @commit_lines;
	}
	my $header = shift @commit_lines;
	if (!($header =~ m/^[0-9a-fA-F]{40}/)) {
		return;
	}
	($co{'id'}, my @parents) = split ' ', $header;
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];
	while (my $line = shift @commit_lines) {
		last if $line eq "\n";
		if ($line =~ m/^tree ([0-9a-fA-F]{40})$/) {
			$co{'tree'} = $1;
		} elsif ($line =~ m/^author (.*) ([0-9]+) (.*)$/) {
			$co{'author'} = $1;
			$co{'author_epoch'} = $2;
			$co{'author_tz'} = $3;
			if ($co{'author'} =~ m/^([^<]+) </) {
				$co{'author_name'} = $1;
			} else {
				$co{'author_name'} = $co{'author'};
			}
		} elsif ($line =~ m/^committer (.*) ([0-9]+) (.*)$/) {
			$co{'committer'} = $1;
			$co{'committer_epoch'} = $2;
			$co{'committer_tz'} = $3;
			$co{'committer_name'} = $co{'committer'};
			$co{'committer_name'} =~ s/ <.*//;
		}
	}
	if (!defined $co{'tree'}) {
		return;
	};

	foreach my $title (@commit_lines) {
		$title =~ s/^    //;
		if ($title ne "") {
			$co{'title'} = chop_str($title, 80, 5);
			# remove leading stuff of merges to make the interesting part visible
			if (length($title) > 50) {
				$title =~ s/^Automatic //;
				$title =~ s/^merge (of|with) /Merge ... /i;
				if (length($title) > 50) {
					$title =~ s/(http|rsync):\/\///;
				}
				if (length($title) > 50) {
					$title =~ s/(master|www|rsync)\.//;
				}
				if (length($title) > 50) {
					$title =~ s/kernel.org:?//;
				}
				if (length($title) > 50) {
					$title =~ s/\/pub\/scm//;
				}
			}
			$co{'title_short'} = chop_str($title, 50, 5);
			last;
		}
	}
	# remove added spaces
	foreach my $line (@commit_lines) {
		$line =~ s/^    //;
	}
	$co{'comment'} = \@commit_lines;

	my $age = time - $co{'committer_epoch'};
	$co{'age'} = $age;
	$co{'age_string'} = age_string($age);
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($co{'committer_epoch'});
	if ($age > 60*60*24*7*2) {
		$co{'age_string_date'} = sprintf "%4i-%02u-%02i", 1900 + $year, $mon+1, $mday;
		$co{'age_string_age'} = $co{'age_string'};
	} else {
		$co{'age_string_date'} = $co{'age_string'};
		$co{'age_string_age'} = sprintf "%4i-%02u-%02i", 1900 + $year, $mon+1, $mday;
	}
	return %co;
}

sub git_diff_print {
	my $from = shift;
	my $from_name = shift;
	my $to = shift;
	my $to_name = shift;
	my $format = shift || "html";

	my $from_tmp = "/dev/null";
	my $to_tmp = "/dev/null";
	my $pid = $$;

	# create tmp from-file
	if (defined $from) {
		$from_tmp = "$git_temp/gitweb_" . $$ . "_from";
		open my $fd2, "> $from_tmp";
		open my $fd, "-|", "$gitbin/git-cat-file blob $from";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	# create tmp to-file
	if (defined $to) {
		$to_tmp = "$git_temp/gitweb_" . $$ . "_to";
		open my $fd2, "> $to_tmp";
		open my $fd, "-|", "$gitbin/git-cat-file blob $to";
		my @file = <$fd>;
		print $fd2 @file;
		close $fd2;
		close $fd;
	}

	open my $fd, "-|", "/usr/bin/diff -u -p -L \'$from_name\' -L \'$to_name\' $from_tmp $to_tmp";
	if ($format eq "plain") {
		undef $/;
		print <$fd>;
		$/ = "\n";
	} else {
		while (my $line = <$fd>) {
			chomp($line);
			my $char = substr($line, 0, 1);
			my $color = "";
			if ($char eq '+') {
				$color = " style=\"color:#008800;\"";
			} elsif ($char eq "-") {
				$color = " style=\"color:#cc0000;\"";
			} elsif ($char eq "@") {
				$color = " style=\"color:#990099;\"";
			} elsif ($char eq "\\") {
				# skip errors
				next;
			}
			while ((my $pos = index($line, "\t")) != -1) {
				if (my $count = (8 - (($pos-1) % 8))) {
					my $spaces = ' ' x $count;
					$line =~ s/\t/$spaces/;
				}
			}
			print "<div class=\"pre\"$color>" . esc_html($line) . "</div>\n";
		}
	}
	close $fd;

	if (defined $from) {
		unlink($from_tmp);
	}
	if (defined $to) {
		unlink($to_tmp);
	}
}

sub mode_str {
	my $mode = oct shift;

	if (S_ISDIR($mode & S_IFMT)) {
		return 'drwxr-xr-x';
	} elsif (S_ISLNK($mode)) {
		return 'lrwxrwxrwx';
	} elsif (S_ISREG($mode)) {
		# git cares only about the executable bit
		if ($mode & S_IXUSR) {
			return '-rwxr-xr-x';
		} else {
			return '-rw-r--r--';
		};
	} else {
		return '----------';
	}
}

sub chop_str {
	my $str = shift;
	my $len = shift;
	my $add_len = shift || 10;

	# allow only $len chars, but don't cut a word if it would fit in $add_len
	# if it doesn't fit, cut it if it's still longer than the dots we would add
	$str =~ m/^(.{0,$len}[^ \/\-_:\.@]{0,$add_len})(.*)/;
	my $body = $1;
	my $tail = $2;
	if (length($tail) > 4) {
		$tail = " ...";
	}
	return "$body$tail";
}

sub file_type {
	my $mode = oct shift;

	if (S_ISDIR($mode & S_IFMT)) {
		return "directory";
	} elsif (S_ISLNK($mode)) {
		return "symlink";
	} elsif (S_ISREG($mode)) {
		return "file";
	} else {
		return "unknown";
	}
}

sub format_log_line_html {
	my $line = shift;

	$line = esc_html($line);
	$line =~ s/ /&nbsp;/g;
	if ($line =~ m/([0-9a-fA-F]{40})/) {
		my $hash_text = $1;
		if (git_get_type($hash_text) eq "commit") {
			my $link = $cgi->a({-class => "text", -href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_text")}, $hash_text);
			$line =~ s/$hash_text/$link/;
		}
	}
	return $line;
}

sub date_str {
	my $epoch = shift;
	my $tz = shift || "-0000";

	my %date;
	my @months = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec");
	my @days = ("Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat");
	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($epoch);
	$date{'hour'} = $hour;
	$date{'minute'} = $min;
	$date{'mday'} = $mday;
	$date{'day'} = $days[$wday];
	$date{'month'} = $months[$mon];
	$date{'rfc2822'} = sprintf "%s, %d %s %4d %02d:%02d:%02d +0000", $days[$wday], $mday, $months[$mon], 1900+$year, $hour ,$min, $sec;
	$date{'mday-time'} = sprintf "%d %s %02d:%02d", $mday, $months[$mon], $hour ,$min;

	$tz =~ m/^([+\-][0-9][0-9])([0-9][0-9])$/;
	my $local = $epoch + ((int $1 + ($2/60)) * 3600);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	return %date;
}

# git-logo (cached in browser for one day)
sub git_logo {
	binmode STDOUT, ':raw';
	print $cgi->header(-type => 'image/png', -expires => '+1d');
	# cat git-logo.png | hexdump -e '16/1 " %02x"  "\n"' | sed 's/ /\\x/g'
	print	"\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52" .
		"\x00\x00\x00\x48\x00\x00\x00\x1b\x04\x03\x00\x00\x00\x2d\xd9\xd4" .
		"\x2d\x00\x00\x00\x18\x50\x4c\x54\x45\xff\xff\xff\x60\x60\x5d\xb0" .
		"\xaf\xaa\x00\x80\x00\xce\xcd\xc7\xc0\x00\x00\xe8\xe8\xe6\xf7\xf7" .
		"\xf6\x95\x0c\xa7\x47\x00\x00\x00\x73\x49\x44\x41\x54\x28\xcf\x63" .
		"\x48\x67\x20\x04\x4a\x5c\x18\x0a\x08\x2a\x62\x53\x61\x20\x02\x08" .
		"\x0d\x69\x45\xac\xa1\xa1\x01\x30\x0c\x93\x60\x36\x26\x52\x91\xb1" .
		"\x01\x11\xd6\xe1\x55\x64\x6c\x6c\xcc\x6c\x6c\x0c\xa2\x0c\x70\x2a" .
		"\x62\x06\x2a\xc1\x62\x1d\xb3\x01\x02\x53\xa4\x08\xe8\x00\x03\x18" .
		"\x26\x56\x11\xd4\xe1\x20\x97\x1b\xe0\xb4\x0e\x35\x24\x71\x29\x82" .
		"\x99\x30\xb8\x93\x0a\x11\xb9\x45\x88\xc1\x8d\xa0\xa2\x44\x21\x06" .
		"\x27\x41\x82\x40\x85\xc1\x45\x89\x20\x70\x01\x00\xa4\x3d\x21\xc5" .
		"\x12\x1c\x9a\xfe\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
}

sub get_file_owner {
	my $path = shift;

	my ($dev, $ino, $mode, $nlink, $st_uid, $st_gid, $rdev, $size) = stat($path);
	my ($name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell) = getpwuid($st_uid);
	if (!defined $gcos) {
		return undef;
	}
	my $owner = $gcos;
	$owner =~ s/[,;].*$//;
	return decode("utf8", $owner, Encode::FB_DEFAULT);
}

sub git_read_projects {
	my @list;

	if (-d $projects_list) {
		# search in directory
		my $dir = $projects_list;
		opendir my $dh, $dir or return undef;
		while (my $dir = readdir($dh)) {
			if (-e "$projectroot/$dir/HEAD") {
				my $pr = {
					path => $dir,
				};
				push @list, $pr
			}
		}
		closedir($dh);
	} elsif (-f $projects_list) {
		# read from file(url-encoded):
		# 'git%2Fgit.git Linus+Torvalds'
		# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
		# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
		open my $fd , $projects_list or return undef;
		while (my $line = <$fd>) {
			chomp $line;
			my ($path, $owner) = split ' ', $line;
			$path = unescape($path);
			$owner = unescape($owner);
			if (!defined $path) {
				next;
			}
			if (-e "$projectroot/$path/HEAD") {
				my $pr = {
					path => $path,
					owner => decode("utf8", $owner, Encode::FB_DEFAULT),
				};
				push @list, $pr
			}
		}
		close $fd;
	}
	@list = sort {$a->{'path'} cmp $b->{'path'}} @list;
	return @list;
}

sub git_get_project_config {
	my $key = shift;

	return unless ($key);
	$key =~ s/^gitweb\.//;
	return if ($key =~ m/\W/);

	my $val = qx(git-repo-config --get gitweb.$key);
	return ($val);
}

sub git_get_project_config_bool {
	my $val = git_get_project_config (@_);
	if ($val and $val =~ m/true|yes|on/) {
		return (1);
	}
	return; # implicit false
}

sub git_project_list {
	my @list = git_read_projects();
	my @projects;
	if (!@list) {
		die_error(undef, "No project found.");
	}
	foreach my $pr (@list) {
		my $head = git_read_head($pr->{'path'});
		if (!defined $head) {
			next;
		}
		$ENV{'GIT_DIR'} = "$projectroot/$pr->{'path'}";
		my %co = git_read_commit($head);
		if (!%co) {
			next;
		}
		$pr->{'commit'} = \%co;
		if (!defined $pr->{'descr'}) {
			my $descr = git_read_description($pr->{'path'}) || "";
			$pr->{'descr'} = chop_str($descr, 25, 5);
		}
		if (!defined $pr->{'owner'}) {
			$pr->{'owner'} = get_file_owner("$projectroot/$pr->{'path'}") || "";
		}
		push @projects, $pr;
	}
	git_header_html();
	if (-f $home_text) {
		print "<div class=\"index_include\">\n";
		open (my $fd, $home_text);
		print <$fd>;
		close $fd;
		print "</div>\n";
	}
	print "<table cellspacing=\"0\">\n" .
	      "<tr>\n";
	if (!defined($order) || (defined($order) && ($order eq "project"))) {
		@projects = sort {$a->{'path'} cmp $b->{'path'}} @projects;
		print "<th>Project</th>\n";
	} else {
		print "<th>" . $cgi->a({-class => "header", -href => "$my_uri?" . esc_param("o=project")}, "Project") . "</th>\n";
	}
	if (defined($order) && ($order eq "descr")) {
		@projects = sort {$a->{'descr'} cmp $b->{'descr'}} @projects;
		print "<th>Description</th>\n";
	} else {
		print "<th>" . $cgi->a({-class => "header", -href => "$my_uri?" . esc_param("o=descr")}, "Description") . "</th>\n";
	}
	if (defined($order) && ($order eq "owner")) {
		@projects = sort {$a->{'owner'} cmp $b->{'owner'}} @projects;
		print "<th>Owner</th>\n";
	} else {
		print "<th>" . $cgi->a({-class => "header", -href => "$my_uri?" . esc_param("o=owner")}, "Owner") . "</th>\n";
	}
	if (defined($order) && ($order eq "age")) {
		@projects = sort {$a->{'commit'}{'age'} <=> $b->{'commit'}{'age'}} @projects;
		print "<th>Last Change</th>\n";
	} else {
		print "<th>" . $cgi->a({-class => "header", -href => "$my_uri?" . esc_param("o=age")}, "Last Change") . "</th>\n";
	}
	print "<th></th>\n" .
	      "</tr>\n";
	my $alternate = 0;
	foreach my $pr (@projects) {
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td>" . $cgi->a({-href => "$my_uri?" . esc_param("p=$pr->{'path'};a=summary"), -class => "list"}, esc_html($pr->{'path'})) . "</td>\n" .
		      "<td>$pr->{'descr'}</td>\n" .
		      "<td><i>" . chop_str($pr->{'owner'}, 15) . "</i></td>\n";
		my $colored_age;
		if ($pr->{'commit'}{'age'} < 60*60*2) {
			$colored_age = "<span style =\"color: #009900;\"><b><i>$pr->{'commit'}{'age_string'}</i></b></span>";
		} elsif ($pr->{'commit'}{'age'} < 60*60*24*2) {
			$colored_age = "<span style =\"color: #009900;\"><i>$pr->{'commit'}{'age_string'}</i></span>";
		} else {
			$colored_age = "<i>$pr->{'commit'}{'age_string'}</i>";
		}
		print "<td>$colored_age</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$pr->{'path'};a=summary")}, "summary") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$pr->{'path'};a=shortlog")}, "shortlog") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$pr->{'path'};a=log")}, "log") .
		      "</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
	git_footer_html();
}

sub read_info_ref {
	my $type = shift || "";
	my %refs;
	# 5dc01c595e6c6ec9ccda4f6f69c131c0dd945f8c	refs/tags/v2.6.11
	# c39ae07f393806ccf406ef966e9a15afc43cc36a	refs/tags/v2.6.11^{}
	open my $fd, "$projectroot/$project/info/refs" or return;
	while (my $line = <$fd>) {
		chomp($line);
		if ($line =~ m/^([0-9a-fA-F]{40})\t.*$type\/([^\^]+)/) {
			if (defined $refs{$1}) {
				$refs{$1} .= " / $2";
			} else {
				$refs{$1} = $2;
			}
		}
	}
	close $fd or return;
	return \%refs;
}

sub git_read_refs {
	my $ref_dir = shift;
	my @reflist;

	my @refs;
	opendir my $dh, "$projectroot/$project/$ref_dir";
	while (my $dir = readdir($dh)) {
		if ($dir =~ m/^\./) {
			next;
		}
		if (-d "$projectroot/$project/$ref_dir/$dir") {
			opendir my $dh2, "$projectroot/$project/$ref_dir/$dir";
			my @subdirs = grep !m/^\./, readdir $dh2;
			closedir($dh2);
			foreach my $subdir (@subdirs) {
				push @refs, "$dir/$subdir"
			}
			next;
		}
		push @refs, $dir;
	}
	closedir($dh);
	foreach my $ref_file (@refs) {
		my $ref_id = git_read_hash("$project/$ref_dir/$ref_file");
		my $type = git_get_type($ref_id) || next;
		my %ref_item;
		my %co;
		$ref_item{'type'} = $type;
		$ref_item{'id'} = $ref_id;
		$ref_item{'epoch'} = 0;
		$ref_item{'age'} = "unknown";
		if ($type eq "tag") {
			my %tag = git_read_tag($ref_id);
			$ref_item{'comment'} = $tag{'comment'};
			if ($tag{'type'} eq "commit") {
				%co = git_read_commit($tag{'object'});
				$ref_item{'epoch'} = $co{'committer_epoch'};
				$ref_item{'age'} = $co{'age_string'};
			} elsif (defined($tag{'epoch'})) {
				my $age = time - $tag{'epoch'};
				$ref_item{'epoch'} = $tag{'epoch'};
				$ref_item{'age'} = age_string($age);
			}
			$ref_item{'reftype'} = $tag{'type'};
			$ref_item{'name'} = $tag{'name'};
			$ref_item{'refid'} = $tag{'object'};
		} elsif ($type eq "commit"){
			%co = git_read_commit($ref_id);
			$ref_item{'reftype'} = "commit";
			$ref_item{'name'} = $ref_file;
			$ref_item{'title'} = $co{'title'};
			$ref_item{'refid'} = $ref_id;
			$ref_item{'epoch'} = $co{'committer_epoch'};
			$ref_item{'age'} = $co{'age_string'};
		}

		push @reflist, \%ref_item;
	}
	# sort tags by age
	@reflist = sort {$b->{'epoch'} <=> $a->{'epoch'}} @reflist;
	return \@reflist;
}

sub git_summary {
	my $descr = git_read_description($project) || "none";
	my $head = git_read_head($project);
	my %co = git_read_commit($head);
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});

	my $owner;
	if (-f $projects_list) {
		open (my $fd , $projects_list);
		while (my $line = <$fd>) {
			chomp $line;
			my ($pr, $ow) = split ' ', $line;
			$pr = unescape($pr);
			$ow = unescape($ow);
			if ($pr eq $project) {
				$owner = decode("utf8", $ow, Encode::FB_DEFAULT);
				last;
			}
		}
		close $fd;
	}
	if (!defined $owner) {
		$owner = get_file_owner("$projectroot/$project");
	}

	my $refs = read_info_ref();
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      "summary".
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$head")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$head")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree")}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	print "<div class=\"title\">&nbsp;</div>\n";
	print "<table cellspacing=\"0\">\n" .
	      "<tr><td>description</td><td>" . esc_html($descr) . "</td></tr>\n" .
	      "<tr><td>owner</td><td>$owner</td></tr>\n" .
	      "<tr><td>last change</td><td>$cd{'rfc2822'}</td></tr>\n" .
	      "</table>\n";
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=17 " . git_read_head($project) or die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog"), -class => "title"}, "shortlog") .
	      "</div>\n";
	my $i = 16;
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $commit (@revlist) {
		my %co = git_read_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		if ($i-- > 0) {
			my $ref = "";
			if (defined $refs->{$commit}) {
				$ref = " <span class=\"tag\">" . esc_html($refs->{$commit}) . "</span>";
			}
			print "<td><i>$co{'age_string'}</i></td>\n" .
			      "<td><i>" . esc_html(chop_str($co{'author_name'}, 10)) . "</i></td>\n" .
			      "<td>";
			if (length($co{'title_short'}) < length($co{'title'})) {
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "list", -title => "$co{'title'}"},
			              "<b>" . esc_html($co{'title_short'}) . "$ref</b>");
			} else {
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "list"},
				      "<b>" . esc_html($co{'title'}) . "$ref</b>");
			}
			print "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit")}, "commit") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$commit")}, "commitdiff") .
			      "</td>\n" .
			      "</tr>";
		} else {
			print "<td>" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "...") . "</td>\n" .
			"</tr>";
			last;
		}
	}
	print "</table\n>";

	my $taglist = git_read_refs("refs/tags");
	if (defined @$taglist) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tags"), -class => "title"}, "tags") .
		      "</div>\n";
		my $i = 16;
		print "<table cellspacing=\"0\">\n";
		my $alternate = 0;
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			my $comment_lines = $tag{'comment'};
			my $comment = shift @$comment_lines;
			if (defined($comment)) {
				$comment = chop_str($comment, 30, 5);
			}
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			if ($i-- > 0) {
				print "<td><i>$tag{'age'}</i></td>\n" .
				      "<td>" .
				      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=$tag{'reftype'};h=$tag{'refid'}"), -class => "list"},
				      "<b>" . esc_html($tag{'name'}) . "</b>") .
				      "</td>\n" .
				      "<td>";
				if (defined($comment)) {
				      print $cgi->a({-class => "list", -href => "$my_uri?" . esc_param("p=$project;a=tag;h=$tag{'id'}")}, $comment);
				}
				print "</td>\n" .
				      "<td class=\"link\">";
				if ($tag{'type'} eq "tag") {
				      print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tag;h=$tag{'id'}")}, "tag") . " | ";
				}
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=$tag{'reftype'};h=$tag{'refid'}")}, $tag{'reftype'});
				if ($tag{'reftype'} eq "commit") {
				      print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}")}, "shortlog") .
				            " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$tag{'refid'}")}, "log");
				}
				print "</td>\n" .
				      "</tr>";
			} else {
				print "<td>" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tags")}, "...") . "</td>\n" .
				"</tr>";
				last;
			}
		}
		print "</table\n>";
	}

	my $headlist = git_read_refs("refs/heads");
	if (defined @$headlist) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=heads"), -class => "title"}, "heads") .
		      "</div>\n";
		my $i = 16;
		print "<table cellspacing=\"0\">\n";
		my $alternate = 0;
		foreach my $entry (@$headlist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			if ($i-- > 0) {
				print "<td><i>$tag{'age'}</i></td>\n" .
				      "<td>" .
				      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}"), -class => "list"},
				      "<b>" . esc_html($tag{'name'}) . "</b>") .
				      "</td>\n" .
				      "<td class=\"link\">" .
				      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}")}, "shortlog") .
				      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$tag{'name'}")}, "log") .
				      "</td>\n" .
				      "</tr>";
			} else {
				print "<td>" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=heads")}, "...") . "</td>\n" .
				"</tr>";
				last;
			}
		}
		print "</table\n>";
	}
	git_footer_html();
}

sub git_tag {
	my $head = git_read_head($project);
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$head")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$head")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;hb=$head")}, "tree") . "<br/>\n" .
	      "<br/>\n" .
	      "</div>\n";
	my %tag = git_read_tag($hash);
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash"), -class => "title"}, esc_html($tag{'name'})) . "\n" .
	      "</div>\n";
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n" .
	      "<tr>\n" .
	      "<td>object</td>\n" .
	      "<td>" . $cgi->a({-class => "list", -href => "$my_uri?" . esc_param("p=$project;a=$tag{'type'};h=$tag{'object'}")}, $tag{'object'}) . "</td>\n" .
	      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=$tag{'type'};h=$tag{'object'}")}, $tag{'type'}) . "</td>\n" .
	      "</tr>\n";
	if (defined($tag{'author'})) {
		my %ad = date_str($tag{'epoch'}, $tag{'tz'});
		print "<tr><td>author</td><td>" . esc_html($tag{'author'}) . "</td></tr>\n";
		print "<tr><td></td><td>" . $ad{'rfc2822'} . sprintf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'}) . "</td></tr>\n";
	}
	print "</table>\n\n" .
	      "</div>\n";
	print "<div class=\"page_body\">";
	my $comment = $tag{'comment'};
	foreach my $line (@$comment) {
		print esc_html($line) . "<br/>\n";
	}
	print "</div>\n";
	git_footer_html();
}

sub git_blame {
	my $fd;
	die_error('403 Permission denied', "Permission denied.") if (!git_get_project_config_bool ('blame'));
	die_error('404 Not Found', "What file will it be, master?") if (!$file_name);
	$hash_base ||= git_read_head($project);
	die_error(undef, "Reading commit failed.") unless ($hash_base);
	my %co = git_read_commit($hash_base)
		or die_error(undef, "Reading commit failed.");
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(undef, "Error lookup file.");
	}
	open ($fd, "-|", "$gitbin/git-annotate", '-l', '-t', '-r', $file_name, $hash_base)
		or die_error(undef, "Open failed.");
	git_header_html();
	print "<div class=\"page_nav\">\n" .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base")}, "commit") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash_base")}, "commitdiff") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash_base")}, "tree") . "<br/>\n";
	print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$hash;hb=$hash_base;f=$file_name")}, "blob") .
		" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blame;f=$file_name")}, "head") . "<br/>\n";
	print "</div>\n".
		"<div>" .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base"), -class => "title"}, esc_html($co{'title'})) .
		"</div>\n";
	print "<div class=\"page_path\"><b>" . esc_html($file_name) . "</b></div>\n";
	print "<div class=\"page_body\">\n";
	print <<HTML;
<table style="border-collapse: collapse;">
  <tr>
    <th>Commit</th>
    <th>Age</th>
    <th>Author</th>
    <th>Line</th>
    <th>Data</th>
  </tr>
HTML
	my @line_class = (qw(light dark));
	my $line_class_len = scalar (@line_class);
	my $line_class_num = $#line_class;
	while (my $line = <$fd>) {
		my $long_rev;
		my $short_rev;
		my $author;
		my $time;
		my $lineno;
		my $data;
		my $age;
		my $age_str;
		my $age_style;

		chomp $line;
		$line_class_num = ($line_class_num + 1) % $line_class_len;

		if ($line =~ m/^([0-9a-fA-F]{40})\t\(\s*([^\t]+)\t(\d+) \+\d\d\d\d\t(\d+)\)(.*)$/) {
			$long_rev = $1;
			$author   = $2;
			$time     = $3;
			$lineno   = $4;
			$data     = $5;
		} else {
			print qq(  <tr><td colspan="5" style="color: red; background-color: yellow;">Unable to parse: $line</td></tr>\n);
			next;
		}
		$short_rev  = substr ($long_rev, 0, 8);
		$age        = time () - $time;
		$age_str    = age_string ($age);
		$age_str    =~ s/ /&nbsp;/g;
		$age_style  = 'font-style: italic;';
		$age_style .= ' color: #009900; background: transparent;' if ($age < 60*60*24*2);
		$age_style .= ' font-weight: bold;' if ($age < 60*60*2);
		$author     = esc_html ($author);
		$author     =~ s/ /&nbsp;/g;
		# escape tabs
		while ((my $pos = index($data, "\t")) != -1) {
			if (my $count = (8 - ($pos % 8))) {
				my $spaces = ' ' x $count;
				$data =~ s/\t/$spaces/;
			}
		}
		$data = esc_html ($data);
		$data =~ s/ /&nbsp;/g;

		print <<HTML;
  <tr class="$line_class[$line_class_num]">
    <td style="font-family: monospace;"><a href="$my_uri?${\esc_param ("p=$project;a=commit;h=$long_rev")}" class="text">$short_rev..</a></td>
    <td style="$age_style">$age_str</td>
    <td>$author</td>
    <td style="text-align: right;"><a id="$lineno" href="#$lineno" class="linenr">$lineno</a></td>
    <td style="font-family: monospace;">$data</td>
  </tr>
HTML
	} # while (my $line = <$fd>)
	print "</table>\n\n";
	close $fd or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tags {
	my $head = git_read_head($project);
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$head")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$head")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;hb=$head")}, "tree") . "<br/>\n" .
	      "<br/>\n" .
	      "</div>\n";
	my $taglist = git_read_refs("refs/tags");
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary"), -class => "title"}, "&nbsp;") .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if (defined @$taglist) {
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			my $comment_lines = $tag{'comment'};
			my $comment = shift @$comment_lines;
			if (defined($comment)) {
				$comment = chop_str($comment, 30, 5);
			}
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			print "<td><i>$tag{'age'}</i></td>\n" .
			      "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=$tag{'reftype'};h=$tag{'refid'}"), -class => "list"},
			      "<b>" . esc_html($tag{'name'}) . "</b>") .
			      "</td>\n" .
			      "<td>";
			if (defined($comment)) {
			      print $cgi->a({-class => "list", -href => "$my_uri?" . esc_param("p=$project;a=tag;h=$tag{'id'}")}, $comment);
			}
			print "</td>\n" .
			      "<td class=\"link\">";
			if ($tag{'type'} eq "tag") {
			      print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tag;h=$tag{'id'}")}, "tag") . " | ";
			}
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=$tag{'reftype'};h=$tag{'refid'}")}, $tag{'reftype'});
			if ($tag{'reftype'} eq "commit") {
			      print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}")}, "shortlog") .
			            " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$tag{'refid'}")}, "log");
			}
			print "</td>\n" .
			      "</tr>";
		}
	}
	print "</table\n>";
	git_footer_html();
}

sub git_heads {
	my $head = git_read_head($project);
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$head")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$head")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;hb=$head")}, "tree") . "<br/>\n" .
	      "<br/>\n" .
	      "</div>\n";
	my $taglist = git_read_refs("refs/heads");
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary"), -class => "title"}, "&nbsp;") .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if (defined @$taglist) {
		foreach my $entry (@$taglist) {
			my %tag = %$entry;
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			print "<td><i>$tag{'age'}</i></td>\n" .
			      "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}"), -class => "list"}, "<b>" . esc_html($tag{'name'}) . "</b>") .
			      "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$tag{'name'}")}, "shortlog") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$tag{'name'}")}, "log") .
			      "</td>\n" .
			      "</tr>";
		}
	}
	print "</table\n>";
	git_footer_html();
}

sub git_get_hash_by_path {
	my $base = shift;
	my $path = shift || return undef;

	my $tree = $base;
	my @parts = split '/', $path;
	while (my $part = shift @parts) {
		open my $fd, "-|", "$gitbin/git-ls-tree $tree" or die_error(undef, "Open git-ls-tree failed.");
		my (@entries) = map { chomp; $_ } <$fd>;
		close $fd or return undef;
		foreach my $line (@entries) {
			#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
			$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t(.+)$/;
			my $t_mode = $1;
			my $t_type = $2;
			my $t_hash = $3;
			my $t_name = validate_input(unquote($4));
			if ($t_name eq $part) {
				if (!(@parts)) {
					return $t_hash;
				}
				if ($t_type eq "tree") {
					$tree = $t_hash;
				}
				last;
			}
		}
	}
}

sub git_blob {
	if (!defined $hash && defined $file_name) {
		my $base = $hash_base || git_read_head($project);
		$hash = git_get_hash_by_path($base, $file_name, "blob") || die_error(undef, "Error lookup file.");
	}
	my $have_blame = git_get_project_config_bool ('blame');
	open my $fd, "-|", "$gitbin/git-cat-file blob $hash" or die_error(undef, "Open failed.");
	git_header_html();
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash_base")}, "commitdiff") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash_base")}, "tree") . "<br/>\n";
		if (defined $file_name) {
			if ($have_blame) {
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blame;h=$hash;hb=$hash_base;f=$file_name")}, "blame") .  " | ";
			}
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob_plain;h=$hash;f=$file_name")}, "plain") .
			" | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;hb=HEAD;f=$file_name")}, "head") . "<br/>\n";
		} else {
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob_plain;h=$hash")}, "plain") . "<br/>\n";
		}
		print "</div>\n".
		       "<div>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base"), -class => "title"}, esc_html($co{'title'})) .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		print "<div class=\"page_path\"><b>" . esc_html($file_name) . "</b></div>\n";
	}
	print "<div class=\"page_body\">\n";
	my $nr;
	while (my $line = <$fd>) {
		chomp $line;
		$nr++;
		while ((my $pos = index($line, "\t")) != -1) {
			if (my $count = (8 - ($pos % 8))) {
				my $spaces = ' ' x $count;
				$line =~ s/\t/$spaces/;
			}
		}
		printf "<div class=\"pre\"><a id=\"l%i\" href=\"#l%i\" class=\"linenr\">%4i</a> %s</div>\n", $nr, $nr, $nr, esc_html($line);
	}
	close $fd or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub mimetype_guess_file {
	my $filename = shift;
	my $mimemap = shift;
	-r $mimemap or return undef;

	my %mimemap;
	open(MIME, $mimemap) or return undef;
	while (<MIME>) {
		my ($mime, $exts) = split(/\t+/);
		my @exts = split(/\s+/, $exts);
		foreach my $ext (@exts) {
			$mimemap{$ext} = $mime;
		}
	}
	close(MIME);

	$filename =~ /\.(.*?)$/;
	return $mimemap{$1};
}

sub mimetype_guess {
	my $filename = shift;
	my $mime;
	$filename =~ /\./ or return undef;

	if ($mimetypes_file) {
		my $file = $mimetypes_file;
		#$file =~ m#^/# or $file = "$projectroot/$path/$file";
		$mime = mimetype_guess_file($filename, $file);
	}
	$mime ||= mimetype_guess_file($filename, '/etc/mime.types');
	return $mime;
}

sub git_blob_plain_mimetype {
	my $fd = shift;
	my $filename = shift;

	# just in case
	return $default_blob_plain_mimetype unless $fd;

	if ($filename) {
		my $mime = mimetype_guess($filename);
		$mime and return $mime;
	}

	if (-T $fd) {
		return 'text/plain' .
		       ($default_text_plain_charset ? '; charset='.$default_text_plain_charset : '');
	} elsif (! $filename) {
		return 'application/octet-stream';
	} elsif ($filename =~ m/\.png$/i) {
		return 'image/png';
	} elsif ($filename =~ m/\.gif$/i) {
		return 'image/gif';
	} elsif ($filename =~ m/\.jpe?g$/i) {
		return 'image/jpeg';
	} else {
		return 'application/octet-stream';
	}
}

sub git_blob_plain {
	open my $fd, "-|", "$gitbin/git-cat-file blob $hash" or return;
	my $type = git_blob_plain_mimetype($fd, $file_name);

	# save as filename, even when no $file_name is given
	my $save_as = "$hash";
	if (defined $file_name) {
		$save_as = $file_name;
	} elsif ($type =~ m/^text\//) {
		$save_as .= '.txt';
	}

	print $cgi->header(-type => "$type", '-content-disposition' => "inline; filename=\"$save_as\"");
	undef $/;
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	$/ = "\n";
	close $fd;
}

sub git_tree {
	if (!defined $hash) {
		$hash = git_read_head($project);
		if (defined $file_name) {
			my $base = $hash_base || $hash;
			$hash = git_get_hash_by_path($base, $file_name, "tree");
		}
		if (!defined $hash_base) {
			$hash_base = $hash;
		}
	}
	$/ = "\0";
	open my $fd, "-|", "$gitbin/git-ls-tree -z $hash" or die_error(undef, "Open git-ls-tree failed.");
	chomp (my (@entries) = <$fd>);
	close $fd or die_error(undef, "Reading tree failed.");
	$/ = "\n";

	my $refs = read_info_ref();
	my $ref = "";
	if (defined $refs->{$hash_base}) {
		$ref = " <span class=\"tag\">" . esc_html($refs->{$hash_base}) . "</span>";
	}
	git_header_html();
	my $base_key = "";
	my $base = "";
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		$base_key = ";hb=$hash_base";
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash_base")}, "shortlog") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash_base")}, "log") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash_base")}, "commitdiff") .
		      " | tree" .
		      "<br/><br/>\n" .
		      "</div>\n";
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base"), -class => "title"}, esc_html($co{'title'}) . $ref) . "\n" .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		$base = esc_html("$file_name/");
		print "<div class=\"page_path\"><b>/" . esc_html($file_name) . "</b></div>\n";
	} else {
		print "<div class=\"page_path\"><b>/</b></div>\n";
	}
	print "<div class=\"page_body\">\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $line (@entries) {
		#'100644	blob	0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t(.+)$/;
		my $t_mode = $1;
		my $t_type = $2;
		my $t_hash = $3;
		my $t_name = validate_input($4);
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td style=\"font-family:monospace\">" . mode_str($t_mode) . "</td>\n";
		if ($t_type eq "blob") {
			print "<td class=\"list\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$t_hash$base_key;f=$base$t_name"), -class => "list"}, esc_html($t_name)) .
			      "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$t_hash$base_key;f=$base$t_name")}, "blob") .
#			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blame;h=$t_hash$base_key;f=$base$t_name")}, "blame") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=history;h=$hash_base;f=$base$t_name")}, "history") .
			      "</td>\n";
		} elsif ($t_type eq "tree") {
			print "<td class=\"list\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$t_hash$base_key;f=$base$t_name")}, esc_html($t_name)) .
			      "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$t_hash$base_key;f=$base$t_name")}, "tree") .
			      "</td>\n";
		}
		print "</tr>\n";
	}
	print "</table>\n" .
	      "</div>";
	git_footer_html();
}

sub git_rss {
	# http://www.notestips.com/80256B3A007F2692/1/NAMO5P9UPQ
	open my $fd, "-|", "$gitbin/git-rev-list --max-count=150 " . git_read_head($project) or die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading rev-list failed.");
	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n".
	      "<rss version=\"2.0\" xmlns:content=\"http://purl.org/rss/1.0/modules/content/\">\n";
	print "<channel>\n";
	print "<title>$project</title>\n".
	      "<link>" . esc_html("$my_url?p=$project;a=summary") . "</link>\n".
	      "<description>$project log</description>\n".
	      "<language>en</language>\n";

	for (my $i = 0; $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my %co = git_read_commit($commit);
		# we read 150, we always show 30 and the ones more recent than 48 hours
		if (($i >= 20) && ((time - $co{'committer_epoch'}) > 48*60*60)) {
			last;
		}
		my %cd = date_str($co{'committer_epoch'});
		open $fd, "-|", "$gitbin/git-diff-tree -r $co{'parent'} $co{'id'}" or next;
		my @difftree = map { chomp; $_ } <$fd>;
		close $fd or next;
		print "<item>\n" .
		      "<title>" .
		      sprintf("%d %s %02d:%02d", $cd{'mday'}, $cd{'month'}, $cd{'hour'}, $cd{'minute'}) . " - " . esc_html($co{'title'}) .
		      "</title>\n" .
		      "<author>" . esc_html($co{'author'}) . "</author>\n" .
		      "<pubDate>$cd{'rfc2822'}</pubDate>\n" .
		      "<guid isPermaLink=\"true\">" . esc_html("$my_url?p=$project;a=commit;h=$commit") . "</guid>\n" .
		      "<link>" . esc_html("$my_url?p=$project;a=commit;h=$commit") . "</link>\n" .
		      "<description>" . esc_html($co{'title'}) . "</description>\n" .
		      "<content:encoded>" .
		      "<![CDATA[\n";
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			$line = decode("utf8", $line, Encode::FB_DEFAULT);
			print "$line<br/>\n";
		}
		print "<br/>\n";
		foreach my $line (@difftree) {
			if (!($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)([0-9]{0,3})\t(.*)$/)) {
				next;
			}
			my $file = validate_input(unquote($7));
			$file = decode("utf8", $file, Encode::FB_DEFAULT);
			print "$file<br/>\n";
		}
		print "]]>\n" .
		      "</content:encoded>\n" .
		      "</item>\n";
	}
	print "</channel></rss>";
}

sub git_opml {
	my @list = git_read_projects();

	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n".
	      "<opml version=\"1.0\">\n".
	      "<head>".
	      "  <title>Git OPML Export</title>\n".
	      "</head>\n".
	      "<body>\n".
	      "<outline text=\"git RSS feeds\">\n";

	foreach my $pr (@list) {
		my %proj = %$pr;
		my $head = git_read_head($proj{'path'});
		if (!defined $head) {
			next;
		}
		$ENV{'GIT_DIR'} = "$projectroot/$proj{'path'}";
		my %co = git_read_commit($head);
		if (!%co) {
			next;
		}

		my $path = esc_html(chop_str($proj{'path'}, 25, 5));
		my $rss =  "$my_url?p=$proj{'path'};a=rss";
		my $html =  "$my_url?p=$proj{'path'};a=summary";
		print "<outline type=\"rss\" text=\"$path\" title=\"$path\" xmlUrl=\"$rss\" htmlUrl=\"$html\"/>\n";
	}
	print "</outline>\n".
	      "</body>\n".
	      "</opml>\n";
}

sub git_log {
	my $head = git_read_head($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = read_info_ref();
	git_header_html();
	print "<div class=\"page_nav\">\n";
	print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash")}, "shortlog") .
	      " | log" .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$hash;hb=$hash")}, "tree") . "<br/>\n";

	my $limit = sprintf("--max-count=%i", (100 * ($page+1)));
	open my $fd, "-|", "$gitbin/git-rev-list $limit $hash" or die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	if ($hash ne $head || $page) {
		print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "HEAD");
	} else {
		print "HEAD";
	}
	if ($page > 0) {
		print " &sdot; " .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash;pg=" . ($page-1)), -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		print " &sdot; prev";
	}
	if ($#revlist >= (100 * ($page+1)-1)) {
		print " &sdot; " .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash;pg=" . ($page+1)), -accesskey => "n", -title => "Alt-n"}, "next");
	} else {
		print " &sdot; next";
	}
	print "<br/>\n" .
	      "</div>\n";
	if (!@revlist) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary"), -class => "title"}, "&nbsp;") .
		      "</div>\n";
		my %co = git_read_commit($hash);
		print "<div class=\"page_body\"> Last change $co{'age_string'}.<br/><br/></div>\n";
	}
	for (my $i = ($page * 100); $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my $ref = "";
		if (defined $refs->{$commit}) {
			$ref = " <span class=\"tag\">" . esc_html($refs->{$commit}) . "</span>";
		}
		my %co = git_read_commit($commit);
		next if !%co;
		my %ad = date_str($co{'author_epoch'});
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "title"},
		      "<span class=\"age\">$co{'age_string'}</span>" . esc_html($co{'title'}) . $ref) . "\n";
		print "</div>\n";
		print "<div class=\"title_text\">\n" .
		      "<div class=\"log_link\">\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$commit")}, "commitdiff") .
		      "<br/>\n" .
		      "</div>\n" .
		      "<i>" . esc_html($co{'author_name'}) .  " [$ad{'rfc2822'}]</i><br/>\n" .
		      "</div>\n" .
		      "<div class=\"log_body\">\n";
		my $comment = $co{'comment'};
		my $empty = 0;
		foreach my $line (@$comment) {
			if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
				next;
			}
			if ($line eq "") {
				if ($empty) {
					next;
				}
				$empty = 1;
			} else {
				$empty = 0;
			}
			print format_log_line_html($line) . "<br/>\n";
		}
		if (!$empty) {
			print "<br/>\n";
		}
		print "</div>\n";
	}
	git_footer_html();
}

sub git_commit {
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	my %ad = date_str($co{'author_epoch'}, $co{'author_tz'});
	my %cd = date_str($co{'committer_epoch'}, $co{'committer_tz'});

	my @difftree;
	my $root = "";
	my $parent = $co{'parent'};
	if (!defined $parent) {
		$root = " --root";
		$parent = "";
	}
	open my $fd, "-|", "$gitbin/git-diff-tree -r -M $root $parent $hash" or die_error(undef, "Open failed.");
	@difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading diff-tree failed.");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}
	my $refs = read_info_ref();
	my $ref = "";
	if (defined $refs->{$co{'id'}}) {
		$ref = " <span class=\"tag\">" . esc_html($refs->{$co{'id'}}) . "</span>";
	}
	git_header_html(undef, $expires);
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash")}, "log") .
	      " | commit";
	if (defined $co{'parent'}) {
		print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash")}, "commitdiff");
	}
	print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash")}, "tree") . "\n" .
	      "<br/><br/></div>\n";
	if (defined $co{'parent'}) {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash"), -class => "title"}, esc_html($co{'title'}) . $ref) . "\n" .
		      "</div>\n";
	} else {
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash"), -class => "title"}, esc_html($co{'title'})) . "\n" .
		      "</div>\n";
	}
	print "<div class=\"title_text\">\n" .
	      "<table cellspacing=\"0\">\n";
	print "<tr><td>author</td><td>" . esc_html($co{'author'}) . "</td></tr>\n".
	      "<tr>" .
	      "<td></td><td> $ad{'rfc2822'}";
	if ($ad{'hour_local'} < 6) {
		printf(" (<span style=\"color: #cc0000;\">%02d:%02d</span> %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "</td>" .
	      "</tr>\n";
	print "<tr><td>committer</td><td>" . esc_html($co{'committer'}) . "</td></tr>\n";
	print "<tr><td></td><td> $cd{'rfc2822'}" . sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) . "</td></tr>\n";
	print "<tr><td>commit</td><td style=\"font-family:monospace\">$co{'id'}</td></tr>\n";
	print "<tr>" .
	      "<td>tree</td>" .
	      "<td style=\"font-family:monospace\">" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash"), class => "list"}, $co{'tree'}) .
	      "</td>" .
	      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash")}, "tree") .
	      "</td>" .
	      "</tr>\n";
	my $parents  = $co{'parents'};
	foreach my $par (@$parents) {
		print "<tr>" .
		      "<td>parent</td>" .
		      "<td style=\"font-family:monospace\">" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$par"), class => "list"}, $par) . "</td>" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$par")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash;hp=$par")}, "commitdiff") .
		      "</td>" .
		      "</tr>\n";
	}
	print "</table>". 
	      "</div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	my $empty = 0;
	my $signed = 0;
	foreach my $line (@$comment) {
		# print only one empty line
		if ($line eq "") {
			if ($empty || $signed) {
				next;
			}
			$empty = 1;
		} else {
			$empty = 0;
		}
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			$signed = 1;
			print "<span style=\"color: #888888\">" . esc_html($line) . "</span><br/>\n";
		} else {
			$signed = 0;
			print format_log_line_html($line) . "<br/>\n";
		}
	}
	print "</div>\n";
	print "<div class=\"list_head\">\n";
	if ($#difftree > 10) {
		print(($#difftree + 1) . " files changed:\n");
	}
	print "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	foreach my $line (@difftree) {
		# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M      ls-files.c'
		# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M      rev-tree.c'
		if (!($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)([0-9]{0,3})\t(.*)$/)) {
			next;
		}
		my $from_mode = $1;
		my $to_mode = $2;
		my $from_id = $3;
		my $to_id = $4;
		my $status = $5;
		my $similarity = $6;
		my $file = validate_input(unquote($7));
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		if ($status eq "A") {
			my $mode_chng = "";
			if (S_ISREG(oct $to_mode)) {
				$mode_chng = sprintf(" with mode: %04o", (oct $to_mode) & 0777);
			}
			print "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file"), -class => "list"}, esc_html($file)) . "</td>\n" .
			      "<td><span style=\"color: #008000;\">[new " . file_type($to_mode) . "$mode_chng]</span></td>\n" .
			      "<td class=\"link\">" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file")}, "blob") . "</td>\n";
		} elsif ($status eq "D") {
			print "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$from_id;hb=$hash;f=$file"), -class => "list"}, esc_html($file)) . "</td>\n" .
			      "<td><span style=\"color: #c00000;\">[deleted " . file_type($from_mode). "]</span></td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$from_id;hb=$hash;f=$file")}, "blob") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=history;h=$hash;f=$file")}, "history") .
			      "</td>\n"
		} elsif ($status eq "M" || $status eq "T") {
			my $mode_chnge = "";
			if ($from_mode != $to_mode) {
				$mode_chnge = " <span style=\"color: #777777;\">[changed";
				if (((oct $from_mode) & S_IFMT) != ((oct $to_mode) & S_IFMT)) {
					$mode_chnge .= " from " . file_type($from_mode) . " to " . file_type($to_mode);
				}
				if (((oct $from_mode) & 0777) != ((oct $to_mode) & 0777)) {
					if (S_ISREG($from_mode) && S_ISREG($to_mode)) {
						$mode_chnge .= sprintf(" mode: %04o->%04o", (oct $from_mode) & 0777, (oct $to_mode) & 0777);
					} elsif (S_ISREG($to_mode)) {
						$mode_chnge .= sprintf(" mode: %04o", (oct $to_mode) & 0777);
					}
				}
				$mode_chnge .= "]</span>\n";
			}
			print "<td>";
			if ($to_id ne $from_id) {
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blobdiff;h=$to_id;hp=$from_id;hb=$hash;f=$file"), -class => "list"}, esc_html($file));
			} else {
				print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file"), -class => "list"}, esc_html($file));
			}
			print "</td>\n" .
			      "<td>$mode_chnge</td>\n" .
			      "<td class=\"link\">";
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file")}, "blob");
			if ($to_id ne $from_id) {
				print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blobdiff;h=$to_id;hp=$from_id;hb=$hash;f=$file")}, "diff");
			}
			print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=history;h=$hash;f=$file")}, "history") . "\n";
			print "</td>\n";
		} elsif ($status eq "R") {
			my ($from_file, $to_file) = split "\t", $file;
			my $mode_chng = "";
			if ($from_mode != $to_mode) {
				$mode_chng = sprintf(", mode: %04o", (oct $to_mode) & 0777);
			}
			print "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$to_file"), -class => "list"}, esc_html($to_file)) . "</td>\n" .
			      "<td><span style=\"color: #777777;\">[moved from " .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$from_id;hb=$hash;f=$from_file"), -class => "list"}, esc_html($from_file)) .
			      " with " . (int $similarity) . "% similarity$mode_chng]</span></td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$to_file")}, "blob");
			if ($to_id ne $from_id) {
				print " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blobdiff;h=$to_id;hp=$from_id;hb=$hash;f=$to_file")}, "diff");
			}
			print "</td>\n";
		}
		print "</tr>\n";
	}
	print "</table>\n";
	git_footer_html();
}

sub git_blobdiff {
	mkdir($git_temp, 0700);
	git_header_html();
	if (defined $hash_base && (my %co = git_read_commit($hash_base))) {
		print "<div class=\"page_nav\">\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash_base")}, "commitdiff") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash_base")}, "tree") .
		      "<br/>\n";
		print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blobdiff_plain;h=$hash;hp=$hash_parent")}, "plain") .
		      "</div>\n";
		print "<div>\n" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash_base"), -class => "title"}, esc_html($co{'title'})) . "\n" .
		      "</div>\n";
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash vs $hash_parent</div>\n";
	}
	if (defined $file_name) {
		print "<div class=\"page_path\"><b>/" . esc_html($file_name) . "</b></div>\n";
	}
	print "<div class=\"page_body\">\n" .
	      "<div class=\"diff_info\">blob:" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$hash_parent;hb=$hash_base;f=$file_name")}, $hash_parent) .
	      " -> blob:" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$hash;hb=$hash_base;f=$file_name")}, $hash) .
	      "</div>\n";
	git_diff_print($hash_parent, $file_name || $hash_parent, $hash, $file_name || $hash);
	print "</div>";
	git_footer_html();
}

sub git_blobdiff_plain {
	mkdir($git_temp, 0700);
	print $cgi->header(-type => "text/plain", -charset => 'utf-8');
	git_diff_print($hash_parent, $file_name || $hash_parent, $hash, $file_name || $hash, "plain");
}

sub git_commitdiff {
	mkdir($git_temp, 0700);
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	if (!defined $hash_parent) {
		$hash_parent = $co{'parent'};
	}
	open my $fd, "-|", "$gitbin/git-diff-tree -r $hash_parent $hash" or die_error(undef, "Open failed.");
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading diff-tree failed.");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}
	my $refs = read_info_ref();
	my $ref = "";
	if (defined $refs->{$co{'id'}}) {
		$ref = " <span class=\"tag\">" . esc_html($refs->{$co{'id'}}) . "</span>";
	}
	git_header_html(undef, $expires);
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash")}, "commit") .
	      " | commitdiff" .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash")}, "tree") . "<br/>\n";
	print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff_plain;h=$hash;hp=$hash_parent")}, "plain") . "\n" .
	      "</div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash"), -class => "title"}, esc_html($co{'title'}) . $ref) . "\n" .
	      "</div>\n";
	print "<div class=\"page_body\">\n";
	my $comment = $co{'comment'};
	my $empty = 0;
	my $signed = 0;
	my @log = @$comment;
	# remove first and empty lines after that
	shift @log;
	while (defined $log[0] && $log[0] eq "") {
		shift @log;
	}
	foreach my $line (@log) {
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			next;
		}
		if ($line eq "") {
			if ($empty) {
				next;
			}
			$empty = 1;
		} else {
			$empty = 0;
		}
		print format_log_line_html($line) . "<br/>\n";
	}
	print "<br/>\n";
	foreach my $line (@difftree) {
		# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M      ls-files.c'
		# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M      rev-tree.c'
		$line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/;
		my $from_mode = $1;
		my $to_mode = $2;
		my $from_id = $3;
		my $to_id = $4;
		my $status = $5;
		my $file = validate_input(unquote($6));
		if ($status eq "A") {
			print "<div class=\"diff_info\">" .  file_type($to_mode) . ":" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file")}, $to_id) . "(new)" .
			      "</div>\n";
			git_diff_print(undef, "/dev/null", $to_id, "b/$file");
		} elsif ($status eq "D") {
			print "<div class=\"diff_info\">" . file_type($from_mode) . ":" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$from_id;hb=$hash;f=$file")}, $from_id) . "(deleted)" .
			      "</div>\n";
			git_diff_print($from_id, "a/$file", undef, "/dev/null");
		} elsif ($status eq "M") {
			if ($from_id ne $to_id) {
				print "<div class=\"diff_info\">" .
				      file_type($from_mode) . ":" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$from_id;hb=$hash;f=$file")}, $from_id) .
				      " -> " .
				      file_type($to_mode) . ":" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$to_id;hb=$hash;f=$file")}, $to_id);
				print "</div>\n";
				git_diff_print($from_id, "a/$file",  $to_id, "b/$file");
			}
		}
	}
	print "<br/>\n" .
	      "</div>";
	git_footer_html();
}

sub git_commitdiff_plain {
	mkdir($git_temp, 0700);
	open my $fd, "-|", "$gitbin/git-diff-tree -r $hash_parent $hash" or die_error(undef, "Open failed.");
	my (@difftree) = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading diff-tree failed.");

	# try to figure out the next tag after this commit
	my $tagname;
	my $refs = read_info_ref("tags");
	open $fd, "-|", "$gitbin/git-rev-list HEAD";
	chomp (my (@commits) = <$fd>);
	close $fd;
	foreach my $commit (@commits) {
		if (defined $refs->{$commit}) {
			$tagname = $refs->{$commit}
		}
		if ($commit eq $hash) {
			last;
		}
	}

	print $cgi->header(-type => "text/plain", -charset => 'utf-8', '-content-disposition' => "inline; filename=\"git-$hash.patch\"");
	my %co = git_read_commit($hash);
	my %ad = date_str($co{'author_epoch'}, $co{'author_tz'});
	my $comment = $co{'comment'};
	print "From: $co{'author'}\n" .
	      "Date: $ad{'rfc2822'} ($ad{'tz_local'})\n".
	      "Subject: $co{'title'}\n";
	if (defined $tagname) {
	      print "X-Git-Tag: $tagname\n";
	}
	print "X-Git-Url: $my_url?p=$project;a=commitdiff;h=$hash\n" .
	      "\n";

	foreach my $line (@$comment) {;
		print "$line\n";
	}
	print "---\n\n";

	foreach my $line (@difftree) {
		$line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/;
		my $from_id = $3;
		my $to_id = $4;
		my $status = $5;
		my $file = $6;
		if ($status eq "A") {
			git_diff_print(undef, "/dev/null", $to_id, "b/$file", "plain");
		} elsif ($status eq "D") {
			git_diff_print($from_id, "a/$file", undef, "/dev/null", "plain");
		} elsif ($status eq "M") {
			git_diff_print($from_id, "a/$file",  $to_id, "b/$file", "plain");
		}
	}
}

sub git_history {
	if (!defined $hash) {
		$hash = git_read_head($project);
	}
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	my $refs = read_info_ref();
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash")}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash"), -class => "title"}, esc_html($co{'title'})) . "\n" .
	      "</div>\n";
	print "<div class=\"page_path\"><b>/" . esc_html($file_name) . "</b><br/></div>\n";

	open my $fd, "-|", "$gitbin/git-rev-list $hash | $gitbin/git-diff-tree -r --stdin -- \'$file_name\'";
	my $commit;
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	while (my $line = <$fd>) {
		if ($line =~ m/^([0-9a-fA-F]{40})/){
			$commit = $1;
			next;
		}
		if ($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/ && (defined $commit)) {
			my %co = git_read_commit($commit);
			if (!%co) {
				next;
			}
			my $ref = "";
			if (defined $refs->{$commit}) {
				$ref = " <span class=\"tag\">" . esc_html($refs->{$commit}) . "</span>";
			}
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
			      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 3)) . "</i></td>\n" .
			      "<td>" . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "list"}, "<b>" .
			      esc_html(chop_str($co{'title'}, 50)) . "$ref</b>") . "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit")}, "commit") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$commit")}, "commitdiff") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;hb=$commit;f=$file_name")}, "blob");
			my $blob = git_get_hash_by_path($hash, $file_name);
			my $blob_parent = git_get_hash_by_path($commit, $file_name);
			if (defined $blob && defined $blob_parent && $blob ne $blob_parent) {
				print " | " .
				$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blobdiff;h=$blob;hp=$blob_parent;hb=$commit;f=$file_name")},
				"diff to current");
			}
			print "</td>\n" .
			      "</tr>\n";
			undef $commit;
		}
	}
	print "</table>\n";
	close $fd;
	git_footer_html();
}

sub git_search {
	if (!defined $searchtext) {
		die_error("", "Text field empty.");
	}
	if (!defined $hash) {
		$hash = git_read_head($project);
	}
	my %co = git_read_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object.");
	}
	# pickaxe may take all resources of your box and run for several minutes
	# with every query - so decide by yourself how public you make this feature :)
	my $commit_search = 1;
	my $author_search = 0;
	my $committer_search = 0;
	my $pickaxe_search = 0;
	if ($searchtext =~ s/^author\\://i) {
		$author_search = 1;
	} elsif ($searchtext =~ s/^committer\\://i) {
		$committer_search = 1;
	} elsif ($searchtext =~ s/^pickaxe\\://i) {
		$commit_search = 0;
		$pickaxe_search = 1;
	}
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary;h=$hash")}, "summary") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "shortlog") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$hash")}, "tree") .
	      "<br/><br/>\n" .
	      "</div>\n";

	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash"), -class => "title"}, esc_html($co{'title'})) . "\n" .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	if ($commit_search) {
		$/ = "\0";
		open my $fd, "-|", "$gitbin/git-rev-list --header --parents $hash" or next;
		while (my $commit_text = <$fd>) {
			if (!grep m/$searchtext/i, $commit_text) {
				next;
			}
			if ($author_search && !grep m/\nauthor .*$searchtext/i, $commit_text) {
				next;
			}
			if ($committer_search && !grep m/\ncommitter .*$searchtext/i, $commit_text) {
				next;
			}
			my @commit_lines = split "\n", $commit_text;
			my %co = git_read_commit(undef, \@commit_lines);
			if (!%co) {
				next;
			}
			if ($alternate) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$alternate ^= 1;
			print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
			      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 5)) . "</i></td>\n" .
			      "<td>" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$co{'id'}"), -class => "list"}, "<b>" . esc_html(chop_str($co{'title'}, 50)) . "</b><br/>");
			my $comment = $co{'comment'};
			foreach my $line (@$comment) {
				if ($line =~ m/^(.*)($searchtext)(.*)$/i) {
					my $lead = esc_html($1) || "";
					$lead = chop_str($lead, 30, 10);
					my $match = esc_html($2) || "";
					my $trail = esc_html($3) || "";
					$trail = chop_str($trail, 30, 10);
					my $text = "$lead<span style=\"color:#e00000\">$match</span>$trail";
					print chop_str($text, 80, 5) . "<br/>\n";
				}
			}
			print "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$co{'id'}")}, "commit") .
			      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$co{'id'}")}, "tree");
			print "</td>\n" .
			      "</tr>\n";
		}
		close $fd;
	}

	if ($pickaxe_search) {
		$/ = "\n";
		open my $fd, "-|", "$gitbin/git-rev-list $hash | $gitbin/git-diff-tree -r --stdin -S\'$searchtext\'";
		undef %co;
		my @files;
		while (my $line = <$fd>) {
			if (%co && $line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)\t(.*)$/) {
				my %set;
				$set{'file'} = $6;
				$set{'from_id'} = $3;
				$set{'to_id'} = $4;
				$set{'id'} = $set{'to_id'};
				if ($set{'id'} =~ m/0{40}/) {
					$set{'id'} = $set{'from_id'};
				}
				if ($set{'id'} =~ m/0{40}/) {
					next;
				}
				push @files, \%set;
			} elsif ($line =~ m/^([0-9a-fA-F]{40})$/){
				if (%co) {
					if ($alternate) {
						print "<tr class=\"dark\">\n";
					} else {
						print "<tr class=\"light\">\n";
					}
					$alternate ^= 1;
					print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
					      "<td><i>" . esc_html(chop_str($co{'author_name'}, 15, 5)) . "</i></td>\n" .
					      "<td>" .
					      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$co{'id'}"), -class => "list"}, "<b>" .
					      esc_html(chop_str($co{'title'}, 50)) . "</b><br/>");
					while (my $setref = shift @files) {
						my %set = %$setref;
						print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=blob;h=$set{'id'};hb=$co{'id'};f=$set{'file'}"), class => "list"},
						      "<span style=\"color:#e00000\">" . esc_html($set{'file'}) . "</span>") .
						      "<br/>\n";
					}
					print "</td>\n" .
					      "<td class=\"link\">" .
					      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$co{'id'}")}, "commit") .
					      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$co{'tree'};hb=$co{'id'}")}, "tree");
					print "</td>\n" .
					      "</tr>\n";
				}
				%co = git_read_commit($1);
			}
		}
		close $fd;
	}
	print "</table>\n";
	git_footer_html();
}

sub git_shortlog {
	my $head = git_read_head($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = read_info_ref();
	git_header_html();
	print "<div class=\"page_nav\">\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary")}, "summary") .
	      " | shortlog" .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=log;h=$hash")}, "log") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$hash")}, "commit") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$hash")}, "commitdiff") .
	      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=tree;h=$hash;hb=$hash")}, "tree") . "<br/>\n";

	my $limit = sprintf("--max-count=%i", (100 * ($page+1)));
	open my $fd, "-|", "$gitbin/git-rev-list $limit $hash" or die_error(undef, "Open failed.");
	my (@revlist) = map { chomp; $_ } <$fd>;
	close $fd;

	if ($hash ne $head || $page) {
		print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog")}, "HEAD");
	} else {
		print "HEAD";
	}
	if ($page > 0) {
		print " &sdot; " .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash;pg=" . ($page-1)), -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		print " &sdot; prev";
	}
	if ($#revlist >= (100 * ($page+1)-1)) {
		print " &sdot; " .
		$cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash;pg=" . ($page+1)), -accesskey => "n", -title => "Alt-n"}, "next");
	} else {
		print " &sdot; next";
	}
	print "<br/>\n" .
	      "</div>\n";
	print "<div>\n" .
	      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=summary"), -class => "title"}, "&nbsp;") .
	      "</div>\n";
	print "<table cellspacing=\"0\">\n";
	my $alternate = 0;
	for (my $i = ($page * 100); $i <= $#revlist; $i++) {
		my $commit = $revlist[$i];
		my $ref = "";
		if (defined $refs->{$commit}) {
			$ref = " <span class=\"tag\">" . esc_html($refs->{$commit}) . "</span>";
		}
		my %co = git_read_commit($commit);
		my %ad = date_str($co{'author_epoch'});
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      "<td><i>" . esc_html(chop_str($co{'author_name'}, 10)) . "</i></td>\n" .
		      "<td>";
		if (length($co{'title_short'}) < length($co{'title'})) {
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "list", -title => "$co{'title'}"},
			      "<b>" . esc_html($co{'title_short'}) . "$ref</b>");
		} else {
			print $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit"), -class => "list"},
			      "<b>" . esc_html($co{'title_short'}) . "$ref</b>");
		}
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commit;h=$commit")}, "commit") .
		      " | " . $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=commitdiff;h=$commit")}, "commitdiff") .
		      "</td>\n" .
		      "</tr>";
	}
	if ($#revlist >= (100 * ($page+1)-1)) {
		print "<tr>\n" .
		      "<td>" .
		      $cgi->a({-href => "$my_uri?" . esc_param("p=$project;a=shortlog;h=$hash;pg=" . ($page+1)), -title => "Alt-n"}, "next") .
		      "</td>\n" .
		      "</tr>\n";
	}
	print "</table\n>";
	git_footer_html();
}
