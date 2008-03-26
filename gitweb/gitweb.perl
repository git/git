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
use File::Find qw();
use File::Basename qw(basename);
binmode STDOUT, ':utf8';

BEGIN {
	CGI->compile() if $ENV{'MOD_PERL'};
}

our $cgi = new CGI;
our $version = "++GIT_VERSION++";
our $my_url = $cgi->url();
our $my_uri = $cgi->url(-absolute => 1);

# core git executable to use
# this can just be "git" if your webserver has a sensible PATH
our $GIT = "++GIT_BINDIR++/git";

# absolute fs-path which will be prepended to the project path
#our $projectroot = "/pub/scm";
our $projectroot = "++GITWEB_PROJECTROOT++";

# fs traversing limit for getting project list
# the number is relative to the projectroot
our $project_maxdepth = "++GITWEB_PROJECT_MAXDEPTH++";

# target of the home link on top of all pages
our $home_link = $my_uri || "/";

# string of the home link on top of all pages
our $home_link_str = "++GITWEB_HOME_LINK_STR++";

# name of your site or organization to appear in page titles
# replace this with something more descriptive for clearer bookmarks
our $site_name = "++GITWEB_SITENAME++"
                 || ($ENV{'SERVER_NAME'} || "Untitled") . " Git";

# filename of html text to include at top of each page
our $site_header = "++GITWEB_SITE_HEADER++";
# html text to include at home page
our $home_text = "++GITWEB_HOMETEXT++";
# filename of html text to include at bottom of each page
our $site_footer = "++GITWEB_SITE_FOOTER++";

# URI of stylesheets
our @stylesheets = ("++GITWEB_CSS++");
# URI of a single stylesheet, which can be overridden in GITWEB_CONFIG.
our $stylesheet = undef;
# URI of GIT logo (72x27 size)
our $logo = "++GITWEB_LOGO++";
# URI of GIT favicon, assumed to be image/png type
our $favicon = "++GITWEB_FAVICON++";

# URI and label (title) of GIT logo link
#our $logo_url = "http://www.kernel.org/pub/software/scm/git/docs/";
#our $logo_label = "git documentation";
our $logo_url = "http://git.or.cz/";
our $logo_label = "git homepage";

# source of projects list
our $projects_list = "++GITWEB_LIST++";

# the width (in characters) of the projects list "Description" column
our $projects_list_description_width = 25;

# default order of projects list
# valid values are none, project, descr, owner, and age
our $default_projects_order = "project";

# show repository only if this file exists
# (only effective if this variable evaluates to true)
our $export_ok = "++GITWEB_EXPORT_OK++";

# only allow viewing of repositories also shown on the overview page
our $strict_export = "++GITWEB_STRICT_EXPORT++";

# list of git base URLs used for URL to where fetch project from,
# i.e. full URL is "$git_base_url/$project"
our @git_base_url_list = grep { $_ ne '' } ("++GITWEB_BASE_URL++");

# default blob_plain mimetype and default charset for text/plain blob
our $default_blob_plain_mimetype = 'text/plain';
our $default_text_plain_charset  = undef;

# file to use for guessing MIME types before trying /etc/mime.types
# (relative to the current git repository)
our $mimetypes_file = undef;

# assume this charset if line contains non-UTF-8 characters;
# it should be valid encoding (see Encoding::Supported(3pm) for list),
# for which encoding all byte sequences are valid, for example
# 'iso-8859-1' aka 'latin1' (it is decoded without checking, so it
# could be even 'utf-8' for the old behavior)
our $fallback_encoding = 'latin1';

# rename detection options for git-diff and git-diff-tree
# - default is '-M', with the cost proportional to
#   (number of removed files) * (number of new files).
# - more costly is '-C' (which implies '-M'), with the cost proportional to
#   (number of changed files + number of removed files) * (number of new files)
# - even more costly is '-C', '--find-copies-harder' with cost
#   (number of files in the original tree) * (number of new files)
# - one might want to include '-B' option, e.g. '-B', '-M'
our @diff_opts = ('-M'); # taken from git_commit

# information about snapshot formats that gitweb is capable of serving
our %known_snapshot_formats = (
	# name => {
	# 	'display' => display name,
	# 	'type' => mime type,
	# 	'suffix' => filename suffix,
	# 	'format' => --format for git-archive,
	# 	'compressor' => [compressor command and arguments]
	# 	                (array reference, optional)}
	#
	'tgz' => {
		'display' => 'tar.gz',
		'type' => 'application/x-gzip',
		'suffix' => '.tar.gz',
		'format' => 'tar',
		'compressor' => ['gzip']},

	'tbz2' => {
		'display' => 'tar.bz2',
		'type' => 'application/x-bzip2',
		'suffix' => '.tar.bz2',
		'format' => 'tar',
		'compressor' => ['bzip2']},

	'zip' => {
		'display' => 'zip',
		'type' => 'application/x-zip',
		'suffix' => '.zip',
		'format' => 'zip'},
);

# Aliases so we understand old gitweb.snapshot values in repository
# configuration.
our %known_snapshot_format_aliases = (
	'gzip'  => 'tgz',
	'bzip2' => 'tbz2',

	# backward compatibility: legacy gitweb config support
	'x-gzip' => undef, 'gz' => undef,
	'x-bzip2' => undef, 'bz2' => undef,
	'x-zip' => undef, '' => undef,
);

# You define site-wide feature defaults here; override them with
# $GITWEB_CONFIG as necessary.
our %feature = (
	# feature => {
	# 	'sub' => feature-sub (subroutine),
	# 	'override' => allow-override (boolean),
	# 	'default' => [ default options...] (array reference)}
	#
	# if feature is overridable (it means that allow-override has true value),
	# then feature-sub will be called with default options as parameters;
	# return value of feature-sub indicates if to enable specified feature
	#
	# if there is no 'sub' key (no feature-sub), then feature cannot be
	# overriden
	#
	# use gitweb_check_feature(<feature>) to check if <feature> is enabled

	# Enable the 'blame' blob view, showing the last commit that modified
	# each line in the file. This can be very CPU-intensive.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'blame'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'blame'}{'override'} = 1;
	# and in project config gitweb.blame = 0|1;
	'blame' => {
		'sub' => \&feature_blame,
		'override' => 0,
		'default' => [0]},

	# Enable the 'snapshot' link, providing a compressed archive of any
	# tree. This can potentially generate high traffic if you have large
	# project.

	# Value is a list of formats defined in %known_snapshot_formats that
	# you wish to offer.
	# To disable system wide have in $GITWEB_CONFIG
	# $feature{'snapshot'}{'default'} = [];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'snapshot'}{'override'} = 1;
	# and in project config, a comma-separated list of formats or "none"
	# to disable.  Example: gitweb.snapshot = tbz2,zip;
	'snapshot' => {
		'sub' => \&feature_snapshot,
		'override' => 0,
		'default' => ['tgz']},

	# Enable text search, which will list the commits which match author,
	# committer or commit text to a given string.  Enabled by default.
	# Project specific override is not supported.
	'search' => {
		'override' => 0,
		'default' => [1]},

	# Enable grep search, which will list the files in currently selected
	# tree containing the given string. Enabled by default. This can be
	# potentially CPU-intensive, of course.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'grep'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'grep'}{'override'} = 1;
	# and in project config gitweb.grep = 0|1;
	'grep' => {
		'override' => 0,
		'default' => [1]},

	# Enable the pickaxe search, which will list the commits that modified
	# a given string in a file. This can be practical and quite faster
	# alternative to 'blame', but still potentially CPU-intensive.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'pickaxe'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'pickaxe'}{'override'} = 1;
	# and in project config gitweb.pickaxe = 0|1;
	'pickaxe' => {
		'sub' => \&feature_pickaxe,
		'override' => 0,
		'default' => [1]},

	# Make gitweb use an alternative format of the URLs which can be
	# more readable and natural-looking: project name is embedded
	# directly in the path and the query string contains other
	# auxiliary information. All gitweb installations recognize
	# URL in either format; this configures in which formats gitweb
	# generates links.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'pathinfo'}{'default'} = [1];
	# Project specific override is not supported.

	# Note that you will need to change the default location of CSS,
	# favicon, logo and possibly other files to an absolute URL. Also,
	# if gitweb.cgi serves as your indexfile, you will need to force
	# $my_uri to contain the script name in your $GITWEB_CONFIG.
	'pathinfo' => {
		'override' => 0,
		'default' => [0]},

	# Make gitweb consider projects in project root subdirectories
	# to be forks of existing projects. Given project $projname.git,
	# projects matching $projname/*.git will not be shown in the main
	# projects list, instead a '+' mark will be added to $projname
	# there and a 'forks' view will be enabled for the project, listing
	# all the forks. If project list is taken from a file, forks have
	# to be listed after the main project.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'forks'}{'default'} = [1];
	# Project specific override is not supported.
	'forks' => {
		'override' => 0,
		'default' => [0]},
);

sub gitweb_check_feature {
	my ($name) = @_;
	return unless exists $feature{$name};
	my ($sub, $override, @defaults) = (
		$feature{$name}{'sub'},
		$feature{$name}{'override'},
		@{$feature{$name}{'default'}});
	if (!$override) { return @defaults; }
	if (!defined $sub) {
		warn "feature $name is not overrideable";
		return @defaults;
	}
	return $sub->(@defaults);
}

sub feature_blame {
	my ($val) = git_get_project_config('blame', '--bool');

	if ($val eq 'true') {
		return 1;
	} elsif ($val eq 'false') {
		return 0;
	}

	return $_[0];
}

sub feature_snapshot {
	my (@fmts) = @_;

	my ($val) = git_get_project_config('snapshot');

	if ($val) {
		@fmts = ($val eq 'none' ? () : split /\s*[,\s]\s*/, $val);
	}

	return @fmts;
}

sub feature_grep {
	my ($val) = git_get_project_config('grep', '--bool');

	if ($val eq 'true') {
		return (1);
	} elsif ($val eq 'false') {
		return (0);
	}

	return ($_[0]);
}

sub feature_pickaxe {
	my ($val) = git_get_project_config('pickaxe', '--bool');

	if ($val eq 'true') {
		return (1);
	} elsif ($val eq 'false') {
		return (0);
	}

	return ($_[0]);
}

