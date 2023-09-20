#!/usr/bin/perl

# gitweb - simple web interface to track changes in git repositories
#
# (C) 2005-2006, Kay Sievers <kay.sievers@vrfy.org>
# (C) 2005, Christian Gierke
#
# This program is licensed under the GPLv2

use 5.008;
use strict;
use warnings;
# handle ACL in file access tests
use filetest 'access';
use CGI qw(:standard :escapeHTML -nosticky);
use CGI::Util qw(unescape);
use CGI::Carp qw(fatalsToBrowser set_message);
use Encode;
use Fcntl ':mode';
use File::Find qw();
use File::Basename qw(basename);
use Time::HiRes qw(gettimeofday tv_interval);
use Digest::MD5 qw(md5_hex);

binmode STDOUT, ':utf8';

if (!defined($CGI::VERSION) || $CGI::VERSION < 4.08) {
	eval 'sub CGI::multi_param { CGI::param(@_) }'
}

our $t0 = [ gettimeofday() ];
our $number_of_git_cmds = 0;

BEGIN {
	CGI->compile() if $ENV{'MOD_PERL'};
}

our $version = "++GIT_VERSION++";

our ($my_url, $my_uri, $base_url, $path_info, $home_link);
sub evaluate_uri {
	our $cgi;

	our $my_url = $cgi->url();
	our $my_uri = $cgi->url(-absolute => 1);

	# Base URL for relative URLs in gitweb ($logo, $favicon, ...),
	# needed and used only for URLs with nonempty PATH_INFO
	our $base_url = $my_url;

	# When the script is used as DirectoryIndex, the URL does not contain the name
	# of the script file itself, and $cgi->url() fails to strip PATH_INFO, so we
	# have to do it ourselves. We make $path_info global because it's also used
	# later on.
	#
	# Another issue with the script being the DirectoryIndex is that the resulting
	# $my_url data is not the full script URL: this is good, because we want
	# generated links to keep implying the script name if it wasn't explicitly
	# indicated in the URL we're handling, but it means that $my_url cannot be used
	# as base URL.
	# Therefore, if we needed to strip PATH_INFO, then we know that we have
	# to build the base URL ourselves:
	our $path_info = decode_utf8($ENV{"PATH_INFO"});
	if ($path_info) {
		# $path_info has already been URL-decoded by the web server, but
		# $my_url and $my_uri have not. URL-decode them so we can properly
		# strip $path_info.
		$my_url = unescape($my_url);
		$my_uri = unescape($my_uri);
		if ($my_url =~ s,\Q$path_info\E$,, &&
		    $my_uri =~ s,\Q$path_info\E$,, &&
		    defined $ENV{'SCRIPT_NAME'}) {
			$base_url = $cgi->url(-base => 1) . $ENV{'SCRIPT_NAME'};
		}
	}

	# target of the home link on top of all pages
	our $home_link = $my_uri || "/";
}

# core git executable to use
# this can just be "git" if your webserver has a sensible PATH
our $GIT = "++GIT_BINDIR++/git";

# absolute fs-path which will be prepended to the project path
#our $projectroot = "/pub/scm";
our $projectroot = "++GITWEB_PROJECTROOT++";

# fs traversing limit for getting project list
# the number is relative to the projectroot
our $project_maxdepth = "++GITWEB_PROJECT_MAXDEPTH++";

# string of the home link on top of all pages
our $home_link_str = "++GITWEB_HOME_LINK_STR++";

# extra breadcrumbs preceding the home link
our @extra_breadcrumbs = ();

# name of your site or organization to appear in page titles
# replace this with something more descriptive for clearer bookmarks
our $site_name = "++GITWEB_SITENAME++"
                 || ($ENV{'SERVER_NAME'} || "Untitled") . " Git";

# html snippet to include in the <head> section of each page
our $site_html_head_string = "++GITWEB_SITE_HTML_HEAD_STRING++";
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
# URI of gitweb.js (JavaScript code for gitweb)
our $javascript = "++GITWEB_JS++";

# URI and label (title) of GIT logo link
#our $logo_url = "https://www.kernel.org/pub/software/scm/git/docs/";
#our $logo_label = "git documentation";
our $logo_url = "https://git-scm.com/";
our $logo_label = "git homepage";

# source of projects list
our $projects_list = "++GITWEB_LIST++";

# the width (in characters) of the projects list "Description" column
our $projects_list_description_width = 25;

# group projects by category on the projects list
# (enabled if this variable evaluates to true)
our $projects_list_group_categories = 0;

# default category if none specified
# (leave the empty string for no category)
our $project_list_default_category = "";

# default order of projects list
# valid values are none, project, descr, owner, and age
our $default_projects_order = "project";

# show repository only if this file exists
# (only effective if this variable evaluates to true)
our $export_ok = "++GITWEB_EXPORT_OK++";

# don't generate age column on the projects list page
our $omit_age_column = 0;

# don't generate information about owners of repositories
our $omit_owner=0;

# show repository only if this subroutine returns true
# when given the path to the project, for example:
#    sub { return -e "$_[0]/git-daemon-export-ok"; }
our $export_auth_hook = undef;

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

# Disables features that would allow repository owners to inject script into
# the gitweb domain.
our $prevent_xss = 0;

# Path to the highlight executable to use (must be the one from
# http://andre-simon.de/zip/download.php due to assumptions about parameters and output).
# Useful if highlight is not installed on your webserver's PATH.
# [Default: highlight]
our $highlight_bin = "++HIGHLIGHT_BIN++";

# information about snapshot formats that gitweb is capable of serving
our %known_snapshot_formats = (
	# name => {
	# 	'display' => display name,
	# 	'type' => mime type,
	# 	'suffix' => filename suffix,
	# 	'format' => --format for git-archive,
	# 	'compressor' => [compressor command and arguments]
	# 	                (array reference, optional)
	# 	'disabled' => boolean (optional)}
	#
	'tgz' => {
		'display' => 'tar.gz',
		'type' => 'application/x-gzip',
		'suffix' => '.tar.gz',
		'format' => 'tar',
		'compressor' => ['gzip', '-n']},

	'tbz2' => {
		'display' => 'tar.bz2',
		'type' => 'application/x-bzip2',
		'suffix' => '.tar.bz2',
		'format' => 'tar',
		'compressor' => ['bzip2']},

	'txz' => {
		'display' => 'tar.xz',
		'type' => 'application/x-xz',
		'suffix' => '.tar.xz',
		'format' => 'tar',
		'compressor' => ['xz'],
		'disabled' => 1},

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
	'xz'    => 'txz',

	# backward compatibility: legacy gitweb config support
	'x-gzip' => undef, 'gz' => undef,
	'x-bzip2' => undef, 'bz2' => undef,
	'x-zip' => undef, '' => undef,
);

# Pixel sizes for icons and avatars. If the default font sizes or lineheights
# are changed, it may be appropriate to change these values too via
# $GITWEB_CONFIG.
our %avatar_size = (
	'default' => 16,
	'double'  => 32
);

# Used to set the maximum load that we will still respond to gitweb queries.
# If server load exceed this value then return "503 server busy" error.
# If gitweb cannot determined server load, it is taken to be 0.
# Leave it undefined (or set to 'undef') to turn off load checking.
our $maxload = 300;

# configuration for 'highlight' (http://andre-simon.de/doku/highlight/en/highlight.php)
# match by basename
our %highlight_basename = (
	#'Program' => 'py',
	#'Library' => 'py',
	'SConstruct' => 'py', # SCons equivalent of Makefile
	'Makefile' => 'make',
);
# match by extension
our %highlight_ext = (
	# main extensions, defining name of syntax;
	# see files in /usr/share/highlight/langDefs/ directory
	(map { $_ => $_ } qw(py rb java css js tex bib xml awk bat ini spec tcl sql)),
	# alternate extensions, see /etc/highlight/filetypes.conf
	(map { $_ => 'c'   } qw(c h)),
	(map { $_ => 'sh'  } qw(sh bash zsh ksh)),
	(map { $_ => 'cpp' } qw(cpp cxx c++ cc)),
	(map { $_ => 'php' } qw(php php3 php4 php5 phps)),
	(map { $_ => 'pl'  } qw(pl perl pm)), # perhaps also 'cgi'
	(map { $_ => 'make'} qw(make mak mk)),
	(map { $_ => 'xml' } qw(xml xhtml html htm)),
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
	# overridden
	#
	# use gitweb_get_feature(<feature>) to retrieve the <feature> value
	# (an array) or gitweb_check_feature(<feature>) to check if <feature>
	# is enabled

	# Enable the 'blame' blob view, showing the last commit that modified
	# each line in the file. This can be very CPU-intensive.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'blame'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'blame'}{'override'} = 1;
	# and in project config gitweb.blame = 0|1;
	'blame' => {
		'sub' => sub { feature_bool('blame', @_) },
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
	#
	# Note that this controls all search features, which means that if
	# it is disabled, then 'grep' and 'pickaxe' search would also be
	# disabled.
	'search' => {
		'override' => 0,
		'default' => [1]},

	# Enable grep search, which will list the files in currently selected
	# tree containing the given string. Enabled by default. This can be
	# potentially CPU-intensive, of course.
	# Note that you need to have 'search' feature enabled too.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'grep'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'grep'}{'override'} = 1;
	# and in project config gitweb.grep = 0|1;
	'grep' => {
		'sub' => sub { feature_bool('grep', @_) },
		'override' => 0,
		'default' => [1]},

	# Enable the pickaxe search, which will list the commits that modified
	# a given string in a file. This can be practical and quite faster
	# alternative to 'blame', but still potentially CPU-intensive.
	# Note that you need to have 'search' feature enabled too.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'pickaxe'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'pickaxe'}{'override'} = 1;
	# and in project config gitweb.pickaxe = 0|1;
	'pickaxe' => {
		'sub' => sub { feature_bool('pickaxe', @_) },
		'override' => 0,
		'default' => [1]},

	# Enable showing size of blobs in a 'tree' view, in a separate
	# column, similar to what 'ls -l' does.  This cost a bit of IO.

	# To disable system wide have in $GITWEB_CONFIG
	# $feature{'show-sizes'}{'default'} = [0];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'show-sizes'}{'override'} = 1;
	# and in project config gitweb.showsizes = 0|1;
	'show-sizes' => {
		'sub' => sub { feature_bool('showsizes', @_) },
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

	# Insert custom links to the action bar of all project pages.
	# This enables you mainly to link to third-party scripts integrating
	# into gitweb; e.g. git-browser for graphical history representation
	# or custom web-based repository administration interface.

	# The 'default' value consists of a list of triplets in the form
	# (label, link, position) where position is the label after which
	# to insert the link and link is a format string where %n expands
	# to the project name, %f to the project path within the filesystem,
	# %h to the current hash (h gitweb parameter) and %b to the current
	# hash base (hb gitweb parameter); %% expands to %.

	# To enable system wide have in $GITWEB_CONFIG e.g.
	# $feature{'actions'}{'default'} = [('graphiclog',
	# 	'/git-browser/by-commit.html?r=%n', 'summary')];
	# Project specific override is not supported.
	'actions' => {
		'override' => 0,
		'default' => []},

	# Allow gitweb scan project content tags of project repository,
	# and display the popular Web 2.0-ish "tag cloud" near the projects
	# list.  Note that this is something COMPLETELY different from the
	# normal Git tags.

	# gitweb by itself can show existing tags, but it does not handle
	# tagging itself; you need to do it externally, outside gitweb.
	# The format is described in git_get_project_ctags() subroutine.
	# You may want to install the HTML::TagCloud Perl module to get
	# a pretty tag cloud instead of just a list of tags.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'ctags'}{'default'} = [1];
	# Project specific override is not supported.

	# In the future whether ctags editing is enabled might depend
	# on the value, but using 1 should always mean no editing of ctags.
	'ctags' => {
		'override' => 0,
		'default' => [0]},

	# The maximum number of patches in a patchset generated in patch
	# view. Set this to 0 or undef to disable patch view, or to a
	# negative number to remove any limit.

	# To disable system wide have in $GITWEB_CONFIG
	# $feature{'patches'}{'default'} = [0];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'patches'}{'override'} = 1;
	# and in project config gitweb.patches = 0|n;
	# where n is the maximum number of patches allowed in a patchset.
	'patches' => {
		'sub' => \&feature_patches,
		'override' => 0,
		'default' => [16]},

	# Avatar support. When this feature is enabled, views such as
	# shortlog or commit will display an avatar associated with
	# the email of the committer(s) and/or author(s).

	# Currently available providers are gravatar and picon.
	# If an unknown provider is specified, the feature is disabled.

	# Picon currently relies on the indiana.edu database.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'avatar'}{'default'} = ['<provider>'];
	# where <provider> is either gravatar or picon.
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'avatar'}{'override'} = 1;
	# and in project config gitweb.avatar = <provider>;
	'avatar' => {
		'sub' => \&feature_avatar,
		'override' => 0,
		'default' => ['']},

	# Enable displaying how much time and how many git commands
	# it took to generate and display page.  Disabled by default.
	# Project specific override is not supported.
	'timed' => {
		'override' => 0,
		'default' => [0]},

	# Enable turning some links into links to actions which require
	# JavaScript to run (like 'blame_incremental').  Not enabled by
	# default.  Project specific override is currently not supported.
	'javascript-actions' => {
		'override' => 0,
		'default' => [0]},

	# Enable and configure ability to change common timezone for dates
	# in gitweb output via JavaScript.  Enabled by default.
	# Project specific override is not supported.
	'javascript-timezone' => {
		'override' => 0,
		'default' => [
			'local',     # default timezone: 'utc', 'local', or '(-|+)HHMM' format,
			             # or undef to turn off this feature
			'gitweb_tz', # name of cookie where to store selected timezone
			'datetime',  # CSS class used to mark up dates for manipulation
		]},

	# Syntax highlighting support. This is based on Daniel Svensson's
	# and Sham Chukoury's work in gitweb-xmms2.git.
	# It requires the 'highlight' program present in $PATH,
	# and therefore is disabled by default.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'highlight'}{'default'} = [1];

	'highlight' => {
		'sub' => sub { feature_bool('highlight', @_) },
		'override' => 0,
		'default' => [0]},

	# Enable displaying of remote heads in the heads list

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'remote_heads'}{'default'} = [1];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'remote_heads'}{'override'} = 1;
	# and in project config gitweb.remoteheads = 0|1;
	'remote_heads' => {
		'sub' => sub { feature_bool('remote_heads', @_) },
		'override' => 0,
		'default' => [0]},

	# Enable showing branches under other refs in addition to heads

	# To set system wide extra branch refs have in $GITWEB_CONFIG
	# $feature{'extra-branch-refs'}{'default'} = ['dirs', 'of', 'choice'];
	# To have project specific config enable override in $GITWEB_CONFIG
	# $feature{'extra-branch-refs'}{'override'} = 1;
	# and in project config gitweb.extrabranchrefs = dirs of choice
	# Every directory is separated with whitespace.

	'extra-branch-refs' => {
		'sub' => \&feature_extra_branch_refs,
		'override' => 0,
		'default' => []},

	# Redact e-mail addresses.

	# To enable system wide have in $GITWEB_CONFIG
	# $feature{'email-privacy'}{'default'} = [1];
	'email-privacy' => {
		'sub' => sub { feature_bool('email-privacy', @_) },
		'override' => 1,
		'default' => [0]},
);

sub gitweb_get_feature {
	my ($name) = @_;
	return unless exists $feature{$name};
	my ($sub, $override, @defaults) = (
		$feature{$name}{'sub'},
		$feature{$name}{'override'},
		@{$feature{$name}{'default'}});
	# project specific override is possible only if we have project
	our $git_dir; # global variable, declared later
	if (!$override || !defined $git_dir) {
		return @defaults;
	}
	if (!defined $sub) {
		warn "feature $name is not overridable";
		return @defaults;
	}
	return $sub->(@defaults);
}

# A wrapper to check if a given feature is enabled.
# With this, you can say
#
#   my $bool_feat = gitweb_check_feature('bool_feat');
#   gitweb_check_feature('bool_feat') or somecode;
#
# instead of
#
#   my ($bool_feat) = gitweb_get_feature('bool_feat');
#   (gitweb_get_feature('bool_feat'))[0] or somecode;
#
sub gitweb_check_feature {
	return (gitweb_get_feature(@_))[0];
}


sub feature_bool {
	my $key = shift;
	my ($val) = git_get_project_config($key, '--bool');

	if (!defined $val) {
		return ($_[0]);
	} elsif ($val eq 'true') {
		return (1);
	} elsif ($val eq 'false') {
		return (0);
	}
}

sub feature_snapshot {
	my (@fmts) = @_;

	my ($val) = git_get_project_config('snapshot');

	if ($val) {
		@fmts = ($val eq 'none' ? () : split /\s*[,\s]\s*/, $val);
	}

	return @fmts;
}

sub feature_patches {
	my @val = (git_get_project_config('patches', '--int'));

	if (@val) {
		return @val;
	}

	return ($_[0]);
}

sub feature_avatar {
	my @val = (git_get_project_config('avatar'));

	return @val ? @val : @_;
}

sub feature_extra_branch_refs {
	my (@branch_refs) = @_;
	my $values = git_get_project_config('extrabranchrefs');

	if ($values) {
		$values = config_to_multi ($values);
		@branch_refs = ();
		foreach my $value (@{$values}) {
			push @branch_refs, split /\s+/, $value;
		}
	}

	return @branch_refs;
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
		(!$export_ok || -e "$dir/$export_ok") &&
		(!$export_auth_hook || $export_auth_hook->($dir)));
}

# process alternate names for backward compatibility
# filter out unsupported (unknown) snapshot formats
sub filter_snapshot_fmts {
	my @fmts = @_;

	@fmts = map {
		exists $known_snapshot_format_aliases{$_} ?
		       $known_snapshot_format_aliases{$_} : $_} @fmts;
	@fmts = grep {
		exists $known_snapshot_formats{$_} &&
		!$known_snapshot_formats{$_}{'disabled'}} @fmts;
}

sub filter_and_validate_refs {
	my @refs = @_;
	my %unique_refs = ();

	foreach my $ref (@refs) {
		die_error(500, "Invalid ref '$ref' in 'extra-branch-refs' feature") unless (is_valid_ref_format($ref));
		# 'heads' are added implicitly in get_branch_refs().
		$unique_refs{$ref} = 1 if ($ref ne 'heads');
	}
	return sort keys %unique_refs;
}

# If it is set to code reference, it is code that it is to be run once per
# request, allowing updating configurations that change with each request,
# while running other code in config file only once.
#
# Otherwise, if it is false then gitweb would process config file only once;
# if it is true then gitweb config would be run for each request.
our $per_request_config = 1;

# read and parse gitweb config file given by its parameter.
# returns true on success, false on recoverable error, allowing
# to chain this subroutine, using first file that exists.
# dies on errors during parsing config file, as it is unrecoverable.
sub read_config_file {
	my $filename = shift;
	return unless defined $filename;
	# die if there are errors parsing config file
	if (-e $filename) {
		do $filename;
		die $@ if $@;
		return 1;
	}
	return;
}

our ($GITWEB_CONFIG, $GITWEB_CONFIG_SYSTEM, $GITWEB_CONFIG_COMMON);
sub evaluate_gitweb_config {
	our $GITWEB_CONFIG = $ENV{'GITWEB_CONFIG'} || "++GITWEB_CONFIG++";
	our $GITWEB_CONFIG_SYSTEM = $ENV{'GITWEB_CONFIG_SYSTEM'} || "++GITWEB_CONFIG_SYSTEM++";
	our $GITWEB_CONFIG_COMMON = $ENV{'GITWEB_CONFIG_COMMON'} || "++GITWEB_CONFIG_COMMON++";

	# Protect against duplications of file names, to not read config twice.
	# Only one of $GITWEB_CONFIG and $GITWEB_CONFIG_SYSTEM is used, so
	# there possibility of duplication of filename there doesn't matter.
	$GITWEB_CONFIG = ""        if ($GITWEB_CONFIG eq $GITWEB_CONFIG_COMMON);
	$GITWEB_CONFIG_SYSTEM = "" if ($GITWEB_CONFIG_SYSTEM eq $GITWEB_CONFIG_COMMON);

	# Common system-wide settings for convenience.
	# Those settings can be overridden by GITWEB_CONFIG or GITWEB_CONFIG_SYSTEM.
	read_config_file($GITWEB_CONFIG_COMMON);

	# Use first config file that exists.  This means use the per-instance
	# GITWEB_CONFIG if exists, otherwise use GITWEB_SYSTEM_CONFIG.
	read_config_file($GITWEB_CONFIG) and return;
	read_config_file($GITWEB_CONFIG_SYSTEM);
}

# Get loadavg of system, to compare against $maxload.
# Currently it requires '/proc/loadavg' present to get loadavg;
# if it is not present it returns 0, which means no load checking.
sub get_loadavg {
	if( -e '/proc/loadavg' ){
		open my $fd, '<', '/proc/loadavg'
			or return 0;
		my @load = split(/\s+/, scalar <$fd>);
		close $fd;

		# The first three columns measure CPU and IO utilization of the last one,
		# five, and 10 minute periods.  The fourth column shows the number of
		# currently running processes and the total number of processes in the m/n
		# format.  The last column displays the last process ID used.
		return $load[0] || 0;
	}
	# additional checks for load average should go here for things that don't export
	# /proc/loadavg

	return 0;
}

# version of the core git binary
our $git_version;
sub evaluate_git_version {
	our $git_version = qx("$GIT" --version) =~ m/git version (.*)$/ ? $1 : "unknown";
	$number_of_git_cmds++;
}

sub check_loadavg {
	if (defined $maxload && get_loadavg() > $maxload) {
		die_error(503, "The load average on the server is too high");
	}
}

# ======================================================================
# input validation and dispatch

# Various hash size-related values.
my $sha1_len = 40;
my $sha256_extra_len = 24;
my $sha256_len = $sha1_len + $sha256_extra_len;

# A regex matching $len hex characters. $len may be a range (e.g. 7,64).
sub oid_nlen_regex {
	my $len = shift;
	my $hchr = qr/[0-9a-fA-F]/;
	return qr/(?:(?:$hchr){$len})/;
}

# A regex matching two sets of $nlen hex characters, prefixed by the literal
# string $prefix and with the literal string $infix between them.
sub oid_nlen_prefix_infix_regex {
	my $nlen = shift;
	my $prefix = shift;
	my $infix = shift;

	my $rx = oid_nlen_regex($nlen);

	return qr/^\Q$prefix\E$rx\Q$infix\E$rx$/;
}

# A regex matching a valid object ID.
our $oid_regex;
{
	my $x = oid_nlen_regex($sha1_len);
	my $y = oid_nlen_regex($sha256_extra_len);
	$oid_regex = qr/(?:$x(?:$y)?)/;
}

# input parameters can be collected from a variety of sources (presently, CGI
# and PATH_INFO), so we define an %input_params hash that collects them all
# together during validation: this allows subsequent uses (e.g. href()) to be
# agnostic of the parameter origin