# checking HEAD file with -e is fragile if the repository was
# initialized long time ago (i.e. symlink HEAD) and was pack-ref'ed
# and then pruned.
sub check_head_link {
	my ($dir) = @_;
	my $headfile = "$dir/HEAD";
	return ((-e $headfile) ||
		(-l $headfile && readlink($headfile) =~ /^refs\/heads\//));
}

sub check_export_ok {
	my ($dir) = @_;
	return (check_head_link($dir) &&
		(!$export_ok || -e "$dir/$export_ok"));
}

# process alternate names for backward compatibility
# filter out unsupported (unknown) snapshot formats
sub filter_snapshot_fmts {
	my @fmts = @_;

	@fmts = map {
		exists $known_snapshot_format_aliases{$_} ?
		       $known_snapshot_format_aliases{$_} : $_} @fmts;
	@fmts = grep(exists $known_snapshot_formats{$_}, @fmts);

}

our $GITWEB_CONFIG = $ENV{'GITWEB_CONFIG'} || "++GITWEB_CONFIG++";
if (-e $GITWEB_CONFIG) {
	do $GITWEB_CONFIG;
} else {
	our $GITWEB_CONFIG_SYSTEM = $ENV{'GITWEB_CONFIG_SYSTEM'} || "++GITWEB_CONFIG_SYSTEM++";
	do $GITWEB_CONFIG_SYSTEM if -e $GITWEB_CONFIG_SYSTEM;
}

# version of the core git binary
our $git_version = qx($GIT --version) =~ m/git version (.*)$/ ? $1 : "unknown";

$projects_list ||= $projectroot;

# ======================================================================
# input validation and dispatch
our $action = $cgi->param('a');
if (defined $action) {
	if ($action =~ m/[^0-9a-zA-Z\.\-_]/) {
		die_error(undef, "Invalid action parameter");
	}
}

# parameters which are pathnames
our $project = $cgi->param('p');
if (defined $project) {
	if (!validate_pathname($project) ||
	    !(-d "$projectroot/$project") ||
	    !check_head_link("$projectroot/$project") ||
	    ($export_ok && !(-e "$projectroot/$project/$export_ok")) ||
	    ($strict_export && !project_in_list($project))) {
		undef $project;
		die_error(undef, "No such project");
	}
}

our $file_name = $cgi->param('f');
if (defined $file_name) {
	if (!validate_pathname($file_name)) {
		die_error(undef, "Invalid file parameter");
	}
}

our $file_parent = $cgi->param('fp');
if (defined $file_parent) {
	if (!validate_pathname($file_parent)) {
		die_error(undef, "Invalid file parent parameter");
	}
}

# parameters which are refnames
our $hash = $cgi->param('h');
if (defined $hash) {
	if (!validate_refname($hash)) {
		die_error(undef, "Invalid hash parameter");
	}
}

our $hash_parent = $cgi->param('hp');
if (defined $hash_parent) {
	if (!validate_refname($hash_parent)) {
		die_error(undef, "Invalid hash parent parameter");
	}
}

our $hash_base = $cgi->param('hb');
if (defined $hash_base) {
	if (!validate_refname($hash_base)) {
		die_error(undef, "Invalid hash base parameter");
	}
}

my %allowed_options = (
	"--no-merges" => [ qw(rss atom log shortlog history) ],
);

our @extra_options = $cgi->param('opt');
if (defined @extra_options) {
	foreach my $opt (@extra_options) {
		if (not exists $allowed_options{$opt}) {
			die_error(undef, "Invalid option parameter");
		}
		if (not grep(/^$action$/, @{$allowed_options{$opt}})) {
			die_error(undef, "Invalid option parameter for this action");
		}
	}
}

our $hash_parent_base = $cgi->param('hpb');
if (defined $hash_parent_base) {
	if (!validate_refname($hash_parent_base)) {
		die_error(undef, "Invalid hash parent base parameter");
	}
}

# other parameters
our $page = $cgi->param('pg');
if (defined $page) {
	if ($page =~ m/[^0-9]/) {
		die_error(undef, "Invalid page parameter");
	}
}

our $searchtype = $cgi->param('st');
if (defined $searchtype) {
	if ($searchtype =~ m/[^a-z]/) {
		die_error(undef, "Invalid searchtype parameter");
	}
}

our $search_use_regexp = $cgi->param('sr');

our $searchtext = $cgi->param('s');
our $search_regexp;
if (defined $searchtext) {
	if (length($searchtext) < 2) {
		die_error(undef, "At least two characters are required for search parameter");
	}
	$search_regexp = $search_use_regexp ? $searchtext : quotemeta $searchtext;
}

# now read PATH_INFO and use it as alternative to parameters
sub evaluate_path_info {
	return if defined $project;
	my $path_info = $ENV{"PATH_INFO"};
	return if !$path_info;
	$path_info =~ s,^/+,,;
	return if !$path_info;
	# find which part of PATH_INFO is project
	$project = $path_info;
	$project =~ s,/+$,,;
	while ($project && !check_head_link("$projectroot/$project")) {
		$project =~ s,/*[^/]*$,,;
	}
	# validate project
	$project = validate_pathname($project);
	if (!$project ||
	    ($export_ok && !-e "$projectroot/$project/$export_ok") ||
	    ($strict_export && !project_in_list($project))) {
		undef $project;
		return;
	}
	# do not change any parameters if an action is given using the query string
	return if $action;
	$path_info =~ s,^$project/*,,;
	my ($refname, $pathname) = split(/:/, $path_info, 2);
	if (defined $pathname) {
		# we got "project.git/branch:filename" or "project.git/branch:dir/"
		# we could use git_get_type(branch:pathname), but it needs $git_dir
		$pathname =~ s,^/+,,;
		if (!$pathname || substr($pathname, -1) eq "/") {
			$action  ||= "tree";
			$pathname =~ s,/$,,;
		} else {
			$action  ||= "blob_plain";
		}
		$hash_base ||= validate_refname($refname);
		$file_name ||= validate_pathname($pathname);
	} elsif (defined $refname) {
		# we got "project.git/branch"
		$action ||= "shortlog";
		$hash   ||= validate_refname($refname);
	}
}
evaluate_path_info();

# path to the current git repository
our $git_dir;
$git_dir = "$projectroot/$project" if $project;

# dispatch
my %actions = (
	"blame" => \&git_blame2,
	"blobdiff" => \&git_blobdiff,
	"blobdiff_plain" => \&git_blobdiff_plain,
	"blob" => \&git_blob,
	"blob_plain" => \&git_blob_plain,
	"commitdiff" => \&git_commitdiff,
	"commitdiff_plain" => \&git_commitdiff_plain,
	"commit" => \&git_commit,
	"forks" => \&git_forks,
	"heads" => \&git_heads,
	"history" => \&git_history,
	"log" => \&git_log,
	"rss" => \&git_rss,
	"atom" => \&git_atom,
	"search" => \&git_search,
	"search_help" => \&git_search_help,
	"shortlog" => \&git_shortlog,
	"summary" => \&git_summary,
	"tag" => \&git_tag,
	"tags" => \&git_tags,
	"tree" => \&git_tree,
	"snapshot" => \&git_snapshot,
	"object" => \&git_object,
	# those below don't need $project
	"opml" => \&git_opml,
	"project_list" => \&git_project_list,
	"project_index" => \&git_project_index,
);

if (!defined $action) {
	if (defined $hash) {
		$action = git_get_type($hash);
	} elsif (defined $hash_base && defined $file_name) {
		$action = git_get_type("$hash_base:$file_name");
	} elsif (defined $project) {
		$action = 'summary';
	} else {
		$action = 'project_list';
	}
}
if (!defined($actions{$action})) {
	die_error(undef, "Unknown action");
}
if ($action !~ m/^(opml|project_list|project_index)$/ &&
    !$project) {
	die_error(undef, "Project needed");
}
$actions{$action}->();
exit;

## ======================================================================
## action links

sub href(%) {
	my %params = @_;
	# default is to use -absolute url() i.e. $my_uri
	my $href = $params{-full} ? $my_url : $my_uri;

	# XXX: Warning: If you touch this, check the search form for updating,
	# too.

	my @mapping = (
		project => "p",
		action => "a",
		file_name => "f",
		file_parent => "fp",
		hash => "h",
		hash_parent => "hp",
		hash_base => "hb",
		hash_parent_base => "hpb",
		page => "pg",
		order => "o",
		searchtext => "s",
		searchtype => "st",
		snapshot_format => "sf",
		extra_options => "opt",
		search_use_regexp => "sr",
	);
	my %mapping = @mapping;

	$params{'project'} = $project unless exists $params{'project'};

	if ($params{-replay}) {
		while (my ($name, $symbol) = each %mapping) {
			if (!exists $params{$name}) {
				# to allow for multivalued params we use arrayref form
				$params{$name} = [ $cgi->param($symbol) ];
			}
		}
	}

	my ($use_pathinfo) = gitweb_check_feature('pathinfo');
	if ($use_pathinfo) {
		# use PATH_INFO for project name
		$href .= "/$params{'project'}" if defined $params{'project'};
		delete $params{'project'};

		# Summary just uses the project path URL
		if (defined $params{'action'} && $params{'action'} eq 'summary') {
			delete $params{'action'};
		}
	}

	# now encode the parameters explicitly
	my @result = ();
	for (my $i = 0; $i < @mapping; $i += 2) {
		my ($name, $symbol) = ($mapping[$i], $mapping[$i+1]);
		if (defined $params{$name}) {
			if (ref($params{$name}) eq "ARRAY") {
				foreach my $par (@{$params{$name}}) {
					push @result, $symbol . "=" . esc_param($par);
				}
			} else {
				push @result, $symbol . "=" . esc_param($params{$name});
			}
		}
	}
	$href .= "?" . join(';', @result) if scalar @result;

	return $href;
}


## ======================================================================
## validation, quoting/unquoting and escaping

sub validate_pathname {
	my $input = shift || return undef;

	# no '.' or '..' as elements of path, i.e. no '.' nor '..'
	# at the beginning, at the end, and between slashes.
	# also this catches doubled slashes
	if ($input =~ m!(^|/)(|\.|\.\.)(/|$)!) {
		return undef;
	}
	# no null characters
	if ($input =~ m!\0!) {
		return undef;
	}
	return $input;
}

sub validate_refname {
	my $input = shift || return undef;

	# textual hashes are O.K.
	if ($input =~ m/^[0-9a-fA-F]{40}$/) {
		return $input;
	}
	# it must be correct pathname
	$input = validate_pathname($input)
		or return undef;
	# restrictions on ref name according to git-check-ref-format
	if ($input =~ m!(/\.|\.\.|[\000-\040\177 ~^:?*\[]|/$)!) {
		return undef;
	}
	return $input;
}

# decode sequences of octets in utf8 into Perl's internal form,
# which is utf-8 with utf8 flag set if needed.  gitweb writes out
# in utf-8 thanks to "binmode STDOUT, ':utf8'" at beginning
sub to_utf8 {
	my $str = shift;
	if (utf8::valid($str)) {
		utf8::decode($str);
		return $str;
	} else {
		return decode($fallback_encoding, $str, Encode::FB_DEFAULT);
	}
}

# quote unsafe chars, but keep the slash, even when it's not
# correct, but quoted slashes look too horrible in bookmarks
sub esc_param {
	my $str = shift;
	$str =~ s/([^A-Za-z0-9\-_.~()\/:@])/sprintf("%%%02X", ord($1))/eg;
	$str =~ s/\+/%2B/g;
	$str =~ s/ /\+/g;
	return $str;
}

# quote unsafe chars in whole URL, so some charactrs cannot be quoted
sub esc_url {
	my $str = shift;
	$str =~ s/([^A-Za-z0-9\-_.~();\/;?:@&=])/sprintf("%%%02X", ord($1))/eg;
	$str =~ s/\+/%2B/g;
	$str =~ s/ /\+/g;
	return $str;
}

# replace invalid utf8 character with SUBSTITUTION sequence
sub esc_html ($;%) {
	my $str = shift;
	my %opts = @_;

	$str = to_utf8($str);
	$str = $cgi->escapeHTML($str);
	if ($opts{'-nbsp'}) {
		$str =~ s/ /&nbsp;/g;
	}
	$str =~ s|([[:cntrl:]])|(($1 ne "\t") ? quot_cec($1) : $1)|eg;
	return $str;
}

# quote control characters and escape filename to HTML
sub esc_path {
	my $str = shift;
	my %opts = @_;

	$str = to_utf8($str);
	$str = $cgi->escapeHTML($str);
	if ($opts{'-nbsp'}) {
		$str =~ s/ /&nbsp;/g;
	}
	$str =~ s|([[:cntrl:]])|quot_cec($1)|eg;
	return $str;
}

# Make control characters "printable", using character escape codes (CEC)
sub quot_cec {
	my $cntrl = shift;
	my %opts = @_;
	my %es = ( # character escape codes, aka escape sequences
		"\t" => '\t',   # tab            (HT)
		"\n" => '\n',   # line feed      (LF)
		"\r" => '\r',   # carrige return (CR)
		"\f" => '\f',   # form feed      (FF)
		"\b" => '\b',   # backspace      (BS)
		"\a" => '\a',   # alarm (bell)   (BEL)
		"\e" => '\e',   # escape         (ESC)
		"\013" => '\v', # vertical tab   (VT)
		"\000" => '\0', # nul character  (NUL)
	);
	my $chr = ( (exists $es{$cntrl})
		    ? $es{$cntrl}
		    : sprintf('\%03o', ord($cntrl)) );
	if ($opts{-nohtml}) {
		return $chr;
	} else {
		return "<span class=\"cntrl\">$chr</span>";
	}
}

# Alternatively use unicode control pictures codepoints,
# Unicode "printable representation" (PR)
sub quot_upr {
	my $cntrl = shift;
	my %opts = @_;

	my $chr = sprintf('&#%04d;', 0x2400+ord($cntrl));
	if ($opts{-nohtml}) {
		return $chr;
	} else {
		return "<span class=\"cntrl\">$chr</span>";
	}
}

# git may return quoted and escaped filenames
sub unquote {
	my $str = shift;

	sub unq {
		my $seq = shift;
		my %es = ( # character escape codes, aka escape sequences
			't' => "\t",   # tab            (HT, TAB)
			'n' => "\n",   # newline        (NL)
			'r' => "\r",   # return         (CR)
			'f' => "\f",   # form feed      (FF)
			'b' => "\b",   # backspace      (BS)
			'a' => "\a",   # alarm (bell)   (BEL)
			'e' => "\e",   # escape         (ESC)
			'v' => "\013", # vertical tab   (VT)
		);

		if ($seq =~ m/^[0-7]{1,3}$/) {
			# octal char sequence
			return chr(oct($seq));
		} elsif (exists $es{$seq}) {
			# C escape sequence, aka character escape code
			return $es{$seq};
		}
		# quoted ordinary character
		return $seq;
	}

	if ($str =~ m/^"(.*)"$/) {
		# needs unquoting
		$str = $1;
		$str =~ s/\\([^0-7]|[0-7]{1,3})/unq($1)/eg;
	}
	return $str;
}

# escape tabs (convert tabs to spaces)
sub untabify {
	my $line = shift;

	while ((my $pos = index($line, "\t")) != -1) {
		if (my $count = (8 - ($pos % 8))) {
			my $spaces = ' ' x $count;
			$line =~ s/\t/$spaces/;
		}
	}

	return $line;
}

sub project_in_list {
	my $project = shift;
	my @list = git_get_projects_list();
	return @list && scalar(grep { $_->{'path'} eq $project } @list);
}

## ----------------------------------------------------------------------
## HTML aware string manipulation

# Try to chop given string on a word boundary between position
# $len and $len+$add_len. If there is no word boundary there,
# chop at $len+$add_len. Do not chop if chopped part plus ellipsis
# (marking chopped part) would be longer than given string.
sub chop_str {
	my $str = shift;
	my $len = shift;
	my $add_len = shift || 10;
	my $where = shift || 'right'; # 'left' | 'center' | 'right'

	# allow only $len chars, but don't cut a word if it would fit in $add_len
	# if it doesn't fit, cut it if it's still longer than the dots we would add
	# remove chopped character entities entirely

	# when chopping in the middle, distribute $len into left and right part
	# return early if chopping wouldn't make string shorter
	if ($where eq 'center') {
		return $str if ($len + 5 >= length($str)); # filler is length 5
		$len = int($len/2);
	} else {
		return $str if ($len + 4 >= length($str)); # filler is length 4
	}

	# regexps: ending and beginning with word part up to $add_len
	my $endre = qr/.{$len}\w{0,$add_len}/;
	my $begre = qr/\w{0,$add_len}.{$len}/;

	if ($where eq 'left') {
		$str =~ m/^(.*?)($begre)$/;
		my ($lead, $body) = ($1, $2);
		if (length($lead) > 4) {
			$body =~ s/^[^;]*;// if ($lead =~ m/&[^;]*$/);
			$lead = " ...";
		}
		return "$lead$body";

	} elsif ($where eq 'center') {
		$str =~ m/^($endre)(.*)$/;
		my ($left, $str)  = ($1, $2);
		$str =~ m/^(.*?)($begre)$/;
		my ($mid, $right) = ($1, $2);
		if (length($mid) > 5) {
			$left  =~ s/&[^;]*$//;
			$right =~ s/^[^;]*;// if ($mid =~ m/&[^;]*$/);
			$mid = " ... ";
		}
		return "$left$mid$right";

	} else {
		$str =~ m/^($endre)(.*)$/;
		my $body = $1;
		my $tail = $2;
		if (length($tail) > 4) {
			$body =~ s/&[^;]*$//;
			$tail = "... ";
		}
		return "$body$tail";
	}
}

# takes the same arguments as chop_str, but also wraps a <span> around the
# result with a title attribute if it does get chopped. Additionally, the
# string is HTML-escaped.
sub chop_and_escape_str {
	my ($str) = @_;

	my $chopped = chop_str(@_);
	if ($chopped eq $str) {
		return esc_html($chopped);
	} else {
		$str =~ s/([[:cntrl:]])/?/g;
		return $cgi->span({-title=>$str}, esc_html($chopped));
	}
}

## ----------------------------------------------------------------------
## functions returning short strings

# CSS class for given age value (in seconds)
sub age_class {
	my $age = shift;

	if (!defined $age) {
		return "noage";
	} elsif ($age < 60*60*2) {
		return "age0";
	} elsif ($age < 60*60*24*2) {
		return "age1";
	} else {
		return "age2";
	}
}

# convert age in seconds to "nn units ago" string
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

use constant {
	S_IFINVALID => 0030000,
	S_IFGITLINK => 0160000,
};

# submodule/subproject, a commit object reference
sub S_ISGITLINK($) {
	my $mode = shift;

	return (($mode & S_IFMT) == S_IFGITLINK)
}

# convert file mode in octal to symbolic file mode string
sub mode_str {
	my $mode = oct shift;

	if (S_ISGITLINK($mode)) {
		return 'm---------';
	} elsif (S_ISDIR($mode & S_IFMT)) {
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

# convert file mode in octal to file type string
sub file_type {
	my $mode = shift;

	if ($mode !~ m/^[0-7]+$/) {
		return $mode;
	} else {
		$mode = oct $mode;
	}

	if (S_ISGITLINK($mode)) {
		return "submodule";
	} elsif (S_ISDIR($mode & S_IFMT)) {
		return "directory";
	} elsif (S_ISLNK($mode)) {
		return "symlink";
	} elsif (S_ISREG($mode)) {
		return "file";
	} else {
		return "unknown";
	}
}

# convert file mode in octal to file type description string
sub file_type_long {
	my $mode = shift;

	if ($mode !~ m/^[0-7]+$/) {
		return $mode;
	} else {
		$mode = oct $mode;
	}

	if (S_ISGITLINK($mode)) {
		return "submodule";
	} elsif (S_ISDIR($mode & S_IFMT)) {
		return "directory";
	} elsif (S_ISLNK($mode)) {
		return "symlink";
	} elsif (S_ISREG($mode)) {
		if ($mode & S_IXUSR) {
			return "executable";
		} else {
			return "file";
		};
	} else {
		return "unknown";
	}
}


## ----------------------------------------------------------------------
## functions returning short HTML fragments, or transforming HTML fragments
## which don't belong to other sections

# format line of commit message.
sub format_log_line_html {
	my $line = shift;

	$line = esc_html($line, -nbsp=>1);
	if ($line =~ m/([0-9a-fA-F]{8,40})/) {
		my $hash_text = $1;
		my $link =
			$cgi->a({-href => href(action=>"object", hash=>$hash_text),
			        -class => "text"}, $hash_text);
		$line =~ s/$hash_text/$link/;
	}
	return $line;
}

# format marker of refs pointing to given object
sub format_ref_marker {
	my ($refs, $id) = @_;
	my $markers = '';

	if (defined $refs->{$id}) {
		foreach my $ref (@{$refs->{$id}}) {
			my ($type, $name) = qw();
			# e.g. tags/v2.6.11 or heads/next
			if ($ref =~ m!^(.*?)s?/(.*)$!) {
				$type = $1;
				$name = $2;
			} else {
				$type = "ref";
				$name = $ref;
			}

			$markers .= " <span class=\"$type\" title=\"$ref\">" .
			            esc_html($name) . "</span>";
		}
	}

	if ($markers) {
		return ' <span class="refs">'. $markers . '</span>';
	} else {
		return "";
	}
}

# format, perhaps shortened and with markers, title line
sub format_subject_html {
	my ($long, $short, $href, $extra) = @_;
	$extra = '' unless defined($extra);

	if (length($short) < length($long)) {
		return $cgi->a({-href => $href, -class => "list subject",
		                -title => to_utf8($long)},
		       esc_html($short) . $extra);
	} else {
		return $cgi->a({-href => $href, -class => "list subject"},
		       esc_html($long)  . $extra);
	}
}

# format git diff header line, i.e. "diff --(git|combined|cc) ..."
sub format_git_diff_header_line {
	my $line = shift;
	my $diffinfo = shift;
	my ($from, $to) = @_;

	if ($diffinfo->{'nparents'}) {
		# combined diff
		$line =~ s!^(diff (.*?) )"?.*$!$1!;
		if ($to->{'href'}) {
			$line .= $cgi->a({-href => $to->{'href'}, -class => "path"},
			                 esc_path($to->{'file'}));
		} else { # file was deleted (no href)
			$line .= esc_path($to->{'file'});
		}
	} else {
		# "ordinary" diff
		$line =~ s!^(diff (.*?) )"?a/.*$!$1!;
		if ($from->{'href'}) {
			$line .= $cgi->a({-href => $from->{'href'}, -class => "path"},
			                 'a/' . esc_path($from->{'file'}));
		} else { # file was added (no href)
			$line .= 'a/' . esc_path($from->{'file'});
		}
		$line .= ' ';
		if ($to->{'href'}) {
			$line .= $cgi->a({-href => $to->{'href'}, -class => "path"},
			                 'b/' . esc_path($to->{'file'}));
		} else { # file was deleted
			$line .= 'b/' . esc_path($to->{'file'});
		}
	}

	return "<div class=\"diff header\">$line</div>\n";
}

# format extended diff header line, before patch itself
sub format_extended_diff_header_line {
	my $line = shift;
	my $diffinfo = shift;
	my ($from, $to) = @_;

	# match <path>
	if ($line =~ s!^((copy|rename) from ).*$!$1! && $from->{'href'}) {
		$line .= $cgi->a({-href=>$from->{'href'}, -class=>"path"},
		                       esc_path($from->{'file'}));
	}
	if ($line =~ s!^((copy|rename) to ).*$!$1! && $to->{'href'}) {
		$line .= $cgi->a({-href=>$to->{'href'}, -class=>"path"},
		                 esc_path($to->{'file'}));
	}
	# match single <mode>
	if ($line =~ m/\s(\d{6})$/) {
		$line .= '<span class="info"> (' .
		         file_type_long($1) .
		         ')</span>';
	}
	# match <hash>
	if ($line =~ m/^index [0-9a-fA-F]{40},[0-9a-fA-F]{40}/) {
		# can match only for combined diff
		$line = 'index ';
		for (my $i = 0; $i < $diffinfo->{'nparents'}; $i++) {
			if ($from->{'href'}[$i]) {
				$line .= $cgi->a({-href=>$from->{'href'}[$i],
				                  -class=>"hash"},
				                 substr($diffinfo->{'from_id'}[$i],0,7));
			} else {
				$line .= '0' x 7;
			}
			# separator
			$line .= ',' if ($i < $diffinfo->{'nparents'} - 1);
		}
		$line .= '..';
		if ($to->{'href'}) {
			$line .= $cgi->a({-href=>$to->{'href'}, -class=>"hash"},
			                 substr($diffinfo->{'to_id'},0,7));
		} else {
			$line .= '0' x 7;
		}

	} elsif ($line =~ m/^index [0-9a-fA-F]{40}..[0-9a-fA-F]{40}/) {
		# can match only for ordinary diff
		my ($from_link, $to_link);
		if ($from->{'href'}) {
			$from_link = $cgi->a({-href=>$from->{'href'}, -class=>"hash"},
			                     substr($diffinfo->{'from_id'},0,7));
		} else {
			$from_link = '0' x 7;
		}
		if ($to->{'href'}) {
			$to_link = $cgi->a({-href=>$to->{'href'}, -class=>"hash"},
			                   substr($diffinfo->{'to_id'},0,7));
		} else {
			$to_link = '0' x 7;
		}
		my ($from_id, $to_id) = ($diffinfo->{'from_id'}, $diffinfo->{'to_id'});
		$line =~ s!$from_id\.\.$to_id!$from_link..$to_link!;
	}

	return $line . "<br/>\n";
}

# format from-file/to-file diff header
sub format_diff_from_to_header {
	my ($from_line, $to_line, $diffinfo, $from, $to, @parents) = @_;
	my $line;
	my $result = '';

	$line = $from_line;
	#assert($line =~ m/^---/) if DEBUG;
	# no extra formatting for "^--- /dev/null"
	if (! $diffinfo->{'nparents'}) {
		# ordinary (single parent) diff
		if ($line =~ m!^--- "?a/!) {
			if ($from->{'href'}) {
				$line = '--- a/' .
				        $cgi->a({-href=>$from->{'href'}, -class=>"path"},
				                esc_path($from->{'file'}));
			} else {
				$line = '--- a/' .
				        esc_path($from->{'file'});
			}
		}
		$result .= qq!<div class="diff from_file">$line</div>\n!;

	} else {
		# combined diff (merge commit)
		for (my $i = 0; $i < $diffinfo->{'nparents'}; $i++) {
			if ($from->{'href'}[$i]) {
				$line = '--- ' .
				        $cgi->a({-href=>href(action=>"blobdiff",
				                             hash_parent=>$diffinfo->{'from_id'}[$i],
				                             hash_parent_base=>$parents[$i],
				                             file_parent=>$from->{'file'}[$i],
				                             hash=>$diffinfo->{'to_id'},
				                             hash_base=>$hash,
				                             file_name=>$to->{'file'}),
				                 -class=>"path",
				                 -title=>"diff" . ($i+1)},
				                $i+1) .
				        '/' .
				        $cgi->a({-href=>$from->{'href'}[$i], -class=>"path"},
				                esc_path($from->{'file'}[$i]));
			} else {
				$line = '--- /dev/null';
			}
			$result .= qq!<div class="diff from_file">$line</div>\n!;
		}
	}

	$line = $to_line;
	#assert($line =~ m/^\+\+\+/) if DEBUG;
	# no extra formatting for "^+++ /dev/null"
	if ($line =~ m!^\+\+\+ "?b/!) {
		if ($to->{'href'}) {
			$line = '+++ b/' .
			        $cgi->a({-href=>$to->{'href'}, -class=>"path"},
			                esc_path($to->{'file'}));
		} else {
			$line = '+++ b/' .
			        esc_path($to->{'file'});
		}
	}
	$result .= qq!<div class="diff to_file">$line</div>\n!;

	return $result;
}

# create note for patch simplified by combined diff
sub format_diff_cc_simplified {
	my ($diffinfo, @parents) = @_;
	my $result = '';

	$result .= "<div class=\"diff header\">" .
	           "diff --cc ";
	if (!is_deleted($diffinfo)) {
		$result .= $cgi->a({-href => href(action=>"blob",
		                                  hash_base=>$hash,
		                                  hash=>$diffinfo->{'to_id'},
		                                  file_name=>$diffinfo->{'to_file'}),
		                    -class => "path"},
		                   esc_path($diffinfo->{'to_file'}));
	} else {
		$result .= esc_path($diffinfo->{'to_file'});
	}
	$result .= "</div>\n" . # class="diff header"
	           "<div class=\"diff nodifferences\">" .
	           "Simple merge" .
	           "</div>\n"; # class="diff nodifferences"

	return $result;
}

# format patch (diff) line (not to be used for diff headers)
sub format_diff_line {
	my $line = shift;
	my ($from, $to) = @_;
	my $diff_class = "";

	chomp $line;

	if ($from && $to && ref($from->{'href'}) eq "ARRAY") {
		# combined diff
		my $prefix = substr($line, 0, scalar @{$from->{'href'}});
		if ($line =~ m/^\@{3}/) {
			$diff_class = " chunk_header";
		} elsif ($line =~ m/^\\/) {
			$diff_class = " incomplete";
		} elsif ($prefix =~ tr/+/+/) {
			$diff_class = " add";
		} elsif ($prefix =~ tr/-/-/) {
			$diff_class = " rem";
		}
	} else {
		# assume ordinary diff
		my $char = substr($line, 0, 1);
		if ($char eq '+') {
			$diff_class = " add";
		} elsif ($char eq '-') {
			$diff_class = " rem";
		} elsif ($char eq '@') {
			$diff_class = " chunk_header";
		} elsif ($char eq "\\") {
			$diff_class = " incomplete";
		}
	}
	$line = untabify($line);
	if ($from && $to && $line =~ m/^\@{2} /) {
		my ($from_text, $from_start, $from_lines, $to_text, $to_start, $to_lines, $section) =
			$line =~ m/^\@{2} (-(\d+)(?:,(\d+))?) (\+(\d+)(?:,(\d+))?) \@{2}(.*)$/;

		$from_lines = 0 unless defined $from_lines;
		$to_lines   = 0 unless defined $to_lines;

		if ($from->{'href'}) {
			$from_text = $cgi->a({-href=>"$from->{'href'}#l$from_start",
			                     -class=>"list"}, $from_text);
		}
		if ($to->{'href'}) {
			$to_text   = $cgi->a({-href=>"$to->{'href'}#l$to_start",
			                     -class=>"list"}, $to_text);
		}
		$line = "<span class=\"chunk_info\">@@ $from_text $to_text @@</span>" .
		        "<span class=\"section\">" . esc_html($section, -nbsp=>1) . "</span>";
		return "<div class=\"diff$diff_class\">$line</div>\n";
	} elsif ($from && $to && $line =~ m/^\@{3}/) {
		my ($prefix, $ranges, $section) = $line =~ m/^(\@+) (.*?) \@+(.*)$/;
		my (@from_text, @from_start, @from_nlines, $to_text, $to_start, $to_nlines);

		@from_text = split(' ', $ranges);
		for (my $i = 0; $i < @from_text; ++$i) {
			($from_start[$i], $from_nlines[$i]) =
				(split(',', substr($from_text[$i], 1)), 0);
		}

		$to_text   = pop @from_text;
		$to_start  = pop @from_start;
		$to_nlines = pop @from_nlines;

		$line = "<span class=\"chunk_info\">$prefix ";
		for (my $i = 0; $i < @from_text; ++$i) {
			if ($from->{'href'}[$i]) {
				$line .= $cgi->a({-href=>"$from->{'href'}[$i]#l$from_start[$i]",
				                  -class=>"list"}, $from_text[$i]);
			} else {
				$line .= $from_text[$i];
			}
			$line .= " ";
		}
		if ($to->{'href'}) {
			$line .= $cgi->a({-href=>"$to->{'href'}#l$to_start",
			                  -class=>"list"}, $to_text);
		} else {
			$line .= $to_text;
		}
		$line .= " $prefix</span>" .
		         "<span class=\"section\">" . esc_html($section, -nbsp=>1) . "</span>";
		return "<div class=\"diff$diff_class\">$line</div>\n";
	}
	return "<div class=\"diff$diff_class\">" . esc_html($line, -nbsp=>1) . "</div>\n";
}

# Generates undef or something like "_snapshot_" or "snapshot (_tbz2_ _zip_)",
# linked.  Pass the hash of the tree/commit to snapshot.
sub format_snapshot_links {
	my ($hash) = @_;
	my @snapshot_fmts = gitweb_check_feature('snapshot');
	@snapshot_fmts = filter_snapshot_fmts(@snapshot_fmts);
	my $num_fmts = @snapshot_fmts;
	if ($num_fmts > 1) {
		# A parenthesized list of links bearing format names.
		# e.g. "snapshot (_tar.gz_ _zip_)"
		return "snapshot (" . join(' ', map
			$cgi->a({
				-href => href(
					action=>"snapshot",
					hash=>$hash,
					snapshot_format=>$_
				)
			}, $known_snapshot_formats{$_}{'display'})
		, @snapshot_fmts) . ")";
	} elsif ($num_fmts == 1) {
		# A single "snapshot" link whose tooltip bears the format name.
		# i.e. "_snapshot_"
		my ($fmt) = @snapshot_fmts;
		return
			$cgi->a({
				-href => href(
					action=>"snapshot",
					hash=>$hash,
					snapshot_format=>$fmt
				),
				-title => "in format: $known_snapshot_formats{$fmt}{'display'}"
			}, "snapshot");
	} else { # $num_fmts == 0
		return undef;
	}
}

## ----------------------------------------------------------------------
## git utility subroutines, invoking git commands

# returns path to the core git executable and the --git-dir parameter as list
sub git_cmd {
	return $GIT, '--git-dir='.$git_dir;
}

# returns path to the core git executable and the --git-dir parameter as string
sub git_cmd_str {
	return join(' ', git_cmd());
}

# get HEAD ref of given project as hash
sub git_get_head_hash {
	my $project = shift;
	my $o_git_dir = $git_dir;
	my $retval = undef;
	$git_dir = "$projectroot/$project";
	if (open my $fd, "-|", git_cmd(), "rev-parse", "--verify", "HEAD") {
		my $head = <$fd>;
		close $fd;
		if (defined $head && $head =~ /^([0-9a-fA-F]{40})$/) {
			$retval = $1;
		}
	}
	if (defined $o_git_dir) {
		$git_dir = $o_git_dir;
	}
	return $retval;
}

# get type of given object
sub git_get_type {
	my $hash = shift;

	open my $fd, "-|", git_cmd(), "cat-file", '-t', $hash or return;
	my $type = <$fd>;
	close $fd or return;
	chomp $type;
	return $type;
}

# repository configuration
our $config_file = '';
our %config;

# store multiple values for single key as anonymous array reference
# single values stored directly in the hash, not as [ <value> ]
sub hash_set_multi {
	my ($hash, $key, $value) = @_;

	if (!exists $hash->{$key}) {
		$hash->{$key} = $value;
	} elsif (!ref $hash->{$key}) {
		$hash->{$key} = [ $hash->{$key}, $value ];
	} else {
		push @{$hash->{$key}}, $value;
	}
}

# return hash of git project configuration
# optionally limited to some section, e.g. 'gitweb'
sub git_parse_project_config {
	my $section_regexp = shift;
	my %config;

	local $/ = "\0";

	open my $fh, "-|", git_cmd(), "config", '-z', '-l',
		or return;

	while (my $keyval = <$fh>) {
		chomp $keyval;
		my ($key, $value) = split(/\n/, $keyval, 2);

		hash_set_multi(\%config, $key, $value)
			if (!defined $section_regexp || $key =~ /^(?:$section_regexp)\./o);
	}
	close $fh;

	return %config;
}

# convert config value to boolean, 'true' or 'false'
# no value, number > 0, 'true' and 'yes' values are true
# rest of values are treated as false (never as error)
sub config_to_bool {
	my $val = shift;

	# strip leading and trailing whitespace
	$val =~ s/^\s+//;
	$val =~ s/\s+$//;

	return (!defined $val ||               # section.key
	        ($val =~ /^\d+$/ && $val) ||   # section.key = 1
	        ($val =~ /^(?:true|yes)$/i));  # section.key = true
}

# convert config value to simple decimal number
# an optional value suffix of 'k', 'm', or 'g' will cause the value
# to be multiplied by 1024, 1048576, or 1073741824
sub config_to_int {
	my $val = shift;

	# strip leading and trailing whitespace
	$val =~ s/^\s+//;
	$val =~ s/\s+$//;

	if (my ($num, $unit) = ($val =~ /^([0-9]*)([kmg])$/i)) {
		$unit = lc($unit);
		# unknown unit is treated as 1
		return $num * ($unit eq 'g' ? 1073741824 :
		               $unit eq 'm' ?    1048576 :
		               $unit eq 'k' ?       1024 : 1);
	}
	return $val;
}

# convert config value to array reference, if needed
sub config_to_multi {
	my $val = shift;

	return ref($val) ? $val : (defined($val) ? [ $val ] : []);
}

sub git_get_project_config {
	my ($key, $type) = @_;

	# key sanity check
	return unless ($key);
	$key =~ s/^gitweb\.//;
	return if ($key =~ m/\W/);

	# type sanity check
	if (defined $type) {
		$type =~ s/^--//;
		$type = undef
			unless ($type eq 'bool' || $type eq 'int');
	}

	# get config
	if (!defined $config_file ||
	    $config_file ne "$git_dir/config") {
		%config = git_parse_project_config('gitweb');
		$config_file = "$git_dir/config";
	}

	# ensure given type
	if (!defined $type) {
		return $config{"gitweb.$key"};
	} elsif ($type eq 'bool') {
		# backward compatibility: 'git config --bool' returns true/false
		return config_to_bool($config{"gitweb.$key"}) ? 'true' : 'false';
	} elsif ($type eq 'int') {
		return config_to_int($config{"gitweb.$key"});
	}
	return $config{"gitweb.$key"};
}

# get hash of given path at given ref
sub git_get_hash_by_path {
	my $base = shift;
	my $path = shift || return undef;
	my $type = shift;

	$path =~ s,/+$,,;

	open my $fd, "-|", git_cmd(), "ls-tree", $base, "--", $path
		or die_error(undef, "Open git-ls-tree failed");
	my $line = <$fd>;
	close $fd or return undef;

	if (!defined $line) {
		# there is no tree or hash given by $path at $base
		return undef;
	}

	#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
	$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t/;
	if (defined $type && $type ne $2) {
		# type doesn't match
		return undef;
	}
	return $3;
}

# get path of entry with given hash at given tree-ish (ref)
# used to get 'from' filename for combined diff (merge commit) for renames
sub git_get_path_by_hash {
	my $base = shift || return;
	my $hash = shift || return;

	local $/ = "\0";

	open my $fd, "-|", git_cmd(), "ls-tree", '-r', '-t', '-z', $base
		or return undef;
	while (my $line = <$fd>) {
		chomp $line;

		#'040000 tree 595596a6a9117ddba9fe379b6b012b558bac8423	gitweb'
		#'100644 blob e02e90f0429be0d2a69b76571101f20b8f75530f	gitweb/README'
		if ($line =~ m/(?:[0-9]+) (?:.+) $hash\t(.+)$/) {
			close $fd;
			return $1;
		}
	}
	close $fd;
	return undef;
}

## ......................................................................
## git utility functions, directly accessing git repository

sub git_get_project_description {
	my $path = shift;

	$git_dir = "$projectroot/$path";
	open my $fd, "$git_dir/description"
		or return git_get_project_config('description');
	my $descr = <$fd>;
	close $fd;
	if (defined $descr) {
		chomp $descr;
	}
	return $descr;
}

sub git_get_project_url_list {
	my $path = shift;

	$git_dir = "$projectroot/$path";
	open my $fd, "$git_dir/cloneurl"
		or return wantarray ?
		@{ config_to_multi(git_get_project_config('url')) } :
		   config_to_multi(git_get_project_config('url'));
	my @git_project_url_list = map { chomp; $_ } <$fd>;
	close $fd;

	return wantarray ? @git_project_url_list : \@git_project_url_list;
}

sub git_get_projects_list {
	my ($filter) = @_;
	my @list;

	$filter ||= '';
	$filter =~ s/\.git$//;

	my ($check_forks) = gitweb_check_feature('forks');

	if (-d $projects_list) {
		# search in directory
		my $dir = $projects_list . ($filter ? "/$filter" : '');
		# remove the trailing "/"
		$dir =~ s!/+$!!;
		my $pfxlen = length("$dir");
		my $pfxdepth = ($dir =~ tr!/!!);

		File::Find::find({
			follow_fast => 1, # follow symbolic links
			follow_skip => 2, # ignore duplicates
			dangling_symlinks => 0, # ignore dangling symlinks, silently
			wanted => sub {
				# skip project-list toplevel, if we get it.
				return if (m!^[/.]$!);
				# only directories can be git repositories
				return unless (-d $_);
				# don't traverse too deep (Find is super slow on os x)
				if (($File::Find::name =~ tr!/!!) - $pfxdepth > $project_maxdepth) {
					$File::Find::prune = 1;
					return;
				}

				my $subdir = substr($File::Find::name, $pfxlen + 1);
				# we check related file in $projectroot
				if ($check_forks and $subdir =~ m#/.#) {
					$File::Find::prune = 1;
				} elsif (check_export_ok("$projectroot/$filter/$subdir")) {
					push @list, { path => ($filter ? "$filter/" : '') . $subdir };
					$File::Find::prune = 1;
				}
			},
		}, "$dir");

	} elsif (-f $projects_list) {
		# read from file(url-encoded):
		# 'git%2Fgit.git Linus+Torvalds'
		# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
		# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
		my %paths;
		open my ($fd), $projects_list or return;
	PROJECT:
		while (my $line = <$fd>) {
			chomp $line;
			my ($path, $owner) = split ' ', $line;
			$path = unescape($path);
			$owner = unescape($owner);
			if (!defined $path) {
				next;
			}
			if ($filter ne '') {
				# looking for forks;
				my $pfx = substr($path, 0, length($filter));
				if ($pfx ne $filter) {
					next PROJECT;
				}
				my $sfx = substr($path, length($filter));
				if ($sfx !~ /^\/.*\.git$/) {
					next PROJECT;
				}
			} elsif ($check_forks) {
			PATH:
				foreach my $filter (keys %paths) {
					# looking for forks;
					my $pfx = substr($path, 0, length($filter));
					if ($pfx ne $filter) {
						next PATH;
					}
					my $sfx = substr($path, length($filter));
					if ($sfx !~ /^\/.*\.git$/) {
						next PATH;
					}
					# is a fork, don't include it in
					# the list
					next PROJECT;
				}
			}
			if (check_export_ok("$projectroot/$path")) {
				my $pr = {
					path => $path,
					owner => to_utf8($owner),
				};
				push @list, $pr;
				(my $forks_path = $path) =~ s/\.git$//;
				$paths{$forks_path}++;
			}
		}
		close $fd;
	}
	return @list;
}

our $gitweb_project_owner = undef;
sub git_get_project_list_from_file {

	return if (defined $gitweb_project_owner);

	$gitweb_project_owner = {};
	# read from file (url-encoded):
	# 'git%2Fgit.git Linus+Torvalds'
	# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
	# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
	if (-f $projects_list) {
		open (my $fd , $projects_list);
		while (my $line = <$fd>) {
			chomp $line;
			my ($pr, $ow) = split ' ', $line;
			$pr = unescape($pr);
			$ow = unescape($ow);
			$gitweb_project_owner->{$pr} = to_utf8($ow);
		}
		close $fd;
	}
}

sub git_get_project_owner {
	my $project = shift;
	my $owner;

	return undef unless $project;
	$git_dir = "$projectroot/$project";

	if (!defined $gitweb_project_owner) {
		git_get_project_list_from_file();
	}

	if (exists $gitweb_project_owner->{$project}) {
		$owner = $gitweb_project_owner->{$project};
	}
	if (!defined $owner){
		$owner = git_get_project_config('owner');
	}
	if (!defined $owner) {
		$owner = get_file_owner("$git_dir");
	}

	return $owner;
}

sub git_get_last_activity {
	my ($path) = @_;
	my $fd;

	$git_dir = "$projectroot/$path";
	open($fd, "-|", git_cmd(), 'for-each-ref',
	     '--format=%(committer)',
	     '--sort=-committerdate',
	     '--count=1',
	     'refs/heads') or return;
	my $most_recent = <$fd>;
	close $fd or return;
	if (defined $most_recent &&
	    $most_recent =~ / (\d+) [-+][01]\d\d\d$/) {
		my $timestamp = $1;
		my $age = time - $timestamp;
		return ($age, age_string($age));
	}
	return (undef, undef);
}

sub git_get_references {
	my $type = shift || "";
	my %refs;
	# 5dc01c595e6c6ec9ccda4f6f69c131c0dd945f8c refs/tags/v2.6.11
	# c39ae07f393806ccf406ef966e9a15afc43cc36a refs/tags/v2.6.11^{}
	open my $fd, "-|", git_cmd(), "show-ref", "--dereference",
		($type ? ("--", "refs/$type") : ()) # use -- <pattern> if $type
		or return;

	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m!^([0-9a-fA-F]{40})\srefs/($type/?[^^]+)!) {
			if (defined $refs{$1}) {
				push @{$refs{$1}}, $2;
			} else {
				$refs{$1} = [ $2 ];
			}
		}
	}
	close $fd or return;
	return \%refs;
}

sub git_get_rev_name_tags {
	my $hash = shift || return undef;

	open my $fd, "-|", git_cmd(), "name-rev", "--tags", $hash
		or return;
	my $name_rev = <$fd>;
	close $fd;

	if ($name_rev =~ m|^$hash tags/(.*)$|) {
		return $1;
	} else {
		# catches also '$hash undefined' output
		return undef;
	}
}

## ----------------------------------------------------------------------
## parse to hash functions

sub parse_date {
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
	$date{'rfc2822'}   = sprintf "%s, %d %s %4d %02d:%02d:%02d +0000",
	                     $days[$wday], $mday, $months[$mon], 1900+$year, $hour ,$min, $sec;
	$date{'mday-time'} = sprintf "%d %s %02d:%02d",
	                     $mday, $months[$mon], $hour ,$min;
	$date{'iso-8601'}  = sprintf "%04d-%02d-%02dT%02d:%02d:%02dZ",
	                     1900+$year, 1+$mon, $mday, $hour ,$min, $sec;

	$tz =~ m/^([+\-][0-9][0-9])([0-9][0-9])$/;
	my $local = $epoch + ((int $1 + ($2/60)) * 3600);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	$date{'iso-tz'} = sprintf("%04d-%02d-%02d %02d:%02d:%02d %s",
	                          1900+$year, $mon+1, $mday,
	                          $hour, $min, $sec, $tz);
	return %date;
}

sub parse_tag {
	my $tag_id = shift;
	my %tag;
	my @comment;

	open my $fd, "-|", git_cmd(), "cat-file", "tag", $tag_id or return;
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

sub parse_commit_text {
	my ($commit_text, $withparents) = @_;
	my @commit_lines = split '\n', $commit_text;
	my %co;

	pop @commit_lines; # Remove '\0'

	if (! @commit_lines) {
		return;
	}

	my $header = shift @commit_lines;
	if ($header !~ m/^[0-9a-fA-F]{40}/) {
		return;
	}
	($co{'id'}, my @parents) = split ' ', $header;
	while (my $line = shift @commit_lines) {
		last if $line eq "\n";
		if ($line =~ m/^tree ([0-9a-fA-F]{40})$/) {
			$co{'tree'} = $1;
		} elsif ((!defined $withparents) && ($line =~ m/^parent ([0-9a-fA-F]{40})$/)) {
			push @parents, $1;
		} elsif ($line =~ m/^author (.*) ([0-9]+) (.*)$/) {
			$co{'author'} = $1;
			$co{'author_epoch'} = $2;
			$co{'author_tz'} = $3;
			if ($co{'author'} =~ m/^([^<]+) <([^>]*)>/) {
				$co{'author_name'}  = $1;
				$co{'author_email'} = $2;
			} else {
				$co{'author_name'} = $co{'author'};
			}
		} elsif ($line =~ m/^committer (.*) ([0-9]+) (.*)$/) {
			$co{'committer'} = $1;
			$co{'committer_epoch'} = $2;
			$co{'committer_tz'} = $3;
			$co{'committer_name'} = $co{'committer'};
			if ($co{'committer'} =~ m/^([^<]+) <([^>]*)>/) {
				$co{'committer_name'}  = $1;
				$co{'committer_email'} = $2;
			} else {
				$co{'committer_name'} = $co{'committer'};
			}
		}
	}
	if (!defined $co{'tree'}) {
		return;
	};
	$co{'parents'} = \@parents;
	$co{'parent'} = $parents[0];

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
	if ($co{'title'} eq "") {
		$co{'title'} = $co{'title_short'} = '(no commit message)';
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

sub parse_commit {
	my ($commit_id) = @_;
	my %co;

	local $/ = "\0";

	open my $fd, "-|", git_cmd(), "rev-list",
		"--parents",
		"--header",
		"--max-count=1",
		$commit_id,
		"--",
		or die_error(undef, "Open git-rev-list failed");
	%co = parse_commit_text(<$fd>, 1);
	close $fd;

	return %co;
}

sub parse_commits {
	my ($commit_id, $maxcount, $skip, $filename, @args) = @_;
	my @cos;

	$maxcount ||= 1;
	$skip ||= 0;

	local $/ = "\0";

	open my $fd, "-|", git_cmd(), "rev-list",
		"--header",
		@args,
		("--max-count=" . $maxcount),
		("--skip=" . $skip),
		@extra_options,
		$commit_id,
		"--",
		($filename ? ($filename) : ())
		or die_error(undef, "Open git-rev-list failed");
	while (my $line = <$fd>) {
		my %co = parse_commit_text($line);
		push @cos, \%co;
	}
	close $fd;

	return wantarray ? @cos : \@cos;
}

# parse ref from ref_file, given by ref_id, with given type
sub parse_ref {
	my $ref_file = shift;
	my $ref_id = shift;
	my $type = shift || git_get_type($ref_id);
	my %ref_item;

	$ref_item{'type'} = $type;
	$ref_item{'id'} = $ref_id;
	$ref_item{'epoch'} = 0;
	$ref_item{'age'} = "unknown";
	if ($type eq "tag") {
		my %tag = parse_tag($ref_id);
		$ref_item{'comment'} = $tag{'comment'};
		if ($tag{'type'} eq "commit") {
			my %co = parse_commit($tag{'object'});
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
		my %co = parse_commit($ref_id);
		$ref_item{'reftype'} = "commit";
		$ref_item{'name'} = $ref_file;
		$ref_item{'title'} = $co{'title'};
		$ref_item{'refid'} = $ref_id;
		$ref_item{'epoch'} = $co{'committer_epoch'};
		$ref_item{'age'} = $co{'age_string'};
	} else {
		$ref_item{'reftype'} = $type;
		$ref_item{'name'} = $ref_file;
		$ref_item{'refid'} = $ref_id;
	}

	return %ref_item;
}

# parse line of git-diff-tree "raw" output
sub parse_difftree_raw_line {
	my $line = shift;
	my %res;

	# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M	ls-files.c'
	# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M	rev-tree.c'
	if ($line =~ m/^:([0-7]{6}) ([0-7]{6}) ([0-9a-fA-F]{40}) ([0-9a-fA-F]{40}) (.)([0-9]{0,3})\t(.*)$/) {
		$res{'from_mode'} = $1;
		$res{'to_mode'} = $2;
		$res{'from_id'} = $3;
		$res{'to_id'} = $4;
		$res{'status'} = $res{'status_str'} = $5;
		$res{'similarity'} = $6;
		if ($res{'status'} eq 'R' || $res{'status'} eq 'C') { # renamed or copied
			($res{'from_file'}, $res{'to_file'}) = map { unquote($_) } split("\t", $7);
		} else {
			$res{'from_file'} = $res{'to_file'} = $res{'file'} = unquote($7);
		}
	}
	# '::100755 100755 100755 60e79ca1b01bc8b057abe17ddab484699a7f5fdb 94067cc5f73388f33722d52ae02f44692bc07490 94067cc5f73388f33722d52ae02f44692bc07490 MR	git-gui/git-gui.sh'
	# combined diff (for merge commit)
	elsif ($line =~ s/^(::+)((?:[0-7]{6} )+)((?:[0-9a-fA-F]{40} )+)([a-zA-Z]+)\t(.*)$//) {
		$res{'nparents'}  = length($1);
		$res{'from_mode'} = [ split(' ', $2) ];
		$res{'to_mode'} = pop @{$res{'from_mode'}};
		$res{'from_id'} = [ split(' ', $3) ];
		$res{'to_id'} = pop @{$res{'from_id'}};
		$res{'status_str'} = $4;
		$res{'status'} = [ split('', $4) ];
		$res{'to_file'} = unquote($5);
	}
	# 'c512b523472485aef4fff9e57b229d9d243c967f'
	elsif ($line =~ m/^([0-9a-fA-F]{40})$/) {
		$res{'commit'} = $1;
	}

	return wantarray ? %res : \%res;
}

# wrapper: return parsed line of git-diff-tree "raw" output
# (the argument might be raw line, or parsed info)
sub parsed_difftree_line {
	my $line_or_ref = shift;

	if (ref($line_or_ref) eq "HASH") {
		# pre-parsed (or generated by hand)
		return $line_or_ref;
	} else {
		return parse_difftree_raw_line($line_or_ref);
	}
}

# parse line of git-ls-tree output
sub parse_ls_tree_line ($;%) {
	my $line = shift;
	my %opts = @_;
	my %res;

	#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
	$line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t(.+)$/s;

	$res{'mode'} = $1;
	$res{'type'} = $2;
	$res{'hash'} = $3;
	if ($opts{'-z'}) {
		$res{'name'} = $4;
	} else {
		$res{'name'} = unquote($4);
	}

	return wantarray ? %res : \%res;
}

# generates _two_ hashes, references to which are passed as 2 and 3 argument
sub parse_from_to_diffinfo {
	my ($diffinfo, $from, $to, @parents) = @_;

	if ($diffinfo->{'nparents'}) {
		# combined diff
		$from->{'file'} = [];
		$from->{'href'} = [];
		fill_from_file_info($diffinfo, @parents)
			unless exists $diffinfo->{'from_file'};
		for (my $i = 0; $i < $diffinfo->{'nparents'}; $i++) {
			$from->{'file'}[$i] =
				defined $diffinfo->{'from_file'}[$i] ?
				        $diffinfo->{'from_file'}[$i] :
				        $diffinfo->{'to_file'};
			if ($diffinfo->{'status'}[$i] ne "A") { # not new (added) file
				$from->{'href'}[$i] = href(action=>"blob",
				                           hash_base=>$parents[$i],
				                           hash=>$diffinfo->{'from_id'}[$i],
				                           file_name=>$from->{'file'}[$i]);
			} else {
				$from->{'href'}[$i] = undef;
			}
		}
	} else {
		# ordinary (not combined) diff
		$from->{'file'} = $diffinfo->{'from_file'};
		if ($diffinfo->{'status'} ne "A") { # not new (added) file
			$from->{'href'} = href(action=>"blob", hash_base=>$hash_parent,
			                       hash=>$diffinfo->{'from_id'},
			                       file_name=>$from->{'file'});
		} else {
			delete $from->{'href'};
		}
	}

	$to->{'file'} = $diffinfo->{'to_file'};
	if (!is_deleted($diffinfo)) { # file exists in result
		$to->{'href'} = href(action=>"blob", hash_base=>$hash,
		                     hash=>$diffinfo->{'to_id'},
		                     file_name=>$to->{'file'});
	} else {
		delete $to->{'href'};
	}
}

## ......................................................................
## parse to array of hashes functions

sub git_get_heads_list {
	my $limit = shift;
	my @headslist;

	open my $fd, '-|', git_cmd(), 'for-each-ref',
		($limit ? '--count='.($limit+1) : ()), '--sort=-committerdate',
		'--format=%(objectname) %(refname) %(subject)%00%(committer)',
		'refs/heads'
		or return;
	while (my $line = <$fd>) {
		my %ref_item;

		chomp $line;
		my ($refinfo, $committerinfo) = split(/\0/, $line);
		my ($hash, $name, $title) = split(' ', $refinfo, 3);
		my ($committer, $epoch, $tz) =
			($committerinfo =~ /^(.*) ([0-9]+) (.*)$/);
		$ref_item{'fullname'}  = $name;
		$name =~ s!^refs/heads/!!;

		$ref_item{'name'}  = $name;
		$ref_item{'id'}    = $hash;
		$ref_item{'title'} = $title || '(no commit message)';
		$ref_item{'epoch'} = $epoch;
		if ($epoch) {
			$ref_item{'age'} = age_string(time - $ref_item{'epoch'});
		} else {
			$ref_item{'age'} = "unknown";
		}

		push @headslist, \%ref_item;
	}
	close $fd;

	return wantarray ? @headslist : \@headslist;
}

sub git_get_tags_list {
	my $limit = shift;
	my @tagslist;

	open my $fd, '-|', git_cmd(), 'for-each-ref',
		($limit ? '--count='.($limit+1) : ()), '--sort=-creatordate',
		'--format=%(objectname) %(objecttype) %(refname) '.
		'%(*objectname) %(*objecttype) %(subject)%00%(creator)',
		'refs/tags'
		or return;
	while (my $line = <$fd>) {
		my %ref_item;

		chomp $line;
		my ($refinfo, $creatorinfo) = split(/\0/, $line);
		my ($id, $type, $name, $refid, $reftype, $title) = split(' ', $refinfo, 6);
		my ($creator, $epoch, $tz) =
			($creatorinfo =~ /^(.*) ([0-9]+) (.*)$/);
		$ref_item{'fullname'} = $name;
		$name =~ s!^refs/tags/!!;

		$ref_item{'type'} = $type;
		$ref_item{'id'} = $id;
		$ref_item{'name'} = $name;
		if ($type eq "tag") {
			$ref_item{'subject'} = $title;
			$ref_item{'reftype'} = $reftype;
			$ref_item{'refid'}   = $refid;
		} else {
			$ref_item{'reftype'} = $type;
			$ref_item{'refid'}   = $id;
		}

		if ($type eq "tag" || $type eq "commit") {
			$ref_item{'epoch'} = $epoch;
			if ($epoch) {
				$ref_item{'age'} = age_string(time - $ref_item{'epoch'});
			} else {
				$ref_item{'age'} = "unknown";
			}
		}

		push @tagslist, \%ref_item;
	}
	close $fd;

	return wantarray ? @tagslist : \@tagslist;
}

## ----------------------------------------------------------------------
## filesystem-related functions

sub get_file_owner {
	my $path = shift;

	my ($dev, $ino, $mode, $nlink, $st_uid, $st_gid, $rdev, $size) = stat($path);
	my ($name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell) = getpwuid($st_uid);
	if (!defined $gcos) {
		return undef;
	}
	my $owner = $gcos;
	$owner =~ s/[,;].*$//;
	return to_utf8($owner);
}

## ......................................................................
## mimetype related functions

sub mimetype_guess_file {
	my $filename = shift;
	my $mimemap = shift;
	-r $mimemap or return undef;

	my %mimemap;
	open(MIME, $mimemap) or return undef;
	while (<MIME>) {
		next if m/^#/; # skip comments
		my ($mime, $exts) = split(/\t+/);
		if (defined $exts) {
			my @exts = split(/\s+/, $exts);
			foreach my $ext (@exts) {
				$mimemap{$ext} = $mime;
			}
		}
	}
	close(MIME);

	$filename =~ /\.([^.]*)$/;
	return $mimemap{$1};
}

sub mimetype_guess {
	my $filename = shift;
	my $mime;
	$filename =~ /\./ or return undef;

	if ($mimetypes_file) {
		my $file = $mimetypes_file;
		if ($file !~ m!^/!) { # if it is relative path
			# it is relative to project
			$file = "$projectroot/$project/$file";
		}
		$mime = mimetype_guess_file($filename, $file);
	}
	$mime ||= mimetype_guess_file($filename, '/etc/mime.types');
	return $mime;
}

sub blob_mimetype {
	my $fd = shift;
	my $filename = shift;

	if ($filename) {
		my $mime = mimetype_guess($filename);
		$mime and return $mime;
	}

	# just in case
	return $default_blob_plain_mimetype unless $fd;

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

## ======================================================================
## functions printing HTML: header, footer, error page

sub git_header_html {
	my $status = shift || "200 OK";
	my $expires = shift;

	my $title = "$site_name";
	if (defined $project) {
		$title .= " - " . to_utf8($project);
		if (defined $action) {
			$title .= "/$action";
			if (defined $file_name) {
				$title .= " - " . esc_path($file_name);
				if ($action eq "tree" && $file_name !~ m|/$|) {
					$title .= "/";
				}
			}
		}
	}
	my $content_type;
	# require explicit support from the UA if we are to send the page as
	# 'application/xhtml+xml', otherwise send it as plain old 'text/html'.
	# we have to do this because MSIE sometimes globs '*/*', pretending to
	# support xhtml+xml but choking when it gets what it asked for.
	if (defined $cgi->http('HTTP_ACCEPT') &&
	    $cgi->http('HTTP_ACCEPT') =~ m/(,|;|\s|^)application\/xhtml\+xml(,|;|\s|$)/ &&
	    $cgi->Accept('application/xhtml+xml') != 0) {
		$content_type = 'application/xhtml+xml';
	} else {
		$content_type = 'text/html';
	}
	print $cgi->header(-type=>$content_type, -charset => 'utf-8',
	                   -status=> $status, -expires => $expires);
	my $mod_perl_version = $ENV{'MOD_PERL'} ? " $ENV{'MOD_PERL'}" : '';
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface version $version, (C) 2005-2006, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke -->
<!-- git core binaries version $git_version -->
<head>
<meta http-equiv="content-type" content="$content_type; charset=utf-8"/>
<meta name="generator" content="gitweb/$version git/$git_version$mod_perl_version"/>
<meta name="robots" content="index, nofollow"/>
<title>$title</title>
EOF
# print out each stylesheet that exist
	if (defined $stylesheet) {
#provides backwards capability for those people who define style sheet in a config file
		print '<link rel="stylesheet" type="text/css" href="'.$stylesheet.'"/>'."\n";
	} else {
		foreach my $stylesheet (@stylesheets) {
			next unless $stylesheet;
			print '<link rel="stylesheet" type="text/css" href="'.$stylesheet.'"/>'."\n";
		}
	}
	if (defined $project) {
		printf('<link rel="alternate" title="%s log RSS feed" '.
		       'href="%s" type="application/rss+xml" />'."\n",
		       esc_param($project), href(action=>"rss"));
		printf('<link rel="alternate" title="%s log RSS feed (no merges)" '.
		       'href="%s" type="application/rss+xml" />'."\n",
		       esc_param($project), href(action=>"rss",
		                                 extra_options=>"--no-merges"));
		printf('<link rel="alternate" title="%s log Atom feed" '.
		       'href="%s" type="application/atom+xml" />'."\n",
		       esc_param($project), href(action=>"atom"));
		printf('<link rel="alternate" title="%s log Atom feed (no merges)" '.
		       'href="%s" type="application/atom+xml" />'."\n",
		       esc_param($project), href(action=>"atom",
		                                 extra_options=>"--no-merges"));
	} else {
		printf('<link rel="alternate" title="%s projects list" '.
		       'href="%s" type="text/plain; charset=utf-8"/>'."\n",
		       $site_name, href(project=>undef, action=>"project_index"));
		printf('<link rel="alternate" title="%s projects feeds" '.
		       'href="%s" type="text/x-opml"/>'."\n",
		       $site_name, href(project=>undef, action=>"opml"));
	}
	if (defined $favicon) {
		print qq(<link rel="shortcut icon" href="$favicon" type="image/png"/>\n);
	}

	print "</head>\n" .
	      "<body>\n";

	if (-f $site_header) {
		open (my $fd, $site_header);
		print <$fd>;
		close $fd;
	}

	print "<div class=\"page_header\">\n" .
	      $cgi->a({-href => esc_url($logo_url),
	               -title => $logo_label},
	              qq(<img src="$logo" width="72" height="27" alt="git" class="logo"/>));
	print $cgi->a({-href => esc_url($home_link)}, $home_link_str) . " / ";
	if (defined $project) {
		print $cgi->a({-href => href(action=>"summary")}, esc_html($project));
		if (defined $action) {
			print " / $action";
		}
		print "\n";
	}
	print "</div>\n";

	my ($have_search) = gitweb_check_feature('search');
	if ((defined $project) && ($have_search)) {
		if (!defined $searchtext) {
			$searchtext = "";
		}
		my $search_hash;
		if (defined $hash_base) {
			$search_hash = $hash_base;
		} elsif (defined $hash) {
			$search_hash = $hash;
		} else {
			$search_hash = "HEAD";
		}
		my $action = $my_uri;
		my ($use_pathinfo) = gitweb_check_feature('pathinfo');
		if ($use_pathinfo) {
			$action .= "/$project";
		} else {
			$cgi->param("p", $project);
		}
		$cgi->param("a", "search");
		$cgi->param("h", $search_hash);
		print $cgi->startform(-method => "get", -action => $action) .
		      "<div class=\"search\">\n" .
		      (!$use_pathinfo && $cgi->hidden(-name => "p") . "\n") .
		      $cgi->hidden(-name => "a") . "\n" .
		      $cgi->hidden(-name => "h") . "\n" .
		      $cgi->popup_menu(-name => 'st', -default => 'commit',
		                       -values => ['commit', 'grep', 'author', 'committer', 'pickaxe']) .
		      $cgi->sup($cgi->a({-href => href(action=>"search_help")}, "?")) .
		      " search:\n",
		      $cgi->textfield(-name => "s", -value => $searchtext) . "\n" .
		      "<span title=\"Extended regular expression\">" .
		      $cgi->checkbox(-name => 'sr', -value => 1, -label => 're',
		                     -checked => $search_use_regexp) .
		      "</span>" .
		      "</div>" .
		      $cgi->end_form() . "\n";
	}
}

sub git_footer_html {
	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_get_project_description($project);
		if (defined $descr) {
			print "<div class=\"page_footer_text\">" . esc_html($descr) . "</div>\n";
		}
		print $cgi->a({-href => href(action=>"rss"),
		              -class => "rss_logo"}, "RSS") . " ";
		print $cgi->a({-href => href(action=>"atom"),
		              -class => "rss_logo"}, "Atom") . "\n";
	} else {
		print $cgi->a({-href => href(project=>undef, action=>"opml"),
		              -class => "rss_logo"}, "OPML") . " ";
		print $cgi->a({-href => href(project=>undef, action=>"project_index"),
		              -class => "rss_logo"}, "TXT") . "\n";
	}
	print "</div>\n" ;

	if (-f $site_footer) {
		open (my $fd, $site_footer);
		print <$fd>;
		close $fd;
	}

	print "</body>\n" .
	      "</html>";
}

sub die_error {
	my $status = shift || "403 Forbidden";
	my $error = shift || "Malformed query, file missing or permission denied";

	git_header_html($status);
	print <<EOF;
<div class="page_body">
<br /><br />
$status - $error
<br />
</div>
EOF
	git_footer_html();
	exit;
}

## ----------------------------------------------------------------------
## functions printing or outputting HTML: navigation

sub git_print_page_nav {
	my ($current, $suppress, $head, $treehead, $treebase, $extra) = @_;
	$extra = '' if !defined $extra; # pager or formats

	my @navs = qw(summary shortlog log commit commitdiff tree);
	if ($suppress) {
		@navs = grep { $_ ne $suppress } @navs;
	}

	my %arg = map { $_ => {action=>$_} } @navs;
	if (defined $head) {
		for (qw(commit commitdiff)) {
			$arg{$_}{'hash'} = $head;
		}
		if ($current =~ m/^(tree | log | shortlog | commit | commitdiff | search)$/x) {
			for (qw(shortlog log)) {
				$arg{$_}{'hash'} = $head;
			}
		}
	}
	$arg{'tree'}{'hash'} = $treehead if defined $treehead;
	$arg{'tree'}{'hash_base'} = $treebase if defined $treebase;

	print "<div class=\"page_nav\">\n" .
		(join " | ",
		 map { $_ eq $current ?
		       $_ : $cgi->a({-href => href(%{$arg{$_}})}, "$_")
		 } @navs);
	print "<br/>\n$extra<br/>\n" .
	      "</div>\n";
}

sub format_paging_nav {
	my ($action, $hash, $head, $page, $nrevs) = @_;
	my $paging_nav;


	if ($hash ne $head || $page) {
		$paging_nav .= $cgi->a({-href => href(action=>$action)}, "HEAD");
	} else {
		$paging_nav .= "HEAD";
	}

	if ($page > 0) {
		$paging_nav .= " &sdot; " .
			$cgi->a({-href => href(-replay=>1, page=>$page-1),
			         -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		$paging_nav .= " &sdot; prev";
	}

	if ($nrevs >= (100 * ($page+1)-1)) {
		$paging_nav .= " &sdot; " .
			$cgi->a({-href => href(-replay=>1, page=>$page+1),
			         -accesskey => "n", -title => "Alt-n"}, "next");
	} else {
		$paging_nav .= " &sdot; next";
	}

	return $paging_nav;
}

## ......................................................................
## functions printing or outputting HTML: div

sub git_print_header_div {
	my ($action, $title, $hash, $hash_base) = @_;
	my %args = ();

	$args{'action'} = $action;
	$args{'hash'} = $hash if $hash;
	$args{'hash_base'} = $hash_base if $hash_base;

	print "<div class=\"header\">\n" .
	      $cgi->a({-href => href(%args), -class => "title"},
	      $title ? $title : $action) .
	      "\n</div>\n";
}

#sub git_print_authorship (\%) {
sub git_print_authorship {
	my $co = shift;

	my %ad = parse_date($co->{'author_epoch'}, $co->{'author_tz'});
	print "<div class=\"author_date\">" .
	      esc_html($co->{'author_name'}) .
	      " [$ad{'rfc2822'}";
	if ($ad{'hour_local'} < 6) {
		printf(" (<span class=\"atnight\">%02d:%02d</span> %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "]</div>\n";
}

sub git_print_page_path {
	my $name = shift;
	my $type = shift;
	my $hb = shift;


	print "<div class=\"page_path\">";
	print $cgi->a({-href => href(action=>"tree", hash_base=>$hb),
	              -title => 'tree root'}, to_utf8("[$project]"));
	print " / ";
	if (defined $name) {
		my @dirname = split '/', $name;
		my $basename = pop @dirname;
		my $fullname = '';

		foreach my $dir (@dirname) {
			$fullname .= ($fullname ? '/' : '') . $dir;
			print $cgi->a({-href => href(action=>"tree", file_name=>$fullname,
			                             hash_base=>$hb),
			              -title => $fullname}, esc_path($dir));
			print " / ";
		}
		if (defined $type && $type eq 'blob') {
			print $cgi->a({-href => href(action=>"blob_plain", file_name=>$file_name,
			                             hash_base=>$hb),
			              -title => $name}, esc_path($basename));
		} elsif (defined $type && $type eq 'tree') {
			print $cgi->a({-href => href(action=>"tree", file_name=>$file_name,
			                             hash_base=>$hb),
			              -title => $name}, esc_path($basename));
			print " / ";
		} else {
			print esc_path($basename);
		}
	}
	print "<br/></div>\n";
}

# sub git_print_log (\@;%) {
sub git_print_log ($;%) {
	my $log = shift;
	my %opts = @_;

	if ($opts{'-remove_title'}) {
		# remove title, i.e. first line of log
		shift @$log;
	}
	# remove leading empty lines
	while (defined $log->[0] && $log->[0] eq "") {
		shift @$log;
	}

	# print log
	my $signoff = 0;
	my $empty = 0;
	foreach my $line (@$log) {
		if ($line =~ m/^ *(signed[ \-]off[ \-]by[ :]|acked[ \-]by[ :]|cc[ :])/i) {
			$signoff = 1;
			$empty = 0;
			if (! $opts{'-remove_signoff'}) {
				print "<span class=\"signoff\">" . esc_html($line) . "</span><br/>\n";
				next;
			} else {
				# remove signoff lines
				next;
			}
		} else {
			$signoff = 0;
		}

		# print only one empty line
		# do not print empty line after signoff
		if ($line eq "") {
			next if ($empty || $signoff);
			$empty = 1;
		} else {
			$empty = 0;
		}

		print format_log_line_html($line) . "<br/>\n";
	}

	if ($opts{'-final_empty_line'}) {
		# end with single empty line
		print "<br/>\n" unless $empty;
	}
}

# return link target (what link points to)
sub git_get_link_target {
	my $hash = shift;
	my $link_target;

	# read link
	open my $fd, "-|", git_cmd(), "cat-file", "blob", $hash
		or return;
	{
		local $/;
		$link_target = <$fd>;
	}
	close $fd
		or return;

	return $link_target;
}

# given link target, and the directory (basedir) the link is in,
# return target of link relative to top directory (top tree);
# return undef if it is not possible (including absolute links).
sub normalize_link_target {
	my ($link_target, $basedir, $hash_base) = @_;

	# we can normalize symlink target only if $hash_base is provided
	return unless $hash_base;

	# absolute symlinks (beginning with '/') cannot be normalized
	return if (substr($link_target, 0, 1) eq '/');

	# normalize link target to path from top (root) tree (dir)
	my $path;
	if ($basedir) {
		$path = $basedir . '/' . $link_target;
	} else {
		# we are in top (root) tree (dir)
		$path = $link_target;
	}

	# remove //, /./, and /../
	my @path_parts;
	foreach my $part (split('/', $path)) {
		# discard '.' and ''
		next if (!$part || $part eq '.');
		# handle '..'
		if ($part eq '..') {
			if (@path_parts) {
				pop @path_parts;
			} else {
				# link leads outside repository (outside top dir)
				return;
			}
		} else {
			push @path_parts, $part;
		}
	}
	$path = join('/', @path_parts);

	return $path;
}

# print tree entry (row of git_tree), but without encompassing <tr> element
sub git_print_tree_entry {
	my ($t, $basedir, $hash_base, $have_blame) = @_;

	my %base_key = ();
	$base_key{'hash_base'} = $hash_base if defined $hash_base;

	# The format of a table row is: mode list link.  Where mode is
	# the mode of the entry, list is the name of the entry, an href,
	# and link is the action links of the entry.

	print "<td class=\"mode\">" . mode_str($t->{'mode'}) . "</td>\n";
	if ($t->{'type'} eq "blob") {
		print "<td class=\"list\">" .
			$cgi->a({-href => href(action=>"blob", hash=>$t->{'hash'},
			                       file_name=>"$basedir$t->{'name'}", %base_key),
			        -class => "list"}, esc_path($t->{'name'}));
		if (S_ISLNK(oct $t->{'mode'})) {
			my $link_target = git_get_link_target($t->{'hash'});
			if ($link_target) {
				my $norm_target = normalize_link_target($link_target, $basedir, $hash_base);
				if (defined $norm_target) {
					print " -> " .
					      $cgi->a({-href => href(action=>"object", hash_base=>$hash_base,
					                             file_name=>$norm_target),
					               -title => $norm_target}, esc_path($link_target));
				} else {
					print " -> " . esc_path($link_target);
				}
			}
		}
		print "</td>\n";
		print "<td class=\"link\">";
		print $cgi->a({-href => href(action=>"blob", hash=>$t->{'hash'},
		                             file_name=>"$basedir$t->{'name'}", %base_key)},
		              "blob");
		if ($have_blame) {
			print " | " .
			      $cgi->a({-href => href(action=>"blame", hash=>$t->{'hash'},
			                             file_name=>"$basedir$t->{'name'}", %base_key)},
			              "blame");
		}
		if (defined $hash_base) {
			print " | " .
			      $cgi->a({-href => href(action=>"history", hash_base=>$hash_base,
			                             hash=>$t->{'hash'}, file_name=>"$basedir$t->{'name'}")},
			              "history");
		}
		print " | " .
			$cgi->a({-href => href(action=>"blob_plain", hash_base=>$hash_base,
			                       file_name=>"$basedir$t->{'name'}")},
			        "raw");
		print "</td>\n";

	} elsif ($t->{'type'} eq "tree") {
		print "<td class=\"list\">";
		print $cgi->a({-href => href(action=>"tree", hash=>$t->{'hash'},
		                             file_name=>"$basedir$t->{'name'}", %base_key)},
		              esc_path($t->{'name'}));
		print "</td>\n";
		print "<td class=\"link\">";
		print $cgi->a({-href => href(action=>"tree", hash=>$t->{'hash'},
		                             file_name=>"$basedir$t->{'name'}", %base_key)},
		              "tree");
		if (defined $hash_base) {
			print " | " .
			      $cgi->a({-href => href(action=>"history", hash_base=>$hash_base,
			                             file_name=>"$basedir$t->{'name'}")},
			              "history");
		}
		print "</td>\n";
	} else {
		# unknown object: we can only present history for it
		# (this includes 'commit' object, i.e. submodule support)
		print "<td class=\"list\">" .
		      esc_path($t->{'name'}) .
		      "</td>\n";
		print "<td class=\"link\">";
		if (defined $hash_base) {
			print $cgi->a({-href => href(action=>"history",
			                             hash_base=>$hash_base,
			                             file_name=>"$basedir$t->{'name'}")},
			              "history");
		}
		print "</td>\n";
	}
}

## ......................................................................
## functions printing large fragments of HTML

# get pre-image filenames for merge (combined) diff
sub fill_from_file_info {
	my ($diff, @parents) = @_;

	$diff->{'from_file'} = [ ];
	$diff->{'from_file'}[$diff->{'nparents'} - 1] = undef;
	for (my $i = 0; $i < $diff->{'nparents'}; $i++) {
		if ($diff->{'status'}[$i] eq 'R' ||
		    $diff->{'status'}[$i] eq 'C') {
			$diff->{'from_file'}[$i] =
				git_get_path_by_hash($parents[$i], $diff->{'from_id'}[$i]);
		}
	}

	return $diff;
}

# is current raw difftree line of file deletion
sub is_deleted {
	my $diffinfo = shift;

	return $diffinfo->{'status_str'} =~ /D/;
}

# does patch correspond to [previous] difftree raw line
# $diffinfo  - hashref of parsed raw diff format
# $patchinfo - hashref of parsed patch diff format
#              (the same keys as in $diffinfo)
sub is_patch_split {
	my ($diffinfo, $patchinfo) = @_;

	return defined $diffinfo && defined $patchinfo
		&& $diffinfo->{'to_file'} eq $patchinfo->{'to_file'};
}


sub git_difftree_body {
	my ($difftree, $hash, @parents) = @_;
	my ($parent) = $parents[0];
	my ($have_blame) = gitweb_check_feature('blame');
	print "<div class=\"list_head\">\n";
	if ($#{$difftree} > 10) {
		print(($#{$difftree} + 1) . " files changed:\n");
	}
	print "</div>\n";

	print "<table class=\"" .
	      (@parents > 1 ? "combined " : "") .
	      "diff_tree\">\n";

	# header only for combined diff in 'commitdiff' view
	my $has_header = @$difftree && @parents > 1 && $action eq 'commitdiff';
	if ($has_header) {
		# table header
		print "<thead><tr>\n" .
		       "<th></th><th></th>\n"; # filename, patchN link
		for (my $i = 0; $i < @parents; $i++) {
			my $par = $parents[$i];
			print "<th>" .
			      $cgi->a({-href => href(action=>"commitdiff",
			                             hash=>$hash, hash_parent=>$par),
			               -title => 'commitdiff to parent number ' .
			                          ($i+1) . ': ' . substr($par,0,7)},
			              $i+1) .
			      "&nbsp;</th>\n";
		}
		print "</tr></thead>\n<tbody>\n";
	}

	my $alternate = 1;
	my $patchno = 0;
	foreach my $line (@{$difftree}) {
		my $diff = parsed_difftree_line($line);

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;

		if (exists $diff->{'nparents'}) { # combined diff

			fill_from_file_info($diff, @parents)
				unless exists $diff->{'from_file'};

			if (!is_deleted($diff)) {
				# file exists in the result (child) commit
				print "<td>" .
				      $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
				                             file_name=>$diff->{'to_file'},
				                             hash_base=>$hash),
				              -class => "list"}, esc_path($diff->{'to_file'})) .
				      "</td>\n";
			} else {
				print "<td>" .
				      esc_path($diff->{'to_file'}) .
				      "</td>\n";
			}

			if ($action eq 'commitdiff') {
				# link to patch
				$patchno++;
				print "<td class=\"link\">" .
				      $cgi->a({-href => "#patch$patchno"}, "patch") .
				      " | " .
				      "</td>\n";
			}

			my $has_history = 0;
			my $not_deleted = 0;
			for (my $i = 0; $i < $diff->{'nparents'}; $i++) {
				my $hash_parent = $parents[$i];
				my $from_hash = $diff->{'from_id'}[$i];
				my $from_path = $diff->{'from_file'}[$i];
				my $status = $diff->{'status'}[$i];

				$has_history ||= ($status ne 'A');
				$not_deleted ||= ($status ne 'D');

				if ($status eq 'A') {
					print "<td  class=\"link\" align=\"right\"> | </td>\n";
				} elsif ($status eq 'D') {
					print "<td class=\"link\">" .
					      $cgi->a({-href => href(action=>"blob",
					                             hash_base=>$hash,
					                             hash=>$from_hash,
					                             file_name=>$from_path)},
					              "blob" . ($i+1)) .
					      " | </td>\n";
				} else {
					if ($diff->{'to_id'} eq $from_hash) {
						print "<td class=\"link nochange\">";
					} else {
						print "<td class=\"link\">";
					}
					print $cgi->a({-href => href(action=>"blobdiff",
					                             hash=>$diff->{'to_id'},
					                             hash_parent=>$from_hash,
					                             hash_base=>$hash,
					                             hash_parent_base=>$hash_parent,
					                             file_name=>$diff->{'to_file'},
					                             file_parent=>$from_path)},
					              "diff" . ($i+1)) .
					      " | </td>\n";
				}
			}

			print "<td class=\"link\">";
			if ($not_deleted) {
				print $cgi->a({-href => href(action=>"blob",
				                             hash=>$diff->{'to_id'},
				                             file_name=>$diff->{'to_file'},
				                             hash_base=>$hash)},
				              "blob");
				print " | " if ($has_history);
			}
			if ($has_history) {
				print $cgi->a({-href => href(action=>"history",
				                             file_name=>$diff->{'to_file'},
				                             hash_base=>$hash)},
				              "history");
			}
			print "</td>\n";

			print "</tr>\n";
			next; # instead of 'else' clause, to avoid extra indent
		}
		# else ordinary diff

		my ($to_mode_oct, $to_mode_str, $to_file_type);
		my ($from_mode_oct, $from_mode_str, $from_file_type);
		if ($diff->{'to_mode'} ne ('0' x 6)) {
			$to_mode_oct = oct $diff->{'to_mode'};
			if (S_ISREG($to_mode_oct)) { # only for regular file
				$to_mode_str = sprintf("%04o", $to_mode_oct & 0777); # permission bits
			}
			$to_file_type = file_type($diff->{'to_mode'});
		}
		if ($diff->{'from_mode'} ne ('0' x 6)) {
			$from_mode_oct = oct $diff->{'from_mode'};
			if (S_ISREG($to_mode_oct)) { # only for regular file
				$from_mode_str = sprintf("%04o", $from_mode_oct & 0777); # permission bits
			}
			$from_file_type = file_type($diff->{'from_mode'});
		}

		if ($diff->{'status'} eq "A") { # created
			my $mode_chng = "<span class=\"file_status new\">[new $to_file_type";
			$mode_chng   .= " with mode: $to_mode_str" if $to_mode_str;
			$mode_chng   .= "]</span>";
			print "<td>";
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
			                             hash_base=>$hash, file_name=>$diff->{'file'}),
			              -class => "list"}, esc_path($diff->{'file'}));
			print "</td>\n";
			print "<td>$mode_chng</td>\n";
			print "<td class=\"link\">";
			if ($action eq 'commitdiff') {
				# link to patch
				$patchno++;
				print $cgi->a({-href => "#patch$patchno"}, "patch");
				print " | ";
			}
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
			                             hash_base=>$hash, file_name=>$diff->{'file'})},
			              "blob");
			print "</td>\n";

		} elsif ($diff->{'status'} eq "D") { # deleted
			my $mode_chng = "<span class=\"file_status deleted\">[deleted $from_file_type]</span>";
			print "<td>";
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'from_id'},
			                             hash_base=>$parent, file_name=>$diff->{'file'}),
			               -class => "list"}, esc_path($diff->{'file'}));
			print "</td>\n";
			print "<td>$mode_chng</td>\n";
			print "<td class=\"link\">";
			if ($action eq 'commitdiff') {
				# link to patch
				$patchno++;
				print $cgi->a({-href => "#patch$patchno"}, "patch");
				print " | ";
			}
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'from_id'},
			                             hash_base=>$parent, file_name=>$diff->{'file'})},
			              "blob") . " | ";
			if ($have_blame) {
				print $cgi->a({-href => href(action=>"blame", hash_base=>$parent,
				                             file_name=>$diff->{'file'})},
				              "blame") . " | ";
			}
			print $cgi->a({-href => href(action=>"history", hash_base=>$parent,
			                             file_name=>$diff->{'file'})},
			              "history");
			print "</td>\n";

		} elsif ($diff->{'status'} eq "M" || $diff->{'status'} eq "T") { # modified, or type changed
			my $mode_chnge = "";
			if ($diff->{'from_mode'} != $diff->{'to_mode'}) {
				$mode_chnge = "<span class=\"file_status mode_chnge\">[changed";
				if ($from_file_type ne $to_file_type) {
					$mode_chnge .= " from $from_file_type to $to_file_type";
				}
				if (($from_mode_oct & 0777) != ($to_mode_oct & 0777)) {
					if ($from_mode_str && $to_mode_str) {
						$mode_chnge .= " mode: $from_mode_str->$to_mode_str";
					} elsif ($to_mode_str) {
						$mode_chnge .= " mode: $to_mode_str";
					}
				}
				$mode_chnge .= "]</span>\n";
			}
			print "<td>";
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
			                             hash_base=>$hash, file_name=>$diff->{'file'}),
			              -class => "list"}, esc_path($diff->{'file'}));
			print "</td>\n";
			print "<td>$mode_chnge</td>\n";
			print "<td class=\"link\">";
			if ($action eq 'commitdiff') {
				# link to patch
				$patchno++;
				print $cgi->a({-href => "#patch$patchno"}, "patch") .
				      " | ";
			} elsif ($diff->{'to_id'} ne $diff->{'from_id'}) {
				# "commit" view and modified file (not onlu mode changed)
				print $cgi->a({-href => href(action=>"blobdiff",
				                             hash=>$diff->{'to_id'}, hash_parent=>$diff->{'from_id'},
				                             hash_base=>$hash, hash_parent_base=>$parent,
				                             file_name=>$diff->{'file'})},
				              "diff") .
				      " | ";
			}
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
			                             hash_base=>$hash, file_name=>$diff->{'file'})},
			               "blob") . " | ";
			if ($have_blame) {
				print $cgi->a({-href => href(action=>"blame", hash_base=>$hash,
				                             file_name=>$diff->{'file'})},
				              "blame") . " | ";
			}
			print $cgi->a({-href => href(action=>"history", hash_base=>$hash,
			                             file_name=>$diff->{'file'})},
			              "history");
			print "</td>\n";

		} elsif ($diff->{'status'} eq "R" || $diff->{'status'} eq "C") { # renamed or copied
			my %status_name = ('R' => 'moved', 'C' => 'copied');
			my $nstatus = $status_name{$diff->{'status'}};
			my $mode_chng = "";
			if ($diff->{'from_mode'} != $diff->{'to_mode'}) {
				# mode also for directories, so we cannot use $to_mode_str
				$mode_chng = sprintf(", mode: %04o", $to_mode_oct & 0777);
			}
			print "<td>" .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$hash,
			                             hash=>$diff->{'to_id'}, file_name=>$diff->{'to_file'}),
			              -class => "list"}, esc_path($diff->{'to_file'})) . "</td>\n" .
			      "<td><span class=\"file_status $nstatus\">[$nstatus from " .
			      $cgi->a({-href => href(action=>"blob", hash_base=>$parent,
			                             hash=>$diff->{'from_id'}, file_name=>$diff->{'from_file'}),
			              -class => "list"}, esc_path($diff->{'from_file'})) .
			      " with " . (int $diff->{'similarity'}) . "% similarity$mode_chng]</span></td>\n" .
			      "<td class=\"link\">";
			if ($action eq 'commitdiff') {
				# link to patch
				$patchno++;
				print $cgi->a({-href => "#patch$patchno"}, "patch") .
				      " | ";
			} elsif ($diff->{'to_id'} ne $diff->{'from_id'}) {
				# "commit" view and modified file (not only pure rename or copy)
				print $cgi->a({-href => href(action=>"blobdiff",
				                             hash=>$diff->{'to_id'}, hash_parent=>$diff->{'from_id'},
				                             hash_base=>$hash, hash_parent_base=>$parent,
				                             file_name=>$diff->{'to_file'}, file_parent=>$diff->{'from_file'})},
				              "diff") .
				      " | ";
			}
			print $cgi->a({-href => href(action=>"blob", hash=>$diff->{'to_id'},
			                             hash_base=>$parent, file_name=>$diff->{'to_file'})},
			              "blob") . " | ";
			if ($have_blame) {
				print $cgi->a({-href => href(action=>"blame", hash_base=>$hash,
				                             file_name=>$diff->{'to_file'})},
				              "blame") . " | ";
			}
			print $cgi->a({-href => href(action=>"history", hash_base=>$hash,
			                            file_name=>$diff->{'to_file'})},
			              "history");
			print "</td>\n";

		} # we should not encounter Unmerged (U) or Unknown (X) status
		print "</tr>\n";
	}
	print "</tbody>" if $has_header;
	print "</table>\n";
}

sub git_patchset_body {
	my ($fd, $difftree, $hash, @hash_parents) = @_;
	my ($hash_parent) = $hash_parents[0];

	my $is_combined = (@hash_parents > 1);
	my $patch_idx = 0;
	my $patch_number = 0;
	my $patch_line;
	my $diffinfo;
	my $to_name;
	my (%from, %to);

	print "<div class=\"patchset\">\n";

	# skip to first patch
	while ($patch_line = <$fd>) {
		chomp $patch_line;

		last if ($patch_line =~ m/^diff /);
	}

 PATCH:
	while ($patch_line) {

		# parse "git diff" header line
		if ($patch_line =~ m/^diff --git (\"(?:[^\\\"]*(?:\\.[^\\\"]*)*)\"|[^ "]*) (.*)$/) {
			# $1 is from_name, which we do not use
			$to_name = unquote($2);
			$to_name =~ s!^b/!!;
		} elsif ($patch_line =~ m/^diff --(cc|combined) ("?.*"?)$/) {
			# $1 is 'cc' or 'combined', which we do not use
			$to_name = unquote($2);
		} else {
			$to_name = undef;
		}

		# check if current patch belong to current raw line
		# and parse raw git-diff line if needed
		if (is_patch_split($diffinfo, { 'to_file' => $to_name })) {
			# this is continuation of a split patch
			print "<div class=\"patch cont\">\n";
		} else {
			# advance raw git-diff output if needed
			$patch_idx++ if defined $diffinfo;

			# read and prepare patch information
			$diffinfo = parsed_difftree_line($difftree->[$patch_idx]);

			# compact combined diff output can have some patches skipped
			# find which patch (using pathname of result) we are at now;
			if ($is_combined) {
				while ($to_name ne $diffinfo->{'to_file'}) {
					print "<div class=\"patch\" id=\"patch". ($patch_idx+1) ."\">\n" .
					      format_diff_cc_simplified($diffinfo, @hash_parents) .
					      "</div>\n";  # class="patch"

					$patch_idx++;
					$patch_number++;

					last if $patch_idx > $#$difftree;
					$diffinfo = parsed_difftree_line($difftree->[$patch_idx]);
				}
			}

			# modifies %from, %to hashes
			parse_from_to_diffinfo($diffinfo, \%from, \%to, @hash_parents);

			# this is first patch for raw difftree line with $patch_idx index
			# we index @$difftree array from 0, but number patches from 1
			print "<div class=\"patch\" id=\"patch". ($patch_idx+1) ."\">\n";
		}

		# git diff header
		#assert($patch_line =~ m/^diff /) if DEBUG;
		#assert($patch_line !~ m!$/$!) if DEBUG; # is chomp-ed
		$patch_number++;
		# print "git diff" header
		print format_git_diff_header_line($patch_line, $diffinfo,
		                                  \%from, \%to);

		# print extended diff header
		print "<div class=\"diff extended_header\">\n";
	EXTENDED_HEADER:
		while ($patch_line = <$fd>) {
			chomp $patch_line;

			last EXTENDED_HEADER if ($patch_line =~ m/^--- |^diff /);

			print format_extended_diff_header_line($patch_line, $diffinfo,
			                                       \%from, \%to);
		}
		print "</div>\n"; # class="diff extended_header"

		# from-file/to-file diff header
		if (! $patch_line) {
			print "</div>\n"; # class="patch"
			last PATCH;
		}
		next PATCH if ($patch_line =~ m/^diff /);
		#assert($patch_line =~ m/^---/) if DEBUG;

		my $last_patch_line = $patch_line;
		$patch_line = <$fd>;
		chomp $patch_line;
		#assert($patch_line =~ m/^\+\+\+/) if DEBUG;

		print format_diff_from_to_header($last_patch_line, $patch_line,
		                                 $diffinfo, \%from, \%to,
		                                 @hash_parents);

		# the patch itself
	LINE:
		while ($patch_line = <$fd>) {
			chomp $patch_line;

			next PATCH if ($patch_line =~ m/^diff /);

			print format_diff_line($patch_line, \%from, \%to);
		}

	} continue {
		print "</div>\n"; # class="patch"
	}

	# for compact combined (--cc) format, with chunk and patch simpliciaction
	# patchset might be empty, but there might be unprocessed raw lines
	for (++$patch_idx if $patch_number > 0;
	     $patch_idx < @$difftree;
	     ++$patch_idx) {
		# read and prepare patch information
		$diffinfo = parsed_difftree_line($difftree->[$patch_idx]);

		# generate anchor for "patch" links in difftree / whatchanged part
		print "<div class=\"patch\" id=\"patch". ($patch_idx+1) ."\">\n" .
		      format_diff_cc_simplified($diffinfo, @hash_parents) .
		      "</div>\n";  # class="patch"

		$patch_number++;
	}

	if ($patch_number == 0) {
		if (@hash_parents > 1) {
			print "<div class=\"diff nodifferences\">Trivial merge</div>\n";
		} else {
			print "<div class=\"diff nodifferences\">No differences found</div>\n";
		}
	}

	print "</div>\n"; # class="patchset"
}

# . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

sub git_project_list_body {
	my ($projlist, $order, $from, $to, $extra, $no_header) = @_;

	my ($check_forks) = gitweb_check_feature('forks');

	my @projects;
	foreach my $pr (@$projlist) {
		my (@aa) = git_get_last_activity($pr->{'path'});
		unless (@aa) {
			next;
		}
		($pr->{'age'}, $pr->{'age_string'}) = @aa;
		if (!defined $pr->{'descr'}) {
			my $descr = git_get_project_description($pr->{'path'}) || "";
			$pr->{'descr_long'} = to_utf8($descr);
			$pr->{'descr'} = chop_str($descr, $projects_list_description_width, 5);
		}
		if (!defined $pr->{'owner'}) {
			$pr->{'owner'} = git_get_project_owner("$pr->{'path'}") || "";
		}
		if ($check_forks) {
			my $pname = $pr->{'path'};
			if (($pname =~ s/\.git$//) &&
			    ($pname !~ /\/$/) &&
			    (-d "$projectroot/$pname")) {
				$pr->{'forks'} = "-d $projectroot/$pname";
			}
			else {
				$pr->{'forks'} = 0;
			}
		}
		push @projects, $pr;
	}

	$order ||= $default_projects_order;
	$from = 0 unless defined $from;
	$to = $#projects if (!defined $to || $#projects < $to);

	print "<table class=\"project_list\">\n";
	unless ($no_header) {
		print "<tr>\n";
		if ($check_forks) {
			print "<th></th>\n";
		}
		if ($order eq "project") {
			@projects = sort {$a->{'path'} cmp $b->{'path'}} @projects;
			print "<th>Project</th>\n";
		} else {
			print "<th>" .
			      $cgi->a({-href => href(project=>undef, order=>'project'),
			               -class => "header"}, "Project") .
			      "</th>\n";
		}
		if ($order eq "descr") {
			@projects = sort {$a->{'descr'} cmp $b->{'descr'}} @projects;
			print "<th>Description</th>\n";
		} else {
			print "<th>" .
			      $cgi->a({-href => href(project=>undef, order=>'descr'),
			               -class => "header"}, "Description") .
			      "</th>\n";
		}
		if ($order eq "owner") {
			@projects = sort {$a->{'owner'} cmp $b->{'owner'}} @projects;
			print "<th>Owner</th>\n";
		} else {
			print "<th>" .
			      $cgi->a({-href => href(project=>undef, order=>'owner'),
			               -class => "header"}, "Owner") .
			      "</th>\n";
		}
		if ($order eq "age") {
			@projects = sort {$a->{'age'} <=> $b->{'age'}} @projects;
			print "<th>Last Change</th>\n";
		} else {
			print "<th>" .
			      $cgi->a({-href => href(project=>undef, order=>'age'),
			               -class => "header"}, "Last Change") .
			      "</th>\n";
		}
		print "<th></th>\n" .
		      "</tr>\n";
	}
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my $pr = $projects[$i];
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		if ($check_forks) {
			print "<td>";
			if ($pr->{'forks'}) {
				print "<!-- $pr->{'forks'} -->\n";
				print $cgi->a({-href => href(project=>$pr->{'path'}, action=>"forks")}, "+");
			}
			print "</td>\n";
		}
		print "<td>" . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary"),
		                        -class => "list"}, esc_html($pr->{'path'})) . "</td>\n" .
		      "<td>" . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary"),
		                        -class => "list", -title => $pr->{'descr_long'}},
		                        esc_html($pr->{'descr'})) . "</td>\n" .
		      "<td><i>" . chop_and_escape_str($pr->{'owner'}, 15) . "</i></td>\n";
		print "<td class=\"". age_class($pr->{'age'}) . "\">" .
		      (defined $pr->{'age_string'} ? $pr->{'age_string'} : "No commits") . "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary")}, "summary")   . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"shortlog")}, "shortlog") . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"log")}, "log") . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"tree")}, "tree") .
		      ($pr->{'forks'} ? " | " . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"forks")}, "forks") : '') .
		      "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n";
		if ($check_forks) {
			print "<td></td>\n";
		}
		print "<td colspan=\"5\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_shortlog_body {
	# uses global variable $project
	my ($commitlist, $from, $to, $refs, $extra) = @_;

	$from = 0 unless defined $from;
	$to = $#{$commitlist} if (!defined $to || $#{$commitlist} < $to);

	print "<table class=\"shortlog\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my %co = %{$commitlist->[$i]};
		my $commit = $co{'id'};
		my $ref = format_ref_marker($refs, $commit);
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		my $author = chop_and_escape_str($co{'author_name'}, 10);
		# git_summary() used print "<td><i>$co{'age_string'}</i></td>\n" .
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      "<td><i>" . $author . "</i></td>\n" .
		      "<td>";
		print format_subject_html($co{'title'}, $co{'title_short'},
		                          href(action=>"commit", hash=>$commit), $ref);
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$commit)}, "commit") . " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff") . " | " .
		      $cgi->a({-href => href(action=>"tree", hash=>$commit, hash_base=>$commit)}, "tree");
		my $snapshot_links = format_snapshot_links($commit);
		if (defined $snapshot_links) {
			print " | " . $snapshot_links;
		}
		print "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"4\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_history_body {
	# Warning: assumes constant type (blob or tree) during history
	my ($commitlist, $from, $to, $refs, $hash_base, $ftype, $extra) = @_;

	$from = 0 unless defined $from;
	$to = $#{$commitlist} unless (defined $to && $to <= $#{$commitlist});

	print "<table class=\"history\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my %co = %{$commitlist->[$i]};
		if (!%co) {
			next;
		}
		my $commit = $co{'id'};

		my $ref = format_ref_marker($refs, $commit);

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
	# shortlog uses      chop_str($co{'author_name'}, 10)
		my $author = chop_and_escape_str($co{'author_name'}, 15, 3);
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      "<td><i>" . $author . "</i></td>\n" .
		      "<td>";
		# originally git_history used chop_str($co{'title'}, 50)
		print format_subject_html($co{'title'}, $co{'title_short'},
		                          href(action=>"commit", hash=>$commit), $ref);
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>$ftype, hash_base=>$commit, file_name=>$file_name)}, $ftype) . " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff");

		if ($ftype eq 'blob') {
			my $blob_current = git_get_hash_by_path($hash_base, $file_name);
			my $blob_parent  = git_get_hash_by_path($commit, $file_name);
			if (defined $blob_current && defined $blob_parent &&
					$blob_current ne $blob_parent) {
				print " | " .
					$cgi->a({-href => href(action=>"blobdiff",
					                       hash=>$blob_current, hash_parent=>$blob_parent,
					                       hash_base=>$hash_base, hash_parent_base=>$commit,
					                       file_name=>$file_name)},
					        "diff to current");
			}
		}
		print "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"4\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_tags_body {
	# uses global variable $project
	my ($taglist, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$taglist} if (!defined $to || $#{$taglist} < $to);

	print "<table class=\"tags\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my $entry = $taglist->[$i];
		my %tag = %$entry;
		my $comment = $tag{'subject'};
		my $comment_short;
		if (defined $comment) {
			$comment_short = chop_str($comment, 30, 5);
		}
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		if (defined $tag{'age'}) {
			print "<td><i>$tag{'age'}</i></td>\n";
		} else {
			print "<td></td>\n";
		}
		print "<td>" .
		      $cgi->a({-href => href(action=>$tag{'reftype'}, hash=>$tag{'refid'}),
		               -class => "list name"}, esc_html($tag{'name'})) .
		      "</td>\n" .
		      "<td>";
		if (defined $comment) {
			print format_subject_html($comment, $comment_short,
			                          href(action=>"tag", hash=>$tag{'id'}));
		}
		print "</td>\n" .
		      "<td class=\"selflink\">";
		if ($tag{'type'} eq "tag") {
			print $cgi->a({-href => href(action=>"tag", hash=>$tag{'id'})}, "tag");
		} else {
			print "&nbsp;";
		}
		print "</td>\n" .
		      "<td class=\"link\">" . " | " .
		      $cgi->a({-href => href(action=>$tag{'reftype'}, hash=>$tag{'refid'})}, $tag{'reftype'});
		if ($tag{'reftype'} eq "commit") {
			print " | " . $cgi->a({-href => href(action=>"shortlog", hash=>$tag{'fullname'})}, "shortlog") .
			      " | " . $cgi->a({-href => href(action=>"log", hash=>$tag{'fullname'})}, "log");
		} elsif ($tag{'reftype'} eq "blob") {
			print " | " . $cgi->a({-href => href(action=>"blob_plain", hash=>$tag{'refid'})}, "raw");
		}
		print "</td>\n" .
		      "</tr>";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"5\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_heads_body {
	# uses global variable $project
	my ($headlist, $head, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$headlist} if (!defined $to || $#{$headlist} < $to);

	print "<table class=\"heads\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my $entry = $headlist->[$i];
		my %ref = %$entry;
		my $curr = $ref{'id'} eq $head;
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td><i>$ref{'age'}</i></td>\n" .
		      ($curr ? "<td class=\"current_head\">" : "<td>") .
		      $cgi->a({-href => href(action=>"shortlog", hash=>$ref{'fullname'}),
		               -class => "list name"},esc_html($ref{'name'})) .
		      "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"shortlog", hash=>$ref{'fullname'})}, "shortlog") . " | " .
		      $cgi->a({-href => href(action=>"log", hash=>$ref{'fullname'})}, "log") . " | " .
		      $cgi->a({-href => href(action=>"tree", hash=>$ref{'fullname'}, hash_base=>$ref{'name'})}, "tree") .
		      "</td>\n" .
		      "</tr>";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"3\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

sub git_search_grep_body {
	my ($commitlist, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$commitlist} if (!defined $to || $#{$commitlist} < $to);

	print "<table class=\"commit_search\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my %co = %{$commitlist->[$i]};
		if (!%co) {
			next;
		}
		my $commit = $co{'id'};
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		my $author = chop_and_escape_str($co{'author_name'}, 15, 5);
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      "<td><i>" . $author . "</i></td>\n" .
		      "<td>" .
		      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'}),
		               -class => "list subject"},
		              chop_and_escape_str($co{'title'}, 50) . "<br/>");
		my $comment = $co{'comment'};
		foreach my $line (@$comment) {
			if ($line =~ m/^(.*?)($search_regexp)(.*)$/i) {
				my ($lead, $match, $trail) = ($1, $2, $3);
				$match = chop_str($match, 70, 5, 'center');
				my $contextlen = int((80 - length($match))/2);
				$contextlen = 30 if ($contextlen > 30);
				$lead  = chop_str($lead,  $contextlen, 10, 'left');
				$trail = chop_str($trail, $contextlen, 10, 'right');

				$lead  = esc_html($lead);
				$match = esc_html($match);
				$trail = esc_html($trail);

				print "$lead<span class=\"match\">$match</span>$trail<br />";
			}
		}
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})}, "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$co{'id'})}, "commitdiff") .
		      " | " .
		      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$co{'id'})}, "tree");
		print "</td>\n" .
		      "</tr>\n";
	}
	if (defined $extra) {
		print "<tr>\n" .
		      "<td colspan=\"3\">$extra</td>\n" .
		      "</tr>\n";
	}
	print "</table>\n";
}