our %input_params = ();

# input parameters are stored with the long parameter name as key. This will
# also be used in the href subroutine to convert parameters to their CGI
# equivalent, and since the href() usage is the most frequent one, we store
# the name -> CGI key mapping here, instead of the reverse.
#
# XXX: Warning: If you touch this, check the search form for updating,
# too.

our @cgi_param_mapping = (
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
	ctag => "by_tag",
	diff_style => "ds",
	project_filter => "pf",
	# this must be last entry (for manipulation from JavaScript)
	javascript => "js"
);
our %cgi_param_mapping = @cgi_param_mapping;

# we will also need to know the possible actions, for validation
our %actions = (
	"blame" => \&git_blame,
	"blame_incremental" => \&git_blame_incremental,
	"blame_data" => \&git_blame_data,
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
	"patch" => \&git_patch,
	"patches" => \&git_patches,
	"remotes" => \&git_remotes,
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

# finally, we have the hash of allowed extra_options for the commands that
# allow them
our %allowed_options = (
	"--no-merges" => [ qw(rss atom log shortlog history) ],
);

# fill %input_params with the CGI parameters. All values except for 'opt'
# should be single values, but opt can be an array. We should probably
# build an array of parameters that can be multi-valued, but since for the time
# being it's only this one, we just single it out
sub evaluate_query_params {
	our $cgi;

	while (my ($name, $symbol) = each %cgi_param_mapping) {
		if ($symbol eq 'opt') {
			$input_params{$name} = [ map { decode_utf8($_) } $cgi->multi_param($symbol) ];
		} else {
			$input_params{$name} = decode_utf8($cgi->param($symbol));
		}
	}
}

# now read PATH_INFO and update the parameter list for missing parameters
sub evaluate_path_info {
	return if defined $input_params{'project'};
	return if !$path_info;
	$path_info =~ s,^/+,,;
	return if !$path_info;

	# find which part of PATH_INFO is project
	my $project = $path_info;
	$project =~ s,/+$,,;
	while ($project && !check_head_link("$projectroot/$project")) {
		$project =~ s,/*[^/]*$,,;
	}
	return unless $project;
	$input_params{'project'} = $project;

	# do not change any parameters if an action is given using the query string
	return if $input_params{'action'};
	$path_info =~ s,^\Q$project\E/*,,;

	# next, check if we have an action
	my $action = $path_info;
	$action =~ s,/.*$,,;
	if (exists $actions{$action}) {
		$path_info =~ s,^$action/*,,;
		$input_params{'action'} = $action;
	}

	# list of actions that want hash_base instead of hash, but can have no
	# pathname (f) parameter
	my @wants_base = (
		'tree',
		'history',
	);

	# we want to catch, among others
	# [$hash_parent_base[:$file_parent]..]$hash_parent[:$file_name]
	my ($parentrefname, $parentpathname, $refname, $pathname) =
		($path_info =~ /^(?:(.+?)(?::(.+))?\.\.)?([^:]+?)?(?::(.+))?$/);

	# first, analyze the 'current' part
	if (defined $pathname) {
		# we got "branch:filename" or "branch:dir/"
		# we could use git_get_type(branch:pathname), but:
		# - it needs $git_dir
		# - it does a git() call
		# - the convention of terminating directories with a slash
		#   makes it superfluous
		# - embedding the action in the PATH_INFO would make it even
		#   more superfluous
		$pathname =~ s,^/+,,;
		if (!$pathname || substr($pathname, -1) eq "/") {
			$input_params{'action'} ||= "tree";
			$pathname =~ s,/$,,;
		} else {
			# the default action depends on whether we had parent info
			# or not
			if ($parentrefname) {
				$input_params{'action'} ||= "blobdiff_plain";
			} else {
				$input_params{'action'} ||= "blob_plain";
			}
		}
		$input_params{'hash_base'} ||= $refname;
		$input_params{'file_name'} ||= $pathname;
	} elsif (defined $refname) {
		# we got "branch". In this case we have to choose if we have to
		# set hash or hash_base.
		#
		# Most of the actions without a pathname only want hash to be
		# set, except for the ones specified in @wants_base that want
		# hash_base instead. It should also be noted that hand-crafted
		# links having 'history' as an action and no pathname or hash
		# set will fail, but that happens regardless of PATH_INFO.
		if (defined $parentrefname) {
			# if there is parent let the default be 'shortlog' action
			# (for http://git.example.com/repo.git/A..B links); if there
			# is no parent, dispatch will detect type of object and set
			# action appropriately if required (if action is not set)
			$input_params{'action'} ||= "shortlog";
		}
		if ($input_params{'action'} &&
		    grep { $_ eq $input_params{'action'} } @wants_base) {
			$input_params{'hash_base'} ||= $refname;
		} else {
			$input_params{'hash'} ||= $refname;
		}
	}

	# next, handle the 'parent' part, if present
	if (defined $parentrefname) {
		# a missing pathspec defaults to the 'current' filename, allowing e.g.
		# someproject/blobdiff/oldrev..newrev:/filename
		if ($parentpathname) {
			$parentpathname =~ s,^/+,,;
			$parentpathname =~ s,/$,,;
			$input_params{'file_parent'} ||= $parentpathname;
		} else {
			$input_params{'file_parent'} ||= $input_params{'file_name'};
		}
		# we assume that hash_parent_base is wanted if a path was specified,
		# or if the action wants hash_base instead of hash
		if (defined $input_params{'file_parent'} ||
			grep { $_ eq $input_params{'action'} } @wants_base) {
			$input_params{'hash_parent_base'} ||= $parentrefname;
		} else {
			$input_params{'hash_parent'} ||= $parentrefname;
		}
	}

	# for the snapshot action, we allow URLs in the form
	# $project/snapshot/$hash.ext
	# where .ext determines the snapshot and gets removed from the
	# passed $refname to provide the $hash.
	#
	# To be able to tell that $refname includes the format extension, we
	# require the following two conditions to be satisfied:
	# - the hash input parameter MUST have been set from the $refname part
	#   of the URL (i.e. they must be equal)
	# - the snapshot format MUST NOT have been defined already (e.g. from
	#   CGI parameter sf)
	# It's also useless to try any matching unless $refname has a dot,
	# so we check for that too
	if (defined $input_params{'action'} &&
		$input_params{'action'} eq 'snapshot' &&
		defined $refname && index($refname, '.') != -1 &&
		$refname eq $input_params{'hash'} &&
		!defined $input_params{'snapshot_format'}) {
		# We loop over the known snapshot formats, checking for
		# extensions. Allowed extensions are both the defined suffix
		# (which includes the initial dot already) and the snapshot
		# format key itself, with a prepended dot
		while (my ($fmt, $opt) = each %known_snapshot_formats) {
			my $hash = $refname;
			unless ($hash =~ s/(\Q$opt->{'suffix'}\E|\Q.$fmt\E)$//) {
				next;
			}
			my $sfx = $1;
			# a valid suffix was found, so set the snapshot format
			# and reset the hash parameter
			$input_params{'snapshot_format'} = $fmt;
			$input_params{'hash'} = $hash;
			# we also set the format suffix to the one requested
			# in the URL: this way a request for e.g. .tgz returns
			# a .tgz instead of a .tar.gz
			$known_snapshot_formats{$fmt}{'suffix'} = $sfx;
			last;
		}
	}
}

our ($action, $project, $file_name, $file_parent, $hash, $hash_parent, $hash_base,
     $hash_parent_base, @extra_options, $page, $searchtype, $search_use_regexp,
     $searchtext, $search_regexp, $project_filter);
sub evaluate_and_validate_params {
	our $action = $input_params{'action'};
	if (defined $action) {
		if (!is_valid_action($action)) {
			die_error(400, "Invalid action parameter");
		}
	}

	# parameters which are pathnames
	our $project = $input_params{'project'};
	if (defined $project) {
		if (!is_valid_project($project)) {
			undef $project;
			die_error(404, "No such project");
		}
	}

	our $project_filter = $input_params{'project_filter'};
	if (defined $project_filter) {
		if (!is_valid_pathname($project_filter)) {
			die_error(404, "Invalid project_filter parameter");
		}
	}

	our $file_name = $input_params{'file_name'};
	if (defined $file_name) {
		if (!is_valid_pathname($file_name)) {
			die_error(400, "Invalid file parameter");
		}
	}

	our $file_parent = $input_params{'file_parent'};
	if (defined $file_parent) {
		if (!is_valid_pathname($file_parent)) {
			die_error(400, "Invalid file parent parameter");
		}
	}

	# parameters which are refnames
	our $hash = $input_params{'hash'};
	if (defined $hash) {
		if (!is_valid_refname($hash)) {
			die_error(400, "Invalid hash parameter");
		}
	}

	our $hash_parent = $input_params{'hash_parent'};
	if (defined $hash_parent) {
		if (!is_valid_refname($hash_parent)) {
			die_error(400, "Invalid hash parent parameter");
		}
	}

	our $hash_base = $input_params{'hash_base'};
	if (defined $hash_base) {
		if (!is_valid_refname($hash_base)) {
			die_error(400, "Invalid hash base parameter");
		}
	}

	our @extra_options = @{$input_params{'extra_options'}};
	# @extra_options is always defined, since it can only be (currently) set from
	# CGI, and $cgi->param() returns the empty array in array context if the param
	# is not set
	foreach my $opt (@extra_options) {
		if (not exists $allowed_options{$opt}) {
			die_error(400, "Invalid option parameter");
		}
		if (not grep(/^$action$/, @{$allowed_options{$opt}})) {
			die_error(400, "Invalid option parameter for this action");
		}
	}

	our $hash_parent_base = $input_params{'hash_parent_base'};
	if (defined $hash_parent_base) {
		if (!is_valid_refname($hash_parent_base)) {
			die_error(400, "Invalid hash parent base parameter");
		}
	}

	# other parameters
	our $page = $input_params{'page'};
	if (defined $page) {
		if ($page =~ m/[^0-9]/) {
			die_error(400, "Invalid page parameter");
		}
	}

	our $searchtype = $input_params{'searchtype'};
	if (defined $searchtype) {
		if ($searchtype =~ m/[^a-z]/) {
			die_error(400, "Invalid searchtype parameter");
		}
	}

	our $search_use_regexp = $input_params{'search_use_regexp'};

	our $searchtext = $input_params{'searchtext'};
	our $search_regexp = undef;
	if (defined $searchtext) {
		if (length($searchtext) < 2) {
			die_error(403, "At least two characters are required for search parameter");
		}
		if ($search_use_regexp) {
			$search_regexp = $searchtext;
			if (!eval { qr/$search_regexp/; 1; }) {
				(my $error = $@) =~ s/ at \S+ line \d+.*\n?//;
				die_error(400, "Invalid search regexp '$search_regexp'",
				          esc_html($error));
			}
		} else {
			$search_regexp = quotemeta $searchtext;
		}
	}
}

# path to the current git repository
our $git_dir;
sub evaluate_git_dir {
	our $git_dir = "$projectroot/$project" if $project;
}

our (@snapshot_fmts, $git_avatar, @extra_branch_refs);
sub configure_gitweb_features {
	# list of supported snapshot formats
	our @snapshot_fmts = gitweb_get_feature('snapshot');
	@snapshot_fmts = filter_snapshot_fmts(@snapshot_fmts);

	our ($git_avatar) = gitweb_get_feature('avatar');
	$git_avatar = '' unless $git_avatar =~ /^(?:gravatar|picon)$/s;

	our @extra_branch_refs = gitweb_get_feature('extra-branch-refs');
	@extra_branch_refs = filter_and_validate_refs (@extra_branch_refs);
}

sub get_branch_refs {
	return ('heads', @extra_branch_refs);
}

# custom error handler: 'die <message>' is Internal Server Error
sub handle_errors_html {
	my $msg = shift; # it is already HTML escaped

	# to avoid infinite loop where error occurs in die_error,
	# change handler to default handler, disabling handle_errors_html
	set_message("Error occurred when inside die_error:\n$msg");

	# you cannot jump out of die_error when called as error handler;
	# the subroutine set via CGI::Carp::set_message is called _after_
	# HTTP headers are already written, so it cannot write them itself
	die_error(undef, undef, $msg, -error_handler => 1, -no_http_header => 1);
}
set_message(\&handle_errors_html);

# dispatch
sub dispatch {
	if (!defined $action) {
		if (defined $hash) {
			$action = git_get_type($hash);
			$action or die_error(404, "Object does not exist");
		} elsif (defined $hash_base && defined $file_name) {
			$action = git_get_type("$hash_base:$file_name");
			$action or die_error(404, "File or directory does not exist");
		} elsif (defined $project) {
			$action = 'summary';
		} else {
			$action = 'project_list';
		}
	}
	if (!defined($actions{$action})) {
		die_error(400, "Unknown action");
	}
	if ($action !~ m/^(?:opml|project_list|project_index)$/ &&
	    !$project) {
		die_error(400, "Project needed");
	}
	$actions{$action}->();
}

sub reset_timer {
	our $t0 = [ gettimeofday() ]
		if defined $t0;
	our $number_of_git_cmds = 0;
}

our $first_request = 1;
sub run_request {
	reset_timer();

	evaluate_uri();
	if ($first_request) {
		evaluate_gitweb_config();
		evaluate_git_version();
	}
	if ($per_request_config) {
		if (ref($per_request_config) eq 'CODE') {
			$per_request_config->();
		} elsif (!$first_request) {
			evaluate_gitweb_config();
		}
	}
	check_loadavg();

	# $projectroot and $projects_list might be set in gitweb config file
	$projects_list ||= $projectroot;

	evaluate_query_params();
	evaluate_path_info();
	evaluate_and_validate_params();
	evaluate_git_dir();

	configure_gitweb_features();

	dispatch();
}

our $is_last_request = sub { 1 };
our ($pre_dispatch_hook, $post_dispatch_hook, $pre_listen_hook);
our $CGI = 'CGI';
our $cgi;
our $FCGI_Stream_PRINT_raw = \&FCGI::Stream::PRINT;
sub configure_as_fcgi {
	require CGI::Fast;
	our $CGI = 'CGI::Fast';
	# FCGI is not Unicode aware hence the UTF-8 encoding must be done manually.
	# However no encoding must be done within git_blob_plain() and git_snapshot()
	# which must still output in raw binary mode.
	no warnings 'redefine';
	my $enc = Encode::find_encoding('UTF-8');
	*FCGI::Stream::PRINT = sub {
		my @OUTPUT = @_;
		for (my $i = 1; $i < @_; $i++) {
			$OUTPUT[$i] = $enc->encode($_[$i], Encode::FB_CROAK|Encode::LEAVE_SRC);
		}
		@_ = @OUTPUT;
		goto $FCGI_Stream_PRINT_raw;
	};

	my $request_number = 0;
	# let each child service 100 requests
	our $is_last_request = sub { ++$request_number > 100 };
}
sub evaluate_argv {
	my $script_name = $ENV{'SCRIPT_NAME'} || $ENV{'SCRIPT_FILENAME'} || __FILE__;
	configure_as_fcgi()
		if $script_name =~ /\.fcgi$/;

	return unless (@ARGV);

	require Getopt::Long;
	Getopt::Long::GetOptions(
		'fastcgi|fcgi|f' => \&configure_as_fcgi,
		'nproc|n=i' => sub {
			my ($arg, $val) = @_;
			return unless eval { require FCGI::ProcManager; 1; };
			my $proc_manager = FCGI::ProcManager->new({
				n_processes => $val,
			});
			our $pre_listen_hook    = sub { $proc_manager->pm_manage()        };
			our $pre_dispatch_hook  = sub { $proc_manager->pm_pre_dispatch()  };
			our $post_dispatch_hook = sub { $proc_manager->pm_post_dispatch() };
		},
	);
}

sub run {
	evaluate_argv();

	$first_request = 1;
	$pre_listen_hook->()
		if $pre_listen_hook;

 REQUEST:
	while ($cgi = $CGI->new()) {
		$pre_dispatch_hook->()
			if $pre_dispatch_hook;

		run_request();

		$post_dispatch_hook->()
			if $post_dispatch_hook;
		$first_request = 0;

		last REQUEST if ($is_last_request->());
	}

 DONE_GITWEB:
	1;
}

run();

if (defined caller) {
	# wrapped in a subroutine processing requests,
	# e.g. mod_perl with ModPerl::Registry, or PSGI with Plack::App::WrapCGI
	return;
} else {
	# pure CGI script, serving single request
	exit;
}

## ======================================================================
## action links

# possible values of extra options
# -full => 0|1      - use absolute/full URL ($my_uri/$my_url as base)
# -replay => 1      - start from a current view (replay with modifications)
# -path_info => 0|1 - don't use/use path_info URL (if possible)
# -anchor => ANCHOR - add #ANCHOR to end of URL, implies -replay if used alone
sub href {
	my %params = @_;
	# default is to use -absolute url() i.e. $my_uri
	my $href = $params{-full} ? $my_url : $my_uri;

	# implicit -replay, must be first of implicit params
	$params{-replay} = 1 if (keys %params == 1 && $params{-anchor});

	$params{'project'} = $project unless exists $params{'project'};

	if ($params{-replay}) {
		while (my ($name, $symbol) = each %cgi_param_mapping) {
			if (!exists $params{$name}) {
				$params{$name} = $input_params{$name};
			}
		}
	}

	my $use_pathinfo = gitweb_check_feature('pathinfo');
	if (defined $params{'project'} &&
	    (exists $params{-path_info} ? $params{-path_info} : $use_pathinfo)) {
		# try to put as many parameters as possible in PATH_INFO:
		#   - project name
		#   - action
		#   - hash_parent or hash_parent_base:/file_parent
		#   - hash or hash_base:/filename
		#   - the snapshot_format as an appropriate suffix

		# When the script is the root DirectoryIndex for the domain,
		# $href here would be something like http://gitweb.example.com/
		# Thus, we strip any trailing / from $href, to spare us double
		# slashes in the final URL
		$href =~ s,/$,,;

		# Then add the project name, if present
		$href .= "/".esc_path_info($params{'project'});
		delete $params{'project'};

		# since we destructively absorb parameters, we keep this
		# boolean that remembers if we're handling a snapshot
		my $is_snapshot = $params{'action'} eq 'snapshot';

		# Summary just uses the project path URL, any other action is
		# added to the URL
		if (defined $params{'action'}) {
			$href .= "/".esc_path_info($params{'action'})
				unless $params{'action'} eq 'summary';
			delete $params{'action'};
		}

		# Next, we put hash_parent_base:/file_parent..hash_base:/file_name,
		# stripping nonexistent or useless pieces
		$href .= "/" if ($params{'hash_base'} || $params{'hash_parent_base'}
			|| $params{'hash_parent'} || $params{'hash'});
		if (defined $params{'hash_base'}) {
			if (defined $params{'hash_parent_base'}) {
				$href .= esc_path_info($params{'hash_parent_base'});
				# skip the file_parent if it's the same as the file_name
				if (defined $params{'file_parent'}) {
					if (defined $params{'file_name'} && $params{'file_parent'} eq $params{'file_name'}) {
						delete $params{'file_parent'};
					} elsif ($params{'file_parent'} !~ /\.\./) {
						$href .= ":/".esc_path_info($params{'file_parent'});
						delete $params{'file_parent'};
					}
				}
				$href .= "..";
				delete $params{'hash_parent'};
				delete $params{'hash_parent_base'};
			} elsif (defined $params{'hash_parent'}) {
				$href .= esc_path_info($params{'hash_parent'}). "..";
				delete $params{'hash_parent'};
			}

			$href .= esc_path_info($params{'hash_base'});
			if (defined $params{'file_name'} && $params{'file_name'} !~ /\.\./) {
				$href .= ":/".esc_path_info($params{'file_name'});
				delete $params{'file_name'};
			}
			delete $params{'hash'};
			delete $params{'hash_base'};
		} elsif (defined $params{'hash'}) {
			$href .= esc_path_info($params{'hash'});
			delete $params{'hash'};
		}

		# If the action was a snapshot, we can absorb the
		# snapshot_format parameter too
		if ($is_snapshot) {
			my $fmt = $params{'snapshot_format'};
			# snapshot_format should always be defined when href()
			# is called, but just in case some code forgets, we
			# fall back to the default
			$fmt ||= $snapshot_fmts[0];
			$href .= $known_snapshot_formats{$fmt}{'suffix'};
			delete $params{'snapshot_format'};
		}
	}

	# now encode the parameters explicitly
	my @result = ();
	for (my $i = 0; $i < @cgi_param_mapping; $i += 2) {
		my ($name, $symbol) = ($cgi_param_mapping[$i], $cgi_param_mapping[$i+1]);
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

	# final transformation: trailing spaces must be escaped (URI-encoded)
	$href =~ s/(\s+)$/CGI::escape($1)/e;

	if ($params{-anchor}) {
		$href .= "#".esc_param($params{-anchor});
	}

	return $href;
}


## ======================================================================
## validation, quoting/unquoting and escaping

sub is_valid_action {
	my $input = shift;
	return undef unless exists $actions{$input};
	return 1;
}

sub is_valid_project {
	my $input = shift;

	return unless defined $input;
	if (!is_valid_pathname($input) ||
		!(-d "$projectroot/$input") ||
		!check_export_ok("$projectroot/$input") ||
		($strict_export && !project_in_list($input))) {
		return undef;
	} else {
		return 1;
	}
}

sub is_valid_pathname {
	my $input = shift;

	return undef unless defined $input;
	# no '.' or '..' as elements of path, i.e. no '.' or '..'
	# at the beginning, at the end, and between slashes.
	# also this catches doubled slashes
	if ($input =~ m!(^|/)(|\.|\.\.)(/|$)!) {
		return undef;
	}
	# no null characters
	if ($input =~ m!\0!) {
		return undef;
	}
	return 1;
}

sub is_valid_ref_format {
	my $input = shift;

	return undef unless defined $input;
	# restrictions on ref name according to git-check-ref-format
	if ($input =~ m!(/\.|\.\.|[\000-\040\177 ~^:?*\[]|/$)!) {
		return undef;
	}
	return 1;
}

sub is_valid_refname {
	my $input = shift;

	return undef unless defined $input;
	# textual hashes are O.K.
	if ($input =~ m/^$oid_regex$/) {
		return 1;
	}
	# it must be correct pathname
	is_valid_pathname($input) or return undef;
	# check git-check-ref-format restrictions
	is_valid_ref_format($input) or return undef;
	return 1;
}

# decode sequences of octets in utf8 into Perl's internal form,
# which is utf-8 with utf8 flag set if needed.  gitweb writes out
# in utf-8 thanks to "binmode STDOUT, ':utf8'" at beginning
sub to_utf8 {
	my $str = shift;
	return undef unless defined $str;

	if (utf8::is_utf8($str) || utf8::decode($str)) {
		return $str;
	} else {
		return decode($fallback_encoding, $str, Encode::FB_DEFAULT);
	}
}

# quote unsafe chars, but keep the slash, even when it's not
# correct, but quoted slashes look too horrible in bookmarks
sub esc_param {
	my $str = shift;
	return undef unless defined $str;
	$str =~ s/([^A-Za-z0-9\-_.~()\/:@ ]+)/CGI::escape($1)/eg;
	$str =~ s/ /\+/g;
	return $str;
}

# the quoting rules for path_info fragment are slightly different
sub esc_path_info {
	my $str = shift;
	return undef unless defined $str;

	# path_info doesn't treat '+' as space (specially), but '?' must be escaped
	$str =~ s/([^A-Za-z0-9\-_.~();\/;:@&= +]+)/CGI::escape($1)/eg;

	return $str;
}

# quote unsafe chars in whole URL, so some characters cannot be quoted
sub esc_url {
	my $str = shift;
	return undef unless defined $str;
	$str =~ s/([^A-Za-z0-9\-_.~();\/;?:@&= ]+)/CGI::escape($1)/eg;
	$str =~ s/ /\+/g;
	return $str;
}

# quote unsafe characters in HTML attributes
sub esc_attr {

	# for XHTML conformance escaping '"' to '&quot;' is not enough
	return esc_html(@_);
}

# replace invalid utf8 character with SUBSTITUTION sequence
sub esc_html {
	my $str = shift;
	my %opts = @_;

	return undef unless defined $str;

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

	return undef unless defined $str;

	$str = to_utf8($str);
	$str = $cgi->escapeHTML($str);
	if ($opts{'-nbsp'}) {
		$str =~ s/ /&nbsp;/g;
	}
	$str =~ s|([[:cntrl:]])|quot_cec($1)|eg;
	return $str;
}

# Sanitize for use in XHTML + application/xml+xhtml (valid XML 1.0)
sub sanitize {
	my $str = shift;

	return undef unless defined $str;

	$str = to_utf8($str);
	$str =~ s|([[:cntrl:]])|(index("\t\n\r", $1) != -1 ? $1 : quot_cec($1))|eg;
	return $str;
}

# Make control characters "printable", using character escape codes (CEC)
sub quot_cec {
	my $cntrl = shift;
	my %opts = @_;
	my %es = ( # character escape codes, aka escape sequences
		"\t" => '\t',   # tab             (HT)
		"\n" => '\n',   # line feed       (LF)
		"\r" => '\r',   # carriage return (CR)
		"\f" => '\f',   # form feed       (FF)
		"\b" => '\b',   # backspace       (BS)
		"\a" => '\a',   # alarm (bell)    (BEL)
		"\e" => '\e',   # escape          (ESC)
		"\013" => '\v', # vertical tab    (VT)
		"\000" => '\0', # nul character   (NUL)
	);
	my $chr = ( (exists $es{$cntrl})
		    ? $es{$cntrl}
		    : sprintf('\%2x', ord($cntrl)) );
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

	# Make sure perl knows it is utf8 encoded so we don't
	# cut in the middle of a utf8 multibyte char.
	$str = to_utf8($str);

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
			$lead = " ...";
		}
		return "$lead$body";

	} elsif ($where eq 'center') {
		$str =~ m/^($endre)(.*)$/;
		my ($left, $str)  = ($1, $2);
		$str =~ m/^(.*?)($begre)$/;
		my ($mid, $right) = ($1, $2);
		if (length($mid) > 5) {
			$mid = " ... ";
		}
		return "$left$mid$right";

	} else {
		$str =~ m/^($endre)(.*)$/;
		my $body = $1;
		my $tail = $2;
		if (length($tail) > 4) {
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
	$str = to_utf8($str);
	if ($chopped eq $str) {
		return esc_html($chopped);
	} else {
		$str =~ s/[[:cntrl:]]/?/g;
		return $cgi->span({-title=>$str}, esc_html($chopped));
	}
}

# Highlight selected fragments of string, using given CSS class,
# and escape HTML.  It is assumed that fragments do not overlap.
# Regions are passed as list of pairs (array references).
#
# Example: esc_html_hl_regions("foobar", "mark", [ 0, 3 ]) returns
# '<span class="mark">foo</span>bar'
sub esc_html_hl_regions {
	my ($str, $css_class, @sel) = @_;
	my %opts = grep { ref($_) ne 'ARRAY' } @sel;
	@sel     = grep { ref($_) eq 'ARRAY' } @sel;
	return esc_html($str, %opts) unless @sel;

	my $out = '';
	my $pos = 0;

	for my $s (@sel) {
		my ($begin, $end) = @$s;

		# Don't create empty <span> elements.
		next if $end <= $begin;

		my $escaped = esc_html(substr($str, $begin, $end - $begin),
		                       %opts);

		$out .= esc_html(substr($str, $pos, $begin - $pos), %opts)
			if ($begin - $pos > 0);
		$out .= $cgi->span({-class => $css_class}, $escaped);

		$pos = $end;
	}
	$out .= esc_html(substr($str, $pos), %opts)
		if ($pos < length($str));

	return $out;
}

# return positions of beginning and end of each match
sub matchpos_list {
	my ($str, $regexp) = @_;
	return unless (defined $str && defined $regexp);

	my @matches;
	while ($str =~ /$regexp/g) {
		push @matches, [$-[0], $+[0]];
	}
	return @matches;
}

# highlight match (if any), and escape HTML
sub esc_html_match_hl {
	my ($str, $regexp) = @_;
	return esc_html($str) unless defined $regexp;

	my @matches = matchpos_list($str, $regexp);
	return esc_html($str) unless @matches;

	return esc_html_hl_regions($str, 'match', @matches);
}


# highlight match (if any) of shortened string, and escape HTML
sub esc_html_match_hl_chopped {
	my ($str, $chopped, $regexp) = @_;
	return esc_html_match_hl($str, $regexp) unless defined $chopped;

	my @matches = matchpos_list($str, $regexp);
	return esc_html($chopped) unless @matches;

	# filter matches so that we mark chopped string
	my $tail = "... "; # see chop_str
	unless ($chopped =~ s/\Q$tail\E$//) {
		$tail = '';
	}
	my $chop_len = length($chopped);
	my $tail_len = length($tail);
	my @filtered;

	for my $m (@matches) {
		if ($m->[0] > $chop_len) {
			push @filtered, [ $chop_len, $chop_len + $tail_len ] if ($tail_len > 0);
			last;
		} elsif ($m->[1] > $chop_len) {
			push @filtered, [ $m->[0], $chop_len + $tail_len ];
			last;
		}
		push @filtered, $m;
	}

	return esc_html_hl_regions($chopped . $tail, 'match', @filtered);
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
sub S_ISGITLINK {
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

	# Potentially abbreviated OID.
	my $regex = oid_nlen_regex("7,64");

	$line = esc_html($line, -nbsp=>1);
	$line =~ s{
        \b
        (
            # The output of "git describe", e.g. v2.10.0-297-gf6727b0
            # or hadoop-20160921-113441-20-g094fb7d
            (?<!-) # see strbuf_check_tag_ref(). Tags can't start with -
            [A-Za-z0-9.-]+
            (?!\.) # refs can't end with ".", see check_refname_format()
            -g$regex
            |
            # Just a normal looking Git SHA1
	    $regex
        )
        \b
    }{
		$cgi->a({-href => href(action=>"object", hash=>$1),
					-class => "text"}, $1);
	}egx;

	return $line;
}

# format marker of refs pointing to given object

# the destination action is chosen based on object type and current context:
# - for annotated tags, we choose the tag view unless it's the current view
#   already, in which case we go to shortlog view
# - for other refs, we keep the current view if we're in history, shortlog or
#   log view, and select shortlog otherwise
sub format_ref_marker {
	my ($refs, $id) = @_;
	my $markers = '';

	if (defined $refs->{$id}) {
		foreach my $ref (@{$refs->{$id}}) {
			# this code exploits the fact that non-lightweight tags are the
			# only indirect objects, and that they are the only objects for which
			# we want to use tag instead of shortlog as action
			my ($type, $name) = qw();
			my $indirect = ($ref =~ s/\^\{\}$//);
			# e.g. tags/v2.6.11 or heads/next
			if ($ref =~ m!^(.*?)s?/(.*)$!) {
				$type = $1;
				$name = $2;
			} else {
				$type = "ref";
				$name = $ref;
			}

			my $class = $type;
			$class .= " indirect" if $indirect;

			my $dest_action = "shortlog";

			if ($indirect) {
				$dest_action = "tag" unless $action eq "tag";
			} elsif ($action =~ /^(history|(short)?log)$/) {
				$dest_action = $action;
			}

			my $dest = "";
			$dest .= "refs/" unless $ref =~ m!^refs/!;
			$dest .= $ref;

			my $link = $cgi->a({
				-href => href(
					action=>$dest_action,
					hash=>$dest
				)}, esc_html($name));

			$markers .= " <span class=\"".esc_attr($class)."\" title=\"".esc_attr($ref)."\">" .
				$link . "</span>";
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
		$long =~ s/[[:cntrl:]]/?/g;
		return $cgi->a({-href => $href, -class => "list subject",
		                -title => to_utf8($long)},
		       esc_html($short)) . $extra;
	} else {
		return $cgi->a({-href => $href, -class => "list subject"},
		       esc_html($long)) . $extra;
	}
}

# Rather than recomputing the url for an email multiple times, we cache it
# after the first hit. This gives a visible benefit in views where the avatar
# for the same email is used repeatedly (e.g. shortlog).
# The cache is shared by all avatar engines (currently gravatar only), which
# are free to use it as preferred. Since only one avatar engine is used for any
# given page, there's no risk for cache conflicts.
our %avatar_cache = ();

# Compute the picon url for a given email, by using the picon search service over at
# http://www.cs.indiana.edu/picons/search.html
sub picon_url {
	my $email = lc shift;
	if (!$avatar_cache{$email}) {
		my ($user, $domain) = split('@', $email);
		$avatar_cache{$email} =
			"//www.cs.indiana.edu/cgi-pub/kinzler/piconsearch.cgi/" .
			"$domain/$user/" .
			"users+domains+unknown/up/single";
	}
	return $avatar_cache{$email};
}

# Compute the gravatar url for a given email, if it's not in the cache already.
# Gravatar stores only the part of the URL before the size, since that's the
# one computationally more expensive. This also allows reuse of the cache for
# different sizes (for this particular engine).
sub gravatar_url {
	my $email = lc shift;
	my $size = shift;
	$avatar_cache{$email} ||=
		"//www.gravatar.com/avatar/" .
			md5_hex($email) . "?s=";
	return $avatar_cache{$email} . $size;
}

# Insert an avatar for the given $email at the given $size if the feature
# is enabled.
sub git_get_avatar {
	my ($email, %opts) = @_;
	my $pre_white  = ($opts{-pad_before} ? "&nbsp;" : "");
	my $post_white = ($opts{-pad_after}  ? "&nbsp;" : "");
	$opts{-size} ||= 'default';
	my $size = $avatar_size{$opts{-size}} || $avatar_size{'default'};
	my $url = "";
	if ($git_avatar eq 'gravatar') {
		$url = gravatar_url($email, $size);
	} elsif ($git_avatar eq 'picon') {
		$url = picon_url($email);
	}
	# Other providers can be added by extending the if chain, defining $url
	# as needed. If no variant puts something in $url, we assume avatars
	# are completely disabled/unavailable.
	if ($url) {
		return $pre_white .
		       "<img width=\"$size\" " .
		            "class=\"avatar\" " .
		            "src=\"".esc_url($url)."\" " .
			    "alt=\"\" " .
		       "/>" . $post_white;
	} else {
		return "";
	}
}

sub format_search_author {
	my ($author, $searchtype, $displaytext) = @_;
	my $have_search = gitweb_check_feature('search');

	if ($have_search) {
		my $performed = "";
		if ($searchtype eq 'author') {
			$performed = "authored";
		} elsif ($searchtype eq 'committer') {
			$performed = "committed";
		}

		return $cgi->a({-href => href(action=>"search", hash=>$hash,
				searchtext=>$author,
				searchtype=>$searchtype), class=>"list",
				title=>"Search for commits $performed by $author"},
				$displaytext);

	} else {
		return $displaytext;
	}
}

# format the author name of the given commit with the given tag
# the author name is chopped and escaped according to the other
# optional parameters (see chop_str).
sub format_author_html {
	my $tag = shift;
	my $co = shift;
	my $author = chop_and_escape_str($co->{'author_name'}, @_);
	return "<$tag class=\"author\">" .
	       format_search_author($co->{'author_name'}, "author",
		       git_get_avatar($co->{'author_email'}, -pad_after => 1) .
		       $author) .
	       "</$tag>";
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
	if ($line =~ oid_nlen_prefix_infix_regex($sha1_len, "index ", ",") |
	    $line =~ oid_nlen_prefix_infix_regex($sha256_len, "index ", ",")) {
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

	} elsif ($line =~ oid_nlen_prefix_infix_regex($sha1_len, "index ", "..") |
		 $line =~ oid_nlen_prefix_infix_regex($sha256_len, "index ", "..")) {
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

sub diff_line_class {
	my ($line, $from, $to) = @_;

	# ordinary diff
	my $num_sign = 1;
	# combined diff
	if ($from && $to && ref($from->{'href'}) eq "ARRAY") {
		$num_sign = scalar @{$from->{'href'}};
	}

	my @diff_line_classifier = (
		{ regexp => qr/^\@\@{$num_sign} /, class => "chunk_header"},
		{ regexp => qr/^\\/,               class => "incomplete"  },
		{ regexp => qr/^ {$num_sign}/,     class => "ctx" },
		# classifier for context must come before classifier add/rem,
		# or we would have to use more complicated regexp, for example
		# qr/(?= {0,$m}\+)[+ ]{$num_sign}/, where $m = $num_sign - 1;
		{ regexp => qr/^[+ ]{$num_sign}/,   class => "add" },
		{ regexp => qr/^[- ]{$num_sign}/,   class => "rem" },
	);
	for my $clsfy (@diff_line_classifier) {
		return $clsfy->{'class'}
			if ($line =~ $clsfy->{'regexp'});
	}

	# fallback
	return "";
}

# assumes that $from and $to are defined and correctly filled,
# and that $line holds a line of chunk header for unified diff
sub format_unidiff_chunk_header {
	my ($line, $from, $to) = @_;

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
	return $line;
}

# assumes that $from and $to are defined and correctly filled,
# and that $line holds a line of chunk header for combined diff
sub format_cc_diff_chunk_header {
	my ($line, $from, $to) = @_;

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
	return $line;
}

# process patch (diff) line (not to be used for diff headers),
# returning HTML-formatted (but not wrapped) line.
# If the line is passed as a reference, it is treated as HTML and not
# esc_html()'ed.
sub format_diff_line {
	my ($line, $diff_class, $from, $to) = @_;

	if (ref($line)) {
		$line = $$line;
	} else {
		chomp $line;
		$line = untabify($line);

		if ($from && $to && $line =~ m/^\@{2} /) {
			$line = format_unidiff_chunk_header($line, $from, $to);
		} elsif ($from && $to && $line =~ m/^\@{3}/) {
			$line = format_cc_diff_chunk_header($line, $from, $to);
		} else {
			$line = esc_html($line, -nbsp=>1);
		}
	}

	my $diff_classes = "diff";
	$diff_classes .= " $diff_class" if ($diff_class);
	$line = "<div class=\"$diff_classes\">$line</div>\n";

	return $line;
}

# Generates undef or something like "_snapshot_" or "snapshot (_tbz2_ _zip_)",
# linked.  Pass the hash of the tree/commit to snapshot.
sub format_snapshot_links {
	my ($hash) = @_;
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

## ......................................................................
## functions returning values to be passed, perhaps after some
## transformation, to other functions; e.g. returning arguments to href()

# returns hash to be passed to href to generate gitweb URL
# in -title key it returns description of link
sub get_feed_info {
	my $format = shift || 'Atom';
	my %res = (action => lc($format));
	my $matched_ref = 0;

	# feed links are possible only for project views
	return unless (defined $project);
	# some views should link to OPML, or to generic project feed,
	# or don't have specific feed yet (so they should use generic)
	return if (!$action || $action =~ /^(?:tags|heads|forks|tag|search)$/x);

	my $branch = undef;
	# branches refs uses 'refs/' + $get_branch_refs()[x] + '/' prefix
	# (fullname) to differentiate from tag links; this also makes
	# possible to detect branch links
	for my $ref (get_branch_refs()) {
		if ((defined $hash_base && $hash_base =~ m!^refs/\Q$ref\E/(.*)$!) ||
		    (defined $hash      && $hash      =~ m!^refs/\Q$ref\E/(.*)$!)) {
			$branch = $1;
			$matched_ref = $ref;
			last;
		}
	}
	# find log type for feed description (title)
	my $type = 'log';
	if (defined $file_name) {
		$type  = "history of $file_name";
		$type .= "/" if ($action eq 'tree');
		$type .= " on '$branch'" if (defined $branch);
	} else {
		$type = "log of $branch" if (defined $branch);
	}

	$res{-title} = $type;
	$res{'hash'} = (defined $branch ? "refs/$matched_ref/$branch" : undef);
	$res{'file_name'} = $file_name;

	return %res;
}

## ----------------------------------------------------------------------
## git utility subroutines, invoking git commands

# returns path to the core git executable and the --git-dir parameter as list
sub git_cmd {
	$number_of_git_cmds++;
	return $GIT, '--git-dir='.$git_dir;
}

# quote the given arguments for passing them to the shell
# quote_command("command", "arg 1", "arg with ' and ! characters")
# => "'command' 'arg 1' 'arg with '\'' and '\!' characters'"
# Try to avoid using this function wherever possible.
sub quote_command {
	return join(' ',
		map { my $a = $_; $a =~ s/(['!])/'\\$1'/g; "'$a'" } @_ );
}

# get HEAD ref of given project as hash
sub git_get_head_hash {
	return git_get_full_hash(shift, 'HEAD');
}

sub git_get_full_hash {
	return git_get_hash(@_);
}

sub git_get_short_hash {
	return git_get_hash(@_, '--short=7');
}

sub git_get_hash {
	my ($project, $hash, @options) = @_;
	my $o_git_dir = $git_dir;
	my $retval = undef;
	$git_dir = "$projectroot/$project";
	if (open my $fd, '-|', git_cmd(), 'rev-parse',
	    '--verify', '-q', @options, $hash) {
		$retval = <$fd>;
		chomp $retval if defined $retval;
		close $fd;
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

# convert config value to boolean: 'true' or 'false'
# no value, number > 0, 'true' and 'yes' values are true
# rest of values are treated as false (never as error)
sub config_to_bool {
	my $val = shift;

	return 1 if !defined $val;             # section.key

	# strip leading and trailing whitespace
	$val =~ s/^\s+//;
	$val =~ s/\s+$//;

	return (($val =~ /^\d+$/ && $val) ||   # section.key = 1
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

	return unless defined $git_dir;

	# key sanity check
	return unless ($key);
	# only subsection, if exists, is case sensitive,
	# and not lowercased by 'git config -z -l'
	if (my ($hi, $mi, $lo) = ($key =~ /^([^.]*)\.(.*)\.([^.]*)$/)) {
		$lo =~ s/_//g;
		$key = join(".", lc($hi), $mi, lc($lo));
		return if ($lo =~ /\W/ || $hi =~ /\W/);
	} else {
		$key = lc($key);
		$key =~ s/_//g;
		return if ($key =~ /\W/);
	}
	$key =~ s/^gitweb\.//;

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

	# check if config variable (key) exists
	return unless exists $config{"gitweb.$key"};

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
		or die_error(500, "Open git-ls-tree failed");
	my $line = <$fd>;
	close $fd or return undef;

	if (!defined $line) {
		# there is no tree or hash given by $path at $base
		return undef;
	}

	#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
	$line =~ m/^([0-9]+) (.+) ($oid_regex)\t/;
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

# get the value of config variable either from file named as the variable
# itself in the repository ($GIT_DIR/$name file), or from gitweb.$name
# configuration variable in the repository config file.
sub git_get_file_or_project_config {
	my ($path, $name) = @_;

	$git_dir = "$projectroot/$path";
	open my $fd, '<', "$git_dir/$name"
		or return git_get_project_config($name);
	my $conf = <$fd>;
	close $fd;
	if (defined $conf) {
		chomp $conf;
	}
	return $conf;
}

sub git_get_project_description {
	my $path = shift;
	return git_get_file_or_project_config($path, 'description');
}

sub git_get_project_category {
	my $path = shift;
	return git_get_file_or_project_config($path, 'category');
}


# supported formats:
# * $GIT_DIR/ctags/<tagname> file (in 'ctags' subdirectory)
#   - if its contents is a number, use it as tag weight,
#   - otherwise add a tag with weight 1
# * $GIT_DIR/ctags file, each line is a tag (with weight 1)
#   the same value multiple times increases tag weight
# * `gitweb.ctag' multi-valued repo config variable
sub git_get_project_ctags {
	my $project = shift;
	my $ctags = {};

	$git_dir = "$projectroot/$project";
	if (opendir my $dh, "$git_dir/ctags") {
		my @files = grep { -f $_ } map { "$git_dir/ctags/$_" } readdir($dh);
		foreach my $tagfile (@files) {
			open my $ct, '<', $tagfile
				or next;
			my $val = <$ct>;
			chomp $val if $val;
			close $ct;

			(my $ctag = $tagfile) =~ s#.*/##;
			if ($val =~ /^\d+$/) {
				$ctags->{$ctag} = $val;
			} else {
				$ctags->{$ctag} = 1;
			}
		}
		closedir $dh;

	} elsif (open my $fh, '<', "$git_dir/ctags") {
		while (my $line = <$fh>) {
			chomp $line;
			$ctags->{$line}++ if $line;
		}
		close $fh;

	} else {
		my $taglist = config_to_multi(git_get_project_config('ctag'));
		foreach my $tag (@$taglist) {
			$ctags->{$tag}++;
		}
	}

	return $ctags;
}