## ======================================================================
## ======================================================================
## actions

sub git_project_list {
	my $order = $cgi->param('o');
	if (defined $order && $order !~ m/none|project|descr|owner|age/) {
		die_error(undef, "Unknown order parameter");
	}

	my @list = git_get_projects_list();
	if (!@list) {
		die_error(undef, "No projects found");
	}

	git_header_html();
	if (-f $home_text) {
		print "<div class=\"index_include\">\n";
		open (my $fd, $home_text);
		print <$fd>;
		close $fd;
		print "</div>\n";
	}
	git_project_list_body(\@list, $order);
	git_footer_html();
}

sub git_forks {
	my $order = $cgi->param('o');
	if (defined $order && $order !~ m/none|project|descr|owner|age/) {
		die_error(undef, "Unknown order parameter");
	}

	my @list = git_get_projects_list($project);
	if (!@list) {
		die_error(undef, "No forks found");
	}

	git_header_html();
	git_print_page_nav('','');
	git_print_header_div('summary', "$project forks");
	git_project_list_body(\@list, $order);
	git_footer_html();
}

sub git_project_index {
	my @projects = git_get_projects_list($project);

	print $cgi->header(
		-type => 'text/plain',
		-charset => 'utf-8',
		-content_disposition => 'inline; filename="index.aux"');

	foreach my $pr (@projects) {
		if (!exists $pr->{'owner'}) {
			$pr->{'owner'} = git_get_project_owner("$pr->{'path'}");
		}

		my ($path, $owner) = ($pr->{'path'}, $pr->{'owner'});
		# quote as in CGI::Util::encode, but keep the slash, and use '+' for ' '
		$path  =~ s/([^a-zA-Z0-9_.\-\/ ])/sprintf("%%%02X", ord($1))/eg;
		$owner =~ s/([^a-zA-Z0-9_.\-\/ ])/sprintf("%%%02X", ord($1))/eg;
		$path  =~ s/ /\+/g;
		$owner =~ s/ /\+/g;

		print "$path $owner\n";
	}
}

sub git_summary {
	my $descr = git_get_project_description($project) || "none";
	my %co = parse_commit("HEAD");
	my %cd = %co ? parse_date($co{'committer_epoch'}, $co{'committer_tz'}) : ();
	my $head = $co{'id'};

	my $owner = git_get_project_owner($project);

	my $refs = git_get_references();
	# These get_*_list functions return one more to allow us to see if
	# there are more ...
	my @taglist  = git_get_tags_list(16);
	my @headlist = git_get_heads_list(16);
	my @forklist;
	my ($check_forks) = gitweb_check_feature('forks');

	if ($check_forks) {
		@forklist = git_get_projects_list($project);
	}

	git_header_html();
	git_print_page_nav('summary','', $head);

	print "<div class=\"title\">&nbsp;</div>\n";
	print "<table class=\"projects_list\">\n" .
	      "<tr><td>description</td><td>" . esc_html($descr) . "</td></tr>\n" .
	      "<tr><td>owner</td><td>" . esc_html($owner) . "</td></tr>\n";
	if (defined $cd{'rfc2822'}) {
		print "<tr><td>last change</td><td>$cd{'rfc2822'}</td></tr>\n";
	}

	# use per project git URL list in $projectroot/$project/cloneurl
	# or make project git URL from git base URL and project name
	my $url_tag = "URL";
	my @url_list = git_get_project_url_list($project);
	@url_list = map { "$_/$project" } @git_base_url_list unless @url_list;
	foreach my $git_url (@url_list) {
		next unless $git_url;
		print "<tr><td>$url_tag</td><td>$git_url</td></tr>\n";
		$url_tag = "";
	}
	print "</table>\n";

	if (-s "$projectroot/$project/README.html") {
		if (open my $fd, "$projectroot/$project/README.html") {
			print "<div class=\"title\">readme</div>\n" .
			      "<div class=\"readme\">\n";
			print $_ while (<$fd>);
			print "\n</div>\n"; # class="readme"
			close $fd;
		}
	}

	# we need to request one more than 16 (0..15) to check if
	# those 16 are all
	my @commitlist = $head ? parse_commits($head, 17) : ();
	if (@commitlist) {
		git_print_header_div('shortlog');
		git_shortlog_body(\@commitlist, 0, 15, $refs,
		                  $#commitlist <=  15 ? undef :
		                  $cgi->a({-href => href(action=>"shortlog")}, "..."));
	}

	if (@taglist) {
		git_print_header_div('tags');
		git_tags_body(\@taglist, 0, 15,
		              $#taglist <=  15 ? undef :
		              $cgi->a({-href => href(action=>"tags")}, "..."));
	}

	if (@headlist) {
		git_print_header_div('heads');
		git_heads_body(\@headlist, $head, 0, 15,
		               $#headlist <= 15 ? undef :
		               $cgi->a({-href => href(action=>"heads")}, "..."));
	}

	if (@forklist) {
		git_print_header_div('forks');
		git_project_list_body(\@forklist, undef, 0, 15,
		                      $#forklist <= 15 ? undef :
		                      $cgi->a({-href => href(action=>"forks")}, "..."),
		                      'noheader');
	}

	git_footer_html();
}

sub git_tag {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	my %tag = parse_tag($hash);

	if (! %tag) {
		die_error(undef, "Unknown tag object");
	}

	git_print_header_div('commit', esc_html($tag{'name'}), $hash);
	print "<div class=\"title_text\">\n" .
	      "<table class=\"object_header\">\n" .
	      "<tr>\n" .
	      "<td>object</td>\n" .
	      "<td>" . $cgi->a({-class => "list", -href => href(action=>$tag{'type'}, hash=>$tag{'object'})},
	                       $tag{'object'}) . "</td>\n" .
	      "<td class=\"link\">" . $cgi->a({-href => href(action=>$tag{'type'}, hash=>$tag{'object'})},
	                                      $tag{'type'}) . "</td>\n" .
	      "</tr>\n";
	if (defined($tag{'author'})) {
		my %ad = parse_date($tag{'epoch'}, $tag{'tz'});
		print "<tr><td>author</td><td>" . esc_html($tag{'author'}) . "</td></tr>\n";
		print "<tr><td></td><td>" . $ad{'rfc2822'} .
			sprintf(" (%02d:%02d %s)", $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'}) .
			"</td></tr>\n";
	}
	print "</table>\n\n" .
	      "</div>\n";
	print "<div class=\"page_body\">";
	my $comment = $tag{'comment'};
	foreach my $line (@$comment) {
		chomp $line;
		print esc_html($line, -nbsp=>1) . "<br/>\n";
	}
	print "</div>\n";
	git_footer_html();
}

sub git_blame2 {
	my $fd;
	my $ftype;

	my ($have_blame) = gitweb_check_feature('blame');
	if (!$have_blame) {
		die_error('403 Permission denied', "Permission denied");
	}
	die_error('404 Not Found', "File name not defined") if (!$file_name);
	$hash_base ||= git_get_head_hash($project);
	die_error(undef, "Couldn't find base commit") unless ($hash_base);
	my %co = parse_commit($hash_base)
		or die_error(undef, "Reading commit failed");
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(undef, "Error looking up file");
	}
	$ftype = git_get_type($hash);
	if ($ftype !~ "blob") {
		die_error('400 Bad Request', "Object is not a blob");
	}
	open ($fd, "-|", git_cmd(), "blame", '-p', '--',
	      $file_name, $hash_base)
		or die_error(undef, "Open git-blame failed");
	git_header_html();
	my $formats_nav =
		$cgi->a({-href => href(action=>"blob", -replay=>1)},
		        "blob") .
		" | " .
		$cgi->a({-href => href(action=>"history", -replay=>1)},
		        "history") .
		" | " .
		$cgi->a({-href => href(action=>"blame", file_name=>$file_name)},
		        "HEAD");
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, $ftype, $hash_base);
	my @rev_color = (qw(light2 dark2));
	my $num_colors = scalar(@rev_color);
	my $current_color = 0;
	my $last_rev;
	print <<HTML;
<div class="page_body">
<table class="blame">
<tr><th>Commit</th><th>Line</th><th>Data</th></tr>
HTML
	my %metainfo = ();
	while (1) {
		$_ = <$fd>;
		last unless defined $_;
		my ($full_rev, $orig_lineno, $lineno, $group_size) =
		    /^([0-9a-f]{40}) (\d+) (\d+)(?: (\d+))?$/;
		if (!exists $metainfo{$full_rev}) {
			$metainfo{$full_rev} = {};
		}
		my $meta = $metainfo{$full_rev};
		while (<$fd>) {
			last if (s/^\t//);
			if (/^(\S+) (.*)$/) {
				$meta->{$1} = $2;
			}
		}
		my $data = $_;
		chomp $data;
		my $rev = substr($full_rev, 0, 8);
		my $author = $meta->{'author'};
		my %date = parse_date($meta->{'author-time'},
		                      $meta->{'author-tz'});
		my $date = $date{'iso-tz'};
		if ($group_size) {
			$current_color = ++$current_color % $num_colors;
		}
		print "<tr class=\"$rev_color[$current_color]\">\n";
		if ($group_size) {
			print "<td class=\"sha1\"";
			print " title=\"". esc_html($author) . ", $date\"";
			print " rowspan=\"$group_size\"" if ($group_size > 1);
			print ">";
			print $cgi->a({-href => href(action=>"commit",
			                             hash=>$full_rev,
			                             file_name=>$file_name)},
			              esc_html($rev));
			print "</td>\n";
		}
		open (my $dd, "-|", git_cmd(), "rev-parse", "$full_rev^")
			or die_error(undef, "Open git-rev-parse failed");
		my $parent_commit = <$dd>;
		close $dd;
		chomp($parent_commit);
		my $blamed = href(action => 'blame',
		                  file_name => $meta->{'filename'},
		                  hash_base => $parent_commit);
		print "<td class=\"linenr\">";
		print $cgi->a({ -href => "$blamed#l$orig_lineno",
		                -id => "l$lineno",
		                -class => "linenr" },
		              esc_html($lineno));
		print "</td>";
		print "<td class=\"pre\">" . esc_html($data) . "</td>\n";
		print "</tr>\n";
	}
	print "</table>\n";
	print "</div>";
	close $fd
		or print "Reading blob failed\n";
	git_footer_html();
}

sub git_blame {
	my $fd;

	my ($have_blame) = gitweb_check_feature('blame');
	if (!$have_blame) {
		die_error('403 Permission denied', "Permission denied");
	}
	die_error('404 Not Found', "File name not defined") if (!$file_name);
	$hash_base ||= git_get_head_hash($project);
	die_error(undef, "Couldn't find base commit") unless ($hash_base);
	my %co = parse_commit($hash_base)
		or die_error(undef, "Reading commit failed");
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(undef, "Error lookup file");
	}
	open ($fd, "-|", git_cmd(), "annotate", '-l', '-t', '-r', $file_name, $hash_base)
		or die_error(undef, "Open git-annotate failed");
	git_header_html();
	my $formats_nav =
		$cgi->a({-href => href(action=>"blob", hash=>$hash, hash_base=>$hash_base, file_name=>$file_name)},
		        "blob") .
		" | " .
		$cgi->a({-href => href(action=>"history", hash=>$hash, hash_base=>$hash_base, file_name=>$file_name)},
			"history") .
		" | " .
		$cgi->a({-href => href(action=>"blame", file_name=>$file_name)},
		        "HEAD");
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, 'blob', $hash_base);
	print "<div class=\"page_body\">\n";
	print <<HTML;
<table class="blame">
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
		my $age_class;

		chomp $line;
		$line_class_num = ($line_class_num + 1) % $line_class_len;

		if ($line =~ m/^([0-9a-fA-F]{40})\t\(\s*([^\t]+)\t(\d+) [+-]\d\d\d\d\t(\d+)\)(.*)$/) {
			$long_rev = $1;
			$author   = $2;
			$time     = $3;
			$lineno   = $4;
			$data     = $5;
		} else {
			print qq(  <tr><td colspan="5" class="error">Unable to parse: $line</td></tr>\n);
			next;
		}
		$short_rev  = substr ($long_rev, 0, 8);
		$age        = time () - $time;
		$age_str    = age_string ($age);
		$age_str    =~ s/ /&nbsp;/g;
		$age_class  = age_class($age);
		$author     = esc_html ($author);
		$author     =~ s/ /&nbsp;/g;

		$data = untabify($data);
		$data = esc_html ($data);

		print <<HTML;
  <tr class="$line_class[$line_class_num]">
    <td class="sha1"><a href="${\href (action=>"commit", hash=>$long_rev)}" class="text">$short_rev..</a></td>
    <td class="$age_class">$age_str</td>
    <td>$author</td>
    <td class="linenr"><a id="$lineno" href="#$lineno" class="linenr">$lineno</a></td>
    <td class="pre">$data</td>
  </tr>
HTML
	} # while (my $line = <$fd>)
	print "</table>\n\n";
	close $fd
		or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tags {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	git_print_header_div('summary', $project);

	my @tagslist = git_get_tags_list();
	if (@tagslist) {
		git_tags_body(\@tagslist);
	}
	git_footer_html();
}

sub git_heads {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
	git_print_header_div('summary', $project);

	my @headslist = git_get_heads_list();
	if (@headslist) {
		git_heads_body(\@headslist, $head);
	}
	git_footer_html();
}

sub git_blob_plain {
	my $expires;

	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(undef, "Error lookup file");
		} else {
			die_error(undef, "No file name defined");
		}
	} elsif ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		# blobs defined by non-textual hash id's can be cached
		$expires = "+1d";
	}

	my $type = shift;
	open my $fd, "-|", git_cmd(), "cat-file", "blob", $hash
		or die_error(undef, "Couldn't cat $file_name, $hash");

	$type ||= blob_mimetype($fd, $file_name);

	# save as filename, even when no $file_name is given
	my $save_as = "$hash";
	if (defined $file_name) {
		$save_as = $file_name;
	} elsif ($type =~ m/^text\//) {
		$save_as .= '.txt';
	}

	print $cgi->header(
		-type => "$type",
		-expires=>$expires,
		-content_disposition => 'inline; filename="' . "$save_as" . '"');
	undef $/;
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	$/ = "\n";
	close $fd;
}

sub git_blob {
	my $expires;

	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(undef, "Error lookup file");
		} else {
			die_error(undef, "No file name defined");
		}
	} elsif ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		# blobs defined by non-textual hash id's can be cached
		$expires = "+1d";
	}

	my ($have_blame) = gitweb_check_feature('blame');
	open my $fd, "-|", git_cmd(), "cat-file", "blob", $hash
		or die_error(undef, "Couldn't cat $file_name, $hash");
	my $mimetype = blob_mimetype($fd, $file_name);
	if ($mimetype !~ m!^(?:text/|image/(?:gif|png|jpeg)$)! && -B $fd) {
		close $fd;
		return git_blob_plain($mimetype);
	}
	# we can have blame only for text/* mimetype
	$have_blame &&= ($mimetype =~ m!^text/!);

	git_header_html(undef, $expires);
	my $formats_nav = '';
	if (defined $hash_base && (my %co = parse_commit($hash_base))) {
		if (defined $file_name) {
			if ($have_blame) {
				$formats_nav .=
					$cgi->a({-href => href(action=>"blame", -replay=>1)},
					        "blame") .
					" | ";
			}
			$formats_nav .=
				$cgi->a({-href => href(action=>"history", -replay=>1)},
				        "history") .
				" | " .
				$cgi->a({-href => href(action=>"blob_plain", -replay=>1)},
				        "raw") .
				" | " .
				$cgi->a({-href => href(action=>"blob",
				                       hash_base=>"HEAD", file_name=>$file_name)},
				        "HEAD");
		} else {
			$formats_nav .=
				$cgi->a({-href => href(action=>"blob_plain", -replay=>1)},
				        "raw");
		}
		git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
		git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	} else {
		print "<div class=\"page_nav\">\n" .
		      "<br/><br/></div>\n" .
		      "<div class=\"title\">$hash</div>\n";
	}
	git_print_page_path($file_name, "blob", $hash_base);
	print "<div class=\"page_body\">\n";
	if ($mimetype =~ m!^image/!) {
		print qq!<img type="$mimetype"!;
		if ($file_name) {
			print qq! alt="$file_name" title="$file_name"!;
		}
		print qq! src="! .
		      href(action=>"blob_plain", hash=>$hash,
		           hash_base=>$hash_base, file_name=>$file_name) .
		      qq!" />\n!;
	} else {
		my $nr;
		while (my $line = <$fd>) {
			chomp $line;
			$nr++;
			$line = untabify($line);
			printf "<div class=\"pre\"><a id=\"l%i\" href=\"#l%i\" class=\"linenr\">%4i</a> %s</div>\n",
			       $nr, $nr, $nr, esc_html($line, -nbsp=>1);
		}
	}
	close $fd
		or print "Reading blob failed.\n";
	print "</div>";
	git_footer_html();
}