# return hash, where keys are content tags ('ctags'),
# and values are sum of weights of given tag in every project
sub git_gather_all_ctags {
	my $projects = shift;
	my $ctags = {};

	foreach my $p (@$projects) {
		foreach my $ct (keys %{$p->{'ctags'}}) {
			$ctags->{$ct} += $p->{'ctags'}->{$ct};
		}
	}

	return $ctags;
}

sub git_populate_project_tagcloud {
	my $ctags = shift;

	# First, merge different-cased tags; tags vote on casing
	my %ctags_lc;
	foreach (keys %$ctags) {
		$ctags_lc{lc $_}->{count} += $ctags->{$_};
		if (not $ctags_lc{lc $_}->{topcount}
		    or $ctags_lc{lc $_}->{topcount} < $ctags->{$_}) {
			$ctags_lc{lc $_}->{topcount} = $ctags->{$_};
			$ctags_lc{lc $_}->{topname} = $_;
		}
	}

	my $cloud;
	my $matched = $input_params{'ctag'};
	if (eval { require HTML::TagCloud; 1; }) {
		$cloud = HTML::TagCloud->new;
		foreach my $ctag (sort keys %ctags_lc) {
			# Pad the title with spaces so that the cloud looks
			# less crammed.
			my $title = esc_html($ctags_lc{$ctag}->{topname});
			$title =~ s/ /&nbsp;/g;
			$title =~ s/^/&nbsp;/g;
			$title =~ s/$/&nbsp;/g;
			if (defined $matched && $matched eq $ctag) {
				$title = qq(<span class="match">$title</span>);
			}
			$cloud->add($title, href(project=>undef, ctag=>$ctag),
			            $ctags_lc{$ctag}->{count});
		}
	} else {
		$cloud = {};
		foreach my $ctag (keys %ctags_lc) {
			my $title = esc_html($ctags_lc{$ctag}->{topname}, -nbsp=>1);
			if (defined $matched && $matched eq $ctag) {
				$title = qq(<span class="match">$title</span>);
			}
			$cloud->{$ctag}{count} = $ctags_lc{$ctag}->{count};
			$cloud->{$ctag}{ctag} =
				$cgi->a({-href=>href(project=>undef, ctag=>$ctag)}, $title);
		}
	}
	return $cloud;
}