sub git_tree {
	if (!defined $hash_base) {
		$hash_base = "HEAD";
	}
	if (!defined $hash) {
		if (defined $file_name) {
			$hash = git_get_hash_by_path($hash_base, $file_name, "tree");
		} else {
			$hash = $hash_base;
		}
	}
	$/ = "\0";
	open my $fd, "-|", git_cmd(), "ls-tree", '-z', $hash
		or die_error(undef, "Open git-ls-tree failed");
	my @entries = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading tree failed");
	$/ = "\n";

	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $hash_base);
	git_header_html();
	my $basedir = '';
	my ($have_blame) = gitweb_check_feature('blame');
	if (defined $hash_base && (my %co = parse_commit($hash_base))) {
		my @views_nav = ();
		if (defined $file_name) {
			push @views_nav,
				$cgi->a({-href => href(action=>"history", -replay=>1)},
				        "history"),
				$cgi->a({-href => href(action=>"tree",
				                       hash_base=>"HEAD", file_name=>$file_name)},
				        "HEAD"),
		}
		my $snapshot_links = format_snapshot_links($hash);
		if (defined $snapshot_links) {
			# FIXME: Should be available when we have no hash base as well.
			push @views_nav, $snapshot_links;
		}
		git_print_page_nav('tree','', $hash_base, undef, undef, join(' | ', @views_nav));
		git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash_base);
	} else {
		undef $hash_base;
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">$hash</div>\n";
	}
	if (defined $file_name) {
		$basedir = $file_name;
		if ($basedir ne '' && substr($basedir, -1) ne '/') {
			$basedir .= '/';
		}
	}
	git_print_page_path($file_name, 'tree', $hash_base);
	print "<div class=\"page_body\">\n";
	print "<table class=\"tree\">\n";
	my $alternate = 1;
	# '..' (top directory) link if possible
	if (defined $hash_base &&
	    defined $file_name && $file_name =~ m![^/]+$!) {
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;

		my $up = $file_name;
		$up =~ s!/?[^/]+$!!;
		undef $up unless $up;
		# based on git_print_tree_entry
		print '<td class="mode">' . mode_str('040000') . "</td>\n";
		print '<td class="list">';
		print $cgi->a({-href => href(action=>"tree", hash_base=>$hash_base,
		                             file_name=>$up)},
		              "..");
		print "</td>\n";
		print "<td class=\"link\"></td>\n";

		print "</tr>\n";
	}
	foreach my $line (@entries) {
		my %t = parse_ls_tree_line($line, -z => 1);

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;

		git_print_tree_entry(\%t, $basedir, $hash_base, $have_blame);

		print "</tr>\n";
	}
	print "</table>\n" .
	      "</div>";
	git_footer_html();
}