sub git_show_project_tagcloud {
	my ($cloud, $count) = @_;
	if (ref $cloud eq 'HTML::TagCloud') {
		return $cloud->html_and_css($count);
	} else {
		my @tags = sort { $cloud->{$a}->{'count'} <=> $cloud->{$b}->{'count'} } keys %$cloud;
		return
			'<div id="htmltagcloud"'.($project ? '' : ' align="center"').'>' .
			join (', ', map {
				$cloud->{$_}->{'ctag'}
			} splice(@tags, 0, $count)) .
			'</div>';
	}
}

sub git_get_project_url_list {
	my $path = shift;

	$git_dir = "$projectroot/$path";
	open my $fd, '<', "$git_dir/cloneurl"
		or return wantarray ?
		@{ config_to_multi(git_get_project_config('url')) } :
		   config_to_multi(git_get_project_config('url'));
	my @git_project_url_list = map { chomp; $_ } <$fd>;
	close $fd;

	return wantarray ? @git_project_url_list : \@git_project_url_list;
}

sub git_get_projects_list {
	my $filter = shift || '';
	my $paranoid = shift;
	my @list;

	if (-d $projects_list) {
		# search in directory
		my $dir = $projects_list;
		# remove the trailing "/"
		$dir =~ s!/+$!!;
		my $pfxlen = length("$dir");
		my $pfxdepth = ($dir =~ tr!/!!);
		# when filtering, search only given subdirectory
		if ($filter && !$paranoid) {
			$dir .= "/$filter";
			$dir =~ s!/+$!!;
		}

		File::Find::find({
			follow_fast => 1, # follow symbolic links
			follow_skip => 2, # ignore duplicates
			dangling_symlinks => 0, # ignore dangling symlinks, silently
			wanted => sub {
				# global variables
				our $project_maxdepth;
				our $projectroot;
				# skip project-list toplevel, if we get it.
				return if (m!^[/.]$!);
				# only directories can be git repositories
				return unless (-d $_);
				# need search permission
				return unless (-x $_);
				# don't traverse too deep (Find is super slow on os x)
				# $project_maxdepth excludes depth of $projectroot
				if (($File::Find::name =~ tr!/!!) - $pfxdepth > $project_maxdepth) {
					$File::Find::prune = 1;
					return;
				}

				my $path = substr($File::Find::name, $pfxlen + 1);
				# paranoidly only filter here
				if ($paranoid && $filter && $path !~ m!^\Q$filter\E/!) {
					next;
				}
				# we check related file in $projectroot
				if (check_export_ok("$projectroot/$path")) {
					push @list, { path => $path };
					$File::Find::prune = 1;
				}
			},
		}, "$dir");

	} elsif (-f $projects_list) {
		# read from file(url-encoded):
		# 'git%2Fgit.git Linus+Torvalds'
		# 'libs%2Fklibc%2Fklibc.git H.+Peter+Anvin'
		# 'linux%2Fhotplug%2Fudev.git Greg+Kroah-Hartman'
		open my $fd, '<', $projects_list or return;
	PROJECT:
		while (my $line = <$fd>) {
			chomp $line;
			my ($path, $owner) = split ' ', $line;
			$path = unescape($path);
			$owner = unescape($owner);
			if (!defined $path) {
				next;
			}
			# if $filter is rpovided, check if $path begins with $filter
			if ($filter && $path !~ m!^\Q$filter\E/!) {
				next;
			}
			if (check_export_ok("$projectroot/$path")) {
				my $pr = {
					path => $path
				};
				if ($owner) {
					$pr->{'owner'} = to_utf8($owner);
				}
				push @list, $pr;
			}
		}
		close $fd;
	}
	return @list;
}

# written with help of Tree::Trie module (Perl Artistic License, GPL compatible)
# as side effects it sets 'forks' field to list of forks for forked projects
sub filter_forks_from_projects_list {
	my $projects = shift;

	my %trie; # prefix tree of directories (path components)
	# generate trie out of those directories that might contain forks
	foreach my $pr (@$projects) {
		my $path = $pr->{'path'};
		$path =~ s/\.git$//;      # forks of 'repo.git' are in 'repo/' directory
		next if ($path =~ m!/$!); # skip non-bare repositories, e.g. 'repo/.git'
		next unless ($path);      # skip '.git' repository: tests, git-instaweb
		next unless (-d "$projectroot/$path"); # containing directory exists
		$pr->{'forks'} = [];      # there can be 0 or more forks of project

		# add to trie
		my @dirs = split('/', $path);
		# walk the trie, until either runs out of components or out of trie
		my $ref = \%trie;
		while (scalar @dirs &&
		       exists($ref->{$dirs[0]})) {
			$ref = $ref->{shift @dirs};
		}
		# create rest of trie structure from rest of components
		foreach my $dir (@dirs) {
			$ref = $ref->{$dir} = {};
		}
		# create end marker, store $pr as a data
		$ref->{''} = $pr if (!exists $ref->{''});
	}

	# filter out forks, by finding shortest prefix match for paths
	my @filtered;
 PROJECT:
	foreach my $pr (@$projects) {
		# trie lookup
		my $ref = \%trie;
	DIR:
		foreach my $dir (split('/', $pr->{'path'})) {
			if (exists $ref->{''}) {
				# found [shortest] prefix, is a fork - skip it
				push @{$ref->{''}{'forks'}}, $pr;
				next PROJECT;
			}
			if (!exists $ref->{$dir}) {
				# not in trie, cannot have prefix, not a fork
				push @filtered, $pr;
				next PROJECT;
			}
			# If the dir is there, we just walk one step down the trie.
			$ref = $ref->{$dir};
		}
		# we ran out of trie
		# (shouldn't happen: it's either no match, or end marker)
		push @filtered, $pr;
	}

	return @filtered;
}

# note: fill_project_list_info must be run first,
# for 'descr_long' and 'ctags' to be filled
sub search_projects_list {
	my ($projlist, %opts) = @_;
	my $tagfilter  = $opts{'tagfilter'};
	my $search_re = $opts{'search_regexp'};

	return @$projlist
		unless ($tagfilter || $search_re);

	# searching projects require filling to be run before it;
	fill_project_list_info($projlist,
	                       $tagfilter  ? 'ctags' : (),
	                       $search_re ? ('path', 'descr') : ());
	my @projects;
 PROJECT:
	foreach my $pr (@$projlist) {

		if ($tagfilter) {
			next unless ref($pr->{'ctags'}) eq 'HASH';
			next unless
				grep { lc($_) eq lc($tagfilter) } keys %{$pr->{'ctags'}};
		}

		if ($search_re) {
			next unless
				$pr->{'path'} =~ /$search_re/ ||
				$pr->{'descr_long'} =~ /$search_re/;
		}

		push @projects, $pr;
	}

	return @projects;
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
		open(my $fd, '<', $projects_list);
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
	     map { "refs/$_" } get_branch_refs ()) or return;
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

# Implementation note: when a single remote is wanted, we cannot use 'git
# remote show -n' because that command always work (assuming it's a remote URL
# if it's not defined), and we cannot use 'git remote show' because that would
# try to make a network roundtrip. So the only way to find if that particular
# remote is defined is to walk the list provided by 'git remote -v' and stop if
# and when we find what we want.
sub git_get_remotes_list {
	my $wanted = shift;
	my %remotes = ();

	open my $fd, '-|' , git_cmd(), 'remote', '-v';
	return unless $fd;
	while (my $remote = <$fd>) {
		chomp $remote;
		$remote =~ s!\t(.*?)\s+\((\w+)\)$!!;
		next if $wanted and not $remote eq $wanted;
		my ($url, $key) = ($1, $2);

		$remotes{$remote} ||= { 'heads' => () };
		$remotes{$remote}{$key} = $url;
	}
	close $fd or return;
	return wantarray ? %remotes : \%remotes;
}