sub git_snapshot {
	my @supported_fmts = gitweb_check_feature('snapshot');
	@supported_fmts = filter_snapshot_fmts(@supported_fmts);

	my $format = $cgi->param('sf');
	if (!@supported_fmts) {
		die_error('403 Permission denied', "Permission denied");
	}
	# default to first supported snapshot format
	$format ||= $supported_fmts[0];
	if ($format !~ m/^[a-z0-9]+$/) {
		die_error(undef, "Invalid snapshot format parameter");
	} elsif (!exists($known_snapshot_formats{$format})) {
		die_error(undef, "Unknown snapshot format");
	} elsif (!grep($_ eq $format, @supported_fmts)) {
		die_error(undef, "Unsupported snapshot format");
	}

	if (!defined $hash) {
		$hash = git_get_head_hash($project);
	}

	my $git_command = git_cmd_str();
	my $name = $project;
	$name =~ s,([^/])/*\.git$,$1,;
	$name = basename($name);
	my $filename = to_utf8($name);
	$name =~ s/\047/\047\\\047\047/g;
	my $cmd;
	$filename .= "-$hash$known_snapshot_formats{$format}{'suffix'}";
	$cmd = "$git_command archive " .
		"--format=$known_snapshot_formats{$format}{'format'} " .
		"--prefix=\'$name\'/ $hash";
	if (exists $known_snapshot_formats{$format}{'compressor'}) {
		$cmd .= ' | ' . join ' ', @{$known_snapshot_formats{$format}{'compressor'}};
	}

	print $cgi->header(
		-type => $known_snapshot_formats{$format}{'type'},
		-content_disposition => 'inline; filename="' . "$filename" . '"',
		-status => '200 OK');

	open my $fd, "-|", $cmd
		or die_error(undef, "Execute git-archive failed");
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	close $fd;
}

sub git_log {
	my $head = git_get_head_hash($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = git_get_references();

	my @commitlist = parse_commits($hash, 101, (100 * $page));

	my $paging_nav = format_paging_nav('log', $hash, $head, $page, (100 * ($page+1)));

	git_header_html();
	git_print_page_nav('log','', $hash,undef,undef, $paging_nav);

	if (!@commitlist) {
		my %co = parse_commit($hash);

		git_print_header_div('summary', $project);
		print "<div class=\"page_body\"> Last change $co{'age_string'}.<br/><br/></div>\n";
	}
	my $to = ($#commitlist >= 99) ? (99) : ($#commitlist);
	for (my $i = 0; $i <= $to; $i++) {
		my %co = %{$commitlist[$i]};
		next if !%co;
		my $commit = $co{'id'};
		my $ref = format_ref_marker($refs, $commit);
		my %ad = parse_date($co{'author_epoch'});
		git_print_header_div('commit',
		               "<span class=\"age\">$co{'age_string'}</span>" .
		               esc_html($co{'title'}) . $ref,
		               $commit);
		print "<div class=\"title_text\">\n" .
		      "<div class=\"log_link\">\n" .
		      $cgi->a({-href => href(action=>"commit", hash=>$commit)}, "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff") .
		      " | " .
		      $cgi->a({-href => href(action=>"tree", hash=>$commit, hash_base=>$commit)}, "tree") .
		      "<br/>\n" .
		      "</div>\n" .
		      "<i>" . esc_html($co{'author_name'}) .  " [$ad{'rfc2822'}]</i><br/>\n" .
		      "</div>\n";

		print "<div class=\"log_body\">\n";
		git_print_log($co{'comment'}, -final_empty_line=> 1);
		print "</div>\n";
	}
	if ($#commitlist >= 100) {
		print "<div class=\"page_nav\">\n";
		print $cgi->a({-href => href(-replay=>1, page=>$page+1),
			       -accesskey => "n", -title => "Alt-n"}, "next");
		print "</div>\n";
	}
	git_footer_html();
}

sub git_commit {
	$hash ||= $hash_base || "HEAD";
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	my %ad = parse_date($co{'author_epoch'}, $co{'author_tz'});
	my %cd = parse_date($co{'committer_epoch'}, $co{'committer_tz'});

	my $parent  = $co{'parent'};
	my $parents = $co{'parents'}; # listref

	# we need to prepare $formats_nav before any parameter munging
	my $formats_nav;
	if (!defined $parent) {
		# --root commitdiff
		$formats_nav .= '(initial)';
	} elsif (@$parents == 1) {
		# single parent commit
		$formats_nav .=
			'(parent: ' .
			$cgi->a({-href => href(action=>"commit",
			                       hash=>$parent)},
			        esc_html(substr($parent, 0, 7))) .
			')';
	} else {
		# merge commit
		$formats_nav .=
			'(merge: ' .
			join(' ', map {
				$cgi->a({-href => href(action=>"commit",
				                       hash=>$_)},
				        esc_html(substr($_, 0, 7)));
			} @$parents ) .
			')';
	}

	if (!defined $parent) {
		$parent = "--root";
	}
	my @difftree;
	open my $fd, "-|", git_cmd(), "diff-tree", '-r', "--no-commit-id",
		@diff_opts,
		(@$parents <= 1 ? $parent : '-c'),
		$hash, "--"
		or die_error(undef, "Open git-diff-tree failed");
	@difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(undef, "Reading git-diff-tree failed");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}
	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $co{'id'});

	git_header_html(undef, $expires);
	git_print_page_nav('commit', '',
	                   $hash, $co{'tree'}, $hash,
	                   $formats_nav);

	if (defined $co{'parent'}) {
		git_print_header_div('commitdiff', esc_html($co{'title'}) . $ref, $hash);
	} else {
		git_print_header_div('tree', esc_html($co{'title'}) . $ref, $co{'tree'}, $hash);
	}
	print "<div class=\"title_text\">\n" .
	      "<table class=\"object_header\">\n";
	print "<tr><td>author</td><td>" . esc_html($co{'author'}) . "</td></tr>\n".
	      "<tr>" .
	      "<td></td><td> $ad{'rfc2822'}";
	if ($ad{'hour_local'} < 6) {
		printf(" (<span class=\"atnight\">%02d:%02d</span> %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	} else {
		printf(" (%02d:%02d %s)",
		       $ad{'hour_local'}, $ad{'minute_local'}, $ad{'tz_local'});
	}
	print "</td>" .
	      "</tr>\n";
	print "<tr><td>committer</td><td>" . esc_html($co{'committer'}) . "</td></tr>\n";
	print "<tr><td></td><td> $cd{'rfc2822'}" .
	      sprintf(" (%02d:%02d %s)", $cd{'hour_local'}, $cd{'minute_local'}, $cd{'tz_local'}) .
	      "</td></tr>\n";
	print "<tr><td>commit</td><td class=\"sha1\">$co{'id'}</td></tr>\n";
	print "<tr>" .
	      "<td>tree</td>" .
	      "<td class=\"sha1\">" .
	      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$hash),
	               class => "list"}, $co{'tree'}) .
	      "</td>" .
	      "<td class=\"link\">" .
	      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$hash)},
	              "tree");
	my $snapshot_links = format_snapshot_links($hash);
	if (defined $snapshot_links) {
		print " | " . $snapshot_links;
	}
	print "</td>" .
	      "</tr>\n";

	foreach my $par (@$parents) {
		print "<tr>" .
		      "<td>parent</td>" .
		      "<td class=\"sha1\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$par),
		               class => "list"}, $par) .
		      "</td>" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>"commit", hash=>$par)}, "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$hash, hash_parent=>$par)}, "diff") .
		      "</td>" .
		      "</tr>\n";
	}
	print "</table>".
	      "</div>\n";

	print "<div class=\"page_body\">\n";
	git_print_log($co{'comment'});
	print "</div>\n";

	git_difftree_body(\@difftree, $hash, @$parents);

	git_footer_html();
}

sub git_object {
	# object is defined by:
	# - hash or hash_base alone
	# - hash_base and file_name
	my $type;

	# - hash or hash_base alone
	if ($hash || ($hash_base && !defined $file_name)) {
		my $object_id = $hash || $hash_base;

		my $git_command = git_cmd_str();
		open my $fd, "-|", "$git_command cat-file -t $object_id 2>/dev/null"
			or die_error('404 Not Found', "Object does not exist");
		$type = <$fd>;
		chomp $type;
		close $fd
			or die_error('404 Not Found', "Object does not exist");

	# - hash_base and file_name
	} elsif ($hash_base && defined $file_name) {
		$file_name =~ s,/+$,,;

		system(git_cmd(), "cat-file", '-e', $hash_base) == 0
			or die_error('404 Not Found', "Base object does not exist");

		# here errors should not hapen
		open my $fd, "-|", git_cmd(), "ls-tree", $hash_base, "--", $file_name
			or die_error(undef, "Open git-ls-tree failed");
		my $line = <$fd>;
		close $fd;

		#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		unless ($line && $line =~ m/^([0-9]+) (.+) ([0-9a-fA-F]{40})\t/) {
			die_error('404 Not Found', "File or directory for given base does not exist");
		}
		$type = $2;
		$hash = $3;
	} else {
		die_error('404 Not Found', "Not enough information to find object");
	}

	print $cgi->redirect(-uri => href(action=>$type, -full=>1,
	                                  hash=>$hash, hash_base=>$hash_base,
	                                  file_name=>$file_name),
	                     -status => '302 Found');
}

sub git_blobdiff {
	my $format = shift || 'html';

	my $fd;
	my @difftree;
	my %diffinfo;
	my $expires;

	# preparing $fd and %diffinfo for git_patchset_body
	# new style URI
	if (defined $hash_base && defined $hash_parent_base) {
		if (defined $file_name) {
			# read raw output
			open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
				$hash_parent_base, $hash_base,
				"--", (defined $file_parent ? $file_parent : ()), $file_name
				or die_error(undef, "Open git-diff-tree failed");
			@difftree = map { chomp; $_ } <$fd>;
			close $fd
				or die_error(undef, "Reading git-diff-tree failed");
			@difftree
				or die_error('404 Not Found', "Blob diff not found");

		} elsif (defined $hash &&
		         $hash =~ /[0-9a-fA-F]{40}/) {
			# try to find filename from $hash

			# read filtered raw output
			open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
				$hash_parent_base, $hash_base, "--"
				or die_error(undef, "Open git-diff-tree failed");
			@difftree =
				# ':100644 100644 03b21826... 3b93d5e7... M	ls-files.c'
				# $hash == to_id
				grep { /^:[0-7]{6} [0-7]{6} [0-9a-fA-F]{40} $hash/ }
				map { chomp; $_ } <$fd>;
			close $fd
				or die_error(undef, "Reading git-diff-tree failed");
			@difftree
				or die_error('404 Not Found', "Blob diff not found");

		} else {
			die_error('404 Not Found', "Missing one of the blob diff parameters");
		}

		if (@difftree > 1) {
			die_error('404 Not Found', "Ambiguous blob diff specification");
		}

		%diffinfo = parse_difftree_raw_line($difftree[0]);
		$file_parent ||= $diffinfo{'from_file'} || $file_name;
		$file_name   ||= $diffinfo{'to_file'};

		$hash_parent ||= $diffinfo{'from_id'};
		$hash        ||= $diffinfo{'to_id'};

		# non-textual hash id's can be cached
		if ($hash_base =~ m/^[0-9a-fA-F]{40}$/ &&
		    $hash_parent_base =~ m/^[0-9a-fA-F]{40}$/) {
			$expires = '+1d';
		}

		# open patch output
		open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			'-p', ($format eq 'html' ? "--full-index" : ()),
			$hash_parent_base, $hash_base,
			"--", (defined $file_parent ? $file_parent : ()), $file_name
			or die_error(undef, "Open git-diff-tree failed");
	}

	# old/legacy style URI
	if (!%diffinfo && # if new style URI failed
	    defined $hash && defined $hash_parent) {
		# fake git-diff-tree raw output
		$diffinfo{'from_mode'} = $diffinfo{'to_mode'} = "blob";
		$diffinfo{'from_id'} = $hash_parent;
		$diffinfo{'to_id'}   = $hash;
		if (defined $file_name) {
			if (defined $file_parent) {
				$diffinfo{'status'} = '2';
				$diffinfo{'from_file'} = $file_parent;
				$diffinfo{'to_file'}   = $file_name;
			} else { # assume not renamed
				$diffinfo{'status'} = '1';
				$diffinfo{'from_file'} = $file_name;
				$diffinfo{'to_file'}   = $file_name;
			}
		} else { # no filename given
			$diffinfo{'status'} = '2';
			$diffinfo{'from_file'} = $hash_parent;
			$diffinfo{'to_file'}   = $hash;
		}

		# non-textual hash id's can be cached
		if ($hash =~ m/^[0-9a-fA-F]{40}$/ &&
		    $hash_parent =~ m/^[0-9a-fA-F]{40}$/) {
			$expires = '+1d';
		}

		# open patch output
		open $fd, "-|", git_cmd(), "diff", @diff_opts,
			'-p', ($format eq 'html' ? "--full-index" : ()),
			$hash_parent, $hash, "--"
			or die_error(undef, "Open git-diff failed");
	} else  {
		die_error('404 Not Found', "Missing one of the blob diff parameters")
			unless %diffinfo;
	}

	# header
	if ($format eq 'html') {
		my $formats_nav =
			$cgi->a({-href => href(action=>"blobdiff_plain", -replay=>1)},
			        "raw");
		git_header_html(undef, $expires);
		if (defined $hash_base && (my %co = parse_commit($hash_base))) {
			git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
			git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
		} else {
			print "<div class=\"page_nav\"><br/>$formats_nav<br/></div>\n";
			print "<div class=\"title\">$hash vs $hash_parent</div>\n";
		}
		if (defined $file_name) {
			git_print_page_path($file_name, "blob", $hash_base);
		} else {
			print "<div class=\"page_path\"></div>\n";
		}

	} elsif ($format eq 'plain') {
		print $cgi->header(
			-type => 'text/plain',
			-charset => 'utf-8',
			-expires => $expires,
			-content_disposition => 'inline; filename="' . "$file_name" . '.patch"');

		print "X-Git-Url: " . $cgi->self_url() . "\n\n";

	} else {
		die_error(undef, "Unknown blobdiff format");
	}

	# patch
	if ($format eq 'html') {
		print "<div class=\"page_body\">\n";

		git_patchset_body($fd, [ \%diffinfo ], $hash_base, $hash_parent_base);
		close $fd;

		print "</div>\n"; # class="page_body"
		git_footer_html();

	} else {
		while (my $line = <$fd>) {
			$line =~ s!a/($hash|$hash_parent)!'a/'.esc_path($diffinfo{'from_file'})!eg;
			$line =~ s!b/($hash|$hash_parent)!'b/'.esc_path($diffinfo{'to_file'})!eg;

			print $line;

			last if $line =~ m!^\+\+\+!;
		}
		local $/ = undef;
		print <$fd>;
		close $fd;
	}
}

sub git_blobdiff_plain {
	git_blobdiff('plain');
}

sub git_commitdiff {
	my $format = shift || 'html';
	$hash ||= $hash_base || "HEAD";
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}

	# choose format for commitdiff for merge
	if (! defined $hash_parent && @{$co{'parents'}} > 1) {
		$hash_parent = '--cc';
	}
	# we need to prepare $formats_nav before almost any parameter munging
	my $formats_nav;
	if ($format eq 'html') {
		$formats_nav =
			$cgi->a({-href => href(action=>"commitdiff_plain", -replay=>1)},
			        "raw");

		if (defined $hash_parent &&
		    $hash_parent ne '-c' && $hash_parent ne '--cc') {
			# commitdiff with two commits given
			my $hash_parent_short = $hash_parent;
			if ($hash_parent =~ m/^[0-9a-fA-F]{40}$/) {
				$hash_parent_short = substr($hash_parent, 0, 7);
			}
			$formats_nav .=
				' (from';
			for (my $i = 0; $i < @{$co{'parents'}}; $i++) {
				if ($co{'parents'}[$i] eq $hash_parent) {
					$formats_nav .= ' parent ' . ($i+1);
					last;
				}
			}
			$formats_nav .= ': ' .
				$cgi->a({-href => href(action=>"commitdiff",
				                       hash=>$hash_parent)},
				        esc_html($hash_parent_short)) .
				')';
		} elsif (!$co{'parent'}) {
			# --root commitdiff
			$formats_nav .= ' (initial)';
		} elsif (scalar @{$co{'parents'}} == 1) {
			# single parent commit
			$formats_nav .=
				' (parent: ' .
				$cgi->a({-href => href(action=>"commitdiff",
				                       hash=>$co{'parent'})},
				        esc_html(substr($co{'parent'}, 0, 7))) .
				')';
		} else {
			# merge commit
			if ($hash_parent eq '--cc') {
				$formats_nav .= ' | ' .
					$cgi->a({-href => href(action=>"commitdiff",
					                       hash=>$hash, hash_parent=>'-c')},
					        'combined');
			} else { # $hash_parent eq '-c'
				$formats_nav .= ' | ' .
					$cgi->a({-href => href(action=>"commitdiff",
					                       hash=>$hash, hash_parent=>'--cc')},
					        'compact');
			}
			$formats_nav .=
				' (merge: ' .
				join(' ', map {
					$cgi->a({-href => href(action=>"commitdiff",
					                       hash=>$_)},
					        esc_html(substr($_, 0, 7)));
				} @{$co{'parents'}} ) .
				')';
		}
	}

	my $hash_parent_param = $hash_parent;
	if (!defined $hash_parent_param) {
		# --cc for multiple parents, --root for parentless
		$hash_parent_param =
			@{$co{'parents'}} > 1 ? '--cc' : $co{'parent'} || '--root';
	}

	# read commitdiff
	my $fd;
	my @difftree;
	if ($format eq 'html') {
		open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			"--no-commit-id", "--patch-with-raw", "--full-index",
			$hash_parent_param, $hash, "--"
			or die_error(undef, "Open git-diff-tree failed");

		while (my $line = <$fd>) {
			chomp $line;
			# empty line ends raw part of diff-tree output
			last unless $line;
			push @difftree, scalar parse_difftree_raw_line($line);
		}

	} elsif ($format eq 'plain') {
		open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			'-p', $hash_parent_param, $hash, "--"
			or die_error(undef, "Open git-diff-tree failed");

	} else {
		die_error(undef, "Unknown commitdiff format");
	}

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^[0-9a-fA-F]{40}$/) {
		$expires = "+1d";
	}

	# write commit message
	if ($format eq 'html') {
		my $refs = git_get_references();
		my $ref = format_ref_marker($refs, $co{'id'});

		git_header_html(undef, $expires);
		git_print_page_nav('commitdiff','', $hash,$co{'tree'},$hash, $formats_nav);
		git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash);
		git_print_authorship(\%co);
		print "<div class=\"page_body\">\n";
		if (@{$co{'comment'}} > 1) {
			print "<div class=\"log\">\n";
			git_print_log($co{'comment'}, -final_empty_line=> 1, -remove_title => 1);
			print "</div>\n"; # class="log"
		}

	} elsif ($format eq 'plain') {
		my $refs = git_get_references("tags");
		my $tagname = git_get_rev_name_tags($hash);
		my $filename = basename($project) . "-$hash.patch";

		print $cgi->header(
			-type => 'text/plain',
			-charset => 'utf-8',
			-expires => $expires,
			-content_disposition => 'inline; filename="' . "$filename" . '"');
		my %ad = parse_date($co{'author_epoch'}, $co{'author_tz'});
		print "From: " . to_utf8($co{'author'}) . "\n";
		print "Date: $ad{'rfc2822'} ($ad{'tz_local'})\n";
		print "Subject: " . to_utf8($co{'title'}) . "\n";

		print "X-Git-Tag: $tagname\n" if $tagname;
		print "X-Git-Url: " . $cgi->self_url() . "\n\n";

		foreach my $line (@{$co{'comment'}}) {
			print to_utf8($line) . "\n";
		}
		print "---\n\n";
	}

	# write patch
	if ($format eq 'html') {
		my $use_parents = !defined $hash_parent ||
			$hash_parent eq '-c' || $hash_parent eq '--cc';
		git_difftree_body(\@difftree, $hash,
		                  $use_parents ? @{$co{'parents'}} : $hash_parent);
		print "<br/>\n";

		git_patchset_body($fd, \@difftree, $hash,
		                  $use_parents ? @{$co{'parents'}} : $hash_parent);
		close $fd;
		print "</div>\n"; # class="page_body"
		git_footer_html();

	} elsif ($format eq 'plain') {
		local $/ = undef;
		print <$fd>;
		close $fd
			or print "Reading git-diff-tree failed\n";
	}
}

sub git_commitdiff_plain {
	git_commitdiff('plain');
}

sub git_history {
	if (!defined $hash_base) {
		$hash_base = git_get_head_hash($project);
	}
	if (!defined $page) {
		$page = 0;
	}
	my $ftype;
	my %co = parse_commit($hash_base);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}

	my $refs = git_get_references();
	my $limit = sprintf("--max-count=%i", (100 * ($page+1)));

	if (!defined $hash && defined $file_name) {
		$hash = git_get_hash_by_path($hash_base, $file_name);
	}
	if (defined $hash) {
		$ftype = git_get_type($hash);
	}

	my @commitlist = parse_commits($hash_base, 101, (100 * $page), $file_name, "--full-history");

	my $paging_nav = '';
	if ($page > 0) {
		$paging_nav .=
			$cgi->a({-href => href(action=>"history", hash=>$hash, hash_base=>$hash_base,
			                       file_name=>$file_name)},
			        "first");
		$paging_nav .= " &sdot; " .
			$cgi->a({-href => href(-replay=>1, page=>$page-1),
			         -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		$paging_nav .= "first";
		$paging_nav .= " &sdot; prev";
	}
	my $next_link = '';
	if ($#commitlist >= 100) {
		$next_link =
			$cgi->a({-href => href(-replay=>1, page=>$page+1),
			         -accesskey => "n", -title => "Alt-n"}, "next");
		$paging_nav .= " &sdot; $next_link";
	} else {
		$paging_nav .= " &sdot; next";
	}

	git_header_html();
	git_print_page_nav('history','', $hash_base,$co{'tree'},$hash_base, $paging_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, $ftype, $hash_base);

	git_history_body(\@commitlist, 0, 99,
	                 $refs, $hash_base, $ftype, $next_link);

	git_footer_html();
}

sub git_search {
	my ($have_search) = gitweb_check_feature('search');
	if (!$have_search) {
		die_error('403 Permission denied', "Permission denied");
	}
	if (!defined $searchtext) {
		die_error(undef, "Text field empty");
	}
	if (!defined $hash) {
		$hash = git_get_head_hash($project);
	}
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(undef, "Unknown commit object");
	}
	if (!defined $page) {
		$page = 0;
	}

	$searchtype ||= 'commit';
	if ($searchtype eq 'pickaxe') {
		# pickaxe may take all resources of your box and run for several minutes
		# with every query - so decide by yourself how public you make this feature
		my ($have_pickaxe) = gitweb_check_feature('pickaxe');
		if (!$have_pickaxe) {
			die_error('403 Permission denied', "Permission denied");
		}
	}
	if ($searchtype eq 'grep') {
		my ($have_grep) = gitweb_check_feature('grep');
		if (!$have_grep) {
			die_error('403 Permission denied', "Permission denied");
		}
	}

	git_header_html();

	if ($searchtype eq 'commit' or $searchtype eq 'author' or $searchtype eq 'committer') {
		my $greptype;
		if ($searchtype eq 'commit') {
			$greptype = "--grep=";
		} elsif ($searchtype eq 'author') {
			$greptype = "--author=";
		} elsif ($searchtype eq 'committer') {
			$greptype = "--committer=";
		}
		$greptype .= $searchtext;
		my @commitlist = parse_commits($hash, 101, (100 * $page), undef,
		                               $greptype, '--regexp-ignore-case',
		                               $search_use_regexp ? '--extended-regexp' : '--fixed-strings');

		my $paging_nav = '';
		if ($page > 0) {
			$paging_nav .=
				$cgi->a({-href => href(action=>"search", hash=>$hash,
				                       searchtext=>$searchtext,
				                       searchtype=>$searchtype)},
				        "first");
			$paging_nav .= " &sdot; " .
				$cgi->a({-href => href(-replay=>1, page=>$page-1),
				         -accesskey => "p", -title => "Alt-p"}, "prev");
		} else {
			$paging_nav .= "first";
			$paging_nav .= " &sdot; prev";
		}
		my $next_link = '';
		if ($#commitlist >= 100) {
			$next_link =
				$cgi->a({-href => href(-replay=>1, page=>$page+1),
				         -accesskey => "n", -title => "Alt-n"}, "next");
			$paging_nav .= " &sdot; $next_link";
		} else {
			$paging_nav .= " &sdot; next";
		}

		if ($#commitlist >= 100) {
		}

		git_print_page_nav('','', $hash,$co{'tree'},$hash, $paging_nav);
		git_print_header_div('commit', esc_html($co{'title'}), $hash);
		git_search_grep_body(\@commitlist, 0, 99, $next_link);
	}

	if ($searchtype eq 'pickaxe') {
		git_print_page_nav('','', $hash,$co{'tree'},$hash);
		git_print_header_div('commit', esc_html($co{'title'}), $hash);

		print "<table class=\"pickaxe search\">\n";
		my $alternate = 1;
		$/ = "\n";
		open my $fd, '-|', git_cmd(), '--no-pager', 'log', @diff_opts,
			'--pretty=format:%H', '--no-abbrev', '--raw', "-S$searchtext",
			($search_use_regexp ? '--pickaxe-regex' : ());
		undef %co;
		my @files;
		while (my $line = <$fd>) {
			chomp $line;
			next unless $line;

			my %set = parse_difftree_raw_line($line);
			if (defined $set{'commit'}) {
				# finish previous commit
				if (%co) {
					print "</td>\n" .
					      "<td class=\"link\">" .
					      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})}, "commit") .
					      " | " .
					      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$co{'id'})}, "tree");
					print "</td>\n" .
					      "</tr>\n";
				}

				if ($alternate) {
					print "<tr class=\"dark\">\n";
				} else {
					print "<tr class=\"light\">\n";
				}
				$alternate ^= 1;
				%co = parse_commit($set{'commit'});
				my $author = chop_and_escape_str($co{'author_name'}, 15, 5);
				print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
				      "<td><i>$author</i></td>\n" .
				      "<td>" .
				      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'}),
				              -class => "list subject"},
				              chop_and_escape_str($co{'title'}, 50) . "<br/>");
			} elsif (defined $set{'to_id'}) {
				next if ($set{'to_id'} =~ m/^0{40}$/);

				print $cgi->a({-href => href(action=>"blob", hash_base=>$co{'id'},
				                             hash=>$set{'to_id'}, file_name=>$set{'to_file'}),
				              -class => "list"},
				              "<span class=\"match\">" . esc_path($set{'file'}) . "</span>") .
				      "<br/>\n";
			}
		}
		close $fd;

		# finish last commit (warning: repetition!)
		if (%co) {
			print "</td>\n" .
			      "<td class=\"link\">" .
			      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})}, "commit") .
			      " | " .
			      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'}, hash_base=>$co{'id'})}, "tree");
			print "</td>\n" .
			      "</tr>\n";
		}

		print "</table>\n";
	}

	if ($searchtype eq 'grep') {
		git_print_page_nav('','', $hash,$co{'tree'},$hash);
		git_print_header_div('commit', esc_html($co{'title'}), $hash);

		print "<table class=\"grep_search\">\n";
		my $alternate = 1;
		my $matches = 0;
		$/ = "\n";
		open my $fd, "-|", git_cmd(), 'grep', '-n',
			$search_use_regexp ? ('-E', '-i') : '-F',
			$searchtext, $co{'tree'};
		my $lastfile = '';
		while (my $line = <$fd>) {
			chomp $line;
			my ($file, $lno, $ltext, $binary);
			last if ($matches++ > 1000);
			if ($line =~ /^Binary file (.+) matches$/) {
				$file = $1;
				$binary = 1;
			} else {
				(undef, $file, $lno, $ltext) = split(/:/, $line, 4);
			}
			if ($file ne $lastfile) {
				$lastfile and print "</td></tr>\n";
				if ($alternate++) {
					print "<tr class=\"dark\">\n";
				} else {
					print "<tr class=\"light\">\n";
				}
				print "<td class=\"list\">".
					$cgi->a({-href => href(action=>"blob", hash=>$co{'hash'},
							       file_name=>"$file"),
						-class => "list"}, esc_path($file));
				print "</td><td>\n";
				$lastfile = $file;
			}
			if ($binary) {
				print "<div class=\"binary\">Binary file</div>\n";
			} else {
				$ltext = untabify($ltext);
				if ($ltext =~ m/^(.*)($search_regexp)(.*)$/i) {
					$ltext = esc_html($1, -nbsp=>1);
					$ltext .= '<span class="match">';
					$ltext .= esc_html($2, -nbsp=>1);
					$ltext .= '</span>';
					$ltext .= esc_html($3, -nbsp=>1);
				} else {
					$ltext = esc_html($ltext, -nbsp=>1);
				}
				print "<div class=\"pre\">" .
					$cgi->a({-href => href(action=>"blob", hash=>$co{'hash'},
							       file_name=>"$file").'#l'.$lno,
						-class => "linenr"}, sprintf('%4i', $lno))
					. ' ' .  $ltext . "</div>\n";
			}
		}
		if ($lastfile) {
			print "</td></tr>\n";
			if ($matches > 1000) {
				print "<div class=\"diff nodifferences\">Too many matches, listing trimmed</div>\n";
			}
		} else {
			print "<div class=\"diff nodifferences\">No matches found</div>\n";
		}
		close $fd;

		print "</table>\n";
	}
	git_footer_html();
}

sub git_search_help {
	git_header_html();
	git_print_page_nav('','', $hash,$hash,$hash);
	print <<EOT;
<p><strong>Pattern</strong> is by default a normal string that is matched precisely (but without
regard to case, except in the case of pickaxe). However, when you check the <em>re</em> checkbox,
the pattern entered is recognized as the POSIX extended
<a href="http://en.wikipedia.org/wiki/Regular_expression">regular expression</a> (also case
insensitive).</p>
<dl>
<dt><b>commit</b></dt>
<dd>The commit messages and authorship information will be scanned for the given pattern.</dd>
EOT
	my ($have_grep) = gitweb_check_feature('grep');
	if ($have_grep) {
		print <<EOT;
<dt><b>grep</b></dt>
<dd>All files in the currently selected tree (HEAD unless you are explicitly browsing
    a different one) are searched for the given pattern. On large trees, this search can take
a while and put some strain on the server, so please use it with some consideration. Note that
due to git-grep peculiarity, currently if regexp mode is turned off, the matches are
case-sensitive.</dd>
EOT
	}
	print <<EOT;
<dt><b>author</b></dt>
<dd>Name and e-mail of the change author and date of birth of the patch will be scanned for the given pattern.</dd>
<dt><b>committer</b></dt>
<dd>Name and e-mail of the committer and date of commit will be scanned for the given pattern.</dd>
EOT
	my ($have_pickaxe) = gitweb_check_feature('pickaxe');
	if ($have_pickaxe) {
		print <<EOT;
<dt><b>pickaxe</b></dt>
<dd>All commits that caused the string to appear or disappear from any file (changes that
added, removed or "modified" the string) will be listed. This search can take a while and
takes a lot of strain on the server, so please use it wisely. Note that since you may be
interested even in changes just changing the case as well, this search is case sensitive.</dd>
EOT
	}
	print "</dl>\n";
	git_footer_html();
}

sub git_shortlog {
	my $head = git_get_head_hash($project);
	if (!defined $hash) {
		$hash = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = git_get_references();

	my @commitlist = parse_commits($hash, 101, (100 * $page));

	my $paging_nav = format_paging_nav('shortlog', $hash, $head, $page, (100 * ($page+1)));
	my $next_link = '';
	if ($#commitlist >= 100) {
		$next_link =
			$cgi->a({-href => href(-replay=>1, page=>$page+1),
			         -accesskey => "n", -title => "Alt-n"}, "next");
	}

	git_header_html();
	git_print_page_nav('shortlog','', $hash,$hash,$hash, $paging_nav);
	git_print_header_div('summary', $project);

	git_shortlog_body(\@commitlist, 0, 99, $refs, $next_link);

	git_footer_html();
}

## ......................................................................
## feeds (RSS, Atom; OPML)

sub git_feed {
	my $format = shift || 'atom';
	my ($have_blame) = gitweb_check_feature('blame');

	# Atom: http://www.atomenabled.org/developers/syndication/
	# RSS:  http://www.notestips.com/80256B3A007F2692/1/NAMO5P9UPQ
	if ($format ne 'rss' && $format ne 'atom') {
		die_error(undef, "Unknown web feed format");
	}

	# log/feed of current (HEAD) branch, log of given branch, history of file/directory
	my $head = $hash || 'HEAD';
	my @commitlist = parse_commits($head, 150, 0, $file_name);

	my %latest_commit;
	my %latest_date;
	my $content_type = "application/$format+xml";
	if (defined $cgi->http('HTTP_ACCEPT') &&
		 $cgi->Accept('text/xml') > $cgi->Accept($content_type)) {
		# browser (feed reader) prefers text/xml
		$content_type = 'text/xml';
	}
	if (defined($commitlist[0])) {
		%latest_commit = %{$commitlist[0]};
		%latest_date   = parse_date($latest_commit{'author_epoch'});
		print $cgi->header(
			-type => $content_type,
			-charset => 'utf-8',
			-last_modified => $latest_date{'rfc2822'});
	} else {
		print $cgi->header(
			-type => $content_type,
			-charset => 'utf-8');
	}

	# Optimization: skip generating the body if client asks only
	# for Last-Modified date.
	return if ($cgi->request_method() eq 'HEAD');

	# header variables
	my $title = "$site_name - $project/$action";
	my $feed_type = 'log';
	if (defined $hash) {
		$title .= " - '$hash'";
		$feed_type = 'branch log';
		if (defined $file_name) {
			$title .= " :: $file_name";
			$feed_type = 'history';
		}
	} elsif (defined $file_name) {
		$title .= " - $file_name";
		$feed_type = 'history';
	}
	$title .= " $feed_type";
	my $descr = git_get_project_description($project);
	if (defined $descr) {
		$descr = esc_html($descr);
	} else {
		$descr = "$project " .
		         ($format eq 'rss' ? 'RSS' : 'Atom') .
		         " feed";
	}
	my $owner = git_get_project_owner($project);
	$owner = esc_html($owner);

	#header
	my $alt_url;
	if (defined $file_name) {
		$alt_url = href(-full=>1, action=>"history", hash=>$hash, file_name=>$file_name);
	} elsif (defined $hash) {
		$alt_url = href(-full=>1, action=>"log", hash=>$hash);
	} else {
		$alt_url = href(-full=>1, action=>"summary");
	}
	print qq!<?xml version="1.0" encoding="utf-8"?>\n!;
	if ($format eq 'rss') {
		print <<XML;
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
<channel>
XML
		print "<title>$title</title>\n" .
		      "<link>$alt_url</link>\n" .
		      "<description>$descr</description>\n" .
		      "<language>en</language>\n";
	} elsif ($format eq 'atom') {
		print <<XML;
<feed xmlns="http://www.w3.org/2005/Atom">
XML
		print "<title>$title</title>\n" .
		      "<subtitle>$descr</subtitle>\n" .
		      '<link rel="alternate" type="text/html" href="' .
		      $alt_url . '" />' . "\n" .
		      '<link rel="self" type="' . $content_type . '" href="' .
		      $cgi->self_url() . '" />' . "\n" .
		      "<id>" . href(-full=>1) . "</id>\n" .
		      # use project owner for feed author
		      "<author><name>$owner</name></author>\n";
		if (defined $favicon) {
			print "<icon>" . esc_url($favicon) . "</icon>\n";
		}
		if (defined $logo_url) {
			# not twice as wide as tall: 72 x 27 pixels
			print "<logo>" . esc_url($logo) . "</logo>\n";
		}
		if (! %latest_date) {
			# dummy date to keep the feed valid until commits trickle in:
			print "<updated>1970-01-01T00:00:00Z</updated>\n";
		} else {
			print "<updated>$latest_date{'iso-8601'}</updated>\n";
		}
	}

	# contents
	for (my $i = 0; $i <= $#commitlist; $i++) {
		my %co = %{$commitlist[$i]};
		my $commit = $co{'id'};
		# we read 150, we always show 30 and the ones more recent than 48 hours
		if (($i >= 20) && ((time - $co{'author_epoch'}) > 48*60*60)) {
			last;
		}
		my %cd = parse_date($co{'author_epoch'});

		# get list of changed files
		open my $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			$co{'parent'} || "--root",
			$co{'id'}, "--", (defined $file_name ? $file_name : ())
			or next;
		my @difftree = map { chomp; $_ } <$fd>;
		close $fd
			or next;

		# print element (entry, item)
		my $co_url = href(-full=>1, action=>"commitdiff", hash=>$commit);
		if ($format eq 'rss') {
			print "<item>\n" .
			      "<title>" . esc_html($co{'title'}) . "</title>\n" .
			      "<author>" . esc_html($co{'author'}) . "</author>\n" .
			      "<pubDate>$cd{'rfc2822'}</pubDate>\n" .
			      "<guid isPermaLink=\"true\">$co_url</guid>\n" .
			      "<link>$co_url</link>\n" .
			      "<description>" . esc_html($co{'title'}) . "</description>\n" .
			      "<content:encoded>" .
			      "<![CDATA[\n";
		} elsif ($format eq 'atom') {
			print "<entry>\n" .
			      "<title type=\"html\">" . esc_html($co{'title'}) . "</title>\n" .
			      "<updated>$cd{'iso-8601'}</updated>\n" .
			      "<author>\n" .
			      "  <name>" . esc_html($co{'author_name'}) . "</name>\n";
			if ($co{'author_email'}) {
				print "  <email>" . esc_html($co{'author_email'}) . "</email>\n";
			}
			print "</author>\n" .
			      # use committer for contributor
			      "<contributor>\n" .
			      "  <name>" . esc_html($co{'committer_name'}) . "</name>\n";
			if ($co{'committer_email'}) {
				print "  <email>" . esc_html($co{'committer_email'}) . "</email>\n";
			}
			print "</contributor>\n" .
			      "<published>$cd{'iso-8601'}</published>\n" .
			      "<link rel=\"alternate\" type=\"text/html\" href=\"$co_url\" />\n" .
			      "<id>$co_url</id>\n" .
			      "<content type=\"xhtml\" xml:base=\"" . esc_url($my_url) . "\">\n" .
			      "<div xmlns=\"http://www.w3.org/1999/xhtml\">\n";
		}
		my $comment = $co{'comment'};
		print "<pre>\n";
		foreach my $line (@$comment) {
			$line = esc_html($line);
			print "$line\n";
		}
		print "</pre><ul>\n";
		foreach my $difftree_line (@difftree) {
			my %difftree = parse_difftree_raw_line($difftree_line);
			next if !$difftree{'from_id'};

			my $file = $difftree{'file'} || $difftree{'to_file'};

			print "<li>" .
			      "[" .
			      $cgi->a({-href => href(-full=>1, action=>"blobdiff",
			                             hash=>$difftree{'to_id'}, hash_parent=>$difftree{'from_id'},
			                             hash_base=>$co{'id'}, hash_parent_base=>$co{'parent'},
			                             file_name=>$file, file_parent=>$difftree{'from_file'}),
			              -title => "diff"}, 'D');
			if ($have_blame) {
				print $cgi->a({-href => href(-full=>1, action=>"blame",
				                             file_name=>$file, hash_base=>$commit),
				              -title => "blame"}, 'B');
			}
			# if this is not a feed of a file history
			if (!defined $file_name || $file_name ne $file) {
				print $cgi->a({-href => href(-full=>1, action=>"history",
				                             file_name=>$file, hash=>$commit),
				              -title => "history"}, 'H');
			}
			$file = esc_path($file);
			print "] ".
			      "$file</li>\n";
		}
		if ($format eq 'rss') {
			print "</ul>]]>\n" .
			      "</content:encoded>\n" .
			      "</item>\n";
		} elsif ($format eq 'atom') {
			print "</ul>\n</div>\n" .
			      "</content>\n" .
			      "</entry>\n";
		}
	}

	# end of feed
	if ($format eq 'rss') {
		print "</channel>\n</rss>\n";
	}	elsif ($format eq 'atom') {
		print "</feed>\n";
	}
}

sub git_rss {
	git_feed('rss');
}

sub git_atom {
	git_feed('atom');
}

sub git_opml {
	my @list = git_get_projects_list();

	print $cgi->header(-type => 'text/xml', -charset => 'utf-8');
	print <<XML;
<?xml version="1.0" encoding="utf-8"?>
<opml version="1.0">
<head>
  <title>$site_name OPML Export</title>
</head>
<body>
<outline text="git RSS feeds">
XML

	foreach my $pr (@list) {
		my %proj = %$pr;
		my $head = git_get_head_hash($proj{'path'});
		if (!defined $head) {
			next;
		}
		$git_dir = "$projectroot/$proj{'path'}";
		my %co = parse_commit($head);
		if (!%co) {
			next;
		}

		my $path = esc_html(chop_str($proj{'path'}, 25, 5));
		my $rss  = "$my_url?p=$proj{'path'};a=rss";
		my $html = "$my_url?p=$proj{'path'};a=summary";
		print "<outline type=\"rss\" text=\"$path\" title=\"$path\" xmlUrl=\"$rss\" htmlUrl=\"$html\"/>\n";
	}
	print <<XML;
</outline>
</body>
</opml>
XML
}