# Takes a hash of remotes as first parameter and fills it by adding the
# available remote heads for each of the indicated remotes.
sub fill_remote_heads {
	my $remotes = shift;
	my @heads = map { "remotes/$_" } keys %$remotes;
	my @remoteheads = git_get_heads_list(undef, @heads);
	foreach my $remote (keys %$remotes) {
		$remotes->{$remote}{'heads'} = [ grep {
			$_->{'name'} =~ s!^$remote/!!
			} @remoteheads ];
	}
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
		if ($line =~ m!^($oid_regex)\srefs/($type.*)$!) {
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

	my ($tz_sign, $tz_hour, $tz_min) =
		($tz =~ m/^([-+])(\d\d)(\d\d)$/);
	$tz_sign = ($tz_sign eq '-' ? -1 : +1);
	my $local = $epoch + $tz_sign*((($tz_hour*60) + $tz_min)*60);
	($sec, $min, $hour, $mday, $mon, $year, $wday, $yday) = gmtime($local);
	$date{'hour_local'} = $hour;
	$date{'minute_local'} = $min;
	$date{'tz_local'} = $tz;
	$date{'iso-tz'} = sprintf("%04d-%02d-%02d %02d:%02d:%02d %s",
	                          1900+$year, $mon+1, $mday,
	                          $hour, $min, $sec, $tz);
	return %date;
}

sub hide_mailaddrs_if_private {
	my $line = shift;
	return $line unless gitweb_check_feature('email-privacy');
	$line =~ s/<[^@>]+@[^>]+>/<redacted>/g;
	return $line;
}

sub parse_tag {
	my $tag_id = shift;
	my %tag;
	my @comment;

	open my $fd, "-|", git_cmd(), "cat-file", "tag", $tag_id or return;
	$tag{'id'} = $tag_id;
	while (my $line = <$fd>) {
		chomp $line;
		if ($line =~ m/^object ($oid_regex)$/) {
			$tag{'object'} = $1;
		} elsif ($line =~ m/^type (.+)$/) {
			$tag{'type'} = $1;
		} elsif ($line =~ m/^tag (.+)$/) {
			$tag{'name'} = $1;
		} elsif ($line =~ m/^tagger (.*) ([0-9]+) (.*)$/) {
			$tag{'author'} = hide_mailaddrs_if_private($1);
			$tag{'author_epoch'} = $2;
			$tag{'author_tz'} = $3;
			if ($tag{'author'} =~ m/^([^<]+) <([^>]*)>/) {
				$tag{'author_name'}  = $1;
				$tag{'author_email'} = $2;
			} else {
				$tag{'author_name'} = $tag{'author'};
			}
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
	if ($header !~ m/^$oid_regex/) {
		return;
	}
	($co{'id'}, my @parents) = split ' ', $header;
	while (my $line = shift @commit_lines) {
		last if $line eq "\n";
		if ($line =~ m/^tree ($oid_regex)$/) {
			$co{'tree'} = $1;
		} elsif ((!defined $withparents) && ($line =~ m/^parent ($oid_regex)$/)) {
			push @parents, $1;
		} elsif ($line =~ m/^author (.*) ([0-9]+) (.*)$/) {
			$co{'author'} = hide_mailaddrs_if_private(to_utf8($1));
			$co{'author_epoch'} = $2;
			$co{'author_tz'} = $3;
			if ($co{'author'} =~ m/^([^<]+) <([^>]*)>/) {
				$co{'author_name'}  = $1;
				$co{'author_email'} = $2;
			} else {
				$co{'author_name'} = $co{'author'};
			}
		} elsif ($line =~ m/^committer (.*) ([0-9]+) (.*)$/) {
			$co{'committer'} = hide_mailaddrs_if_private(to_utf8($1));
			$co{'committer_epoch'} = $2;
			$co{'committer_tz'} = $3;
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
			$co{'title_short'} = chop_str($title, 50, 5);
			last;
		}
	}
	if (! defined $co{'title'} || $co{'title'} eq "") {
		$co{'title'} = $co{'title_short'} = '(no commit message)';
	}
	# remove added spaces, redact e-mail addresses if applicable.
	foreach my $line (@commit_lines) {
		$line =~ s/^    //;
		$line = hide_mailaddrs_if_private($line);
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
		or die_error(500, "Open git-rev-list failed");
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
		or die_error(500, "Open git-rev-list failed");
	while (my $line = <$fd>) {
		my %co = parse_commit_text($line);
		push @cos, \%co;
	}
	close $fd;

	return wantarray ? @cos : \@cos;
}

# parse line of git-diff-tree "raw" output
sub parse_difftree_raw_line {
	my $line = shift;
	my %res;

	# ':100644 100644 03b218260e99b78c6df0ed378e59ed9205ccc96d 3b93d5e7cc7f7dd4ebed13a5cc1a4ad976fc94d8 M	ls-files.c'
	# ':100644 100644 7f9281985086971d3877aca27704f2aaf9c448ce bc190ebc71bbd923f2b728e505408f5e54bd073a M	rev-tree.c'
	if ($line =~ m/^:([0-7]{6}) ([0-7]{6}) ($oid_regex) ($oid_regex) (.)([0-9]{0,3})\t(.*)$/) {
		$res{'from_mode'} = $1;
		$res{'to_mode'} = $2;
		$res{'from_id'} = $3;
		$res{'to_id'} = $4;
		$res{'status'} = $5;
		$res{'similarity'} = $6;
		if ($res{'status'} eq 'R' || $res{'status'} eq 'C') { # renamed or copied
			($res{'from_file'}, $res{'to_file'}) = map { unquote($_) } split("\t", $7);
		} else {
			$res{'from_file'} = $res{'to_file'} = $res{'file'} = unquote($7);
		}
	}
	# '::100755 100755 100755 60e79ca1b01bc8b057abe17ddab484699a7f5fdb 94067cc5f73388f33722d52ae02f44692bc07490 94067cc5f73388f33722d52ae02f44692bc07490 MR	git-gui/git-gui.sh'
	# combined diff (for merge commit)
	elsif ($line =~ s/^(::+)((?:[0-7]{6} )+)((?:$oid_regex )+)([a-zA-Z]+)\t(.*)$//) {
		$res{'nparents'}  = length($1);
		$res{'from_mode'} = [ split(' ', $2) ];
		$res{'to_mode'} = pop @{$res{'from_mode'}};
		$res{'from_id'} = [ split(' ', $3) ];
		$res{'to_id'} = pop @{$res{'from_id'}};
		$res{'status'} = [ split('', $4) ];
		$res{'to_file'} = unquote($5);
	}
	# 'c512b523472485aef4fff9e57b229d9d243c967f'
	elsif ($line =~ m/^($oid_regex)$/) {
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
sub parse_ls_tree_line {
	my $line = shift;
	my %opts = @_;
	my %res;

	if ($opts{'-l'}) {
		#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa   16717	panic.c'
		$line =~ m/^([0-9]+) (.+) ($oid_regex) +(-|[0-9]+)\t(.+)$/s;

		$res{'mode'} = $1;
		$res{'type'} = $2;
		$res{'hash'} = $3;
		$res{'size'} = $4;
		if ($opts{'-z'}) {
			$res{'name'} = $5;
		} else {
			$res{'name'} = unquote($5);
		}
	} else {
		#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		$line =~ m/^([0-9]+) (.+) ($oid_regex)\t(.+)$/s;

		$res{'mode'} = $1;
		$res{'type'} = $2;
		$res{'hash'} = $3;
		if ($opts{'-z'}) {
			$res{'name'} = $4;
		} else {
			$res{'name'} = unquote($4);
		}
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
	my ($limit, @classes) = @_;
	@classes = get_branch_refs() unless @classes;
	my @patterns = map { "refs/$_" } @classes;
	my @headslist;

	open my $fd, '-|', git_cmd(), 'for-each-ref',
		($limit ? '--count='.($limit+1) : ()),
		'--sort=-HEAD', '--sort=-committerdate',
		'--format=%(objectname) %(refname) %(subject)%00%(committer)',
		@patterns
		or return;
	while (my $line = <$fd>) {
		my %ref_item;

		chomp $line;
		my ($refinfo, $committerinfo) = split(/\0/, $line);
		my ($hash, $name, $title) = split(' ', $refinfo, 3);
		my ($committer, $epoch, $tz) =
			($committerinfo =~ /^(.*) ([0-9]+) (.*)$/);
		$ref_item{'fullname'}  = $name;
		my $strip_refs = join '|', map { quotemeta } get_branch_refs();
		$name =~ s!^refs/($strip_refs|remotes)/!!;
		$ref_item{'name'} = $name;
		# for refs neither in 'heads' nor 'remotes' we want to
		# show their ref dir
		my $ref_dir = (defined $1) ? $1 : '';
		if ($ref_dir ne '' and $ref_dir ne 'heads' and $ref_dir ne 'remotes') {
		    $ref_item{'name'} .= ' (' . $ref_dir . ')';
		}

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

# assume that file exists
sub insert_file {
	my $filename = shift;

	open my $fd, '<', $filename;
	print map { to_utf8($_) } <$fd>;
	close $fd;
}

## ......................................................................
## mimetype related functions

sub mimetype_guess_file {
	my $filename = shift;
	my $mimemap = shift;
	-r $mimemap or return undef;

	my %mimemap;
	open(my $mh, '<', $mimemap) or return undef;
	while (<$mh>) {
		next if m/^#/; # skip comments
		my ($mimetype, @exts) = split(/\s+/);
		foreach my $ext (@exts) {
			$mimemap{$ext} = $mimetype;
		}
	}
	close($mh);

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
		return 'text/plain';
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

sub blob_contenttype {
	my ($fd, $file_name, $type) = @_;

	$type ||= blob_mimetype($fd, $file_name);
	if ($type eq 'text/plain' && defined $default_text_plain_charset) {
		$type .= "; charset=$default_text_plain_charset";
	}

	return $type;
}

# guess file syntax for syntax highlighting; return undef if no highlighting
# the name of syntax can (in the future) depend on syntax highlighter used
sub guess_file_syntax {
	my ($highlight, $file_name) = @_;
	return undef unless ($highlight && defined $file_name);
	my $basename = basename($file_name, '.in');
	return $highlight_basename{$basename}
		if exists $highlight_basename{$basename};

	$basename =~ /\.([^.]*)$/;
	my $ext = $1 or return undef;
	return $highlight_ext{$ext}
		if exists $highlight_ext{$ext};

	return undef;
}

# run highlighter and return FD of its output,
# or return original FD if no highlighting
sub run_highlighter {
	my ($fd, $highlight, $syntax) = @_;
	return $fd unless ($highlight);

	close $fd;
	my $syntax_arg = (defined $syntax) ? "--syntax $syntax" : "--force";
	open $fd, quote_command(git_cmd(), "cat-file", "blob", $hash)." | ".
	          quote_command($^X, '-CO', '-MEncode=decode,FB_DEFAULT', '-pse',
	            '$_ = decode($fe, $_, FB_DEFAULT) if !utf8::decode($_);',
	            '--', "-fe=$fallback_encoding")." | ".
	          quote_command($highlight_bin).
	          " --replace-tabs=8 --fragment $syntax_arg |"
		or die_error(500, "Couldn't open file or run syntax highlighter");
	return $fd;
}

## ======================================================================
## functions printing HTML: header, footer, error page

sub get_page_title {
	my $title = to_utf8($site_name);

	unless (defined $project) {
		if (defined $project_filter) {
			$title .= " - projects in '" . esc_path($project_filter) . "'";
		}
		return $title;
	}
	$title .= " - " . to_utf8($project);

	return $title unless (defined $action);
	$title .= "/$action"; # $action is US-ASCII (7bit ASCII)

	return $title unless (defined $file_name);
	$title .= " - " . esc_path($file_name);
	if ($action eq "tree" && $file_name !~ m|/$|) {
		$title .= "/";
	}

	return $title;
}

sub get_content_type_html {
	# require explicit support from the UA if we are to send the page as
	# 'application/xhtml+xml', otherwise send it as plain old 'text/html'.
	# we have to do this because MSIE sometimes globs '*/*', pretending to
	# support xhtml+xml but choking when it gets what it asked for.
	if (defined $cgi->http('HTTP_ACCEPT') &&
	    $cgi->http('HTTP_ACCEPT') =~ m/(,|;|\s|^)application\/xhtml\+xml(,|;|\s|$)/ &&
	    $cgi->Accept('application/xhtml+xml') != 0) {
		return 'application/xhtml+xml';
	} else {
		return 'text/html';
	}
}

sub print_feed_meta {
	if (defined $project) {
		my %href_params = get_feed_info();
		if (!exists $href_params{'-title'}) {
			$href_params{'-title'} = 'log';
		}

		foreach my $format (qw(RSS Atom)) {
			my $type = lc($format);
			my %link_attr = (
				'-rel' => 'alternate',
				'-title' => esc_attr("$project - $href_params{'-title'} - $format feed"),
				'-type' => "application/$type+xml"
			);

			$href_params{'extra_options'} = undef;
			$href_params{'action'} = $type;
			$link_attr{'-href'} = esc_attr(href(%href_params));
			print "<link ".
			      "rel=\"$link_attr{'-rel'}\" ".
			      "title=\"$link_attr{'-title'}\" ".
			      "href=\"$link_attr{'-href'}\" ".
			      "type=\"$link_attr{'-type'}\" ".
			      "/>\n";

			$href_params{'extra_options'} = '--no-merges';
			$link_attr{'-href'} = esc_attr(href(%href_params));
			$link_attr{'-title'} .= ' (no merges)';
			print "<link ".
			      "rel=\"$link_attr{'-rel'}\" ".
			      "title=\"$link_attr{'-title'}\" ".
			      "href=\"$link_attr{'-href'}\" ".
			      "type=\"$link_attr{'-type'}\" ".
			      "/>\n";
		}

	} else {
		printf('<link rel="alternate" title="%s projects list" '.
		       'href="%s" type="text/plain; charset=utf-8" />'."\n",
		       esc_attr($site_name),
		       esc_attr(href(project=>undef, action=>"project_index")));
		printf('<link rel="alternate" title="%s projects feeds" '.
		       'href="%s" type="text/x-opml" />'."\n",
		       esc_attr($site_name),
		       esc_attr(href(project=>undef, action=>"opml")));
	}
}

sub print_header_links {
	my $status = shift;

	# print out each stylesheet that exist, providing backwards capability
	# for those people who defined $stylesheet in a config file
	if (defined $stylesheet) {
		print '<link rel="stylesheet" type="text/css" href="'.esc_url($stylesheet).'"/>'."\n";
	} else {
		foreach my $stylesheet (@stylesheets) {
			next unless $stylesheet;
			print '<link rel="stylesheet" type="text/css" href="'.esc_url($stylesheet).'"/>'."\n";
		}
	}
	print_feed_meta()
		if ($status eq '200 OK');
	if (defined $favicon) {
		print qq(<link rel="shortcut icon" href=").esc_url($favicon).qq(" type="image/png" />\n);
	}
}

sub print_nav_breadcrumbs_path {
	my $dirprefix = undef;
	while (my $part = shift) {
		$dirprefix .= "/" if defined $dirprefix;
		$dirprefix .= $part;
		print $cgi->a({-href => href(project => undef,
		                             project_filter => $dirprefix,
		                             action => "project_list")},
			      esc_html($part)) . " / ";
	}
}

sub print_nav_breadcrumbs {
	my %opts = @_;

	for my $crumb (@extra_breadcrumbs, [ $home_link_str => $home_link ]) {
		print $cgi->a({-href => esc_url($crumb->[1])}, $crumb->[0]) . " / ";
	}
	if (defined $project) {
		my @dirname = split '/', $project;
		my $projectbasename = pop @dirname;
		print_nav_breadcrumbs_path(@dirname);
		print $cgi->a({-href => href(action=>"summary")}, esc_html($projectbasename));
		if (defined $action) {
			my $action_print = $action ;
			if (defined $opts{-action_extra}) {
				$action_print = $cgi->a({-href => href(action=>$action)},
					$action);
			}
			print " / $action_print";
		}
		if (defined $opts{-action_extra}) {
			print " / $opts{-action_extra}";
		}
		print "\n";
	} elsif (defined $project_filter) {
		print_nav_breadcrumbs_path(split '/', $project_filter);
	}
}

sub print_search_form {
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
	my $use_pathinfo = gitweb_check_feature('pathinfo');
	if ($use_pathinfo) {
		$action .= "/".esc_url($project);
	}
	print $cgi->start_form(-method => "get", -action => $action) .
	      "<div class=\"search\">\n" .
	      (!$use_pathinfo &&
	      $cgi->input({-name=>"p", -value=>$project, -type=>"hidden"}) . "\n") .
	      $cgi->input({-name=>"a", -value=>"search", -type=>"hidden"}) . "\n" .
	      $cgi->input({-name=>"h", -value=>$search_hash, -type=>"hidden"}) . "\n" .
	      $cgi->popup_menu(-name => 'st', -default => 'commit',
	                       -values => ['commit', 'grep', 'author', 'committer', 'pickaxe']) .
	      " " . $cgi->a({-href => href(action=>"search_help"),
			     -title => "search help" }, "?") . " search:\n",
	      $cgi->textfield(-name => "s", -value => $searchtext, -override => 1) . "\n" .
	      "<span title=\"Extended regular expression\">" .
	      $cgi->checkbox(-name => 'sr', -value => 1, -label => 're',
	                     -checked => $search_use_regexp) .
	      "</span>" .
	      "</div>" .
	      $cgi->end_form() . "\n";
}

sub git_header_html {
	my $status = shift || "200 OK";
	my $expires = shift;
	my %opts = @_;

	my $title = get_page_title();
	print $cgi->header(-type=>get_content_type_html(), -charset => 'utf-8',
	                   -status=> $status, -expires => $expires)
		unless ($opts{'-no_http_header'});
	my $mod_perl_version = $ENV{'MOD_PERL'} ? " $ENV{'MOD_PERL'}" : '';
	print <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html [
	<!ENTITY nbsp "&#xA0;">
	<!ENTITY sdot "&#x22C5;">
]>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US" lang="en-US">
<!-- git web interface version $version, (C) 2005-2006, Kay Sievers <kay.sievers\@vrfy.org>, Christian Gierke -->
<!-- git core binaries version $git_version -->
<head>
<meta name="generator" content="gitweb/$version git/$git_version$mod_perl_version"/>
<meta name="robots" content="index, nofollow"/>
<title>$title</title>
EOF
	# the stylesheet, favicon etc urls won't work correctly with path_info
	# unless we set the appropriate base URL
	if ($ENV{'PATH_INFO'}) {
		print "<base href=\"".esc_url($base_url)."\" />\n";
	}
	print_header_links($status);

	if (defined $site_html_head_string) {
		print to_utf8($site_html_head_string);
	}

	print "</head>\n" .
	      "<body>\n";

	if (defined $site_header && -f $site_header) {
		insert_file($site_header);
	}

	print "<div class=\"page_header\">\n";
	if (defined $logo) {
		print $cgi->a({-href => esc_url($logo_url),
		               -title => $logo_label},
		              $cgi->img({-src => esc_url($logo),
		                         -width => 72, -height => 27,
		                         -alt => "git",
		                         -class => "logo"}));
	}
	print_nav_breadcrumbs(%opts);
	print "</div>\n";

	my $have_search = gitweb_check_feature('search');
	if (defined $project && $have_search) {
		print_search_form();
	}
}

sub git_footer_html {
	my $feed_class = 'rss_logo';

	print "<div class=\"page_footer\">\n";
	if (defined $project) {
		my $descr = git_get_project_description($project);
		if (defined $descr) {
			print "<div class=\"page_footer_text\">" . esc_html($descr) . "</div>\n";
		}

		my %href_params = get_feed_info();
		if (!%href_params) {
			$feed_class .= ' generic';
		}
		$href_params{'-title'} ||= 'log';

		foreach my $format (qw(RSS Atom)) {
			$href_params{'action'} = lc($format);
			print $cgi->a({-href => href(%href_params),
			              -title => "$href_params{'-title'} $format feed",
			              -class => $feed_class}, $format)."\n";
		}

	} else {
		print $cgi->a({-href => href(project=>undef, action=>"opml",
		                             project_filter => $project_filter),
		              -class => $feed_class}, "OPML") . " ";
		print $cgi->a({-href => href(project=>undef, action=>"project_index",
		                             project_filter => $project_filter),
		              -class => $feed_class}, "TXT") . "\n";
	}
	print "</div>\n"; # class="page_footer"

	if (defined $t0 && gitweb_check_feature('timed')) {
		print "<div id=\"generating_info\">\n";
		print 'This page took '.
		      '<span id="generating_time" class="time_span">'.
		      tv_interval($t0, [ gettimeofday() ]).
		      ' seconds </span>'.
		      ' and '.
		      '<span id="generating_cmd">'.
		      $number_of_git_cmds.
		      '</span> git commands '.
		      " to generate.\n";
		print "</div>\n"; # class="page_footer"
	}

	if (defined $site_footer && -f $site_footer) {
		insert_file($site_footer);
	}

	print qq!<script type="text/javascript" src="!.esc_url($javascript).qq!"></script>\n!;
	if (defined $action &&
	    $action eq 'blame_incremental') {
		print qq!<script type="text/javascript">\n!.
		      qq!startBlame("!. esc_attr(href(action=>"blame_data", -replay=>1)) .qq!",\n!.
		      qq!           "!. esc_attr(href()) .qq!");\n!.
		      qq!</script>\n!;
	} else {
		my ($jstimezone, $tz_cookie, $datetime_class) =
			gitweb_get_feature('javascript-timezone');

		print qq!<script type="text/javascript">\n!.
		      qq!window.onload = function () {\n!;
		if (gitweb_check_feature('javascript-actions')) {
			print qq!	fixLinks();\n!;
		}
		if ($jstimezone && $tz_cookie && $datetime_class) {
			print qq!	var tz_cookie = { name: '$tz_cookie', expires: 14, path: '/' };\n!. # in days
			      qq!	onloadTZSetup('$jstimezone', tz_cookie, '$datetime_class');\n!;
		}
		print qq!};\n!.
		      qq!</script>\n!;
	}

	print "</body>\n" .
	      "</html>";
}

# die_error(<http_status_code>, <error_message>[, <detailed_html_description>])
# Example: die_error(404, 'Hash not found')
# By convention, use the following status codes (as defined in RFC 2616):
# 400: Invalid or missing CGI parameters, or
#      requested object exists but has wrong type.
# 403: Requested feature (like "pickaxe" or "snapshot") not enabled on
#      this server or project.
# 404: Requested object/revision/project doesn't exist.
# 500: The server isn't configured properly, or
#      an internal error occurred (e.g. failed assertions caused by bugs), or
#      an unknown error occurred (e.g. the git binary died unexpectedly).
# 503: The server is currently unavailable (because it is overloaded,
#      or down for maintenance).  Generally, this is a temporary state.
sub die_error {
	my $status = shift || 500;
	my $error = esc_html(shift) || "Internal Server Error";
	my $extra = shift;
	my %opts = @_;

	my %http_responses = (
		400 => '400 Bad Request',
		403 => '403 Forbidden',
		404 => '404 Not Found',
		500 => '500 Internal Server Error',
		503 => '503 Service Unavailable',
	);
	git_header_html($http_responses{$status}, undef, %opts);
	print <<EOF;
<div class="page_body">
<br /><br />
$status - $error
<br />
EOF
	if (defined $extra) {
		print "<hr />\n" .
		      "$extra\n";
	}
	print "</div>\n";

	git_footer_html();
	goto DONE_GITWEB
		unless ($opts{'-error_handler'});
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

	my @actions = gitweb_get_feature('actions');
	my %repl = (
		'%' => '%',
		'n' => $project,         # project name
		'f' => $git_dir,         # project path within filesystem
		'h' => $treehead || '',  # current hash ('h' parameter)
		'b' => $treebase || '',  # hash base ('hb' parameter)
	);
	while (@actions) {
		my ($label, $link, $pos) = splice(@actions,0,3);
		# insert
		@navs = map { $_ eq $pos ? ($_, $label) : $_ } @navs;
		# munch munch
		$link =~ s/%([%nfhb])/$repl{$1}/g;
		$arg{$label}{'_href'} = $link;
	}

	print "<div class=\"page_nav\">\n" .
		(join " | ",
		 map { $_ eq $current ?
		       $_ : $cgi->a({-href => ($arg{$_}{_href} ? $arg{$_}{_href} : href(%{$arg{$_}}))}, "$_")
		 } @navs);
	print "<br/>\n$extra<br/>\n" .
	      "</div>\n";
}

# returns a submenu for the navigation of the refs views (tags, heads,
# remotes) with the current view disabled and the remotes view only
# available if the feature is enabled
sub format_ref_views {
	my ($current) = @_;
	my @ref_views = qw{tags heads};
	push @ref_views, 'remotes' if gitweb_check_feature('remote_heads');
	return join " | ", map {
		$_ eq $current ? $_ :
		$cgi->a({-href => href(action=>$_)}, $_)
	} @ref_views
}

sub format_paging_nav {
	my ($action, $page, $has_next_link) = @_;
	my $paging_nav;


	if ($page > 0) {
		$paging_nav .=
			$cgi->a({-href => href(-replay=>1, page=>undef)}, "first") .
			" &sdot; " .
			$cgi->a({-href => href(-replay=>1, page=>$page-1),
			         -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		$paging_nav .= "first &sdot; prev";
	}

	if ($has_next_link) {
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

sub format_repo_url {
	my ($name, $url) = @_;
	return "<tr class=\"metadata_url\"><td>$name</td><td>$url</td></tr>\n";
}

# Group output by placing it in a DIV element and adding a header.
# Options for start_div() can be provided by passing a hash reference as the
# first parameter to the function.
# Options to git_print_header_div() can be provided by passing an array
# reference. This must follow the options to start_div if they are present.
# The content can be a scalar, which is output as-is, a scalar reference, which
# is output after html escaping, an IO handle passed either as *handle or
# *handle{IO}, or a function reference. In the latter case all following
# parameters will be taken as argument to the content function call.
sub git_print_section {
	my ($div_args, $header_args, $content);
	my $arg = shift;
	if (ref($arg) eq 'HASH') {
		$div_args = $arg;
		$arg = shift;
	}
	if (ref($arg) eq 'ARRAY') {
		$header_args = $arg;
		$arg = shift;
	}
	$content = $arg;

	print $cgi->start_div($div_args);
	git_print_header_div(@$header_args);

	if (ref($content) eq 'CODE') {
		$content->(@_);
	} elsif (ref($content) eq 'SCALAR') {
		print esc_html($$content);
	} elsif (ref($content) eq 'GLOB' or ref($content) eq 'IO::Handle') {
		print <$content>;
	} elsif (!ref($content) && defined($content)) {
		print $content;
	}

	print $cgi->end_div;
}

sub format_timestamp_html {
	my $date = shift;
	my $strtime = $date->{'rfc2822'};

	my (undef, undef, $datetime_class) =
		gitweb_get_feature('javascript-timezone');
	if ($datetime_class) {
		$strtime = qq!<span class="$datetime_class">$strtime</span>!;
	}

	my $localtime_format = '(%02d:%02d %s)';
	if ($date->{'hour_local'} < 6) {
		$localtime_format = '(<span class="atnight">%02d:%02d</span> %s)';
	}
	$strtime .= ' ' .
	            sprintf($localtime_format,
	                    $date->{'hour_local'}, $date->{'minute_local'}, $date->{'tz_local'});

	return $strtime;
}

# Outputs the author name and date in long form
sub git_print_authorship {
	my $co = shift;
	my %opts = @_;
	my $tag = $opts{-tag} || 'div';
	my $author = $co->{'author_name'};

	my %ad = parse_date($co->{'author_epoch'}, $co->{'author_tz'});
	print "<$tag class=\"author_date\">" .
	      format_search_author($author, "author", esc_html($author)) .
	      " [".format_timestamp_html(\%ad)."]".
	      git_get_avatar($co->{'author_email'}, -pad_before => 1) .
	      "</$tag>\n";
}

# Outputs table rows containing the full author or committer information,
# in the format expected for 'commit' view (& similar).
# Parameters are a commit hash reference, followed by the list of people
# to output information for. If the list is empty it defaults to both
# author and committer.
sub git_print_authorship_rows {
	my $co = shift;
	# too bad we can't use @people = @_ || ('author', 'committer')
	my @people = @_;
	@people = ('author', 'committer') unless @people;
	foreach my $who (@people) {
		my %wd = parse_date($co->{"${who}_epoch"}, $co->{"${who}_tz"});
		print "<tr><td>$who</td><td>" .
		      format_search_author($co->{"${who}_name"}, $who,
		                           esc_html($co->{"${who}_name"})) . " " .
		      format_search_author($co->{"${who}_email"}, $who,
		                           esc_html("<" . $co->{"${who}_email"} . ">")) .
		      "</td><td rowspan=\"2\">" .
		      git_get_avatar($co->{"${who}_email"}, -size => 'double') .
		      "</td></tr>\n" .
		      "<tr>" .
		      "<td></td><td>" .
		      format_timestamp_html(\%wd) .
		      "</td>" .
		      "</tr>\n";
	}
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

sub git_print_log {
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
	my $skip_blank_line = 0;
	foreach my $line (@$log) {
		if ($line =~ m/^\s*([A-Z][-A-Za-z]*-([Bb]y|[Tt]o)|C[Cc]|(Clos|Fix)es): /) {
			if (! $opts{'-remove_signoff'}) {
				print "<span class=\"signoff\">" . esc_html($line) . "</span><br/>\n";
				$skip_blank_line = 1;
			}
			next;
		}

		if ($line =~ m,\s*([a-z]*link): (https?://\S+),i) {
			if (! $opts{'-remove_signoff'}) {
				print "<span class=\"signoff\">" . esc_html($1) . ": " .
					"<a href=\"" . esc_html($2) . "\">" . esc_html($2) . "</a>" .
					"</span><br/>\n";
				$skip_blank_line = 1;
			}
			next;
		}

		# print only one empty line
		# do not print empty line after signoff
		if ($line eq "") {
			next if ($skip_blank_line);
			$skip_blank_line = 1;
		} else {
			$skip_blank_line = 0;
		}

		print format_log_line_html($line) . "<br/>\n";
	}

	if ($opts{'-final_empty_line'}) {
		# end with single empty line
		print "<br/>\n" unless $skip_blank_line;
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
		local $/ = undef;
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
	my ($link_target, $basedir) = @_;

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
	if (exists $t->{'size'}) {
		print "<td class=\"size\">$t->{'size'}</td>\n";
	}
	if ($t->{'type'} eq "blob") {
		print "<td class=\"list\">" .
			$cgi->a({-href => href(action=>"blob", hash=>$t->{'hash'},
			                       file_name=>"$basedir$t->{'name'}", %base_key),
			        -class => "list"}, esc_path($t->{'name'}));
		if (S_ISLNK(oct $t->{'mode'})) {
			my $link_target = git_get_link_target($t->{'hash'});
			if ($link_target) {
				my $norm_target = normalize_link_target($link_target, $basedir);
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
		                             file_name=>"$basedir$t->{'name'}",
		                             %base_key)},
		              esc_path($t->{'name'}));
		print "</td>\n";
		print "<td class=\"link\">";
		print $cgi->a({-href => href(action=>"tree", hash=>$t->{'hash'},
		                             file_name=>"$basedir$t->{'name'}",
		                             %base_key)},
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

	return $diffinfo->{'to_id'} eq ('0' x 40) || $diffinfo->{'to_id'} eq ('0' x 64);
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
	my $have_blame = gitweb_check_feature('blame');
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
				      $cgi->a({-href => href(-anchor=>"patch$patchno")},
				              "patch") .
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
			if (S_ISREG($from_mode_oct)) { # only for regular file
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
				print $cgi->a({-href => href(-anchor=>"patch$patchno")},
				              "patch") .
				      " | ";
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
				print $cgi->a({-href => href(-anchor=>"patch$patchno")},
				              "patch") .
				      " | ";
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
				print $cgi->a({-href => href(-anchor=>"patch$patchno")},
				              "patch") .
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
				print $cgi->a({-href => href(-anchor=>"patch$patchno")},
				              "patch") .
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

# Print context lines and then rem/add lines in a side-by-side manner.
sub print_sidebyside_diff_lines {
	my ($ctx, $rem, $add) = @_;

	# print context block before add/rem block
	if (@$ctx) {
		print join '',
			'<div class="chunk_block ctx">',
				'<div class="old">',
				@$ctx,
				'</div>',
				'<div class="new">',
				@$ctx,
				'</div>',
			'</div>';
	}

	if (!@$add) {
		# pure removal
		print join '',
			'<div class="chunk_block rem">',
				'<div class="old">',
				@$rem,
				'</div>',
			'</div>';
	} elsif (!@$rem) {
		# pure addition
		print join '',
			'<div class="chunk_block add">',
				'<div class="new">',
				@$add,
				'</div>',
			'</div>';
	} else {
		print join '',
			'<div class="chunk_block chg">',
				'<div class="old">',
				@$rem,
				'</div>',
				'<div class="new">',
				@$add,
				'</div>',
			'</div>';
	}
}

# Print context lines and then rem/add lines in inline manner.
sub print_inline_diff_lines {
	my ($ctx, $rem, $add) = @_;

	print @$ctx, @$rem, @$add;
}

# Format removed and added line, mark changed part and HTML-format them.
# Implementation is based on contrib/diff-highlight
sub format_rem_add_lines_pair {
	my ($rem, $add, $num_parents) = @_;

	# We need to untabify lines before split()'ing them;
	# otherwise offsets would be invalid.
	chomp $rem;
	chomp $add;
	$rem = untabify($rem);
	$add = untabify($add);

	my @rem = split(//, $rem);
	my @add = split(//, $add);
	my ($esc_rem, $esc_add);
	# Ignore leading +/- characters for each parent.
	my ($prefix_len, $suffix_len) = ($num_parents, 0);
	my ($prefix_has_nonspace, $suffix_has_nonspace);

	my $shorter = (@rem < @add) ? @rem : @add;
	while ($prefix_len < $shorter) {
		last if ($rem[$prefix_len] ne $add[$prefix_len]);

		$prefix_has_nonspace = 1 if ($rem[$prefix_len] !~ /\s/);
		$prefix_len++;
	}

	while ($prefix_len + $suffix_len < $shorter) {
		last if ($rem[-1 - $suffix_len] ne $add[-1 - $suffix_len]);

		$suffix_has_nonspace = 1 if ($rem[-1 - $suffix_len] !~ /\s/);
		$suffix_len++;
	}

	# Mark lines that are different from each other, but have some common
	# part that isn't whitespace.  If lines are completely different, don't
	# mark them because that would make output unreadable, especially if
	# diff consists of multiple lines.
	if ($prefix_has_nonspace || $suffix_has_nonspace) {
		$esc_rem = esc_html_hl_regions($rem, 'marked',
		        [$prefix_len, @rem - $suffix_len], -nbsp=>1);
		$esc_add = esc_html_hl_regions($add, 'marked',
		        [$prefix_len, @add - $suffix_len], -nbsp=>1);
	} else {
		$esc_rem = esc_html($rem, -nbsp=>1);
		$esc_add = esc_html($add, -nbsp=>1);
	}

	return format_diff_line(\$esc_rem, 'rem'),
	       format_diff_line(\$esc_add, 'add');
}

# HTML-format diff context, removed and added lines.
sub format_ctx_rem_add_lines {
	my ($ctx, $rem, $add, $num_parents) = @_;
	my (@new_ctx, @new_rem, @new_add);
	my $can_highlight = 0;
	my $is_combined = ($num_parents > 1);

	# Highlight if every removed line has a corresponding added line.
	if (@$add > 0 && @$add == @$rem) {
		$can_highlight = 1;

		# Highlight lines in combined diff only if the chunk contains
		# diff between the same version, e.g.
		#
		#    - a
		#   -  b
		#    + c
		#   +  d
		#
		# Otherwise the highlighting would be confusing.
		if ($is_combined) {
			for (my $i = 0; $i < @$add; $i++) {
				my $prefix_rem = substr($rem->[$i], 0, $num_parents);
				my $prefix_add = substr($add->[$i], 0, $num_parents);

				$prefix_rem =~ s/-/+/g;

				if ($prefix_rem ne $prefix_add) {
					$can_highlight = 0;
					last;
				}
			}
		}
	}

	if ($can_highlight) {
		for (my $i = 0; $i < @$add; $i++) {
			my ($line_rem, $line_add) = format_rem_add_lines_pair(
			        $rem->[$i], $add->[$i], $num_parents);
			push @new_rem, $line_rem;
			push @new_add, $line_add;
		}
	} else {
		@new_rem = map { format_diff_line($_, 'rem') } @$rem;
		@new_add = map { format_diff_line($_, 'add') } @$add;
	}

	@new_ctx = map { format_diff_line($_, 'ctx') } @$ctx;

	return (\@new_ctx, \@new_rem, \@new_add);
}

# Print context lines and then rem/add lines.
sub print_diff_lines {
	my ($ctx, $rem, $add, $diff_style, $num_parents) = @_;
	my $is_combined = $num_parents > 1;

	($ctx, $rem, $add) = format_ctx_rem_add_lines($ctx, $rem, $add,
	        $num_parents);

	if ($diff_style eq 'sidebyside' && !$is_combined) {
		print_sidebyside_diff_lines($ctx, $rem, $add);
	} else {
		# default 'inline' style and unknown styles
		print_inline_diff_lines($ctx, $rem, $add);
	}
}

sub print_diff_chunk {
	my ($diff_style, $num_parents, $from, $to, @chunk) = @_;
	my (@ctx, @rem, @add);

	# The class of the previous line.
	my $prev_class = '';

	return unless @chunk;

	# incomplete last line might be among removed or added lines,
	# or both, or among context lines: find which
	for (my $i = 1; $i < @chunk; $i++) {
		if ($chunk[$i][0] eq 'incomplete') {
			$chunk[$i][0] = $chunk[$i-1][0];
		}
	}

	# guardian
	push @chunk, ["", ""];

	foreach my $line_info (@chunk) {
		my ($class, $line) = @$line_info;

		# print chunk headers
		if ($class && $class eq 'chunk_header') {
			print format_diff_line($line, $class, $from, $to);
			next;
		}

		## print from accumulator when have some add/rem lines or end
		# of chunk (flush context lines), or when have add and rem
		# lines and new block is reached (otherwise add/rem lines could
		# be reordered)
		if (!$class || ((@rem || @add) && $class eq 'ctx') ||
		    (@rem && @add && $class ne $prev_class)) {
			print_diff_lines(\@ctx, \@rem, \@add,
		                         $diff_style, $num_parents);
			@ctx = @rem = @add = ();
		}

		## adding lines to accumulator
		# guardian value
		last unless $line;
		# rem, add or change
		if ($class eq 'rem') {
			push @rem, $line;
		} elsif ($class eq 'add') {
			push @add, $line;
		}
		# context line
		if ($class eq 'ctx') {
			push @ctx, $line;
		}

		$prev_class = $class;
	}
}

sub git_patchset_body {
	my ($fd, $diff_style, $difftree, $hash, @hash_parents) = @_;
	my ($hash_parent) = $hash_parents[0];

	my $is_combined = (@hash_parents > 1);
	my $patch_idx = 0;
	my $patch_number = 0;
	my $patch_line;
	my $diffinfo;
	my $to_name;
	my (%from, %to);
	my @chunk; # for side-by-side diff

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

			my $class = diff_line_class($patch_line, \%from, \%to);

			if ($class eq 'chunk_header') {
				print_diff_chunk($diff_style, scalar @hash_parents, \%from, \%to, @chunk);
				@chunk = ();
			}

			push @chunk, [ $class, $patch_line ];
		}

	} continue {
		if (@chunk) {
			print_diff_chunk($diff_style, scalar @hash_parents, \%from, \%to, @chunk);
			@chunk = ();
		}
		print "</div>\n"; # class="patch"
	}

	# for compact combined (--cc) format, with chunk and patch simplification
	# the patchset might be empty, but there might be unprocessed raw lines
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

sub git_project_search_form {
	my ($searchtext, $search_use_regexp) = @_;

	my $limit = '';
	if ($project_filter) {
		$limit = " in '$project_filter/'";
	}

	print "<div class=\"projsearch\">\n";
	print $cgi->start_form(-method => 'get', -action => $my_uri) .
	      $cgi->hidden(-name => 'a', -value => 'project_list')  . "\n";
	print $cgi->hidden(-name => 'pf', -value => $project_filter). "\n"
		if (defined $project_filter);
	print $cgi->textfield(-name => 's', -value => $searchtext,
	                      -title => "Search project by name and description$limit",
	                      -size => 60) . "\n" .
	      "<span title=\"Extended regular expression\">" .
	      $cgi->checkbox(-name => 'sr', -value => 1, -label => 're',
	                     -checked => $search_use_regexp) .
	      "</span>\n" .
	      $cgi->submit(-name => 'btnS', -value => 'Search') .
	      $cgi->end_form() . "\n" .
	      $cgi->a({-href => href(project => undef, searchtext => undef,
	                             project_filter => $project_filter)},
	              esc_html("List all projects$limit")) . "<br />\n";
	print "</div>\n";
}

# entry for given @keys needs filling if at least one of keys in list
# is not present in %$project_info
sub project_info_needs_filling {
	my ($project_info, @keys) = @_;

	# return List::MoreUtils::any { !exists $project_info->{$_} } @keys;
	foreach my $key (@keys) {
		if (!exists $project_info->{$key}) {
			return 1;
		}
	}
	return;
}

# fills project list info (age, description, owner, category, forks, etc.)
# for each project in the list, removing invalid projects from
# returned list, or fill only specified info.
#
# Invalid projects are removed from the returned list if and only if you
# ask 'age' or 'age_string' to be filled, because they are the only fields
# that run unconditionally git command that requires repository, and
# therefore do always check if project repository is invalid.
#
# USAGE:
# * fill_project_list_info(\@project_list, 'descr_long', 'ctags')
#   ensures that 'descr_long' and 'ctags' fields are filled
# * @project_list = fill_project_list_info(\@project_list)
#   ensures that all fields are filled (and invalid projects removed)
#
# NOTE: modifies $projlist, but does not remove entries from it
sub fill_project_list_info {
	my ($projlist, @wanted_keys) = @_;
	my @projects;
	my $filter_set = sub { return @_; };
	if (@wanted_keys) {
		my %wanted_keys = map { $_ => 1 } @wanted_keys;
		$filter_set = sub { return grep { $wanted_keys{$_} } @_; };
	}

	my $show_ctags = gitweb_check_feature('ctags');
 PROJECT:
	foreach my $pr (@$projlist) {
		if (project_info_needs_filling($pr, $filter_set->('age', 'age_string'))) {
			my (@activity) = git_get_last_activity($pr->{'path'});
			unless (@activity) {
				next PROJECT;
			}
			($pr->{'age'}, $pr->{'age_string'}) = @activity;
		}
		if (project_info_needs_filling($pr, $filter_set->('descr', 'descr_long'))) {
			my $descr = git_get_project_description($pr->{'path'}) || "";
			$descr = to_utf8($descr);
			$pr->{'descr_long'} = $descr;
			$pr->{'descr'} = chop_str($descr, $projects_list_description_width, 5);
		}
		if (project_info_needs_filling($pr, $filter_set->('owner'))) {
			$pr->{'owner'} = git_get_project_owner("$pr->{'path'}") || "";
		}
		if ($show_ctags &&
		    project_info_needs_filling($pr, $filter_set->('ctags'))) {
			$pr->{'ctags'} = git_get_project_ctags($pr->{'path'});
		}
		if ($projects_list_group_categories &&
		    project_info_needs_filling($pr, $filter_set->('category'))) {
			my $cat = git_get_project_category($pr->{'path'}) ||
			                                   $project_list_default_category;
			$pr->{'category'} = to_utf8($cat);
		}

		push @projects, $pr;
	}

	return @projects;
}

sub sort_projects_list {
	my ($projlist, $order) = @_;

	sub order_str {
		my $key = shift;
		return sub { $a->{$key} cmp $b->{$key} };
	}

	sub order_num_then_undef {
		my $key = shift;
		return sub {
			defined $a->{$key} ?
				(defined $b->{$key} ? $a->{$key} <=> $b->{$key} : -1) :
				(defined $b->{$key} ? 1 : 0)
		};
	}

	my %orderings = (
		project => order_str('path'),
		descr => order_str('descr_long'),
		owner => order_str('owner'),
		age => order_num_then_undef('age'),
	);

	my $ordering = $orderings{$order};
	return defined $ordering ? sort $ordering @$projlist : @$projlist;
}

# returns a hash of categories, containing the list of project
# belonging to each category
sub build_projlist_by_category {
	my ($projlist, $from, $to) = @_;
	my %categories;

	$from = 0 unless defined $from;
	$to = $#$projlist if (!defined $to || $#$projlist < $to);

	for (my $i = $from; $i <= $to; $i++) {
		my $pr = $projlist->[$i];
		push @{$categories{ $pr->{'category'} }}, $pr;
	}

	return wantarray ? %categories : \%categories;
}

# print 'sort by' <th> element, generating 'sort by $name' replay link
# if that order is not selected
sub print_sort_th {
	print format_sort_th(@_);
}

sub format_sort_th {
	my ($name, $order, $header) = @_;
	my $sort_th = "";
	$header ||= ucfirst($name);

	if ($order eq $name) {
		$sort_th .= "<th>$header</th>\n";
	} else {
		$sort_th .= "<th>" .
		            $cgi->a({-href => href(-replay=>1, order=>$name),
		                     -class => "header"}, $header) .
		            "</th>\n";
	}

	return $sort_th;
}

sub git_project_list_rows {
	my ($projlist, $from, $to, $check_forks) = @_;

	$from = 0 unless defined $from;
	$to = $#$projlist if (!defined $to || $#$projlist < $to);

	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my $pr = $projlist->[$i];

		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;

		if ($check_forks) {
			print "<td>";
			if ($pr->{'forks'}) {
				my $nforks = scalar @{$pr->{'forks'}};
				if ($nforks > 0) {
					print $cgi->a({-href => href(project=>$pr->{'path'}, action=>"forks"),
					               -title => "$nforks forks"}, "+");
				} else {
					print $cgi->span({-title => "$nforks forks"}, "+");
				}
			}
			print "</td>\n";
		}
		print "<td>" . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary"),
		                        -class => "list"},
		                       esc_html_match_hl($pr->{'path'}, $search_regexp)) .
		      "</td>\n" .
		      "<td>" . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary"),
		                        -class => "list",
		                        -title => $pr->{'descr_long'}},
		                        $search_regexp
		                        ? esc_html_match_hl_chopped($pr->{'descr_long'},
		                                                    $pr->{'descr'}, $search_regexp)
		                        : esc_html($pr->{'descr'})) .
		      "</td>\n";
		unless ($omit_owner) {
		        print "<td><i>" . chop_and_escape_str($pr->{'owner'}, 15) . "</i></td>\n";
		}
		unless ($omit_age_column) {
		        print "<td class=\"". age_class($pr->{'age'}) . "\">" .
		            (defined $pr->{'age_string'} ? $pr->{'age_string'} : "No commits") . "</td>\n";
		}
		print"<td class=\"link\">" .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"summary")}, "summary")   . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"shortlog")}, "shortlog") . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"log")}, "log") . " | " .
		      $cgi->a({-href => href(project=>$pr->{'path'}, action=>"tree")}, "tree") .
		      ($pr->{'forks'} ? " | " . $cgi->a({-href => href(project=>$pr->{'path'}, action=>"forks")}, "forks") : '') .
		      "</td>\n" .
		      "</tr>\n";
	}
}

sub git_project_list_body {
	# actually uses global variable $project
	my ($projlist, $order, $from, $to, $extra, $no_header) = @_;
	my @projects = @$projlist;

	my $check_forks = gitweb_check_feature('forks');
	my $show_ctags  = gitweb_check_feature('ctags');
	my $tagfilter = $show_ctags ? $input_params{'ctag'} : undef;
	$check_forks = undef
		if ($tagfilter || $search_regexp);

	# filtering out forks before filling info allows to do less work
	@projects = filter_forks_from_projects_list(\@projects)
		if ($check_forks);
	# search_projects_list pre-fills required info
	@projects = search_projects_list(\@projects,
	                                 'search_regexp' => $search_regexp,
	                                 'tagfilter'  => $tagfilter)
		if ($tagfilter || $search_regexp);
	# fill the rest
	my @all_fields = ('descr', 'descr_long', 'ctags', 'category');
	push @all_fields, ('age', 'age_string') unless($omit_age_column);
	push @all_fields, 'owner' unless($omit_owner);
	@projects = fill_project_list_info(\@projects, @all_fields);

	$order ||= $default_projects_order;
	$from = 0 unless defined $from;
	$to = $#projects if (!defined $to || $#projects < $to);

	# short circuit
	if ($from > $to) {
		print "<center>\n".
		      "<b>No such projects found</b><br />\n".
		      "Click ".$cgi->a({-href=>href(project=>undef)},"here")." to view all projects<br />\n".
		      "</center>\n<br />\n";
		return;
	}

	@projects = sort_projects_list(\@projects, $order);

	if ($show_ctags) {
		my $ctags = git_gather_all_ctags(\@projects);
		my $cloud = git_populate_project_tagcloud($ctags);
		print git_show_project_tagcloud($cloud, 64);
	}

	print "<table class=\"project_list\">\n";
	unless ($no_header) {
		print "<tr>\n";
		if ($check_forks) {
			print "<th></th>\n";
		}
		print_sort_th('project', $order, 'Project');
		print_sort_th('descr', $order, 'Description');
		print_sort_th('owner', $order, 'Owner') unless $omit_owner;
		print_sort_th('age', $order, 'Last Change') unless $omit_age_column;
		print "<th></th>\n" . # for links
		      "</tr>\n";
	}

	if ($projects_list_group_categories) {
		# only display categories with projects in the $from-$to window
		@projects = sort {$a->{'category'} cmp $b->{'category'}} @projects[$from..$to];
		my %categories = build_projlist_by_category(\@projects, $from, $to);
		foreach my $cat (sort keys %categories) {
			unless ($cat eq "") {
				print "<tr>\n";
				if ($check_forks) {
					print "<td></td>\n";
				}
				print "<td class=\"category\" colspan=\"5\">".esc_html($cat)."</td>\n";
				print "</tr>\n";
			}

			git_project_list_rows($categories{$cat}, undef, undef, $check_forks);
		}
	} else {
		git_project_list_rows(\@projects, $from, $to, $check_forks);
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

sub git_log_body {
	# uses global variable $project
	my ($commitlist, $from, $to, $refs, $extra) = @_;

	$from = 0 unless defined $from;
	$to = $#{$commitlist} if (!defined $to || $#{$commitlist} < $to);

	for (my $i = 0; $i <= $to; $i++) {
		my %co = %{$commitlist->[$i]};
		next if !%co;
		my $commit = $co{'id'};
		my $ref = format_ref_marker($refs, $commit);
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
		      "</div>\n";
		      git_print_authorship(\%co, -tag => 'span');
		      print "<br/>\n</div>\n";

		print "<div class=\"log_body\">\n";
		git_print_log($co{'comment'}, -final_empty_line=> 1);
		print "</div>\n";
	}
	if ($extra) {
		print "<div class=\"page_nav\">\n";
		print "$extra\n";
		print "</div>\n";
	}
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
		# git_summary() used print "<td><i>$co{'age_string'}</i></td>\n" .
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      format_author_html('td', \%co, 10) . "<td>";
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
	my ($commitlist, $from, $to, $refs, $extra,
	    $file_name, $file_hash, $ftype) = @_;

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
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
	# shortlog:   format_author_html('td', \%co, 10)
		      format_author_html('td', \%co, 15, 3) . "<td>";
		# originally git_history used chop_str($co{'title'}, 50)
		print format_subject_html($co{'title'}, $co{'title_short'},
		                          href(action=>"commit", hash=>$commit), $ref);
		print "</td>\n" .
		      "<td class=\"link\">" .
		      $cgi->a({-href => href(action=>$ftype, hash_base=>$commit, file_name=>$file_name)}, $ftype) . " | " .
		      $cgi->a({-href => href(action=>"commitdiff", hash=>$commit)}, "commitdiff");

		if ($ftype eq 'blob') {
			print " | " .
			      $cgi->a({-href => href(action=>"blob_plain", hash_base=>$commit, file_name=>$file_name)}, "raw");

			my $blob_current = $file_hash;
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
	my ($headlist, $head_at, $from, $to, $extra) = @_;
	$from = 0 unless defined $from;
	$to = $#{$headlist} if (!defined $to || $#{$headlist} < $to);

	print "<table class=\"heads\">\n";
	my $alternate = 1;
	for (my $i = $from; $i <= $to; $i++) {
		my $entry = $headlist->[$i];
		my %ref = %$entry;
		my $curr = defined $head_at && $ref{'id'} eq $head_at;
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
		      $cgi->a({-href => href(action=>"tree", hash=>$ref{'fullname'}, hash_base=>$ref{'fullname'})}, "tree") .
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

# Display a single remote block
sub git_remote_block {
	my ($remote, $rdata, $limit, $head) = @_;

	my $heads = $rdata->{'heads'};
	my $fetch = $rdata->{'fetch'};
	my $push = $rdata->{'push'};

	my $urls_table = "<table class=\"projects_list\">\n" ;

	if (defined $fetch) {
		if ($fetch eq $push) {
			$urls_table .= format_repo_url("URL", $fetch);
		} else {
			$urls_table .= format_repo_url("Fetch URL", $fetch);
			$urls_table .= format_repo_url("Push URL", $push) if defined $push;
		}
	} elsif (defined $push) {
		$urls_table .= format_repo_url("Push URL", $push);
	} else {
		$urls_table .= format_repo_url("", "No remote URL");
	}

	$urls_table .= "</table>\n";

	my $dots;
	if (defined $limit && $limit < @$heads) {
		$dots = $cgi->a({-href => href(action=>"remotes", hash=>$remote)}, "...");
	}

	print $urls_table;
	git_heads_body($heads, $head, 0, $limit, $dots);
}

# Display a list of remote names with the respective fetch and push URLs
sub git_remotes_list {
	my ($remotedata, $limit) = @_;
	print "<table class=\"heads\">\n";
	my $alternate = 1;
	my @remotes = sort keys %$remotedata;

	my $limited = $limit && $limit < @remotes;

	$#remotes = $limit - 1 if $limited;

	while (my $remote = shift @remotes) {
		my $rdata = $remotedata->{$remote};
		my $fetch = $rdata->{'fetch'};
		my $push = $rdata->{'push'};
		if ($alternate) {
			print "<tr class=\"dark\">\n";
		} else {
			print "<tr class=\"light\">\n";
		}
		$alternate ^= 1;
		print "<td>" .
		      $cgi->a({-href=> href(action=>'remotes', hash=>$remote),
			       -class=> "list name"},esc_html($remote)) .
		      "</td>";
		print "<td class=\"link\">" .
		      (defined $fetch ? $cgi->a({-href=> $fetch}, "fetch") : "fetch") .
		      " | " .
		      (defined $push ? $cgi->a({-href=> $push}, "push") : "push") .
		      "</td>";

		print "</tr>\n";
	}

	if ($limited) {
		print "<tr>\n" .
		      "<td colspan=\"3\">" .
		      $cgi->a({-href => href(action=>"remotes")}, "...") .
		      "</td>\n" . "</tr>\n";
	}

	print "</table>";
}

# Display remote heads grouped by remote, unless there are too many
# remotes, in which case we only display the remote names
sub git_remotes_body {
	my ($remotedata, $limit, $head) = @_;
	if ($limit and $limit < keys %$remotedata) {
		git_remotes_list($remotedata, $limit);
	} else {
		fill_remote_heads($remotedata);
		while (my ($remote, $rdata) = each %$remotedata) {
			git_print_section({-class=>"remote", -id=>$remote},
				["remotes", $remote, $remote], sub {
					git_remote_block($remote, $rdata, $limit, $head);
				});
		}
	}
}

sub git_search_message {
	my %co = @_;

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
			$cgi->a({-href => href(-replay=>1, page=>undef)},
			        "first") .
			" &sdot; " .
			$cgi->a({-href => href(-replay=>1, page=>$page-1),
			         -accesskey => "p", -title => "Alt-p"}, "prev");
	} else {
		$paging_nav .= "first &sdot; prev";
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

	git_print_page_nav('','', $hash,$co{'tree'},$hash, $paging_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash);
	if ($page == 0 && !@commitlist) {
		print "<p>No match.</p>\n";
	} else {
		git_search_grep_body(\@commitlist, 0, 99, $next_link);
	}

	git_footer_html();
}

sub git_search_changes {
	my %co = @_;

	local $/ = "\n";
	open my $fd, '-|', git_cmd(), '--no-pager', 'log', @diff_opts,
		'--pretty=format:%H', '--no-abbrev', '--raw', "-S$searchtext",
		($search_use_regexp ? '--pickaxe-regex' : ())
			or die_error(500, "Open git-log failed");

	git_header_html();

	git_print_page_nav('','', $hash,$co{'tree'},$hash);
	git_print_header_div('commit', esc_html($co{'title'}), $hash);

	print "<table class=\"pickaxe search\">\n";
	my $alternate = 1;
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
				      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})},
				              "commit") .
				      " | " .
				      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'},
				                             hash_base=>$co{'id'})},
				              "tree") .
				      "</td>\n" .
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
			next if is_deleted(\%set);

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
		      $cgi->a({-href => href(action=>"commit", hash=>$co{'id'})},
		              "commit") .
		      " | " .
		      $cgi->a({-href => href(action=>"tree", hash=>$co{'tree'},
		                             hash_base=>$co{'id'})},
		              "tree") .
		      "</td>\n" .
		      "</tr>\n";
	}

	print "</table>\n";

	git_footer_html();
}

sub git_search_files {
	my %co = @_;

	local $/ = "\n";
	open my $fd, "-|", git_cmd(), 'grep', '-n', '-z',
		$search_use_regexp ? ('-E', '-i') : '-F',
		$searchtext, $co{'tree'}
			or die_error(500, "Open git-grep failed");

	git_header_html();

	git_print_page_nav('','', $hash,$co{'tree'},$hash);
	git_print_header_div('commit', esc_html($co{'title'}), $hash);

	print "<table class=\"grep_search\">\n";
	my $alternate = 1;
	my $matches = 0;
	my $lastfile = '';
	my $file_href;
	while (my $line = <$fd>) {
		chomp $line;
		my ($file, $lno, $ltext, $binary);
		last if ($matches++ > 1000);
		if ($line =~ /^Binary file (.+) matches$/) {
			$file = $1;
			$binary = 1;
		} else {
			($file, $lno, $ltext) = split(/\0/, $line, 3);
			$file =~ s/^$co{'tree'}://;
		}
		if ($file ne $lastfile) {
			$lastfile and print "</td></tr>\n";
			if ($alternate++) {
				print "<tr class=\"dark\">\n";
			} else {
				print "<tr class=\"light\">\n";
			}
			$file_href = href(action=>"blob", hash_base=>$co{'id'},
			                  file_name=>$file);
			print "<td class=\"list\">".
				$cgi->a({-href => $file_href, -class => "list"}, esc_path($file));
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
				$cgi->a({-href => $file_href.'#l'.$lno,
				        -class => "linenr"}, sprintf('%4i', $lno)) .
				' ' .  $ltext . "</div>\n";
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

	git_footer_html();
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
		print "<td title=\"$co{'age_string_age'}\"><i>$co{'age_string_date'}</i></td>\n" .
		      format_author_html('td', \%co, 15, 5) .
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
	my $order = $input_params{'order'};
	if (defined $order && $order !~ m/none|project|descr|owner|age/) {
		die_error(400, "Unknown order parameter");
	}

	my @list = git_get_projects_list($project_filter, $strict_export);
	if (!@list) {
		die_error(404, "No projects found");
	}

	git_header_html();
	if (defined $home_text && -f $home_text) {
		print "<div class=\"index_include\">\n";
		insert_file($home_text);
		print "</div>\n";
	}

	git_project_search_form($searchtext, $search_use_regexp);
	git_project_list_body(\@list, $order);
	git_footer_html();
}

sub git_forks {
	my $order = $input_params{'order'};
	if (defined $order && $order !~ m/none|project|descr|owner|age/) {
		die_error(400, "Unknown order parameter");
	}

	my $filter = $project;
	$filter =~ s/\.git$//;
	my @list = git_get_projects_list($filter);
	if (!@list) {
		die_error(404, "No forks found");
	}

	git_header_html();
	git_print_page_nav('','');
	git_print_header_div('summary', "$project forks");
	git_project_list_body(\@list, $order);
	git_footer_html();
}

sub git_project_index {
	my @projects = git_get_projects_list($project_filter, $strict_export);
	if (!@projects) {
		die_error(404, "No projects found");
	}

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
	my $remote_heads = gitweb_check_feature('remote_heads');

	my $owner = git_get_project_owner($project);

	my $refs = git_get_references();
	# These get_*_list functions return one more to allow us to see if
	# there are more ...
	my @taglist  = git_get_tags_list(16);
	my @headlist = git_get_heads_list(16);
	my %remotedata = $remote_heads ? git_get_remotes_list() : ();
	my @forklist;
	my $check_forks = gitweb_check_feature('forks');

	if ($check_forks) {
		# find forks of a project
		my $filter = $project;
		$filter =~ s/\.git$//;
		@forklist = git_get_projects_list($filter);
		# filter out forks of forks
		@forklist = filter_forks_from_projects_list(\@forklist)
			if (@forklist);
	}

	git_header_html();
	git_print_page_nav('summary','', $head);

	print "<div class=\"title\">&nbsp;</div>\n";
	print "<table class=\"projects_list\">\n" .
	      "<tr id=\"metadata_desc\"><td>description</td><td>" . esc_html($descr) . "</td></tr>\n";
        if ($owner and not $omit_owner) {
	        print  "<tr id=\"metadata_owner\"><td>owner</td><td>" . esc_html($owner) . "</td></tr>\n";
        }
	if (defined $cd{'rfc2822'}) {
		print "<tr id=\"metadata_lchange\"><td>last change</td>" .
		      "<td>".format_timestamp_html(\%cd)."</td></tr>\n";
	}

	# use per project git URL list in $projectroot/$project/cloneurl
	# or make project git URL from git base URL and project name
	my $url_tag = "URL";
	my @url_list = git_get_project_url_list($project);
	@url_list = map { "$_/$project" } @git_base_url_list unless @url_list;
	foreach my $git_url (@url_list) {
		next unless $git_url;
		print format_repo_url($url_tag, $git_url);
		$url_tag = "";
	}

	# Tag cloud
	my $show_ctags = gitweb_check_feature('ctags');
	if ($show_ctags) {
		my $ctags = git_get_project_ctags($project);
		if (%$ctags) {
			# without ability to add tags, don't show if there are none
			my $cloud = git_populate_project_tagcloud($ctags);
			print "<tr id=\"metadata_ctags\">" .
			      "<td>content tags</td>" .
			      "<td>".git_show_project_tagcloud($cloud, 48)."</td>" .
			      "</tr>\n";
		}
	}

	print "</table>\n";

	# If XSS prevention is on, we don't include README.html.
	# TODO: Allow a readme in some safe format.
	if (!$prevent_xss && -s "$projectroot/$project/README.html") {
		print "<div class=\"title\">readme</div>\n" .
		      "<div class=\"readme\">\n";
		insert_file("$projectroot/$project/README.html");
		print "\n</div>\n"; # class="readme"
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

	if (%remotedata) {
		git_print_header_div('remotes');
		git_remotes_body(\%remotedata, 15, $head);
	}

	if (@forklist) {
		git_print_header_div('forks');
		git_project_list_body(\@forklist, 'age', 0, 15,
		                      $#forklist <= 15 ? undef :
		                      $cgi->a({-href => href(action=>"forks")}, "..."),
		                      'no_header');
	}

	git_footer_html();
}

sub git_tag {
	my %tag = parse_tag($hash);

	if (! %tag) {
		die_error(404, "Unknown tag object");
	}

	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head);
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
		git_print_authorship_rows(\%tag, 'author');
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

sub git_blame_common {
	my $format = shift || 'porcelain';
	if ($format eq 'porcelain' && $input_params{'javascript'}) {
		$format = 'incremental';
		$action = 'blame_incremental'; # for page title etc
	}

	# permissions
	gitweb_check_feature('blame')
		or die_error(403, "Blame view not allowed");

	# error checking
	die_error(400, "No file name given") unless $file_name;
	$hash_base ||= git_get_head_hash($project);
	die_error(404, "Couldn't find base commit") unless $hash_base;
	my %co = parse_commit($hash_base)
		or die_error(404, "Commit not found");
	my $ftype = "blob";
	if (!defined $hash) {
		$hash = git_get_hash_by_path($hash_base, $file_name, "blob")
			or die_error(404, "Error looking up file");
	} else {
		$ftype = git_get_type($hash);
		if ($ftype !~ "blob") {
			die_error(400, "Object is not a blob");
		}
	}

	my $fd;
	if ($format eq 'incremental') {
		# get file contents (as base)
		open $fd, "-|", git_cmd(), 'cat-file', 'blob', $hash
			or die_error(500, "Open git-cat-file failed");
	} elsif ($format eq 'data') {
		# run git-blame --incremental
		open $fd, "-|", git_cmd(), "blame", "--incremental",
			$hash_base, "--", $file_name
			or die_error(500, "Open git-blame --incremental failed");
	} else {
		# run git-blame --porcelain
		open $fd, "-|", git_cmd(), "blame", '-p',
			$hash_base, '--', $file_name
			or die_error(500, "Open git-blame --porcelain failed");
	}
	binmode $fd, ':utf8';

	# incremental blame data returns early
	if ($format eq 'data') {
		print $cgi->header(
			-type=>"text/plain", -charset => "utf-8",
			-status=> "200 OK");
		local $| = 1; # output autoflush
		while (my $line = <$fd>) {
			print to_utf8($line);
		}
		close $fd
			or print "ERROR $!\n";

		print 'END';
		if (defined $t0 && gitweb_check_feature('timed')) {
			print ' '.
			      tv_interval($t0, [ gettimeofday() ]).
			      ' '.$number_of_git_cmds;
		}
		print "\n";

		return;
	}

	# page header
	git_header_html();
	my $formats_nav =
		$cgi->a({-href => href(action=>"blob", -replay=>1)},
		        "blob") .
		" | ";
	if ($format eq 'incremental') {
		$formats_nav .=
			$cgi->a({-href => href(action=>"blame", javascript=>0, -replay=>1)},
			        "blame") . " (non-incremental)";
	} else {
		$formats_nav .=
			$cgi->a({-href => href(action=>"blame_incremental", -replay=>1)},
			        "blame") . " (incremental)";
	}
	$formats_nav .=
		" | " .
		$cgi->a({-href => href(action=>"history", -replay=>1)},
		        "history") .
		" | " .
		$cgi->a({-href => href(action=>$action, file_name=>$file_name)},
		        "HEAD");
	git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
	git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
	git_print_page_path($file_name, $ftype, $hash_base);

	# page body
	if ($format eq 'incremental') {
		print "<noscript>\n<div class=\"error\"><center><b>\n".
		      "This page requires JavaScript to run.\n Use ".
		      $cgi->a({-href => href(action=>'blame',javascript=>0,-replay=>1)},
		              'this page').
		      " instead.\n".
		      "</b></center></div>\n</noscript>\n";

		print qq!<div id="progress_bar" style="width: 100%; background-color: yellow"></div>\n!;
	}

	print qq!<div class="page_body">\n!;
	print qq!<div id="progress_info">... / ...</div>\n!
		if ($format eq 'incremental');
	print qq!<table id="blame_table" class="blame" width="100%">\n!.
	      #qq!<col width="5.5em" /><col width="2.5em" /><col width="*" />\n!.
	      qq!<thead>\n!.
	      qq!<tr><th>Commit</th><th>Line</th><th>Data</th></tr>\n!.
	      qq!</thead>\n!.
	      qq!<tbody>\n!;

	my @rev_color = qw(light dark);
	my $num_colors = scalar(@rev_color);
	my $current_color = 0;

	if ($format eq 'incremental') {
		my $color_class = $rev_color[$current_color];

		#contents of a file
		my $linenr = 0;
	LINE:
		while (my $line = <$fd>) {
			chomp $line;
			$linenr++;

			print qq!<tr id="l$linenr" class="$color_class">!.
			      qq!<td class="sha1"><a href=""> </a></td>!.
			      qq!<td class="linenr">!.
			      qq!<a class="linenr" href="">$linenr</a></td>!;
			print qq!<td class="pre">! . esc_html($line) . "</td>\n";
			print qq!</tr>\n!;
		}

	} else { # porcelain, i.e. ordinary blame
		my %metainfo = (); # saves information about commits

		# blame data
	LINE:
		while (my $line = <$fd>) {
			chomp $line;
			# the header: <SHA-1> <src lineno> <dst lineno> [<lines in group>]
			# no <lines in group> for subsequent lines in group of lines
			my ($full_rev, $orig_lineno, $lineno, $group_size) =
			   ($line =~ /^($oid_regex) (\d+) (\d+)(?: (\d+))?$/);
			if (!exists $metainfo{$full_rev}) {
				$metainfo{$full_rev} = { 'nprevious' => 0 };
			}
			my $meta = $metainfo{$full_rev};
			my $data;
			while ($data = <$fd>) {
				chomp $data;
				last if ($data =~ s/^\t//); # contents of line
				if ($data =~ /^(\S+)(?: (.*))?$/) {
					$meta->{$1} = $2 unless exists $meta->{$1};
				}
				if ($data =~ /^previous /) {
					$meta->{'nprevious'}++;
				}
			}
			my $short_rev = substr($full_rev, 0, 8);
			my $author = $meta->{'author'};
			my %date =
				parse_date($meta->{'author-time'}, $meta->{'author-tz'});
			my $date = $date{'iso-tz'};
			if ($group_size) {
				$current_color = ($current_color + 1) % $num_colors;
			}
			my $tr_class = $rev_color[$current_color];
			$tr_class .= ' boundary' if (exists $meta->{'boundary'});
			$tr_class .= ' no-previous' if ($meta->{'nprevious'} == 0);
			$tr_class .= ' multiple-previous' if ($meta->{'nprevious'} > 1);
			print "<tr id=\"l$lineno\" class=\"$tr_class\">\n";
			if ($group_size) {
				print "<td class=\"sha1\"";
				print " title=\"". esc_html($author) . ", $date\"";
				print " rowspan=\"$group_size\"" if ($group_size > 1);
				print ">";
				print $cgi->a({-href => href(action=>"commit",
				                             hash=>$full_rev,
				                             file_name=>$file_name)},
				              esc_html($short_rev));
				if ($group_size >= 2) {
					my @author_initials = ($author =~ /\b([[:upper:]])\B/g);
					if (@author_initials) {
						print "<br />" .
						      esc_html(join('', @author_initials));
						#           or join('.', ...)
					}
				}
				print "</td>\n";
			}
			# 'previous' <sha1 of parent commit> <filename at commit>
			if (exists $meta->{'previous'} &&
			    $meta->{'previous'} =~ /^($oid_regex) (.*)$/) {
				$meta->{'parent'} = $1;
				$meta->{'file_parent'} = unquote($2);
			}
			my $linenr_commit =
				exists($meta->{'parent'}) ?
				$meta->{'parent'} : $full_rev;
			my $linenr_filename =
				exists($meta->{'file_parent'}) ?
				$meta->{'file_parent'} : unquote($meta->{'filename'});
			my $blamed = href(action => 'blame',
			                  file_name => $linenr_filename,
			                  hash_base => $linenr_commit);
			print "<td class=\"linenr\">";
			print $cgi->a({ -href => "$blamed#l$orig_lineno",
			                -class => "linenr" },
			              esc_html($lineno));
			print "</td>";
			print "<td class=\"pre\">" . esc_html($data) . "</td>\n";
			print "</tr>\n";
		} # end while

	}

	# footer
	print "</tbody>\n".
	      "</table>\n"; # class="blame"
	print "</div>\n";   # class="blame_body"
	close $fd
		or print "Reading blob failed\n";

	git_footer_html();
}

sub git_blame {
	git_blame_common();
}

sub git_blame_incremental {
	git_blame_common('incremental');
}

sub git_blame_data {
	git_blame_common('data');
}

sub git_tags {
	my $head = git_get_head_hash($project);
	git_header_html();
	git_print_page_nav('','', $head,undef,$head,format_ref_views('tags'));
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
	git_print_page_nav('','', $head,undef,$head,format_ref_views('heads'));
	git_print_header_div('summary', $project);

	my @headslist = git_get_heads_list();
	if (@headslist) {
		git_heads_body(\@headslist, $head);
	}
	git_footer_html();
}

# used both for single remote view and for list of all the remotes
sub git_remotes {
	gitweb_check_feature('remote_heads')
		or die_error(403, "Remote heads view is disabled");

	my $head = git_get_head_hash($project);
	my $remote = $input_params{'hash'};

	my $remotedata = git_get_remotes_list($remote);
	die_error(500, "Unable to get remote information") unless defined $remotedata;

	unless (%$remotedata) {
		die_error(404, defined $remote ?
			"Remote $remote not found" :
			"No remotes found");
	}

	git_header_html(undef, undef, -action_extra => $remote);
	git_print_page_nav('', '',  $head, undef, $head,
		format_ref_views($remote ? '' : 'remotes'));

	fill_remote_heads($remotedata);
	if (defined $remote) {
		git_print_header_div('remotes', "$remote remote for $project");
		git_remote_block($remote, $remotedata->{$remote}, undef, $head);
	} else {
		git_print_header_div('summary', "$project remotes");
		git_remotes_body($remotedata, undef, $head);
	}

	git_footer_html();
}

sub git_blob_plain {
	my $type = shift;
	my $expires;

	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(404, "Cannot find file");
		} else {
			die_error(400, "No file name defined");
		}
	} elsif ($hash =~ m/^$oid_regex$/) {
		# blobs defined by non-textual hash id's can be cached
		$expires = "+1d";
	}

	open my $fd, "-|", git_cmd(), "cat-file", "blob", $hash
		or die_error(500, "Open git-cat-file blob '$hash' failed");

	# content-type (can include charset)
	$type = blob_contenttype($fd, $file_name, $type);

	# "save as" filename, even when no $file_name is given
	my $save_as = "$hash";
	if (defined $file_name) {
		$save_as = $file_name;
	} elsif ($type =~ m/^text\//) {
		$save_as .= '.txt';
	}

	# With XSS prevention on, blobs of all types except a few known safe
	# ones are served with "Content-Disposition: attachment" to make sure
	# they don't run in our security domain.  For certain image types,
	# blob view writes an <img> tag referring to blob_plain view, and we
	# want to be sure not to break that by serving the image as an
	# attachment (though Firefox 3 doesn't seem to care).
	my $sandbox = $prevent_xss &&
		$type !~ m!^(?:text/[a-z]+|image/(?:gif|png|jpeg))(?:[ ;]|$)!;

	# serve text/* as text/plain
	if ($prevent_xss &&
	    ($type =~ m!^text/[a-z]+\b(.*)$! ||
	     ($type =~ m!^[a-z]+/[a-z]\+xml\b(.*)$! && -T $fd))) {
		my $rest = $1;
		$rest = defined $rest ? $rest : '';
		$type = "text/plain$rest";
	}

	print $cgi->header(
		-type => $type,
		-expires => $expires,
		-content_disposition =>
			($sandbox ? 'attachment' : 'inline')
			. '; filename="' . $save_as . '"');
	local $/ = undef;
	local *FCGI::Stream::PRINT = $FCGI_Stream_PRINT_raw;
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	close $fd;
}

sub git_blob {
	my $expires;

	if (!defined $hash) {
		if (defined $file_name) {
			my $base = $hash_base || git_get_head_hash($project);
			$hash = git_get_hash_by_path($base, $file_name, "blob")
				or die_error(404, "Cannot find file");
		} else {
			die_error(400, "No file name defined");
		}
	} elsif ($hash =~ m/^$oid_regex$/) {
		# blobs defined by non-textual hash id's can be cached
		$expires = "+1d";
	}

	my $have_blame = gitweb_check_feature('blame');
	open my $fd, "-|", git_cmd(), "cat-file", "blob", $hash
		or die_error(500, "Couldn't cat $file_name, $hash");
	my $mimetype = blob_mimetype($fd, $file_name);
	# use 'blob_plain' (aka 'raw') view for files that cannot be displayed
	if ($mimetype !~ m!^(?:text/|image/(?:gif|png|jpeg)$)! && -B $fd) {
		close $fd;
		return git_blob_plain($mimetype);
	}
	# we can have blame only for text/* mimetype
	$have_blame &&= ($mimetype =~ m!^text/!);

	my $highlight = gitweb_check_feature('highlight');
	my $syntax = guess_file_syntax($highlight, $file_name);
	$fd = run_highlighter($fd, $highlight, $syntax);

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
		      "<div class=\"title\">".esc_html($hash)."</div>\n";
	}
	git_print_page_path($file_name, "blob", $hash_base);
	print "<div class=\"page_body\">\n";
	if ($mimetype =~ m!^image/!) {
		print qq!<img class="blob" type="!.esc_attr($mimetype).qq!"!;
		if ($file_name) {
			print qq! alt="!.esc_attr($file_name).qq!" title="!.esc_attr($file_name).qq!"!;
		}
		print qq! src="! .
		      esc_attr(href(action=>"blob_plain", hash=>$hash,
		           hash_base=>$hash_base, file_name=>$file_name)) .
		      qq!" />\n!;
	} else {
		my $nr;
		while (my $line = <$fd>) {
			chomp $line;
			$nr++;
			$line = untabify($line);
			printf qq!<div class="pre"><a id="l%i" href="%s#l%i" class="linenr">%4i</a> %s</div>\n!,
			       $nr, esc_attr(href(-replay => 1)), $nr, $nr,
			       $highlight ? sanitize($line) : esc_html($line, -nbsp=>1);
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
	die_error(404, "No such tree") unless defined($hash);

	my $show_sizes = gitweb_check_feature('show-sizes');
	my $have_blame = gitweb_check_feature('blame');

	my @entries = ();
	{
		local $/ = "\0";
		open my $fd, "-|", git_cmd(), "ls-tree", '-z',
			($show_sizes ? '-l' : ()), @extra_options, $hash
			or die_error(500, "Open git-ls-tree failed");
		@entries = map { chomp; $_ } <$fd>;
		close $fd
			or die_error(404, "Reading tree failed");
	}

	my $refs = git_get_references();
	my $ref = format_ref_marker($refs, $hash_base);
	git_header_html();
	my $basedir = '';
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
		git_print_page_nav('tree','', $hash_base, undef, undef,
		                   join(' | ', @views_nav));
		git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash_base);
	} else {
		undef $hash_base;
		print "<div class=\"page_nav\">\n";
		print "<br/><br/></div>\n";
		print "<div class=\"title\">".esc_html($hash)."</div>\n";
	}
	if (defined $file_name) {
		$basedir = $file_name;
		if ($basedir ne '' && substr($basedir, -1) ne '/') {
			$basedir .= '/';
		}
		git_print_page_path($file_name, 'tree', $hash_base);
	}
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
		print '<td class="size">&nbsp;</td>'."\n" if $show_sizes;
		print '<td class="list">';
		print $cgi->a({-href => href(action=>"tree",
		                             hash_base=>$hash_base,
		                             file_name=>$up)},
		              "..");
		print "</td>\n";
		print "<td class=\"link\"></td>\n";

		print "</tr>\n";
	}
	foreach my $line (@entries) {
		my %t = parse_ls_tree_line($line, -z => 1, -l => $show_sizes);

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

sub sanitize_for_filename {
    my $name = shift;

    $name =~ s!/!-!g;
    $name =~ s/[^[:alnum:]_.-]//g;

    return $name;
}

sub snapshot_name {
	my ($project, $hash) = @_;

	# path/to/project.git  -> project
	# path/to/project/.git -> project
	my $name = to_utf8($project);
	$name =~ s,([^/])/*\.git$,$1,;
	$name = sanitize_for_filename(basename($name));

	my $ver = $hash;
	if ($hash =~ /^[0-9a-fA-F]+$/) {
		# shorten SHA-1 hash
		my $full_hash = git_get_full_hash($project, $hash);
		if ($full_hash =~ /^$hash/ && length($hash) > 7) {
			$ver = git_get_short_hash($project, $hash);
		}
	} elsif ($hash =~ m!^refs/tags/(.*)$!) {
		# tags don't need shortened SHA-1 hash
		$ver = $1;
	} else {
		# branches and other need shortened SHA-1 hash
		my $strip_refs = join '|', map { quotemeta } get_branch_refs();
		if ($hash =~ m!^refs/($strip_refs|remotes)/(.*)$!) {
			my $ref_dir = (defined $1) ? $1 : '';
			$ver = $2;

			$ref_dir = sanitize_for_filename($ref_dir);
			# for refs neither in heads nor remotes we want to
			# add a ref dir to archive name
			if ($ref_dir ne '' and $ref_dir ne 'heads' and $ref_dir ne 'remotes') {
				$ver = $ref_dir . '-' . $ver;
			}
		}
		$ver .= '-' . git_get_short_hash($project, $hash);
	}
	# special case of sanitization for filename - we change
	# slashes to dots instead of dashes
	# in case of hierarchical branch names
	$ver =~ s!/!.!g;
	$ver =~ s/[^[:alnum:]_.-]//g;

	# name = project-version_string
	$name = "$name-$ver";

	return wantarray ? ($name, $name) : $name;
}

sub exit_if_unmodified_since {
	my ($latest_epoch) = @_;
	our $cgi;

	my $if_modified = $cgi->http('IF_MODIFIED_SINCE');
	if (defined $if_modified) {
		my $since;
		if (eval { require HTTP::Date; 1; }) {
			$since = HTTP::Date::str2time($if_modified);
		} elsif (eval { require Time::ParseDate; 1; }) {
			$since = Time::ParseDate::parsedate($if_modified, GMT => 1);
		}
		if (defined $since && $latest_epoch <= $since) {
			my %latest_date = parse_date($latest_epoch);
			print $cgi->header(
				-last_modified => $latest_date{'rfc2822'},
				-status => '304 Not Modified');
			goto DONE_GITWEB;
		}
	}
}

sub git_snapshot {
	my $format = $input_params{'snapshot_format'};
	if (!@snapshot_fmts) {
		die_error(403, "Snapshots not allowed");
	}
	# default to first supported snapshot format
	$format ||= $snapshot_fmts[0];
	if ($format !~ m/^[a-z0-9]+$/) {
		die_error(400, "Invalid snapshot format parameter");
	} elsif (!exists($known_snapshot_formats{$format})) {
		die_error(400, "Unknown snapshot format");
	} elsif ($known_snapshot_formats{$format}{'disabled'}) {
		die_error(403, "Snapshot format not allowed");
	} elsif (!grep($_ eq $format, @snapshot_fmts)) {
		die_error(403, "Unsupported snapshot format");
	}

	my $type = git_get_type("$hash^{}");
	if (!$type) {
		die_error(404, 'Object does not exist');
	}  elsif ($type eq 'blob') {
		die_error(400, 'Object is not a tree-ish');
	}

	my ($name, $prefix) = snapshot_name($project, $hash);
	my $filename = "$name$known_snapshot_formats{$format}{'suffix'}";

	my %co = parse_commit($hash);
	exit_if_unmodified_since($co{'committer_epoch'}) if %co;

	my $cmd = quote_command(
		git_cmd(), 'archive',
		"--format=$known_snapshot_formats{$format}{'format'}",
		"--prefix=$prefix/", $hash);
	if (exists $known_snapshot_formats{$format}{'compressor'}) {
		$cmd .= ' | ' . quote_command(@{$known_snapshot_formats{$format}{'compressor'}});
	}

	$filename =~ s/(["\\])/\\$1/g;
	my %latest_date;
	if (%co) {
		%latest_date = parse_date($co{'committer_epoch'}, $co{'committer_tz'});
	}

	print $cgi->header(
		-type => $known_snapshot_formats{$format}{'type'},
		-content_disposition => 'inline; filename="' . $filename . '"',
		%co ? (-last_modified => $latest_date{'rfc2822'}) : (),
		-status => '200 OK');

	open my $fd, "-|", $cmd
		or die_error(500, "Execute git-archive failed");
	local *FCGI::Stream::PRINT = $FCGI_Stream_PRINT_raw;
	binmode STDOUT, ':raw';
	print <$fd>;
	binmode STDOUT, ':utf8'; # as set at the beginning of gitweb.cgi
	close $fd;
}

sub git_log_generic {
	my ($fmt_name, $body_subr, $base, $parent, $file_name, $file_hash) = @_;

	my $head = git_get_head_hash($project);
	if (!defined $base) {
		$base = $head;
	}
	if (!defined $page) {
		$page = 0;
	}
	my $refs = git_get_references();

	my $commit_hash = $base;
	if (defined $parent) {
		$commit_hash = "$parent..$base";
	}
	my @commitlist =
		parse_commits($commit_hash, 101, (100 * $page),
		              defined $file_name ? ($file_name, "--full-history") : ());

	my $ftype;
	if (!defined $file_hash && defined $file_name) {
		# some commits could have deleted file in question,
		# and not have it in tree, but one of them has to have it
		for (my $i = 0; $i < @commitlist; $i++) {
			$file_hash = git_get_hash_by_path($commitlist[$i]{'id'}, $file_name);
			last if defined $file_hash;
		}
	}
	if (defined $file_hash) {
		$ftype = git_get_type($file_hash);
	}
	if (defined $file_name && !defined $ftype) {
		die_error(500, "Unknown type of object");
	}
	my %co;
	if (defined $file_name) {
		%co = parse_commit($base)
			or die_error(404, "Unknown commit object");
	}


	my $paging_nav = format_paging_nav($fmt_name, $page, $#commitlist >= 100);
	my $next_link = '';
	if ($#commitlist >= 100) {
		$next_link =
			$cgi->a({-href => href(-replay=>1, page=>$page+1),
			         -accesskey => "n", -title => "Alt-n"}, "next");
	}
	my $patch_max = gitweb_get_feature('patches');
	if ($patch_max && !defined $file_name &&
		!gitweb_check_feature('email-privacy')) {
		if ($patch_max < 0 || @commitlist <= $patch_max) {
			$paging_nav .= " &sdot; " .
				$cgi->a({-href => href(action=>"patches", -replay=>1)},
					"patches");
		}
	}

	git_header_html();
	git_print_page_nav($fmt_name,'', $hash,$hash,$hash, $paging_nav);
	if (defined $file_name) {
		git_print_header_div('commit', esc_html($co{'title'}), $base);
	} else {
		git_print_header_div('summary', $project)
	}
	git_print_page_path($file_name, $ftype, $hash_base)
		if (defined $file_name);

	$body_subr->(\@commitlist, 0, 99, $refs, $next_link,
	             $file_name, $file_hash, $ftype);

	git_footer_html();
}

sub git_log {
	git_log_generic('log', \&git_log_body,
	                $hash, $hash_parent);
}

sub git_commit {
	$hash ||= $hash_base || "HEAD";
	my %co = parse_commit($hash)
	    or die_error(404, "Unknown commit object");

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
	if (gitweb_check_feature('patches') && @$parents <= 1 &&
		!gitweb_check_feature('email-privacy')) {
		$formats_nav .= " | " .
			$cgi->a({-href => href(action=>"patch", -replay=>1)},
				"patch");
	}

	if (!defined $parent) {
		$parent = "--root";
	}
	my @difftree;
	open my $fd, "-|", git_cmd(), "diff-tree", '-r', "--no-commit-id",
		@diff_opts,
		(@$parents <= 1 ? $parent : '-c'),
		$hash, "--"
		or die_error(500, "Open git-diff-tree failed");
	@difftree = map { chomp; $_ } <$fd>;
	close $fd or die_error(404, "Reading git-diff-tree failed");

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^$oid_regex$/) {
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
	git_print_authorship_rows(\%co);
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

		open my $fd, "-|", quote_command(
			git_cmd(), 'cat-file', '-t', $object_id) . ' 2> /dev/null'
			or die_error(404, "Object does not exist");
		$type = <$fd>;
		defined $type && chomp $type;
		close $fd
			or die_error(404, "Object does not exist");

	# - hash_base and file_name
	} elsif ($hash_base && defined $file_name) {
		$file_name =~ s,/+$,,;

		system(git_cmd(), "cat-file", '-e', $hash_base) == 0
			or die_error(404, "Base object does not exist");

		# here errors should not happen
		open my $fd, "-|", git_cmd(), "ls-tree", $hash_base, "--", $file_name
			or die_error(500, "Open git-ls-tree failed");
		my $line = <$fd>;
		close $fd;

		#'100644 blob 0fa3f3a66fb6a137f6ec2c19351ed4d807070ffa	panic.c'
		unless ($line && $line =~ m/^([0-9]+) (.+) ($oid_regex)\t/) {
			die_error(404, "File or directory for given base does not exist");
		}
		$type = $2;
		$hash = $3;
	} else {
		die_error(400, "Not enough information to find object");
	}

	print $cgi->redirect(-uri => href(action=>$type, -full=>1,
	                                  hash=>$hash, hash_base=>$hash_base,
	                                  file_name=>$file_name),
	                     -status => '302 Found');
}

sub git_blobdiff {
	my $format = shift || 'html';
	my $diff_style = $input_params{'diff_style'} || 'inline';

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
				or die_error(500, "Open git-diff-tree failed");
			@difftree = map { chomp; $_ } <$fd>;
			close $fd
				or die_error(404, "Reading git-diff-tree failed");
			@difftree
				or die_error(404, "Blob diff not found");

		} elsif (defined $hash &&
		         $hash =~ $oid_regex) {
			# try to find filename from $hash

			# read filtered raw output
			open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
				$hash_parent_base, $hash_base, "--"
				or die_error(500, "Open git-diff-tree failed");
			@difftree =
				# ':100644 100644 03b21826... 3b93d5e7... M	ls-files.c'
				# $hash == to_id
				grep { /^:[0-7]{6} [0-7]{6} $oid_regex $hash/ }
				map { chomp; $_ } <$fd>;
			close $fd
				or die_error(404, "Reading git-diff-tree failed");
			@difftree
				or die_error(404, "Blob diff not found");

		} else {
			die_error(400, "Missing one of the blob diff parameters");
		}

		if (@difftree > 1) {
			die_error(400, "Ambiguous blob diff specification");
		}

		%diffinfo = parse_difftree_raw_line($difftree[0]);
		$file_parent ||= $diffinfo{'from_file'} || $file_name;
		$file_name   ||= $diffinfo{'to_file'};

		$hash_parent ||= $diffinfo{'from_id'};
		$hash        ||= $diffinfo{'to_id'};

		# non-textual hash id's can be cached
		if ($hash_base =~ m/^$oid_regex$/ &&
		    $hash_parent_base =~ m/^$oid_regex$/) {
			$expires = '+1d';
		}

		# open patch output
		open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			'-p', ($format eq 'html' ? "--full-index" : ()),
			$hash_parent_base, $hash_base,
			"--", (defined $file_parent ? $file_parent : ()), $file_name
			or die_error(500, "Open git-diff-tree failed");
	}

	# old/legacy style URI -- not generated anymore since 1.4.3.
	if (!%diffinfo) {
		die_error('404 Not Found', "Missing one of the blob diff parameters")
	}

	# header
	if ($format eq 'html') {
		my $formats_nav =
			$cgi->a({-href => href(action=>"blobdiff_plain", -replay=>1)},
			        "raw");
		$formats_nav .= diff_style_nav($diff_style);
		git_header_html(undef, $expires);
		if (defined $hash_base && (my %co = parse_commit($hash_base))) {
			git_print_page_nav('','', $hash_base,$co{'tree'},$hash_base, $formats_nav);
			git_print_header_div('commit', esc_html($co{'title'}), $hash_base);
		} else {
			print "<div class=\"page_nav\"><br/>$formats_nav<br/></div>\n";
			print "<div class=\"title\">".esc_html("$hash vs $hash_parent")."</div>\n";
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
		die_error(400, "Unknown blobdiff format");
	}

	# patch
	if ($format eq 'html') {
		print "<div class=\"page_body\">\n";

		git_patchset_body($fd, $diff_style,
		                  [ \%diffinfo ], $hash_base, $hash_parent_base);
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

# assumes that it is added as later part of already existing navigation,
# so it returns "| foo | bar" rather than just "foo | bar"
sub diff_style_nav {
	my ($diff_style, $is_combined) = @_;
	$diff_style ||= 'inline';

	return "" if ($is_combined);

	my @styles = (inline => 'inline', 'sidebyside' => 'side by side');
	my %styles = @styles;
	@styles =
		@styles[ map { $_ * 2 } 0..$#styles/2 ];

	return join '',
		map { " | ".$_ }
		map {
			$_ eq $diff_style ? $styles{$_} :
			$cgi->a({-href => href(-replay=>1, diff_style => $_)}, $styles{$_})
		} @styles;
}

sub git_commitdiff {
	my %params = @_;
	my $format = $params{-format} || 'html';
	my $diff_style = $input_params{'diff_style'} || 'inline';

	my ($patch_max) = gitweb_get_feature('patches');
	if ($format eq 'patch') {
		die_error(403, "Patch view not allowed") unless $patch_max;
	}

	$hash ||= $hash_base || "HEAD";
	my %co = parse_commit($hash)
	    or die_error(404, "Unknown commit object");

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
		if ($patch_max && @{$co{'parents'}} <= 1 &&
			!gitweb_check_feature('email-privacy')) {
			$formats_nav .= " | " .
				$cgi->a({-href => href(action=>"patch", -replay=>1)},
					"patch");
		}
		$formats_nav .= diff_style_nav($diff_style, @{$co{'parents'}} > 1);

		if (defined $hash_parent &&
		    $hash_parent ne '-c' && $hash_parent ne '--cc') {
			# commitdiff with two commits given
			my $hash_parent_short = $hash_parent;
			if ($hash_parent =~ m/^$oid_regex$/) {
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
				$cgi->a({-href => href(-replay=>1,
				                       hash=>$hash_parent, hash_base=>undef)},
				        esc_html($hash_parent_short)) .
				')';
		} elsif (!$co{'parent'}) {
			# --root commitdiff
			$formats_nav .= ' (initial)';
		} elsif (scalar @{$co{'parents'}} == 1) {
			# single parent commit
			$formats_nav .=
				' (parent: ' .
				$cgi->a({-href => href(-replay=>1,
				                       hash=>$co{'parent'}, hash_base=>undef)},
				        esc_html(substr($co{'parent'}, 0, 7))) .
				')';
		} else {
			# merge commit
			if ($hash_parent eq '--cc') {
				$formats_nav .= ' | ' .
					$cgi->a({-href => href(-replay=>1,
					                       hash=>$hash, hash_parent=>'-c')},
					        'combined');
			} else { # $hash_parent eq '-c'
				$formats_nav .= ' | ' .
					$cgi->a({-href => href(-replay=>1,
					                       hash=>$hash, hash_parent=>'--cc')},
					        'compact');
			}
			$formats_nav .=
				' (merge: ' .
				join(' ', map {
					$cgi->a({-href => href(-replay=>1,
					                       hash=>$_, hash_base=>undef)},
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
			or die_error(500, "Open git-diff-tree failed");

		while (my $line = <$fd>) {
			chomp $line;
			# empty line ends raw part of diff-tree output
			last unless $line;
			push @difftree, scalar parse_difftree_raw_line($line);
		}

	} elsif ($format eq 'plain') {
		open $fd, "-|", git_cmd(), "diff-tree", '-r', @diff_opts,
			'-p', $hash_parent_param, $hash, "--"
			or die_error(500, "Open git-diff-tree failed");
	} elsif ($format eq 'patch') {
		# For commit ranges, we limit the output to the number of
		# patches specified in the 'patches' feature.
		# For single commits, we limit the output to a single patch,
		# diverging from the git-format-patch default.
		my @commit_spec = ();
		if ($hash_parent) {
			if ($patch_max > 0) {
				push @commit_spec, "-$patch_max";
			}
			push @commit_spec, '-n', "$hash_parent..$hash";
		} else {
			if ($params{-single}) {
				push @commit_spec, '-1';
			} else {
				if ($patch_max > 0) {
					push @commit_spec, "-$patch_max";
				}
				push @commit_spec, "-n";
			}
			push @commit_spec, '--root', $hash;
		}
		open $fd, "-|", git_cmd(), "format-patch", @diff_opts,
			'--encoding=utf8', '--stdout', @commit_spec
			or die_error(500, "Open git-format-patch failed");
	} else {
		die_error(400, "Unknown commitdiff format");
	}

	# non-textual hash id's can be cached
	my $expires;
	if ($hash =~ m/^$oid_regex$/) {
		$expires = "+1d";
	}

	# write commit message
	if ($format eq 'html') {
		my $refs = git_get_references();
		my $ref = format_ref_marker($refs, $co{'id'});

		git_header_html(undef, $expires);
		git_print_page_nav('commitdiff','', $hash,$co{'tree'},$hash, $formats_nav);
		git_print_header_div('commit', esc_html($co{'title'}) . $ref, $hash);
		print "<div class=\"title_text\">\n" .
		      "<table class=\"object_header\">\n";
		git_print_authorship_rows(\%co);
		print "</table>".
		      "</div>\n";
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
	} elsif ($format eq 'patch') {
		my $filename = basename($project) . "-$hash.patch";

		print $cgi->header(
			-type => 'text/plain',
			-charset => 'utf-8',
			-expires => $expires,
			-content_disposition => 'inline; filename="' . "$filename" . '"');
	}

	# write patch
	if ($format eq 'html') {
		my $use_parents = !defined $hash_parent ||
			$hash_parent eq '-c' || $hash_parent eq '--cc';
		git_difftree_body(\@difftree, $hash,
		                  $use_parents ? @{$co{'parents'}} : $hash_parent);
		print "<br/>\n";

		git_patchset_body($fd, $diff_style,
		                  \@difftree, $hash,
		                  $use_parents ? @{$co{'parents'}} : $hash_parent);
		close $fd;
		print "</div>\n"; # class="page_body"
		git_footer_html();

	} elsif ($format eq 'plain') {
		local $/ = undef;
		print <$fd>;
		close $fd
			or print "Reading git-diff-tree failed\n";
	} elsif ($format eq 'patch') {
		local $/ = undef;
		print <$fd>;
		close $fd
			or print "Reading git-format-patch failed\n";
	}
}

sub git_commitdiff_plain {
	git_commitdiff(-format => 'plain');
}

# format-patch-style patches
sub git_patch {
	git_commitdiff(-format => 'patch', -single => 1);
}

sub git_patches {
	git_commitdiff(-format => 'patch');
}

sub git_history {
	git_log_generic('history', \&git_history_body,
	                $hash_base, $hash_parent_base,
	                $file_name, $hash);
}

sub git_search {
	$searchtype ||= 'commit';

	# check if appropriate features are enabled
	gitweb_check_feature('search')
		or die_error(403, "Search is disabled");
	if ($searchtype eq 'pickaxe') {
		# pickaxe may take all resources of your box and run for several minutes
		# with every query - so decide by yourself how public you make this feature
		gitweb_check_feature('pickaxe')
			or die_error(403, "Pickaxe search is disabled");
	}
	if ($searchtype eq 'grep') {
		# grep search might be potentially CPU-intensive, too
		gitweb_check_feature('grep')
			or die_error(403, "Grep search is disabled");
	}

	if (!defined $searchtext) {
		die_error(400, "Text field is empty");
	}
	if (!defined $hash) {
		$hash = git_get_head_hash($project);
	}
	my %co = parse_commit($hash);
	if (!%co) {
		die_error(404, "Unknown commit object");
	}
	if (!defined $page) {
		$page = 0;
	}

	if ($searchtype eq 'commit' ||
	    $searchtype eq 'author' ||
	    $searchtype eq 'committer') {
		git_search_message(%co);
	} elsif ($searchtype eq 'pickaxe') {
		git_search_changes(%co);
	} elsif ($searchtype eq 'grep') {
		git_search_files(%co);
	} else {
		die_error(400, "Unknown search type");
	}
}

sub git_search_help {
	git_header_html();
	git_print_page_nav('','', $hash,$hash,$hash);
	print <<EOT;
<p><strong>Pattern</strong> is by default a normal string that is matched precisely (but without
regard to case, except in the case of pickaxe). However, when you check the <em>re</em> checkbox,
the pattern entered is recognized as the POSIX extended
<a href="https://en.wikipedia.org/wiki/Regular_expression">regular expression</a> (also case
insensitive).</p>
<dl>
<dt><b>commit</b></dt>
<dd>The commit messages and authorship information will be scanned for the given pattern.</dd>
EOT
	my $have_grep = gitweb_check_feature('grep');
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
	my $have_pickaxe = gitweb_check_feature('pickaxe');
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
	git_log_generic('shortlog', \&git_shortlog_body,
	                $hash, $hash_parent);
}

## ......................................................................
## feeds (RSS, Atom; OPML)

sub git_feed {
	my $format = shift || 'atom';
	my $have_blame = gitweb_check_feature('blame');

	# Atom: https://web.archive.org/web/20230815171113/https://www.atomenabled.org/developers/syndication/
	# RSS:  https://web.archive.org/web/20030729001534/http://www.notestips.com/80256B3A007F2692/1/NAMO5P9UPQ
	if ($format ne 'rss' && $format ne 'atom') {
		die_error(400, "Unknown web feed format");
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
		my $latest_epoch = $latest_commit{'committer_epoch'};
		exit_if_unmodified_since($latest_epoch);
		%latest_date = parse_date($latest_epoch, $latest_commit{'committer_tz'});
	}
	print $cgi->header(
		-type => $content_type,
		-charset => 'utf-8',
		%latest_date ? (-last_modified => $latest_date{'rfc2822'}) : (),
		-status => '200 OK');

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
	$title = esc_html($title);
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
	$alt_url = esc_attr($alt_url);
	print qq!<?xml version="1.0" encoding="utf-8"?>\n!;
	if ($format eq 'rss') {
		print <<XML;
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
<channel>
XML
		print "<title>$title</title>\n" .
		      "<link>$alt_url</link>\n" .
		      "<description>$descr</description>\n" .
		      "<language>en</language>\n" .
		      # project owner is responsible for 'editorial' content
		      "<managingEditor>$owner</managingEditor>\n";
		if (defined $logo || defined $favicon) {
			# prefer the logo to the favicon, since RSS
			# doesn't allow both
			my $img = esc_url($logo || $favicon);
			print "<image>\n" .
			      "<url>$img</url>\n" .
			      "<title>$title</title>\n" .
			      "<link>$alt_url</link>\n" .
			      "</image>\n";
		}
		if (%latest_date) {
			print "<pubDate>$latest_date{'rfc2822'}</pubDate>\n";
			print "<lastBuildDate>$latest_date{'rfc2822'}</lastBuildDate>\n";
		}
		print "<generator>gitweb v.$version/$git_version</generator>\n";
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
		      "<id>" . esc_url(href(-full=>1)) . "</id>\n" .
		      # use project owner for feed author
		      "<author><name>$owner</name></author>\n";
		if (defined $favicon) {
			print "<icon>" . esc_url($favicon) . "</icon>\n";
		}
		if (defined $logo) {
			# not twice as wide as tall: 72 x 27 pixels
			print "<logo>" . esc_url($logo) . "</logo>\n";
		}
		if (! %latest_date) {
			# dummy date to keep the feed valid until commits trickle in:
			print "<updated>1970-01-01T00:00:00Z</updated>\n";
		} else {
			print "<updated>$latest_date{'iso-8601'}</updated>\n";
		}
		print "<generator version='$version/$git_version'>gitweb</generator>\n";
	}

	# contents
	for (my $i = 0; $i <= $#commitlist; $i++) {
		my %co = %{$commitlist[$i]};
		my $commit = $co{'id'};
		# we read 150, we always show 30 and the ones more recent than 48 hours
		if (($i >= 20) && ((time - $co{'author_epoch'}) > 48*60*60)) {
			last;
		}
		my %cd = parse_date($co{'author_epoch'}, $co{'author_tz'});

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
			      "<link>" . esc_html($co_url) . "</link>\n" .
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
			      "<link rel=\"alternate\" type=\"text/html\" href=\"" . esc_attr($co_url) . "\" />\n" .
			      "<id>" . esc_html($co_url) . "</id>\n" .
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
	} elsif ($format eq 'atom') {
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
	my @list = git_get_projects_list($project_filter, $strict_export);
	if (!@list) {
		die_error(404, "No projects found");
	}

	print $cgi->header(
		-type => 'text/xml',
		-charset => 'utf-8',
		-content_disposition => 'inline; filename="opml.xml"');

	my $title = esc_html($site_name);
	my $filter = " within subdirectory ";
	if (defined $project_filter) {
		$filter .= esc_html($project_filter);
	} else {
		$filter = "";
	}
	print <<XML;
<?xml version="1.0" encoding="utf-8"?>
<opml version="1.0">
<head>
  <title>$title OPML Export$filter</title>
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
		my $rss  = esc_attr(href('project' => $proj{'path'}, 'action' => 'rss', -full => 1));
		my $html = esc_attr(href('project' => $proj{'path'}, 'action' => 'summary', -full => 1));
		print "<outline type=\"rss\" text=\"$path\" title=\"$path\" xmlUrl=\"$rss\" htmlUrl=\"$html\"/>\n";
	}
	print <<XML;
</outline>
</body>
</opml>
XML
}
